/* Aurora as the renderer of the game's GX stream (web/host/aurora_bridge.cpp). */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the render thread: it owns the WebGPU device and Aurora (WebGPU objects belong to the thread that made
   them). Returns 1 once Aurora is ready, 0 if it could not start. headless: render the EFB offscreen (tests). */
int smg_bridge_start(int headless);

/* Before the process exits (any thread but the render thread): the render thread ends its frame, shuts Aurora down
   and returns; then exit() can run the C++ static destructors (with Aurora alive one of them waits forever). */
void smg_bridge_stop(void);

/* From the game's simulation thread, in stream order (the host's gx_submit and gx_pe_marker). They do not allocate:
   the game owns operator new on that thread (web/platform/operator_new_wasm.cpp). */
void smg_bridge_submit(const uint8_t* bytes, size_t size);
void smg_bridge_pe_marker(int finish, uint16_t token);
/* The host's gx_peek_efb: an EFB pixel (type 0 colour as GXPeekARGB, 1 depth as GXPeekZ) at the stream position of
   draw-sync token `token` (0: where the game peeks), answered from a capture of an earlier frame; 0 until the first. */
uint32_t smg_bridge_peek_efb(uint16_t x, uint16_t y, uint32_t type, uint16_t token);
/* The host's vi_frame: the XFB the VI shows from this retrace (physical address, 0: none) and whether it is black.
   The bridge posts that XFB's display copy to the page (BroadcastChannel "smg-present", web/shell/present.js). */
void smg_bridge_vi_frame(uint32_t xfb, int black);
/* Frames posted to the page. */
uint32_t smg_bridge_presented(void);
/* The XFB the VI shows (physical address, 0: none). */
uint32_t smg_bridge_vi_xfb(void);
/* Tests: the game's peeks since the last call, and in *captured how many were answered from a capture. */
int smg_bridge_game_peeks(uint32_t* captured);

/* Tests. smg_bridge_frames: frames Aurora has drawn. smg_bridge_set_peeks: EFB points (x, y) whose colour is read at
   each frame's end (GXPeekARGB's 0xAARRGGBB, through Aurora's readback). smg_bridge_get_peeks: 1 and the colours once
   a readback has completed, else 0. */
uint32_t smg_bridge_frames(void);
int smg_bridge_set_peeks(const uint16_t (*points)[2], uint32_t count);
int smg_bridge_get_peeks(uint32_t* argb, uint32_t count);

/* Tests. smg_bridge_set_xfb_probe: a display copy destination (GXCopyDisp, width x height) read back at each frame's
   end. smg_bridge_get_xfb: 1 and the colours (0xAARRGGBB) at the points, once a readback has completed, else 0. */
void smg_bridge_set_xfb_probe(const void* xfb, uint32_t width, uint32_t height);
int smg_bridge_get_xfb(const uint16_t (*points)[2], uint32_t count, uint32_t* argb);

#ifdef __cplusplus
}
#endif
