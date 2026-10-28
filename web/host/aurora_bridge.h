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

/* From the game's simulation thread, in stream order (the host's gx_submit and gx_pe_marker). They do not allocate:
   the game owns operator new on that thread (web/platform/operator_new_wasm.cpp). */
void smg_bridge_submit(const uint8_t* bytes, size_t size);
void smg_bridge_pe_marker(int finish, uint16_t token);

/* Tests. smg_bridge_frames: frames Aurora has drawn. smg_bridge_set_peeks: EFB points (x, y) whose colour is read at
   each frame's end (GXPeekARGB's 0xAARRGGBB, through Aurora's readback). smg_bridge_get_peeks: 1 and the colours once
   a readback has completed, else 0. */
uint32_t smg_bridge_frames(void);
int smg_bridge_set_peeks(const uint16_t (*points)[2], uint32_t count);
int smg_bridge_get_peeks(uint32_t* argb, uint32_t count);

#ifdef __cplusplus
}
#endif
