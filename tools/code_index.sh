#!/bin/bash
# Local C/C++ code analysis for the port's own code (web/host, web/platform, the patched Aurora), with the compiler
# database of the real build, read as WebAssembly with Emscripten's headers: nothing leaves the machine.
#   tools/code_index.sh                  # writes build/all/clangd/compile_commands.json (the build must be configured)
#   tools/code_index.sh check <file>     # clangd's diagnostics for one file, as the build compiles it
#   tools/code_index.sh tidy <file>...   # clang-tidy (bug-prone and clang-analyzer checks) on those files
# Needs emsdk in the environment (source <emsdk>/emsdk_env.sh): clangd and clang come from emsdk's LLVM.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/all}"
RAW="$BUILD_DIR/compile_commands.json"
DB_DIR="$BUILD_DIR/clangd"
DB="$DB_DIR/compile_commands.json"
command -v emcc >/dev/null || { echo "emcc not found: source <emsdk>/emsdk_env.sh first" >&2; exit 1; }
LLVM_BIN="$(dirname "$(command -v emcc)")/../bin"
CLANGD="${CLANGD:-$LLVM_BIN/clangd}"

ensure_db() {
  if [ ! -f "$RAW" ]; then
    # Only adds the database to the existing configuration: no flag changes, nothing rebuilds
    cmake -S "$ROOT/web" -B "$BUILD_DIR" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null
  fi
  [ -f "$RAW" ] || { echo "no compile_commands.json in $BUILD_DIR (configure the build first: tools/build_web.sh)" >&2; exit 1; }
  if [ ! -f "$DB" ] || [ "$RAW" -nt "$DB" ]; then
    mkdir -p "$DB_DIR"
    # emcc/em++ are drivers clangd does not know: each command becomes emsdk's clang with the flags emcc adds itself
    # (emcc --cflags: the wasm32 target, Emscripten's sysroot and its include folders), without the -mllvm backend
    # options a syntax check does not use
    python3 - "$RAW" "$DB" "$(realpath "$LLVM_BIN")" "$(emcc --cflags -pthread)" <<'EOF'
import json, shlex, sys
raw, out, llvm, cflags = sys.argv[1:]
extra, skip = [], False
for flag in shlex.split(cflags):
    if skip:
        skip = False
        continue
    if flag == '-mllvm':
        skip = True
        continue
    extra.append(flag)
entries = json.load(open(raw))
for e in entries:
    args = e.pop('arguments', None) or shlex.split(e.pop('command'))
    cxx = args[0].endswith('em++') or e['file'].endswith(('.cpp', '.cc', '.cxx'))
    e['arguments'] = [f"{llvm}/{'clang++' if cxx else 'clang'}", *extra, *args[1:]]
json.dump(entries, open(out, 'w'), indent=0)
EOF
  fi
}

case "${1:-index}" in
  index)
    ensure_db
    echo "$DB: $(grep -c '"file"' "$DB") compiled files"
    ;;
  check)
    ensure_db
    # The real diagnostics only: clangd's own count also includes the refactorings it tries on each spot ("tweak:")
    out="$("$CLANGD" --compile-commands-dir="$DB_DIR" --check="$(realpath "$2")" 2>&1 || true)"
    grep -oE "internal \(cc1\) args are: -cc1 -triple [^ ]+" <<<"$out" | sed 's/.*-triple /target: /' || true
    real="$(grep -E "^E\[" <<<"$out" | grep -v "tweak:" || true)"
    [ -n "$real" ] && echo "$real"
    echo "$2: $(grep -c . <<<"$real" || true) diagnostics"
    ;;
  tidy)
    ensure_db
    shift
    "$LLVM_BIN/clang-tidy" -p "$DB_DIR" --quiet \
      --checks='-*,bugprone-*,clang-analyzer-*,-bugprone-easily-swappable-parameters' "$@" 2>/dev/null \
      || clang-tidy -p "$DB_DIR" --quiet --checks='-*,bugprone-*,clang-analyzer-*,-bugprone-easily-swappable-parameters' "$@"
    ;;
  *)
    sed -n 2,7p "$0"
    exit 1
    ;;
esac
