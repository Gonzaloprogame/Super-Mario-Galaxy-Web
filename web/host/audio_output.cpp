// The game's sound on the page: its JAudio DSP program run by Nebula's host mixer (third_party/nebula/runtime,
// jaudio_host_mixer.cpp, compiled from there), and each AI DMA block the audio interface takes copied to a ring of
// 16-bit stereo samples in the engine's shared memory, which the page's AudioWorklet plays (web/shell/audio_worklet.js).
//
// The mixer's commands are dispatched as Nebula's own host does (runtime/src/native_host.cpp,
// NativeGameAudio::dsp_command): 0x81 the tables, 0x8E the wave memory, 0x82 a frame, a one-word release of a halted
// subframe mixes it. Its addresses are the game's physical ones, resolved through the game module's memory regions.
#include "audio_output.h"

#include <atomic>
#include <cstring>

#include <nebula_game_api.h>

#include "galaxy/jaudio_host_mixer.h"

namespace {

NebulaGameMemoryRegion g_regions[4];
int g_regionCount = 0;

std::byte* resolve(void*, std::uint32_t address, std::uint32_t size) {
    if (g_regionCount == 0) {
        const int count = nebula_game_memory_regions(g_regions, 4);
        g_regionCount = count < 4 ? count : 4;
    }
    // Physical, or a cached or uncached virtual address of it
    const std::uint32_t physical = address & 0x3FFFFFFFu;
    for (int i = 0; i < g_regionCount; ++i) {
        const NebulaGameMemoryRegion& region = g_regions[i];
        const std::uint32_t offset = physical - region.physical;
        if (physical >= region.physical && offset < region.size && size <= region.size - offset) {
            return reinterpret_cast<std::byte*>(region.host) + offset;
        }
    }
    return nullptr;
}

galaxy::host::JAudioHostMixer g_mixer;
const galaxy::host::JAudioGuestMemory g_guest{nullptr, &resolve};

// The ring the page reads (layout in audio_output.h): written here, on the AI thread; read by the page's worklet
alignas(64) SmgAudioRing g_ring;

}  // namespace

extern "C" int smg_audio_dsp(const std::uint32_t* words, std::uint32_t count) {
    const auto word = [&](std::uint32_t index) { return index < count ? words[index] : 0u; };
    if (count == 0u) {
        return g_mixer.release_halt(words[0], g_guest) ? 1 : 0;
    }
    switch (words[0] >> 24u) {
    case 0x81u:
        if (count >= 5u) {
            g_mixer.setup(words[0], word(1u), word(2u), word(3u), word(4u), g_guest);
        } else {
            g_mixer.note_ignored_command();
        }
        break;
    case 0x8Eu:
        g_mixer.set_varam(word(1u));
        break;
    case 0x82u:
        // Every volume category at full: the page has one volume
        g_mixer.set_bus_gains({1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
        g_mixer.sync_frame(words[0], word(1u), word(2u), word(3u), word(4u), g_guest);
        break;
    default:
        g_mixer.note_ignored_command();
        break;
    }
    return 0;
}

extern "C" void smg_audio_block(std::uint32_t physical, std::uint32_t size, std::uint32_t rate) {
    const std::uint32_t frames = size / 4u; // stereo 16-bit
    const std::byte* source = resolve(nullptr, physical, frames * 4u);
    if (source == nullptr || frames == 0u) {
        return;
    }
    std::atomic_ref<std::int32_t> write{g_ring.write};
    std::atomic_ref<std::int32_t> read{g_ring.read};
    g_ring.rate = static_cast<std::int32_t>(rate);
    std::uint32_t at = static_cast<std::uint32_t>(write.load(std::memory_order_relaxed));
    const std::uint32_t pending = at - static_cast<std::uint32_t>(read.load(std::memory_order_acquire));
    if (pending + frames > SMG_AUDIO_RING_FRAMES) {
        // The page reads nothing (no audio yet, a hidden tab): the block is dropped rather than overwrite what it has
        g_ring.dropped++;
        return;
    }
    // Big-endian, as the console's AI reads them. nebula_game_api.h says host order, but measured on the game's own
    // blocks (the title's music) host order reads as full-scale noise (RMS 18902, mean step 21818) and big-endian as
    // music (RMS 2798, mean step 365); Nebula's host plays them with submit_pcm16_be_stereo.
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(source);
    const auto be16 = [](const std::uint8_t* p) {
        return static_cast<std::int16_t>(static_cast<std::uint16_t>(p[0] << 8 | p[1]));
    };
    for (std::uint32_t i = 0; i < frames; ++i, ++at) {
        const std::uint32_t slot = at % SMG_AUDIO_RING_FRAMES;
        g_ring.samples[slot * 2u] = be16(bytes + i * 4u);
        g_ring.samples[slot * 2u + 1u] = be16(bytes + i * 4u + 2u);
    }
    write.store(static_cast<std::int32_t>(at), std::memory_order_release);
}

extern "C" SmgAudioRing* smg_audio_ring(void) {
    g_ring.magic = SMG_AUDIO_RING_MAGIC;
    g_ring.capacity = SMG_AUDIO_RING_FRAMES;
    return &g_ring;
}

extern "C" void smg_audio_stats(char* out, size_t size) {
    const auto& s = g_mixer.stats();
    snprintf(out, size,
             "frames %llu, subframes %llu, channel renders %llu, ADPCM blocks %llu, streams %llu, unresolved reads %llu; "
             "ring: %d frames written, %d read, %d blocks dropped",
             static_cast<unsigned long long>(s.frames), static_cast<unsigned long long>(s.subframes),
             static_cast<unsigned long long>(s.channel_renders), static_cast<unsigned long long>(s.adpcm_blocks),
             static_cast<unsigned long long>(s.stream_renders), static_cast<unsigned long long>(s.unresolved_reads),
             std::atomic_ref<std::int32_t>{g_ring.write}.load(std::memory_order_relaxed),
             std::atomic_ref<std::int32_t>{g_ring.read}.load(std::memory_order_relaxed), g_ring.dropped);
}
