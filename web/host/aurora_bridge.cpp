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
#include <aurora/guest_memory.h>
#include <dolphin/gx/GXAurora.h>

#include "gx/fifo.hpp"

#include <emscripten.h>
#include <pthread.h>

#include <atomic>
#include <vector>
#include <cstdio>
#include <cstdlib>

#include <nebula_game_api.h>

namespace {

struct Item {
  const uint8_t* bytes; // a span, or nullptr for a marker
  uint32_t size;
  uint16_t token;
  uint8_t finish;
};

constexpr uint32_t QueueSize = 4096;
Item g_queue[QueueSize];
std::atomic<uint32_t> g_head{0}; // next item to read (render thread)
std::atomic<uint32_t> g_tail{0}; // next item to write (simulation thread)

std::atomic<int> g_state{0}; // 0 starting, 1 ready, -1 failed
std::atomic<uint32_t> g_frames{0};
int g_headless = 0;

constexpr uint32_t MaxPeeks = 16;
AuroraReadbackPeekRequest g_peekRequests[MaxPeeks];
std::atomic<uint32_t> g_peekCount{0};
uint32_t g_peekResults[MaxPeeks];
std::atomic<int> g_peeksReady{0};

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

void log_callback(AuroraLogLevel level, const char* module, const char* message, unsigned int) {
  static const char* const names[] = {"DEBUG", "INFO", "WARNING", "ERROR", "FATAL"};
  if (level >= LOG_INFO) {
    fprintf(stderr, "[aurora %s: %s] %s\n", names[level], module, message);
  }
  if (level == LOG_FATAL) {
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
  g_state.store(1, std::memory_order_release);

  bool frameOpen = false;
  for (;;) {
    if (!frameOpen) {
      aurora_update();
      frameOpen = aurora_begin_frame();
      if (!frameOpen) {
        emscripten_sleep(1);
        continue;
      }
    }
    const uint32_t head = g_head.load(std::memory_order_relaxed);
    if (head == g_tail.load(std::memory_order_acquire)) {
      // Nothing queued: back to this worker's event loop, where WebGPU delivers its callbacks.
      emscripten_sleep(1);
      continue;
    }
    const Item item = g_queue[head % QueueSize];
    g_head.store(head + 1, std::memory_order_release);
    if (item.bytes != nullptr) {
      aurora::gx::fifo::write_data(item.bytes, item.size); // copied: the span goes back to the game now
      nebula_game_gx_release(item.size);
      continue;
    }
    if (item.finish) {
      collect_peeks(stream);
      collect_xfb(xfbStream);
      aurora_end_frame();
      frameOpen = false;
      g_frames.fetch_add(1, std::memory_order_release);
    }
    nebula_game_gx_event(item.finish, item.token);
  }
  return nullptr;
}

} // namespace

extern "C" int smg_bridge_start(int headless) {
  g_headless = headless;
  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 1 << 20);
  if (pthread_create(&thread, &attr, render_main, nullptr) != 0) {
    return 0;
  }
  pthread_attr_destroy(&attr);
  while (g_state.load(std::memory_order_acquire) == 0) {
    sched_yield();
  }
  return g_state.load(std::memory_order_acquire) == 1;
}

extern "C" void smg_bridge_submit(const uint8_t* bytes, size_t size) {
  push(Item{bytes, static_cast<uint32_t>(size), 0, 0});
}

extern "C" void smg_bridge_pe_marker(int finish, uint16_t token) {
  push(Item{nullptr, 0, token, static_cast<uint8_t>(finish != 0)});
}

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
