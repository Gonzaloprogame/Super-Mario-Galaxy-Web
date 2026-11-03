// The global operator new and delete of a wasm binary that holds the game module and C++ host code (Aurora).
//
// The game defines the global operator new/delete (JKRHeap.cpp): every `new` in the game takes memory from the current
// JKRHeap, in the game's arena. Nebula keeps the game in its own native module, where those definitions stay local
// (docs/NATIVE_PORT.md, "Module layout"). A wasm binary is one link, and Emscripten's dynamic linking does not keep
// them local (operator new is exported whatever the visibility) and is experimental with pthreads, so the binary is
// linked with --wrap for the four replaceable forms the game defines (web/game/CMakeLists.txt) and these decide:
//
// - new: the game's (JKRHeap) on the game's simulation thread once it has booted; malloc everywhere else (Aurora on
//   its render thread, static constructors, the host). Host callbacks the game makes on the simulation thread must
//   not allocate with new (web/host keeps them to C and fixed queues), except inside a host-allocation scope
//   (nebula_web_host_allocations): C++ host code the simulation thread has to run, as the export of Aurora's
//   pipelines (web/host/aurora_bridge.cpp), whose vectors must not take the game's heap. Without the scope they did,
//   and JKRHeap returns null when its current heap has no room: the vector then wrote its data at address 0, over
//   the binary's static data (CocoonExGalaxy's crash, "table index is out of bounds", docs/PROGRESO_AUTONOMO.md).
// - delete: by address. JKRHeap memory is always inside the game's arena and malloc memory never is.
// C headers: this file is built with the game's options, against the game's C++ library (MSL), which has no <cstdint>.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
uint8_t* nebula_arena_base(void);  // sdk/memory.c
int nebula_web_in_game_context(void);  // web/platform/context_switch_wasm.c

void* __real__Znwm(size_t size);
void* __real__Znam(size_t size);
void __real__ZdlPv(void* p);
void __real__ZdaPv(void* p);

static int in_arena(const void* p) {
    const uint8_t* const arena = nebula_arena_base();
    const uintptr_t kArenaSize = 0x20000000u;  // memory.c's kReserveSize
    return arena != nullptr && (uintptr_t)p - (uintptr_t)arena < kArenaSize;
}

size_t emscripten_get_heap_size(void);

// Outside the game, new without exceptions: a failed malloc would hand back NULL, and in wasm writing through it does
// not trap (address 0 is memory), so the failure would show up later as anything. It stops here, saying so.
static void* host_new(size_t size) {
    void* const p = malloc(size != 0 ? size : 1);
    if (p == nullptr) {
        fprintf(stderr, "[host] out of memory: new of %zu bytes failed outside the game (wasm memory %zu MB)\n", size,
                emscripten_get_heap_size() >> 20);
        abort();
    }
    return p;
}

// Open host-allocation scopes on the simulation thread (only that thread changes it, and it only matters there: every
// other thread takes malloc anyway). The thread does not switch inside one: malloc does not call the game's scheduler.
static int sHostScopes;

void nebula_web_host_allocations(int enter) {
    sHostScopes += enter ? 1 : -1;
}

static int game_new(void) {
    return sHostScopes == 0 && nebula_web_in_game_context();
}

void* __wrap__Znwm(size_t size) {
    return game_new() ? __real__Znwm(size) : host_new(size);
}
void* __wrap__Znam(size_t size) {
    return game_new() ? __real__Znam(size) : host_new(size);
}
void __wrap__ZdlPv(void* p) {
    if (in_arena(p)) {
        __real__ZdlPv(p);
    } else {
        free(p);
    }
}
void __wrap__ZdaPv(void* p) {
    if (in_arena(p)) {
        __real__ZdaPv(p);
    } else {
        free(p);
    }
}
}
