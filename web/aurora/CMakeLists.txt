# Aurora's GX layer (third_party/aurora: lib/gx, lib/gfx) compiled with Emscripten against the browser's WebGPU
# (emdawnwebgpu), as the renderer of the GX command stream the game module writes. Aurora's own CMake fetches native
# Dawn, SDL3 and the rest; this project uses Emscripten's ports and the pinned sources of tools/fetch_sources.sh.
cmake_minimum_required(VERSION 3.24)
project(SmgWebAurora LANGUAGES C CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "Configura con emcmake cmake (toolchain de Emscripten).")
endif()

set(SMG_THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party" CACHE PATH "Salida de tools/fetch_sources.sh")
set(aurora "${SMG_THIRD_PARTY}/aurora")
set(deps "${SMG_THIRD_PARTY}/deps")
if(NOT EXISTS "${aurora}/lib/gx/command_processor.cpp" OR NOT EXISTS "${deps}/abseil/absl")
    message(FATAL_ERROR "Faltan fuentes: ejecuta tools/fetch_sources.sh")
endif()

# Web patches (web/aurora/patches/*.patch, in order) are applied to a copy of Aurora's lib/ and include/ in the build
# directory: third_party/aurora stays as fetched. The copy is redone whenever a patch or the revision changes.
file(GLOB web_patches CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/patches/*.patch")
list(SORT web_patches)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${web_patches} "${aurora}/.git/HEAD")
set(patched "${CMAKE_CURRENT_BINARY_DIR}/aurora-patched")
set(patch_stamp_text "")
foreach(patch ${web_patches})
    file(SHA256 "${patch}" hash)
    string(APPEND patch_stamp_text "${patch} ${hash}\n")
endforeach()
execute_process(COMMAND git -C "${aurora}" rev-parse HEAD OUTPUT_VARIABLE aurora_rev OUTPUT_STRIP_TRAILING_WHITESPACE)
string(APPEND patch_stamp_text "aurora ${aurora_rev}\n")
set(patch_stamp "${patched}/.web-patches")
if(EXISTS "${patch_stamp}")
    file(READ "${patch_stamp}" previous_stamp)
else()
    set(previous_stamp "")
endif()
if(NOT previous_stamp STREQUAL patch_stamp_text)
    file(REMOVE_RECURSE "${patched}")
    file(MAKE_DIRECTORY "${patched}")
    file(COPY "${aurora}/lib" "${aurora}/include" "${aurora}/cmake" DESTINATION "${patched}")
    foreach(patch ${web_patches})
        # patch(1), not git apply: inside a git work tree (this repository), git apply skips the paths outside the
        # current directory without an error.
        execute_process(COMMAND patch -p1 --forward --batch --no-backup-if-mismatch -i "${patch}"
                        WORKING_DIRECTORY "${patched}"
                        RESULT_VARIABLE applied OUTPUT_VARIABLE apply_error ERROR_VARIABLE apply_error)
        if(NOT applied EQUAL 0)
            message(FATAL_ERROR "No se pudo aplicar ${patch}: ${apply_error}")
        endif()
        message(STATUS "Aurora: aplicado ${patch}")
    endforeach()
    file(WRITE "${patch_stamp}" "${patch_stamp_text}")
endif()
set(aurora "${patched}")

# The GX layer's sources, as Aurora lists them (cmake/aurora_gx.cmake), so the list follows Aurora's revision.
file(STRINGS "${aurora}/cmake/aurora_gx.cmake" gx_lines REGEX "^[ \t]+lib/[^ ]+\\.cpp$")
set(gx_sources "")
foreach(line ${gx_lines})
    string(STRIP "${line}" path)
    list(APPEND gx_sources "${aurora}/${path}")
endforeach()
# RmlUi's pipeline is listed under AURORA_ENABLE_RMLUI, which this build leaves off (as Aurora's default does).
list(FILTER gx_sources EXCLUDE REGEX "/lib/rmlui/")
list(LENGTH gx_sources gx_count)
message(STATUS "Aurora GX: ${gx_count} sources")

# Web replacements (web/aurora/platform): each file names the Aurora file it replaces on its first line.
file(GLOB web_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.cpp")
foreach(replacement ${web_sources})
    file(STRINGS "${replacement}" replaces REGEX "^/\\* REPLACES: ")
    if(replaces)
        string(REGEX REPLACE "^/\\* REPLACES: ([^ ]+) .*" "\\1" replaced "${replaces}")
        list(REMOVE_ITEM gx_sources "${aurora}/${replaced}")
    endif()
endforeach()

# zlib and libpng (png_io.cpp: texture dumps and texture packs), the versions Aurora fetches (zlib 1.3.2 as Emscripten's
# port, libpng 1.6.58), built here from git: this session's proxy refuses GitHub's source archives, which Emscripten's
# zlib, libpng and SDL3 ports download.
set(zlib_dir "${deps}/zlib")
add_library(zlib_web STATIC
    ${zlib_dir}/adler32.c ${zlib_dir}/compress.c ${zlib_dir}/crc32.c ${zlib_dir}/deflate.c ${zlib_dir}/gzclose.c
    ${zlib_dir}/gzlib.c ${zlib_dir}/gzread.c ${zlib_dir}/gzwrite.c ${zlib_dir}/infback.c ${zlib_dir}/inffast.c
    ${zlib_dir}/inflate.c ${zlib_dir}/inftrees.c ${zlib_dir}/trees.c ${zlib_dir}/uncompr.c ${zlib_dir}/zutil.c)
target_include_directories(zlib_web PUBLIC "${zlib_dir}")
target_compile_definitions(zlib_web PRIVATE HAVE_UNISTD_H)

set(png_dir "${deps}/libpng")
configure_file("${png_dir}/scripts/pnglibconf.h.prebuilt" "${CMAKE_CURRENT_BINARY_DIR}/png/pnglibconf.h" COPYONLY)
add_library(png_web STATIC
    ${png_dir}/png.c ${png_dir}/pngerror.c ${png_dir}/pngget.c ${png_dir}/pngmem.c ${png_dir}/pngpread.c
    ${png_dir}/pngread.c ${png_dir}/pngrio.c ${png_dir}/pngrtran.c ${png_dir}/pngrutil.c ${png_dir}/pngset.c
    ${png_dir}/pngtrans.c ${png_dir}/pngwio.c ${png_dir}/pngwrite.c ${png_dir}/pngwtran.c ${png_dir}/pngwutil.c)
target_include_directories(png_web PUBLIC "${png_dir}" "${CMAKE_CURRENT_BINARY_DIR}/png")
target_link_libraries(png_web PUBLIC zlib_web)

# SQLite (Aurora's pipeline cache database), from tools/fetch_sources.sh. On the web the database lives in Emscripten's
# file system: in memory for the session until it is given persistent storage.
add_library(sqlite_web STATIC "${deps}/sqlite/sqlite3.c")
target_include_directories(sqlite_web PUBLIC "${deps}/sqlite")
target_compile_definitions(sqlite_web PRIVATE SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0)

add_library(aurora_gx_web STATIC ${gx_sources} ${web_sources})
target_link_libraries(aurora_gx_web PUBLIC png_web sqlite_web)
# Aurora assumes a 64-bit size_t: two texture sizes (uint64_t) initialise a ByteBuffer in braces, an error on wasm32. The
# sizes are a texture's, below 4 GB (and one is first checked against the data's own size).
set_source_files_properties("${aurora}/lib/gfx/texture_replacement.cpp" PROPERTIES COMPILE_OPTIONS "-Wno-c++11-narrowing")
target_include_directories(aurora_gx_web PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${aurora}/include"
    "${deps}/abseil"
    "${deps}/xxhash"
    "${deps}/fmt/include"
    "${deps}/tracy/public"
    "${deps}/imgui"
    "${deps}/magic_enum/include"
    "${deps}/sdl3/include"
    PRIVATE "${aurora}/lib")
target_compile_definitions(aurora_gx_web PUBLIC AURORA TARGET_PC=1 AURORA_ENABLE_GX FMT_HEADER_ONLY)
target_compile_features(aurora_gx_web PUBLIC cxx_std_20)
# The browser's WebGPU through Dawn's webgpu.h and webgpu_cpp.h (Emscripten's emdawnwebgpu port, a Dawn release asset).
target_compile_options(aurora_gx_web PUBLIC --use-port=emdawnwebgpu)
target_link_options(aurora_gx_web PUBLIC --use-port=emdawnwebgpu)
