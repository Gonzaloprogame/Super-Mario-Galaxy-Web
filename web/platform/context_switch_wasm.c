/* REPLACES: sdk/os/context_switch.c (Nebula's x86-64/arm64 stack switch) */
/* Switching between game threads' stacks on WebAssembly.

   WebAssembly cannot move its own call stack, so the switch is an Emscripten fiber (Asyncify): the running thread's
   wasm frames are unwound into its Asyncify buffer and the next thread's are rewound from its own. The interface is
   Nebula's (sdk/os/context_switch.c): nebula_prepare_stack lays a new thread out on the memory it is given and returns
   an opaque handle, nebula_switch_stack saves the current thread's handle and resumes another. The scheduler above it
   (sdk/os/os_native.c) is unchanged.

   A prepared region holds, from its low end: the C stack (the frames' address-taken locals, which wasm keeps in linear
   memory), the Asyncify buffer (the unwound wasm frames), then the fiber record. The C stack therefore stays in the
   game's arena, as on the native hosts, where the GPU can read stack data the game gives GX; and it grows down
   towards the region's lowest word, where the SDK keeps its overflow canary (OSThread's stackEnd, 0xDEADBABE). */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <emscripten/fiber.h>

void nebula_thread_start(void (*entry)(void*), void* arg);

typedef struct {
    emscripten_fiber_t fiber;
    void (*entry)(void*);
    void* arg;
} NebulaFiber;

enum {
    /* The host stack's Asyncify buffer: the frames below the first switch (the host's call into the game). */
    kHostAsyncifySize = 64 * 1024,
    /* Of each prepared region, the share kept for the Asyncify buffer (the rest is the C stack). */
    kAsyncifyShareShift = 1,
};

static NebulaFiber sHostFiber;
static uint8_t sHostAsyncify[kHostAsyncifySize] __attribute__((aligned(16)));
static NebulaFiber* sCurrent;

static void fiber_entry(void* p) {
    NebulaFiber* self = (NebulaFiber*)p;
    nebula_thread_start(self->entry, self->arg);
    fprintf(stderr, "nebula_switch_stack: a thread returned past its entry\n");
    abort();
}

void* nebula_prepare_stack(void* top, size_t size, void (*entry)(void*), void* arg) {
    uint8_t* const end = (uint8_t*)(((uintptr_t)top) & ~(uintptr_t)15);
    uint8_t* const base = (uint8_t*)(((uintptr_t)end - size + 15) & ~(uintptr_t)15);
    NebulaFiber* const self = (NebulaFiber*)(end - ((sizeof(NebulaFiber) + 15) & ~(size_t)15));
    const size_t asyncifySize = (size_t)((uint8_t*)self - base) >> kAsyncifyShareShift & ~(size_t)15;
    uint8_t* const asyncify = (uint8_t*)self - asyncifySize;
    uint8_t* const cStack = base;
#if defined(NEBULA_WEB_TRACE_SWITCH)
    fprintf(stderr, "[prepare] %p: C stack %p+%zu, Asyncify %p+%zu\n", (void*)self, (void*)cStack,
            (size_t)(asyncify - cStack), (void*)asyncify, asyncifySize);
#endif
    self->entry = entry;
    self->arg = arg;
    emscripten_fiber_init(&self->fiber, fiber_entry, self, cStack, (size_t)(asyncify - cStack), asyncify, asyncifySize);
    return self;
}

void nebula_switch_stack(void** from_sp, void* to_sp) {
    NebulaFiber* const to = (NebulaFiber*)to_sp;
    NebulaFiber* from = sCurrent;
    if (from == NULL) {
        /* The first switch leaves the host's own stack. */
        emscripten_fiber_init_from_current_context(&sHostFiber.fiber, sHostAsyncify, sizeof(sHostAsyncify));
        from = &sHostFiber;
    }
    *from_sp = from;
#if defined(NEBULA_WEB_TRACE_SWITCH)
    fprintf(stderr, "[switch] %p -> %p\n", (void*)from, (void*)to);
#endif
    sCurrent = to;
    emscripten_fiber_swap(&from->fiber, &to->fiber);
    /* Running again: whoever switched here set sCurrent to this thread. */
}
