// THP movies: the host's thp_video_decode (nebula_game_api.h) on Nebula's decoder (GPL-3.0, compiled unchanged from
// third_party/nebula/runtime/src/native_thp_video.cpp; in WebAssembly its fused multiply-adds come from
// web/host/compat/galaxy/fused_multiply_add.h).
//
// The game's movie player (THPSimplePlayerWrapper) hands each frame's video component to the host, which writes the
// Y, U and V planes in GX's I8 tiling into the game's textures. The player does not flush them afterwards (on the
// console the decoder's own writes reach memory), so the renderer is told here that those ranges changed, in stream
// order, as the game's cache flushes do (smg_bridge_coherent). Nebula's host does the same
// (native/host/host_main.cpp, svc_thp_video_decode).
#include "thp_video.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <span>

#include "galaxy/native_thp_video.h"

#if defined(SMG_HOST_AURORA)
#include <nebula_game_api.h>

#include "aurora_bridge.h"
#endif

namespace {

std::atomic<std::uint64_t> g_decoded{0};
std::atomic<std::uint64_t> g_refused{0};
std::atomic<std::uint64_t> g_nanoseconds{0};

// Bytes of an I8 plane of w x h texels: 8x4 tiles of 32 bytes
std::size_t plane_bytes(std::size_t width, std::size_t height) {
    return ((width + 7) / 8) * ((height + 3) / 4) * 32;
}

#if defined(SMG_HOST_AURORA)
// The game's memory, looked up once (the regions do not move)
void tell_renderer(const void* plane, std::size_t size) {
    static NebulaGameMemoryRegion regions[4];
    static const int count = nebula_game_memory_regions(regions, 4);
    const auto* p = static_cast<const std::uint8_t*>(plane);
    for (int i = 0; i < count; ++i) {
        const NebulaGameMemoryRegion& region = regions[i];
        if (p >= region.host && p + size <= region.host + region.size) {
            smg_bridge_coherent(region.physical + static_cast<std::uint32_t>(p - region.host), static_cast<std::uint32_t>(size));
            return;
        }
    }
}
#endif

}  // namespace

extern "C" int smg_thp_frame_size(const void* frame, uint32_t size, uint16_t* width, uint16_t* height) {
    const auto* p = static_cast<const std::uint8_t*>(frame);
    if (frame == nullptr || size < 4 || p[0] != 0xFF || p[1] != 0xD8) {
        return 0;
    }
    // Segments up to the scan (SOS): each is FF, marker, then a big-endian length that counts itself
    std::size_t at = 2;
    while (at + 4 <= size && p[at] == 0xFF && p[at + 1] != 0xDA) {
        const std::size_t length = static_cast<std::size_t>(p[at + 2]) << 8 | p[at + 3];
        if (length < 2 || at + 2 + length > size) {
            return 0;
        }
        if (p[at + 1] == 0xC0) {  // SOF0: precision, height, width
            if (length < 7) {
                return 0;
            }
            *height = static_cast<std::uint16_t>(p[at + 5] << 8 | p[at + 6]);
            *width = static_cast<std::uint16_t>(p[at + 7] << 8 | p[at + 8]);
            return *width != 0 && *height != 0;
        }
        at += 2 + length;
    }
    return 0;
}

extern "C" int32_t smg_thp_video_decode(const void* frame, uint32_t size, void* y, void* u, void* v) {
    const auto start = std::chrono::steady_clock::now();
    std::uint16_t width = 0, height = 0;
    int32_t result = -1;
    if (smg_thp_frame_size(frame, size, &width, &height)) {
        const std::size_t luma = plane_bytes(width, height);
        const std::size_t chroma = plane_bytes((width + 1u) / 2u, (height + 1u) / 2u);
        result = galaxy::thp::decode_video({static_cast<const std::uint8_t*>(frame), size}, width, height,
                                           {static_cast<std::uint8_t*>(y), luma}, {static_cast<std::uint8_t*>(u), chroma},
                                           {static_cast<std::uint8_t*>(v), chroma});
#if defined(SMG_HOST_AURORA)
        if (result == 0) {
            tell_renderer(y, luma);
            tell_renderer(u, chroma);
            tell_renderer(v, chroma);
        }
#endif
    }
    (result == 0 ? g_decoded : g_refused).fetch_add(1, std::memory_order_relaxed);
    g_nanoseconds.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - start).count()),
                            std::memory_order_relaxed);
    return result;
}

extern "C" void smg_thp_stats(uint64_t* decoded, uint64_t* refused, uint64_t* nanoseconds) {
    *decoded = g_decoded.load(std::memory_order_relaxed);
    *refused = g_refused.load(std::memory_order_relaxed);
    *nanoseconds = g_nanoseconds.load(std::memory_order_relaxed);
}
