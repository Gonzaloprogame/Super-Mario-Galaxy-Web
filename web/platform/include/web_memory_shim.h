/* Forced into Nebula's sdk/memory.c only (web/game/CMakeLists.txt): its address-space calls, mapped onto wasm's one
   linear memory by web/platform/memory_shim_wasm.c. Defined before the system headers, so their declarations name
   these functions too. */
#pragma once

#define mmap nebula_web_mmap
#define munmap nebula_web_munmap
#define mprotect nebula_web_mprotect
#define dladdr nebula_web_dladdr
