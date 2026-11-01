#!/bin/sh
# The RVZ reader (web/host/rvz_reader.c): synthetic RVZ images made and checked against nod by tools/rvz_fixture
# (Rust), then read by the reader compiled to wasm, in Node.
#   web/tests/run_rvz_tests.sh [directorio de salida]
# Requiere emcc en el PATH, cargo y tools/fetch_sources.sh ejecutado.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$ROOT/build/rvz-tests}
Z=$ROOT/third_party/deps/zstd/lib
mkdir -p "$OUT"

CARGO_TARGET_DIR="$OUT/cargo" cargo run -q --release --locked --manifest-path "$ROOT/tools/rvz_fixture/Cargo.toml" -- "$OUT"

emcc -O2 -I"$ROOT/web/host" -I"$Z" -DZSTD_DISABLE_ASM=1 -DZSTD_LEGACY_SUPPORT=0 -pthread -sNODERAWFS \
    -sALLOW_MEMORY_GROWTH -sENVIRONMENT=node -sEXIT_RUNTIME \
    "$ROOT/web/tests/rvz_test.c" "$ROOT/web/host/rvz_reader.c" \
    "$Z/common/debug.c" "$Z/common/entropy_common.c" "$Z/common/error_private.c" "$Z/common/fse_decompress.c" \
    "$Z/common/xxhash.c" "$Z/common/zstd_common.c" "$Z/decompress/huf_decompress.c" "$Z/decompress/zstd_ddict.c" \
    "$Z/decompress/zstd_decompress.c" "$Z/decompress/zstd_decompress_block.c" -o "$OUT/rvz_test.js"
node "$OUT/rvz_test.js" "$OUT" rvz_zstd_128k rvz_zstd_2m rvz_none_128k
