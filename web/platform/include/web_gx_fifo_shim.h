/* Forced into Nebula's sdk/gx/gx_fifo.c only (web/game/CMakeLists.txt): its command ring, on wasm's linear memory
   (web/platform/gx_fifo_shim_wasm.c). Defined before the system headers, so their declarations name these too. */
#pragma once

#define shm_open nebula_web_shm_open
#define shm_unlink nebula_web_shm_unlink
#define ftruncate nebula_web_ftruncate
#define mmap nebula_web_ring_mmap
