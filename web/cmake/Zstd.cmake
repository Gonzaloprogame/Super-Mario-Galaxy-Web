# Zstandard's decoder (third_party/deps/zstd, 1.5.7, BSD): it decompresses the user's RVZ disc image in the page
# (web/host/rvz_reader.c). Only the decompression sources, without assembly, legacy formats or threads.
if(NOT TARGET smg_zstd)
    set(SMG_ZSTD "${CMAKE_CURRENT_LIST_DIR}/../../third_party/deps/zstd/lib")
    if(NOT EXISTS "${SMG_ZSTD}/zstd.h")
        message(FATAL_ERROR "No se encuentra ${SMG_ZSTD}: ejecuta tools/fetch_sources.sh")
    endif()
    add_library(smg_zstd STATIC
        "${SMG_ZSTD}/common/debug.c" "${SMG_ZSTD}/common/entropy_common.c" "${SMG_ZSTD}/common/error_private.c"
        "${SMG_ZSTD}/common/fse_decompress.c" "${SMG_ZSTD}/common/xxhash.c" "${SMG_ZSTD}/common/zstd_common.c"
        "${SMG_ZSTD}/decompress/huf_decompress.c" "${SMG_ZSTD}/decompress/zstd_ddict.c"
        "${SMG_ZSTD}/decompress/zstd_decompress.c" "${SMG_ZSTD}/decompress/zstd_decompress_block.c")
    target_include_directories(smg_zstd PUBLIC "${SMG_ZSTD}")
    target_compile_definitions(smg_zstd PRIVATE ZSTD_DISABLE_ASM=1 ZSTD_LEGACY_SUPPORT=0)
    # Linked into binaries with threads: built with their atomics
    target_compile_options(smg_zstd PRIVATE -pthread -O2)
endif()
