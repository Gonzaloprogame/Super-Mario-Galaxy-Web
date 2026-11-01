/* Replays a GX trace (a renderer failure's, copied from the engine's log: web/host/aurora_bridge.cpp) through the same
   bridge to Aurora, without the game: the spans and PE markers in their order. The page (gx_replay.html) writes the
   decoded trace to /trace.bin before main runs. The game's memory is zeros (a texture or array the trace names reads
   as zero); what a replay shows is whether Aurora reads the stream to its end, and what it draws of it.
   Reports {markers, frames, fatal} on globalThis.replay. */
#include <emscripten.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <nebula_game_api.h>

#include "aurora_bridge.h"

/* The game module's side of the bridge, which a replay has no game for */
void nebula_game_gx_release(size_t size) {
    (void)size; /* the trace's spans stay allocated */
}

void nebula_game_gx_event(int finish, uint16_t token) {
    (void)finish;
    (void)token;
}

/* The game's memory as zeros, at the module's physical layout (native/sdk/memory.c): MEM1, MEM2, the image window */
int nebula_game_memory_regions(NebulaGameMemoryRegion* out, int max) {
    static uint8_t* mem1;
    static uint8_t* mem2;
    static uint8_t* image;
    enum { kSize = 64 << 20 };
    if (mem1 == NULL) {
        mem1 = calloc(1, kSize);
        mem2 = calloc(1, kSize);
        image = calloc(1, kSize);
    }
    const NebulaGameMemoryRegion regions[3] = {
        {0x00000000u, kSize, mem1}, {0x10000000u, kSize, mem2}, {0x08000000u, kSize, image}};
    for (int i = 0; i < 3 && i < max; i++) {
        out[i] = regions[i];
    }
    return 3;
}

int main(void) {
    FILE* file = fopen("/trace.bin", "rb");
    if (file == NULL) {
        fprintf(stderr, "[replay] no trace\n");
        return 2;
    }
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t* trace = malloc((size_t)size);
    if (fread(trace, 1, (size_t)size, file) != (size_t)size) {
        fprintf(stderr, "[replay] the trace cannot be read\n");
        return 2;
    }
    fclose(file);
    if (!smg_bridge_start(1)) {
        fprintf(stderr, "[replay] Aurora did not start\n");
        return 2;
    }
    uint32_t spans = 0, markers = 0, finishes = 0;
    for (long at = 0; at < size;) {
        if (trace[at] == 'S' && at + 5 <= size) {
            const uint32_t length = (uint32_t)trace[at + 1] << 24 | (uint32_t)trace[at + 2] << 16 |
                                    (uint32_t)trace[at + 3] << 8 | trace[at + 4];
            smg_bridge_submit(trace + at + 5, length); /* stays valid: the trace is never freed */
            at += 5 + length;
            spans++;
        } else if (trace[at] == 'M' && at + 4 <= size) {
            const int finish = trace[at + 1] != 0;
            smg_bridge_pe_marker(finish, (uint16_t)(trace[at + 2] << 8 | trace[at + 3]));
            at += 4;
            markers++;
            finishes += finish;
        } else {
            fprintf(stderr, "[replay] the trace is malformed at %ld\n", at);
            return 2;
        }
    }
    printf("[replay] %u spans and %u PE markers (%u draw done) given to Aurora\n", spans, markers, finishes);
    /* Every draw done ends a frame: wait for them (a renderer failure is reported to the page meanwhile) */
    for (int waited = 0; smg_bridge_frames() < finishes && waited < 600; waited++) {
        struct timespec pause = {0, 100 * 1000 * 1000};
        nanosleep(&pause, NULL);
    }
    printf("[replay] Aurora drew %u of %u frames\n", smg_bridge_frames(), finishes);
    MAIN_THREAD_ASYNC_EM_ASM({ globalThis.replay.frames = $0; globalThis.replay.expected = $1; }, smg_bridge_frames(),
                             finishes);
    /* Not exit(): with Aurora's thread alive, exit's C++ static destructors wait forever on the page's thread, which
       then stops answering (docs/PUENTE_FIFO.md). The page keeps showing the last frame. */
    for (;;) {
        struct timespec pause = {1, 0};
        nanosleep(&pause, NULL);
    }
}
