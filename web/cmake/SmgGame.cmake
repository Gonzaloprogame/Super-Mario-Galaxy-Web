# Compile options of the game module (third_party/nebula/native/CMakeLists.txt, with the web's differences), for the
# module and for any target whose sources include the game's SDK headers (web/bridge). Included by each directory that
# needs it (no include guard: its variables are directory-scoped; including it again only redefines them).

set(SMG_THIRD_PARTY "${CMAKE_CURRENT_LIST_DIR}/../../third_party" CACHE PATH "Salida de tools/fetch_sources.sh")
set(SMG_NEBULA_NATIVE "${SMG_THIRD_PARTY}/nebula/native")
set(SMG_PETARI "${SMG_NEBULA_NATIVE}/petari")
set(SMG_WEB_PLATFORM "${CMAKE_CURRENT_LIST_DIR}/../platform")

function(smg_game_options target)
    set(nebula "${SMG_NEBULA_NATIVE}")
    set(petari "${SMG_PETARI}")
    target_include_directories(${target} BEFORE PRIVATE
        "${nebula}/msl/include"
        "${petari}/include"
        "${petari}/libs/JSystem/include"
        "${petari}/libs/MSL_C++/include"
        "${petari}/libs/RVLFaceLib/include"
        "${petari}/libs/RVL_SDK/include"
        "${petari}/libs/nw4r/include"
        "${nebula}/sdk"
        "${nebula}/include")
    set(version_ids RMGJ01=0 RMGE01=1 RMGP01=2 RMGK01=3)
    target_compile_definitions(${target} PRIVATE VERSION=1 ${version_ids} TARGET_PC=1 NEBULA_GAME_MODULE=1 _GNU_SOURCE)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11 C_EXTENSIONS ON
        CXX_STANDARD 17 CXX_EXTENSIONS ON
        C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    # Mismas opciones que Nebula. Web: sin -fno-plt (no aplica a wasm) y con -fno-builtin-wcslen: LLVM reconoce los
    # bucles de longitud de cadena ancha (nebula_msl_wcslen) y los sustituye por wcslen de la libc, cuyo wchar_t es de 4
    # bytes en Emscripten, mientras que el juego usa wchar_t de 2 (-fshort-wchar; docs/COMPILACION_WASM.md).
    target_compile_options(${target} PRIVATE
        -pthread
        -fms-extensions -fno-builtin-wcslen
        -fsigned-char -fshort-wchar
        -fno-strict-aliasing -fno-strict-float-cast-overflow -fno-math-errno -fno-stack-protector
        -fno-ms-compatibility -fno-delayed-template-parsing
        -Wno-everything
        -Werror=return-type -Werror=pointer-to-int-cast -Werror=void-pointer-to-int-cast
        -Werror=int-to-pointer-cast -Werror=pointer-to-enum-cast
        -Werror=implicit-function-declaration -Werror=implicit-int -Werror=int-conversion
        -Werror=incompatible-function-pointer-types -Werror=return-stack-address -Werror=array-bounds
        "SHELL:-include nebula_prelude.h"
        "-ffile-prefix-map=${petari}=petari" "-ffile-prefix-map=${nebula}=native"
        $<$<COMPILE_LANGUAGE:CXX>:-nostdinc++ -fno-rtti -fno-exceptions -fno-sized-deallocation -fno-aligned-allocation
            -fno-threadsafe-statics -fcheck-new>
        -Wno-register -Wno-c++11-narrowing)
endfunction()

# Link options of every binary holding the game module.
# Web: fibers (Asyncify) para el cambio de hilo del juego; pthreads para los relojes del host (decrementador, retrace);
# memoria creciente hasta 4 GB: la arena de 512 MB alineada a su tamaño ocupa [512 MB, 1 GB). --wrap: el operator new y
# delete globales del juego (JKRHeap) frente a los del resto del binario (web/platform/operator_new_wasm.cpp).
set(SMG_WEB_LINK_OPTIONS
    -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536
    -pthread -sPTHREAD_POOL_SIZE=4
    -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=4GB
    -sSTACK_SIZE=1MB
    -sEXIT_RUNTIME
    -Wl,--wrap=_Znwm -Wl,--wrap=_Znam -Wl,--wrap=_ZdlPv -Wl,--wrap=_ZdaPv)

# División entera con la semántica de PowerPC (tools/wasm_ppc_divide.py): cada div/rem del binario enlazado pasa a una
# función que da el resultado de la consola donde wasm haría trap. Sustituye a native/sdk/os/divide_native.c.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
function(smg_ppc_divide target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../tools/wasm_ppc_divide.py"
            "$<TARGET_FILE_DIR:${target}>/${target}.wasm" "$<TARGET_FILE_DIR:${target}>/${target}.wasm"
        VERBATIM)
endfunction()
