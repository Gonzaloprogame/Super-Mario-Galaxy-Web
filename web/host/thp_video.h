/* THP movie frames for the web engine (web/host/thp_video.cpp): Nebula's decoder behind the host's thp_video_decode. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A frame's video component is one baseline JPEG; its SOF0 segment gives the movie's size. 1 with the size set, 0 when
   the bytes are not such a frame. */
int smg_thp_frame_size(const void* frame, uint32_t size, uint16_t* width, uint16_t* height);

/* The host's thp_video_decode: writes the frame's Y, U and V planes (GX I8 tiles) into the game's buffers and tells the
   renderer they changed. 0 on success, as THPVideoDecode returns. Called on the game's simulation thread. */
int32_t smg_thp_video_decode(const void* frame, uint32_t size, void* y, void* u, void* v);

/* Frames decoded, frames refused, and the decoder's total time (for the host report). */
void smg_thp_stats(uint64_t* decoded, uint64_t* refused, uint64_t* nanoseconds);

#ifdef __cplusplus
}
#endif
