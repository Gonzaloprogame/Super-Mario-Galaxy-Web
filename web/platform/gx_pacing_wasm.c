/* The game's lead over the renderer, bounded as the console's FIFO bounds the CPU's lead over the GPU (linked with
   --wrap=GXSetDrawDone and --wrap=GXSetDrawSync, web/cmake/SmgGame.cmake).

   The web host's renderer (Aurora) reads the stream on its own thread. Its command ring (16 MB) let the game run
   hundreds of frames ahead with a slow renderer (software WebGPU): the renderer then read the game's memory (vertex
   arrays, textures, display lists) long after the game had moved on and reused it as the first stage loaded, and its
   thread aborted. After each PE marker (GXSetDrawDone, GXSetDrawSync), while the renderer has more than
   kMaxMarkersBehind markers still to answer, the game's thread sleeps in the game's OS (OSSleepTicks): the game's other
   threads (its loader, its audio) run meanwhile, as on the console while the CPU waits for the GPU. A wait in the host
   would stop them all: they share one wasm thread. No wait where interrupts are disabled (no sleeping there). */
#include <revolution/gx.h>
#include <revolution/os.h>

enum { kMaxMarkersBehind = 6 }; /* two or three frames: each has a draw done and a few draw-sync tokens */

/* PE markers the renderer has still to answer (web/host/aurora_bridge.cpp); none without a renderer */
__attribute__((weak)) unsigned smg_host_gx_markers_behind(void) {
    return 0;
}

static void wait_for_renderer(void) {
    const BOOL enabled = OSDisableInterrupts();
    OSRestoreInterrupts(enabled);
    if (!enabled) {
        return;
    }
    while (smg_host_gx_markers_behind() > kMaxMarkersBehind) {
        OSSleepTicks(OSMillisecondsToTicks(1));
    }
}

void __real_GXSetDrawDone(void);
void __real_GXSetDrawSync(u16 token);

void __wrap_GXSetDrawDone(void) {
    __real_GXSetDrawDone();
    wait_for_renderer();
}

void __wrap_GXSetDrawSync(u16 token) {
    __real_GXSetDrawSync(token);
    wait_for_renderer();
}
