// The game's GX command stream drawn by Aurora on the browser's WebGPU.
//
// The game writes the console's FIFO into Nebula's command ring and hands the host each span at a flush (gx_submit),
// in place; draw-sync tokens and draw done arrive as markers at their position (gx_pe_marker). As in Nebula, the
// renderer is another thread: here the one that owns the WebGPU device. The simulation thread queues spans and markers
// in a fixed ring (no allocation: the game owns operator new on its thread); the render thread appends each span to
// Aurora's FIFO (the console's format, as Aurora's own GX API writes it), releases it to the game, and at a draw done
// ends Aurora's frame (Aurora processes and draws what it holds) and reports the finish to the game.
//
// The stream names memory by the game's physical addresses (vertex array bases, display list calls): Aurora resolves
// them through the game module's memory regions (resolve_guest, web/aurora/patches/0009). Not yet: textures, TLUTs and
// EFB copies, which Aurora still takes only through its own commands (docs/PUENTE_FIFO.md).
#include "aurora_bridge.h"

#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/gfx.h>
#include <aurora/guest_memory.h>
#include <dolphin/gx/GXAurora.h>

#include "gx/fifo.hpp"

#include <emscripten.h>
#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

#include <nebula_game_api.h>

namespace {

struct Item {
  const uint8_t* bytes; // a span, or nullptr for a marker
  uint32_t size;
  uint16_t token;
  uint8_t finish;
  uint8_t peekChannel = 0xFF; // a marker that is a game peek's position (token 0): the channel to capture
  uint8_t coherent = 0;       // the game wrote [physical, physical + size) for the GPU (a cache flush)
  uint32_t physical = 0;
};

constexpr uint32_t QueueSize = 4096;
Item g_queue[QueueSize];
std::atomic<uint32_t> g_head{0}; // next item to read (render thread)
std::atomic<uint32_t> g_tail{0}; // next item to write (simulation thread)

std::atomic<int> g_state{0}; // 0 starting, 1 ready, -1 failed
std::atomic<int> g_stop{0};  // the host is ending the process: the render thread shuts Aurora down and returns
pthread_t g_renderThread;
std::atomic<uint32_t> g_frames{0};

// What the render thread is doing, for the host's stall report (smg_bridge_describe)
enum Phase { PhaseWaiting, PhaseBeginFrame, PhaseBeginRefused, PhaseSpan, PhasePeeks, PhaseEndFrame, PhaseMarker };
const char* const kPhaseNames[] = {"waiting for the game's stream", "starting a frame", "frame refused by Aurora (retrying)",
                                   "reading a span of the stream", "capturing EFB peeks", "ending a frame",
                                   "answering a PE marker"};
std::atomic<int> g_phase{PhaseWaiting};
std::atomic<int64_t> g_phaseSinceMs{0};
std::atomic<uint64_t> g_spansDone{0}, g_markersDone{0}, g_beginRefused{0};

// The largest frame each of Aurora's frame buffers has held (its fixed sizes: lib/gfx/resources.hpp), for the report
std::atomic<uint32_t> g_peakStorage{0}, g_peakUniform{0}, g_peakVerts{0}, g_peakIndices{0}, g_peakTextures{0};

void raise_peak(std::atomic<uint32_t>& peak, uint32_t value) {
  if (value > peak.load(std::memory_order_relaxed)) {
    peak.store(value, std::memory_order_relaxed);
  }
}

int64_t now_ms() {
  timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return static_cast<int64_t>(t.tv_sec) * 1000 + t.tv_nsec / 1000000;
}

void set_phase(Phase phase) {
  if (g_phase.exchange(phase, std::memory_order_relaxed) != phase) {
    g_phaseSinceMs.store(now_ms(), std::memory_order_relaxed);
  }
}
int g_headless = 0;

constexpr uint32_t MaxPeeks = 16;
AuroraReadbackPeekRequest g_peekRequests[MaxPeeks];
std::atomic<uint32_t> g_peekCount{0};
uint32_t g_peekResults[MaxPeeks];
std::atomic<int> g_peeksReady{0};

// The game's EFB peeks (gx_peek_efb: GXPeekARGB, GXPeekZ). The answer cannot wait: the stream it reads is still in
// Aurora's queue, and Aurora reads the EFB back only after a frame is submitted. As Nebula's published peek fields,
// each peek is a point of a channel, its draw-sync token (0 when the game peeks outside a token's callback), and is
// answered from the latest capture of that point; the bridge captures a channel's points at its token in later
// frames, or for token 0 where the game peeked. A point not captured yet reads 0.
struct PeekChannel {
  static constexpr uint32_t MaxPoints = 16;
  std::atomic<uint32_t> token{UINT32_MAX}; // UINT32_MAX: unbound
  std::atomic<uint32_t> count{0};          // points published to the render thread
  AuroraReadbackPeekRequest points[MaxPoints];
  std::atomic<uint32_t> values[MaxPoints];
  std::atomic<uint32_t> captured[MaxPoints]; // 1 once a capture answered the point
  std::atomic<uint32_t> queuedFrame{UINT32_MAX}; // token 0: the frame whose position marker is queued
  AuroraReadbackStream stream = 0;               // render thread
};
constexpr uint32_t MaxPeekChannels = 8;
PeekChannel g_peekChannels[MaxPeekChannels];
std::atomic<uint32_t> g_gamePeeks{0};         // peeks the game made (tests)
std::atomic<uint32_t> g_gamePeeksCaptured{0}; // of them, answered from a capture

// Presentation: the XFB the VI shows (vi_frame), read back at each frame's end and posted to the page as RGBA rows on
// the BroadcastChannel "smg-present" (web/shell/present.js paints it on a 2D canvas). The readback is asynchronous:
// the page gets the frame a frame or two late. It does not depend on presenting WebGPU to a canvas, which Chromium
// without a GPU cannot do (docs/BACKEND_WEBGPU.md).
std::atomic<uint32_t> g_viXfb{0};  // physical address, 0: none
std::atomic<int> g_viBlack{1};
std::vector<uint8_t> g_presentTiles;  // render thread
std::vector<uint8_t> g_presentPixels; // render thread
std::atomic<uint32_t> g_presented{0};

// The XFB probe (tests): a display copy's destination, read back at each frame's end as the hardware would have
// written it to memory (RGBA8 here: 4x4-texel tiles of 64 bytes, alpha and red then green and blue).
std::atomic<const void*> g_xfbProbe{nullptr};
uint32_t g_xfbWidth = 0;
uint32_t g_xfbHeight = 0;
std::vector<uint8_t> g_xfbData;           // render thread
std::vector<uint8_t> g_xfbPublished;      // copied under g_xfbReady
std::atomic<int> g_xfbReady{0};           // 1: g_xfbPublished holds a frame (render thread writes it once)

void push(const Item& item) {
  const uint32_t tail = g_tail.load(std::memory_order_relaxed);
  // Full: the render thread is behind by QueueSize items; wait for it (it never waits on the game).
  while (tail - g_head.load(std::memory_order_acquire) >= QueueSize) {
    sched_yield();
  }
  g_queue[tail % QueueSize] = item;
  g_tail.store(tail + 1, std::memory_order_release);
}

// The game's memory as the GPU sees it: MEM1, MEM2 and the module image's window (sdk/memory.c). Fixed once the game
// has started, which it has by the time its stream reaches Aurora.
NebulaGameMemoryRegion g_regions[4];
int g_regionCount = 0;

const uint8_t* resolve_guest(uint32_t physical, uint32_t* available) {
  if (g_regionCount == 0) {
    const int count = nebula_game_memory_regions(g_regions, 4);
    g_regionCount = count < 4 ? count : 4;
  }
  for (int i = 0; i < g_regionCount; ++i) {
    const NebulaGameMemoryRegion& region = g_regions[i];
    const uint32_t offset = physical - region.physical;
    if (physical >= region.physical && offset < region.size) {
      *available = region.size - offset;
      return reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(region.host) + offset);
    }
  }
  return nullptr;
}

// The GX trace (render thread): everything given to Aurora, in order, to replay a renderer failure elsewhere
// (web/bridge/gx_replay). Records: 'S', u32 length, the span's bytes; 'M', u8 finish, u16 token (a PE marker). Kept
// up to TraceLimit bytes; printed to the log, in base64, when Aurora stops on an error.
constexpr size_t TraceLimit = 4u << 20;
constexpr size_t TraceDumpLimit = 256u << 10;
std::vector<uint8_t> g_trace;
bool g_traceTruncated = false;

void trace_record(char type, const uint8_t* bytes, uint32_t size, uint8_t finish, uint16_t token) {
  const size_t need = type == 'S' ? 5 + size : 4;
  if (g_trace.size() + need > TraceLimit) {
    g_traceTruncated = true;
    return;
  }
  g_trace.push_back(static_cast<uint8_t>(type));
  if (type == 'S') {
    for (int shift = 24; shift >= 0; shift -= 8) {
      g_trace.push_back(static_cast<uint8_t>(size >> shift));
    }
    g_trace.insert(g_trace.end(), bytes, bytes + size);
  } else {
    g_trace.push_back(finish);
    g_trace.push_back(static_cast<uint8_t>(token >> 8));
    g_trace.push_back(static_cast<uint8_t>(token));
  }
}

void dump_trace() {
  static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const size_t size = g_trace.size();
  if (size > TraceDumpLimit) {
    fprintf(stderr, "[gx-trace] %zu bytes recorded: too large to print (limit %zu)\n", size, TraceDumpLimit);
    return;
  }
  fprintf(stderr, "[gx-trace] begin %zu bytes%s\n", size, g_traceTruncated ? " (truncated)" : "");
  char line[80];
  size_t used = 0;
  for (size_t i = 0; i < size; i += 3) {
    const uint32_t a = g_trace[i];
    const uint32_t b = i + 1 < size ? g_trace[i + 1] : 0;
    const uint32_t c = i + 2 < size ? g_trace[i + 2] : 0;
    const uint32_t triple = a << 16 | b << 8 | c;
    line[used++] = kAlphabet[triple >> 18 & 63];
    line[used++] = kAlphabet[triple >> 12 & 63];
    line[used++] = i + 1 < size ? kAlphabet[triple >> 6 & 63] : '=';
    line[used++] = i + 2 < size ? kAlphabet[triple & 63] : '=';
    if (used == 76 || i + 3 >= size) {
      line[used] = 0;
      fprintf(stderr, "[gx-trace] %s\n", line);
      used = 0;
    }
  }
  fprintf(stderr, "[gx-trace] end\n");
}

void log_callback(AuroraLogLevel level, const char* module, const char* message, unsigned int) {
  static const char* const names[] = {"DEBUG", "INFO", "WARNING", "ERROR", "FATAL"};
  if (level >= LOG_INFO) {
    fprintf(stderr, "[aurora %s: %s] %s\n", names[level], module, message);
  }
  if (level == LOG_FATAL) {
    // The renderer cannot go on: the trace for a replay, then the page is told (aborting this thread alone left the
    // page showing the engine as running)
    dump_trace();
    EM_ASM(
        {
          const channel = new BroadcastChannel('smg-present');
          channel.postMessage({type : 'fatal', text : UTF8ToString($0)});
          channel.close();
        },
        message);
    abort();
  }
}

void collect_peeks(AuroraReadbackStream stream) {
  const uint32_t count = g_peekCount.load(std::memory_order_acquire);
  if (count == 0) {
    return;
  }
  uint32_t results[MaxPeeks]{};
  AuroraReadbackDest dests[MaxPeeks];
  for (uint32_t i = 0; i < count; ++i) {
    dests[i] = {i, &results[i], sizeof(uint32_t), GX_FALSE};
  }
  if (AuroraReadbackGet(stream, dests, count)) {
    bool all = true;
    for (uint32_t i = 0; i < count; ++i) {
      all &= dests[i].found != GX_FALSE;
    }
    if (all) {
      for (uint32_t i = 0; i < count; ++i) {
        g_peekResults[i] = results[i];
      }
      g_peeksReady.store(1, std::memory_order_release);
    }
  }
  // Not in the first frame: Aurora begins a frame by clearing the EFB with the copy clear colour it already holds,
  // which in the first frame is its default, not the game's (a reading of that frame showed a black background).
  if (g_frames.load(std::memory_order_relaxed) >= 1) {
    AuroraReadbackCapturePeeks(stream, g_peekRequests, count);
  }
}

// RGBA8 as the hardware writes it (4x4-texel tiles of 64 bytes: alpha and red, then green and blue) to RGBA rows,
// opaque (the XFB has no alpha for the display).
void detile_rgba8(const uint8_t* tiles, uint32_t width, uint32_t height, uint8_t* rows) {
  const uint32_t tilesX = width / 4;
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const uint8_t* tile = tiles + ((y / 4) * tilesX + x / 4) * 64;
      const uint32_t texel = (y % 4) * 4 + x % 4;
      uint8_t* out = rows + (y * width + x) * 4;
      out[0] = tile[texel * 2 + 1];
      out[1] = tile[32 + texel * 2];
      out[2] = tile[32 + texel * 2 + 1];
      out[3] = 255;
    }
  }
}

void present(AuroraReadbackStream stream) {
  const uint32_t physical = g_viXfb.load(std::memory_order_acquire);
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t available = 0;
  const uint8_t* xfb = physical != 0 ? resolve_guest(physical, &available) : nullptr;
  if (xfb == nullptr || !aurora_guest_copy_size(physical, &width, &height) || width % 4 != 0 || height % 4 != 0) {
    return; // no display copy at that address yet
  }
  const uint32_t size = width * height * 4;
  g_presentTiles.resize(size);
  AuroraReadbackDest dest = {0, g_presentTiles.data(), size, GX_FALSE};
  if (AuroraReadbackGet(stream, &dest, 1) && dest.found) {
    g_presentPixels.resize(size);
    detile_rgba8(g_presentTiles.data(), width, height, g_presentPixels.data());
    // The page's thread is not this one: a BroadcastChannel reaches it from any worker (a copy of the rows).
    EM_ASM(
        {
          if (!globalThis.smgPresentChannel) {
            globalThis.smgPresentChannel = new BroadcastChannel('smg-present');
          }
          const pixels = new Uint8Array(wasmMemory.buffer, $0, $1 * $2 * 4).slice();
          globalThis.smgPresentChannel.postMessage({width : $1, height : $2, black : $3, pixels : pixels.buffer});
        },
        g_presentPixels.data(), width, height, g_viBlack.load(std::memory_order_relaxed));
    g_presented.fetch_add(1, std::memory_order_relaxed);
  }
  const AuroraReadbackCopySrc source = {0, xfb};
  AuroraReadbackCaptureCopies(stream, &source, 1);
}

void collect_xfb(AuroraReadbackStream stream) {
  const void* const xfb = g_xfbProbe.load(std::memory_order_acquire);
  if (xfb == nullptr) {
    return;
  }
  const uint32_t size = g_xfbWidth * g_xfbHeight * 4;
  if (g_xfbReady.load(std::memory_order_acquire) == 0) {
    g_xfbData.resize(size);
    AuroraReadbackDest dest = {0, g_xfbData.data(), size, GX_FALSE};
    if (AuroraReadbackGet(stream, &dest, 1) && dest.found) {
      g_xfbPublished = g_xfbData;
      g_xfbReady.store(1, std::memory_order_release);
    }
  }
  // Not in the first frame, as for the peeks
  if (g_frames.load(std::memory_order_relaxed) >= 1) {
    const AuroraReadbackCopySrc source = {0, xfb};
    AuroraReadbackCaptureCopies(stream, &source, 1);
  }
}

// Render thread, at the channel's position in the stream: takes the latest completed capture's values, then captures
// the channel's points here (in the stream's order: Aurora's readback commands follow the game's).
void capture_channel(PeekChannel& channel) {
  const uint32_t count = channel.count.load(std::memory_order_acquire);
  if (count == 0) {
    return;
  }
  if (channel.stream == 0) {
    channel.stream = AuroraReadbackCreateStream();
  }
  uint32_t results[PeekChannel::MaxPoints]{};
  AuroraReadbackDest dests[PeekChannel::MaxPoints];
  for (uint32_t i = 0; i < count; ++i) {
    dests[i] = {i, &results[i], sizeof(uint32_t), GX_FALSE};
  }
  if (AuroraReadbackGet(channel.stream, dests, count)) {
    for (uint32_t i = 0; i < count; ++i) {
      if (dests[i].found) {
        channel.values[i].store(results[i], std::memory_order_relaxed);
        channel.captured[i].store(1, std::memory_order_release);
      }
    }
  }
  AuroraReadbackCapturePeeks(channel.stream, channel.points, count);
}

PeekChannel* find_channel(uint32_t token) {
  for (auto& channel : g_peekChannels) {
    if (channel.token.load(std::memory_order_acquire) == token) {
      return &channel;
    }
  }
  return nullptr;
}

void* render_main(void*) {
  const AuroraConfig config = {
      .appName = "smg_web",
      .logCallback = &log_callback,
      .allowCpuAdapter = true,
      .gxProcessingMode = GX_PROCESSING_DRAIN,
      .surface = {.type = g_headless ? AURORA_SURFACE_HEADLESS : AURORA_SURFACE_SDL_MANAGED,
                  .width = g_headless ? 640u : 0u,
                  .height = g_headless ? 480u : 0u},
  };
  aurora_set_guest_resolver(&resolve_guest);
  aurora_initialize(0, nullptr, &config);
  // Every draw waits for its pipeline. Without a pipeline thread (patch 0004) Aurora builds a few pipelines per frame
  // and drops the draws whose pipeline is not built yet: in the first frames that dropped the clear of the display
  // copy (the EFB kept the previous frame's triangle; at frame 60 it was cleared). The cost is a pause the first time
  // a pipeline appears, instead of a missing draw.
  AuroraSetDrawsBlocking(true);
  const AuroraReadbackStream stream = AuroraReadbackCreateStream();
  const AuroraReadbackStream xfbStream = AuroraReadbackCreateStream();
  const AuroraReadbackStream presentStream = AuroraReadbackCreateStream();
  g_state.store(1, std::memory_order_release);

  bool frameOpen = false;
  uint64_t frameBytes = 0; // the game's stream in the frame being drawn
  for (;;) {
    if (g_stop.load(std::memory_order_acquire)) {
      // Before exit(): with Aurora alive, exit's C++ static destructors wait forever (docs/PUENTE_FIFO.md)
      if (frameOpen) {
        aurora_end_frame();
      }
      aurora_shutdown();
      return nullptr;
    }
    if (!frameOpen) {
      set_phase(PhaseBeginFrame);
      aurora_update();
      frameOpen = aurora_begin_frame();
      if (!frameOpen) {
        set_phase(PhaseBeginRefused);
        g_beginRefused.fetch_add(1, std::memory_order_relaxed);
        emscripten_sleep(1);
        continue;
      }
    }
    const uint32_t head = g_head.load(std::memory_order_relaxed);
    if (head == g_tail.load(std::memory_order_acquire)) {
      // Nothing queued: back to this worker's event loop, where WebGPU delivers its callbacks.
      set_phase(PhaseWaiting);
      emscripten_sleep(1);
      continue;
    }
    const Item item = g_queue[head % QueueSize];
    g_head.store(head + 1, std::memory_order_release);
    if (item.bytes != nullptr) {
      frameBytes += item.size;
      set_phase(PhaseSpan);
      trace_record('S', item.bytes, item.size, 0, 0);
      aurora::gx::fifo::write_data(item.bytes, item.size); // copied: the span goes back to the game now
      nebula_game_gx_release(item.size);
      g_spansDone.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    if (item.coherent) {
      // In stream order: what Aurora reuses of that memory within the frame is dropped (patch 0015)
      uint32_t available = 0;
      if (const uint8_t* host = resolve_guest(item.physical, &available)) {
        aurora_guest_written(host, std::min<uint32_t>(item.size, available));
      }
      continue;
    }
    if (item.peekChannel != 0xFF) {
      set_phase(PhasePeeks);
      capture_channel(g_peekChannels[item.peekChannel]);
      continue;
    }
    set_phase(item.finish ? PhaseEndFrame : PhaseMarker);
    trace_record('M', nullptr, 0, item.finish, item.token);
    if (!item.finish) {
      // A draw-sync token: its channel's points are captured here, where the console's GPU stopped for it
      if (PeekChannel* channel = find_channel(item.token)) {
        capture_channel(*channel);
      }
    }
    if (item.finish) {
      // A frame far larger than the game's usual (tens to hundreds of KB): said, with its size, before Aurora ends it
      if (frameBytes > (32u << 20)) {
        std::fprintf(stderr, "[bridge] frame %u: %u MB of GX\n", g_frames.load(std::memory_order_relaxed),
                     static_cast<unsigned>(frameBytes >> 20));
      }
      frameBytes = 0;
      collect_peeks(stream);
      collect_xfb(xfbStream);
      present(presentStream);
      aurora_end_frame();
      frameOpen = false;
      if (const AuroraStats* stats = aurora_get_stats()) {
        raise_peak(g_peakStorage, stats->lastStorageSize);
        raise_peak(g_peakUniform, stats->lastUniformSize);
        raise_peak(g_peakVerts, stats->lastVertSize);
        raise_peak(g_peakIndices, stats->lastIndexSize);
        raise_peak(g_peakTextures, stats->lastTextureUploadSize);
      }
      g_frames.fetch_add(1, std::memory_order_release);
    }
    nebula_game_gx_event(item.finish, item.token);
    g_markersDone.fetch_add(1, std::memory_order_relaxed);
  }
  return nullptr;
}

} // namespace

extern "C" int smg_bridge_start(int headless) {
  g_headless = headless;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 1 << 20);
  if (pthread_create(&g_renderThread, &attr, render_main, nullptr) != 0) {
    return 0;
  }
  pthread_attr_destroy(&attr);
  while (g_state.load(std::memory_order_acquire) == 0) {
    sched_yield();
  }
  return g_state.load(std::memory_order_acquire) == 1;
}

extern "C" void smg_bridge_describe(char* out, size_t size) {
  const uint32_t queued = g_tail.load(std::memory_order_acquire) - g_head.load(std::memory_order_acquire);
  snprintf(out, size,
           "render thread %s for %lld ms; %u items queued (of %u); %llu spans read, %llu PE markers answered, "
           "%u frames drawn, %llu frames refused by Aurora",
           kPhaseNames[g_phase.load(std::memory_order_relaxed)],
           static_cast<long long>(now_ms() - g_phaseSinceMs.load(std::memory_order_relaxed)), queued, QueueSize,
           static_cast<unsigned long long>(g_spansDone.load(std::memory_order_relaxed)),
           static_cast<unsigned long long>(g_markersDone.load(std::memory_order_relaxed)),
           g_frames.load(std::memory_order_relaxed),
           static_cast<unsigned long long>(g_beginRefused.load(std::memory_order_relaxed)));
}

extern "C" void smg_bridge_buffer_peaks(char* out, size_t size) {
  const auto kb = [](const std::atomic<uint32_t>& v) { return v.load(std::memory_order_relaxed) >> 10; };
  snprintf(out, size, "storage %u KB of 16384, uniform %u KB of 24576, vertices %u KB of 5120, indices %u KB of 2048, "
           "texture uploads %u KB of 24576", kb(g_peakStorage), kb(g_peakUniform), kb(g_peakVerts), kb(g_peakIndices),
           kb(g_peakTextures));
}

extern "C" void smg_bridge_stop(void) {
  if (g_state.load(std::memory_order_acquire) != 1 || g_stop.exchange(1, std::memory_order_acq_rel) != 0) {
    return;
  }
  pthread_join(g_renderThread, nullptr);
}

extern "C" void smg_bridge_exit(int code) {
  smg_bridge_stop();
  exit(code);
}

extern "C" void smg_bridge_coherent(uint32_t physical, uint32_t size) {
  Item item{nullptr, size, 0, 0};
  item.coherent = 1;
  item.physical = physical;
  push(item);
}

extern "C" void smg_bridge_submit(const uint8_t* bytes, size_t size) {
  push(Item{bytes, static_cast<uint32_t>(size), 0, 0});
}

std::atomic<uint64_t> g_markersQueued{0};

extern "C" void smg_bridge_pe_marker(int finish, uint16_t token) {
  g_markersQueued.fetch_add(1, std::memory_order_relaxed);
  push(Item{nullptr, 0, token, static_cast<uint8_t>(finish != 0)});
}

// The game's lead over the renderer, in PE markers still to answer (web/platform/gx_pacing_wasm.c waits on it)
extern "C" unsigned smg_host_gx_markers_behind(void) {
  return static_cast<unsigned>(g_markersQueued.load(std::memory_order_relaxed) -
                               g_markersDone.load(std::memory_order_acquire));
}

extern "C" uint32_t smg_bridge_peek_efb(uint16_t x, uint16_t y, uint32_t type, uint16_t token) {
  g_gamePeeks.fetch_add(1, std::memory_order_relaxed);
  PeekChannel* channel = find_channel(token);
  if (channel == nullptr) {
    for (auto& candidate : g_peekChannels) {
      uint32_t unbound = UINT32_MAX;
      if (candidate.token.compare_exchange_strong(unbound, token, std::memory_order_acq_rel)) {
        channel = &candidate;
        break;
      }
    }
    if (channel == nullptr) {
      return 0; // more tokens than channels
    }
  }
  const auto kind = type == 0 ? AURORA_READBACK_PEEK_ARGB : AURORA_READBACK_PEEK_Z;
  const uint32_t count = channel->count.load(std::memory_order_relaxed); // only this thread adds points
  uint32_t index = 0;
  while (index < count && (channel->points[index].x != x || channel->points[index].y != y ||
                           channel->points[index].kind != kind)) {
    ++index;
  }
  if (index == count) {
    if (count == PeekChannel::MaxPoints) {
      return 0;
    }
    channel->points[index] = {index, kind, x, y};
    channel->count.store(count + 1, std::memory_order_release);
  }
  if (token == 0) {
    // Captured where the game peeks: once per frame, at its first peek of the channel
    const uint32_t frame = g_frames.load(std::memory_order_acquire);
    if (channel->queuedFrame.exchange(frame, std::memory_order_acq_rel) != frame) {
      push(Item{nullptr, 0, 0, 0, static_cast<uint8_t>(channel - g_peekChannels)});
    }
  }
  if (channel->captured[index].load(std::memory_order_acquire) == 0) {
    return 0;
  }
  g_gamePeeksCaptured.fetch_add(1, std::memory_order_relaxed);
  return channel->values[index].load(std::memory_order_relaxed);
}

extern "C" int smg_bridge_game_peeks(uint32_t* captured) {
  *captured = g_gamePeeksCaptured.exchange(0, std::memory_order_relaxed);
  return static_cast<int>(g_gamePeeks.exchange(0, std::memory_order_relaxed));
}

extern "C" void smg_bridge_vi_frame(uint32_t xfb, int black) {
  g_viBlack.store(black, std::memory_order_relaxed);
  g_viXfb.store(xfb, std::memory_order_release);
}

extern "C" uint32_t smg_bridge_presented(void) { return g_presented.load(std::memory_order_relaxed); }

extern "C" uint32_t smg_bridge_vi_xfb(void) { return g_viXfb.load(std::memory_order_relaxed); }

extern "C" uint32_t smg_bridge_frames(void) { return g_frames.load(std::memory_order_acquire); }

extern "C" int smg_bridge_set_peeks(const uint16_t (*points)[2], uint32_t count) {
  if (count > MaxPeeks) {
    return 0;
  }
  for (uint32_t i = 0; i < count; ++i) {
    g_peekRequests[i] = {i, AURORA_READBACK_PEEK_ARGB, points[i][0], points[i][1]};
  }
  g_peekCount.store(count, std::memory_order_release);
  return 1;
}

extern "C" void smg_bridge_set_xfb_probe(const void* xfb, uint32_t width, uint32_t height) {
  g_xfbWidth = width;
  g_xfbHeight = height;
  g_xfbProbe.store(xfb, std::memory_order_release);
}

extern "C" int smg_bridge_get_xfb(const uint16_t (*points)[2], uint32_t count, uint32_t* argb) {
  if (!g_xfbReady.load(std::memory_order_acquire)) {
    return 0;
  }
  const uint32_t tilesX = g_xfbWidth / 4;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t x = points[i][0];
    const uint32_t y = points[i][1];
    const uint8_t* tile = g_xfbPublished.data() + ((y / 4) * tilesX + x / 4) * 64;
    const uint32_t texel = (y % 4) * 4 + x % 4;
    const uint32_t a = tile[texel * 2], r = tile[texel * 2 + 1];
    const uint32_t g = tile[32 + texel * 2], b = tile[32 + texel * 2 + 1];
    argb[i] = a << 24 | r << 16 | g << 8 | b;
  }
  return 1;
}

extern "C" int smg_bridge_get_peeks(uint32_t* argb, uint32_t count) {
  if (!g_peeksReady.load(std::memory_order_acquire) || count > g_peekCount.load(std::memory_order_acquire)) {
    return 0;
  }
  for (uint32_t i = 0; i < count; ++i) {
    argb[i] = g_peekResults[i];
  }
  return 1;
}
