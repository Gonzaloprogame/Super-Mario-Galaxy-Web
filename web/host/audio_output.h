/* The game's sound for the page (web/host/audio_output.cpp): its JAudio DSP program mixed on the host, and the AI's
   blocks in a ring of samples the page's AudioWorklet plays (web/shell/audio_worklet.js). */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The ring, in the engine's shared memory: 32-bit words, then the samples. write and read count stereo frames
   (wrapping at 2^32, taken modulo capacity); the host writes `write`, the page `read`. */
#define SMG_AUDIO_RING_FRAMES 16384 /* about half a second at 32 kHz */
#define SMG_AUDIO_RING_MAGIC 0x534D4741 /* "SMGA" */
typedef struct {
    int32_t magic;
    int32_t capacity; /* frames */
    int32_t rate;     /* the AI's output rate (Hz) of the latest block */
    int32_t write;
    int32_t read;
    int32_t dropped;  /* blocks dropped because the page did not read */
    int32_t reserved[2];
    int16_t samples[SMG_AUDIO_RING_FRAMES * 2];
} SmgAudioRing;

/* The JAudio DSP program (the host service audio_dsp): 1 when the words completed a subframe */
int smg_audio_dsp(const uint32_t* words, uint32_t count);
/* An AI DMA block the audio interface takes: `size` bytes of 16-bit stereo (big-endian) at physical `physical` */
void smg_audio_block(uint32_t physical, uint32_t size, uint32_t rate);
SmgAudioRing* smg_audio_ring(void);
void smg_audio_stats(char* out, size_t size);

#ifdef __cplusplus
}
#endif
