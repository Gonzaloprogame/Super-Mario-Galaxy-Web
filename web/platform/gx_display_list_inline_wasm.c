/* GXCallDisplayList with the list's commands copied into the stream (linked with --wrap=GXCallDisplayList,
   web/cmake/SmgGame.cmake): the renderer gets them in the stream, instead of the list's address to read later.

   The web host's renderer (Aurora, web/host/aurora_bridge.cpp) reads the stream on its own thread, behind the game; a
   call by address made it read the list from game memory when it got there. Nebula's renderer reads the stream at
   each PE marker, on the game's thread, before the game goes on; the web host cannot make the game wait for it
   (a wait there stops every game thread: they share one wasm thread). With software WebGPU the renderer fell seconds
   behind, the game reused a list's memory as the first stage loaded, and Aurora stopped on what it read there ("draw
   vertex data overrun"). A list is plain GX commands (no call inside it, no Nebula marker), so running it in the
   stream is what calling it does.

   The original function runs first with size 0: it brings the GX state up to date (what the list's commands rely on)
   and writes a call of nothing; the list's bytes follow in one reservation, contiguous in the stream. */
#include <string.h>

#include <revolution/gx/GXDispList.h>

#include <nebula_gx_fifo.h>

void __real_GXCallDisplayList(const void* list, u32 nbytes);

void __wrap_GXCallDisplayList(const void* list, u32 nbytes) {
    __real_GXCallDisplayList(list, 0);
    if (nbytes != 0) {
        memcpy(__NebulaGXReserve(nbytes), list, nbytes);
    }
}
