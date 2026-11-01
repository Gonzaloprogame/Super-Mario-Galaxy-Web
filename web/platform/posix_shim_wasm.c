/* POSIX calls Nebula's native layer makes that Emscripten's libc declares but does not provide. */
#define _GNU_SOURCE
#include <pthread.h>

/* Thread names are diagnostic only (Nebula's thread sampler and debuggers); browsers' workers keep theirs. */
int pthread_setname_np(pthread_t thread, const char* name) {
    (void)thread;
    (void)name;
    return 0;
}
