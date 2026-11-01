/* The game's sound for the page (web/host/audio_output.cpp): each AI block, big-endian 16-bit stereo in the game's
   memory, reaches the ring the page's AudioWorklet plays in host order, across the ring's end, and a block is dropped
   rather than overwrite what the page has not read. The game's memory is one region here, as the module gives it. */
#include <stdio.h>
#include <string.h>

#include <nebula_game_api.h>

#include "audio_output.h"

static int failures;

static void check(int ok, const char* what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += !ok;
}

enum { kPhysical = 0x10000000 };
static uint8_t sMemory[1 << 16];

int nebula_game_memory_regions(NebulaGameMemoryRegion* out, int max) {
    if (max < 1) {
        return 0;
    }
    out[0].physical = kPhysical;
    out[0].size = sizeof(sMemory);
    out[0].host = sMemory;
    return 1;
}

/* `frames` stereo frames at the start of the game's memory, big-endian as the AI reads them: left `first + i`,
   right `-(first + i)` */
static void put_block(int first, uint32_t frames) {
    for (uint32_t i = 0; i < frames; i++) {
        const int16_t left = (int16_t)(first + (int)i), right = (int16_t)-(first + (int)i);
        sMemory[i * 4] = (uint8_t)((uint16_t)left >> 8);
        sMemory[i * 4 + 1] = (uint8_t)left;
        sMemory[i * 4 + 2] = (uint8_t)((uint16_t)right >> 8);
        sMemory[i * 4 + 3] = (uint8_t)right;
    }
}

static int frames_are(const SmgAudioRing* ring, uint32_t from, int first, uint32_t frames) {
    for (uint32_t i = 0; i < frames; i++) {
        const uint32_t slot = (from + i) % (uint32_t)ring->capacity;
        if (ring->samples[slot * 2] != (int16_t)(first + (int)i) || ring->samples[slot * 2 + 1] != (int16_t)-(first + (int)i)) {
            return 0;
        }
    }
    return 1;
}

int main(void) {
    SmgAudioRing* ring = smg_audio_ring();
    check(ring->magic == SMG_AUDIO_RING_MAGIC && ring->capacity == SMG_AUDIO_RING_FRAMES, "the ring says what it is");

    /* A first block: host order in the ring, the rate recorded, `write` past it */
    put_block(0x1234, 64);
    smg_audio_block(kPhysical, 64 * 4, 32000);
    check(ring->write == 64 && ring->rate == 32000, "a block moves the ring's write by its frames, with its rate");
    check(frames_are(ring, 0, 0x1234, 64), "the samples are in host order (big-endian in the game's memory)");

    /* Across the ring's end: the page has read up to 3 frames before the end */
    ring->write = ring->read = SMG_AUDIO_RING_FRAMES - 3;
    put_block(-500, 8);
    smg_audio_block(kPhysical, 8 * 4, 48000);
    check(ring->write == SMG_AUDIO_RING_FRAMES + 5 && frames_are(ring, SMG_AUDIO_RING_FRAMES - 3, -500, 8),
          "a block across the ring's end goes on at its start");

    /* The page reads nothing: once the ring is full, blocks are dropped and what it holds stays */
    ring->write = ring->read = 0;
    const int dropped = ring->dropped;
    for (int i = 0; i < SMG_AUDIO_RING_FRAMES / 64; i++) {
        put_block(i, 64);
        smg_audio_block(kPhysical, 64 * 4, 32000);
    }
    put_block(7777, 64);
    smg_audio_block(kPhysical, 64 * 4, 32000);
    check(ring->write == SMG_AUDIO_RING_FRAMES && ring->dropped == dropped + 1 && frames_are(ring, 0, 0, 64),
          "a full ring drops the next block, keeping what the page has not read");

    /* A block outside the game's memory is ignored */
    const int32_t write = ring->write;
    smg_audio_block(0x0F000000, 64 * 4, 32000);
    check(ring->write == write, "a block outside the game's memory is ignored");

    puts(failures == 0 ? "all passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
