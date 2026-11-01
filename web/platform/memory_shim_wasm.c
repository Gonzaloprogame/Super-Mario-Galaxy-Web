/* The address-space calls of Nebula's sdk/memory.c on WebAssembly (web/platform/include/web_memory_shim.h).

   wasm has one linear memory with no page protection and no address-space reservation. memory.c reserves twice the
   arena's size and keeps the part aligned to that size, so that an arena pointer's low 29 bits are its console
   physical address; here that reservation returns the aligned arena directly (half the request, aligned to its own
   size), and the trims of the unused parts do nothing. Guard pages cannot exist: mprotect succeeds without effect, so
   a game thread that overflows its stack overwrites the slot below instead of faulting.

   The module image is the start of linear memory (static data, then the C stack and the heap): dladdr reports base
   0, and memory.c's image window maps linear addresses below 128 MB to physical 0x08000000 and up. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

void* nebula_web_mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    (void)prot;
    (void)offset;
    if (addr != NULL || fd != -1 || !(flags & MAP_ANONYMOUS) || length < 2 || (length & (length - 1)) != 0) {
        errno = ENOTSUP;
        return MAP_FAILED;
    }
    const size_t arena = length / 2;
    void* p = aligned_alloc(arena, arena);
    if (p == NULL) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    memset(p, 0, arena);
    return p;
}

int nebula_web_munmap(void* addr, size_t length) {
    (void)addr;
    (void)length;
    return 0;
}

int nebula_web_mprotect(void* addr, size_t length, int prot) {
    (void)addr;
    (void)length;
    (void)prot;
    return 0;
}

int nebula_web_dladdr(const void* addr, Dl_info* info) {
    (void)addr;
    memset(info, 0, sizeof(*info));
    return 1;
}
