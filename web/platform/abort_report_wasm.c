/* abort() in any thread of the engine, told to the page before it happens (linked with --wrap=abort,
   web/cmake/SmgGame.cmake).

   An abort in one of the engine's threads (Aurora's render thread: a full frame buffer, libc++ without exceptions)
   ends that thread's worker with an error the engine's main thread and the page never get: the page went on showing
   "Motor en marcha" while nothing was drawn any more. Here the message and the C stack (wasm function indices, which
   tools/wasm_symbols.py names) go on the presenter's channel (web/shell/present.js) from the aborting thread itself,
   as Aurora's fatal errors do, and to the console; not through stdio, which another thread serves and which may be the
   one waiting on this one. Emscripten's own aborts from JS do not pass here. */
#include <emscripten.h>
#include <stdlib.h>

_Noreturn void __real_abort(void);

_Noreturn void __wrap_abort(void) {
    static char stack[4096];
    emscripten_get_callstack(EM_LOG_C_STACK, stack, sizeof(stack));
    EM_ASM(
        {
            const stack = UTF8ToString($0);
            console.error('abort() in a thread of the engine\n' + stack);
            if (typeof BroadcastChannel !== 'undefined') {
                const channel = new BroadcastChannel('smg-present');
                channel.postMessage({type : 'fatal', text : 'abort() en un hilo del motor', stack : stack});
                channel.close();
            }
        },
        stack);
    __real_abort();
}
