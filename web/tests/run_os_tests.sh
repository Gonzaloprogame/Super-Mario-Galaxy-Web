#!/bin/sh
# Compila con Emscripten y ejecuta en Node los tests de la capa nativa de Nebula (cambio de contexto, planificador
# del SDK sobre el núcleo nativo, biblioteca MTX y runtime MSL), con las sustituciones web de web/platform.
# Es el equivalente wasm de third_party/nebula/native/sdk/os/tests/run_tests.sh, con las mismas opciones de C.
#   web/tests/run_os_tests.sh [directorio de salida]
# Requiere emcc en el PATH (source emsdk_env.sh) y tools/fetch_sources.sh ejecutado.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$ROOT/build/os-tests}
N=$ROOT/third_party/nebula/native
P=$N/petari
W=$ROOT/web/platform
mkdir -p "$OUT"

FL="-std=gnu11 -O2 -g -fno-builtin-wcslen -DTARGET_PC=1 -DVERSION=1 -DNEBULA_GAME_MODULE=1 -D_GNU_SOURCE -fsigned-char -fshort-wchar
    -fno-strict-aliasing -fno-strict-float-cast-overflow -fno-math-errno -Wno-everything -Werror=return-type
    -Werror=int-to-pointer-cast -Werror=pointer-to-int-cast -Werror=void-pointer-to-int-cast -Werror=pointer-to-enum-cast
    -Werror=implicit-function-declaration -Werror=implicit-int -Werror=int-conversion
    -Werror=incompatible-function-pointer-types -Werror=return-stack-address -Werror=array-bounds
    -include nebula_prelude.h -I$N/msl/include -I$P/libs/RVL_SDK/include -I$P/include -I$N/sdk -I$N/include"
# Web: fibers (Asyncify) for the thread switch; pthreads for the decrementer's clock thread; room for the 512 MB arena,
# which is aligned to its size.
LINK="-sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -pthread -sPTHREAD_POOL_SIZE=2 -sALLOW_MEMORY_GROWTH
      -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=4GB -sEXIT_RUNTIME -sENVIRONMENT=node,worker"
OS="$N/sdk/os/os_native.c $W/context_switch_wasm.c $N/sdk/os/divide_native.c $N/msl/src/msl_runtime.c"
SDK="$P/src/RVL_SDK/os/OSThread.c $P/src/RVL_SDK/os/OSMutex.c $P/src/RVL_SDK/os/OSMessage.c $P/src/RVL_SDK/os/OSAlarm.c
     $P/src/RVL_SDK/os/OSTime.c $P/src/RVL_SDK/os/OSArena.c"

# The forced include applies to the files after it on the command line only through separate compiles.
emcc $FL -pthread -c $N/sdk/memory.c -include "$W/include/web_memory_shim.h" -o "$OUT/memory.o"
emcc $FL $LINK "$N/sdk/os/tests/context_switch_tests.c" "$W/context_switch_wasm.c" -o "$OUT/context_switch_tests.js"
emcc $FL $LINK "$N/sdk/os/tests/os_scheduler_tests.c" $OS "$OUT/memory.o" "$W/memory_shim_wasm.c" "$W/posix_shim_wasm.c" $SDK \
    -o "$OUT/os_scheduler_tests.js"
emcc $FL $LINK "$N/sdk/mtx/tests/mtx_tests.c" "$N/sdk/mtx/mtx_native.c" -o "$OUT/mtx_tests.js"
emcc $FL $LINK "$N/msl/tests/msl_runtime_tests.c" "$N/msl/src/msl_runtime.c" -o "$OUT/msl_runtime_tests.js"

status=0
for t in context_switch_tests os_scheduler_tests mtx_tests msl_runtime_tests; do
    echo "== $t"
    node "$OUT/$t.js" || { echo "FAILED: $t (exit $?)"; status=1; }
done
exit $status
