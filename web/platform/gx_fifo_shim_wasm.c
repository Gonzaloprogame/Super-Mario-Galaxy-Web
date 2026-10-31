/* The GX command ring of Nebula's sdk/gx/gx_fifo.c on WebAssembly (web/platform/include/web_gx_fifo_shim.h).

   Natively the ring is shared memory mapped twice in a row, so that a command running past the ring's end continues
   in the mirror and every span handed to the renderer is contiguous. wasm cannot alias memory. Here the ring is one
   plain allocation of twice the ring's size: a span that runs past the end continues in the second half, which is
   real memory rather than an alias. That keeps every property gx_fifo.c relies on: spans are contiguous, the writer
   returns to the first half after a span that crossed the end ("the same bytes" there are only the position, nothing
   is read through the alias), and the ring's accounting (ring_room) still keeps the writer off every byte the
   renderer holds, whichever half it lies in. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>

enum { kRingObject = 0x7E5A };

int nebula_web_shm_open(const char* name, int oflag, mode_t mode) {
    (void)name;
    (void)oflag;
    (void)mode;
    return kRingObject;
}

int nebula_web_shm_unlink(const char* name) {
    (void)name;
    return 0;
}

int nebula_web_ftruncate(int fd, off_t length) {
    (void)length;
    if (fd != kRingObject) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

/* The reservation (anonymous, twice the ring) is the ring's memory; the two views of the ring object placed in it
   (MAP_FIXED) are that memory already. */
void* nebula_web_ring_mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    (void)prot;
    (void)offset;
    if (addr == NULL && fd == -1 && (flags & MAP_ANONYMOUS)) {
        void* p = aligned_alloc(64, length);
        if (p == NULL) {
            errno = ENOMEM;
            return MAP_FAILED;
        }
        memset(p, 0, length);
        return p;
    }
    if (addr != NULL && fd == kRingObject && (flags & MAP_FIXED)) {
        return addr;
    }
    errno = ENOTSUP;
    return MAP_FAILED;
}
