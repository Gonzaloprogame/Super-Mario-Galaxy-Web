#!/bin/bash
# The in-place index writer (Aurora patch 0027) against the appending one it replaced: natively (g++) and in
# WebAssembly (em++, run by Node), where the engine runs it.
#   web/tests/run_index_writer_test.sh [patched Aurora tree, default build/all/aurora/aurora-patched]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
AURORA="${1:-$ROOT/build/all/aurora/aurora-patched}"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
FLAGS=(-std=c++20 -O2 -Wall -I"$AURORA/lib" -I"$AURORA/include" "$ROOT/web/tests/index_writer_test.cpp")
g++ "${FLAGS[@]}" -o "$OUT/native"
"$OUT/native"
if command -v em++ >/dev/null; then
  em++ "${FLAGS[@]}" -o "$OUT/wasm.js"
  node "$OUT/wasm.js"
else
  echo "em++ not found (source emsdk_env.sh): the WebAssembly run is skipped"
fi
