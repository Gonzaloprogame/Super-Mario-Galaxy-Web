# El módulo de juego (Petari, fork de Nebula, más la capa SDK nativa de Nebula) compilado a wasm32 con Emscripten.
# Reproduce las opciones de third_party/nebula/native/CMakeLists.txt; las diferencias de la plataforma web están
# marcadas y se documentan en docs/COMPILACION_WASM.md.
cmake_minimum_required(VERSION 3.24)
project(SmgWebGame LANGUAGES C CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "Configura con emcmake cmake (toolchain de Emscripten).")
endif()

set(SMG_THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party" CACHE PATH "Salida de tools/fetch_sources.sh")
set(nebula "${SMG_THIRD_PARTY}/nebula/native")
set(petari "${nebula}/petari")
if(NOT EXISTS "${petari}/src/Game/System/GameSystem.cpp")
    message(FATAL_ERROR "No se encuentra ${petari}: ejecuta tools/fetch_sources.sh")
endif()

# Unidades del juego y del SDK reutilizado: la misma selección que el CMake de Nebula.
file(GLOB_RECURSE game_sources CONFIGURE_DEPENDS
    "${petari}/src/Game/*.cpp" "${petari}/src/Game/*.c"
    "${petari}/src/JSystem/*.cpp" "${petari}/src/JSystem/*.c"
    "${petari}/src/nw4r/*.cpp" "${petari}/src/nw4r/*.c"
    "${petari}/src/RVLFaceLib/*.c" "${petari}/src/RVLFaceLib/*.cpp")
file(GLOB sdk_sources CONFIGURE_DEPENDS
    "${petari}/src/RVL_SDK/gx/*.c"
    "${petari}/src/RVL_SDK/gd/*.c"
    "${petari}/src/RVL_SDK/mem/*.c"
    "${petari}/src/RVL_SDK/kpad/*.c"
    "${petari}/src/RVL_SDK/dvd/dvdfs.c"
    "${petari}/src/RVL_SDK/arc/arc.c"
    "${petari}/src/RVL_SDK/wenc/wenc.c")
list(REMOVE_ITEM sdk_sources "${petari}/src/RVL_SDK/gx/GXFifo.c")
foreach(unit OSThread OSMutex OSMessage OSAlarm OSTime OSArena OSAlloc)
    list(APPEND sdk_sources "${petari}/src/RVL_SDK/os/${unit}.c")
endforeach()
file(GLOB_RECURSE layer_sources CONFIGURE_DEPENDS "${nebula}/sdk/*.cpp" "${nebula}/sdk/*.c")
list(FILTER layer_sources EXCLUDE REGEX "/tests/")

# Sustituciones web de la capa de plataforma (web/platform): cada fichero listado reemplaza al de Nebula.
set(SMG_WEB_PLATFORM "${CMAKE_CURRENT_SOURCE_DIR}/../platform")
file(GLOB web_platform_sources CONFIGURE_DEPENDS "${SMG_WEB_PLATFORM}/*.c" "${SMG_WEB_PLATFORM}/*.cpp")
foreach(replacement ${web_platform_sources})
    file(STRINGS "${replacement}" replaces REGEX "^/\\* REPLACES: ")
    if(replaces)
        string(REGEX REPLACE "^/\\* REPLACES: ([^ ]+) .*" "\\1" replaced "${replaces}")
        list(REMOVE_ITEM layer_sources "${nebula}/${replaced}")
    endif()
endforeach()

# Nebula's sdk/memory.c, unchanged, on wasm's linear memory (web/platform/memory_shim_wasm.c).
set_source_files_properties("${nebula}/sdk/memory.c" PROPERTIES
    COMPILE_OPTIONS "-include;${SMG_WEB_PLATFORM}/include/web_memory_shim.h")
# Nebula's sdk/gx/gx_fifo.c, unchanged: its command ring without double mapping (web/platform/gx_fifo_shim_wasm.c).
set_source_files_properties("${nebula}/sdk/gx/gx_fifo.c" PROPERTIES
    COMPILE_OPTIONS "-include;${SMG_WEB_PLATFORM}/include/web_gx_fifo_shim.h")

add_library(smg_game STATIC
    ${game_sources}
    ${sdk_sources}
    ${layer_sources}
    ${web_platform_sources}
    "${nebula}/msl/src/msl_runtime.c")

target_include_directories(smg_game BEFORE PRIVATE
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
target_compile_definitions(smg_game PRIVATE VERSION=1 ${version_ids} TARGET_PC=1 NEBULA_GAME_MODULE=1 _GNU_SOURCE)

set_target_properties(smg_game PROPERTIES
    C_STANDARD 11 C_EXTENSIONS ON
    CXX_STANDARD 17 CXX_EXTENSIONS ON
    C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# Mismas opciones que Nebula. Web: sin -fno-plt (no aplica a wasm) y con -fno-builtin-wcslen: LLVM reconoce los bucles
# de longitud de cadena ancha (nebula_msl_wcslen) y los sustituye por wcslen de la libc, cuyo wchar_t es de 4 bytes
# en Emscripten, mientras que el juego usa wchar_t de 2 (-fshort-wchar; docs/COMPILACION_WASM.md).
target_compile_options(smg_game PRIVATE
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

# ---- Host headless (web/host/headless_host.c): primera prueba de ejecución del módulo completo ----
# Web: fibers (Asyncify) para el cambio de hilo del juego; pthreads para los relojes del host (decrementador, retrace);
# memoria creciente hasta 4 GB: la arena de 512 MB alineada a su tamaño ocupa [512 MB, 1 GB).
set(SMG_WEB_LINK_OPTIONS
    -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536
    -pthread -sPTHREAD_POOL_SIZE=4
    -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=4GB
    -sSTACK_SIZE=1MB
    -sEXIT_RUNTIME)
target_compile_options(smg_game PRIVATE -pthread)

add_executable(smg_headless "${CMAKE_CURRENT_SOURCE_DIR}/../host/headless_host.c")
target_include_directories(smg_headless PRIVATE "${nebula}/include")
target_compile_options(smg_headless PRIVATE -pthread)
target_link_libraries(smg_headless PRIVATE smg_game)
target_link_options(smg_headless PRIVATE ${SMG_WEB_LINK_OPTIONS} -sNODERAWFS -sENVIRONMENT=node)

# Variante para navegador del mismo host: el programa se ejecuta dentro de un Worker dedicado (web/shell/boot_worker.post.js),
# que puede bloquear (Atomics.wait) y lee el game.pak del usuario por trozos (WORKERFS), sin copiarlo a memoria.
# Requiere aislamiento de origen cruzado (COOP/COEP) para SharedArrayBuffer.
add_executable(smg_headless_web "${CMAKE_CURRENT_SOURCE_DIR}/../host/headless_host.c")
target_include_directories(smg_headless_web PRIVATE "${nebula}/include")
target_compile_options(smg_headless_web PRIVATE -pthread)
target_link_libraries(smg_headless_web PRIVATE smg_game)
target_link_options(smg_headless_web PRIVATE ${SMG_WEB_LINK_OPTIONS}
    -sENVIRONMENT=worker -sMODULARIZE -sEXPORT_NAME=createSmgModule -sINVOKE_RUN=0
    -lworkerfs.js "-sEXPORTED_RUNTIME_METHODS=['callMain','FS','WORKERFS']"
    "--extern-post-js=${CMAKE_CURRENT_SOURCE_DIR}/../shell/boot_worker.post.js")
set_property(TARGET smg_headless_web APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../shell/boot_worker.post.js")
