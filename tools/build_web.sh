#!/bin/sh
# Compila el motor para Node.js y para el navegador y monta el sitio en build/site/.
#   tools/build_web.sh            (requiere emcc 6.0.13 en el PATH: source <emsdk>/emsdk_env.sh)
# Variables: BUILD_TYPE (Release), BUILD_DIR (build/web).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
EMSCRIPTEN_VERSION=6.0.13
BUILD_TYPE=${BUILD_TYPE:-Release}
BUILD_DIR=${BUILD_DIR:-$ROOT/build/web}
SITE=$ROOT/build/site

command -v emcc >/dev/null || { echo "error: emcc no está en el PATH (source <emsdk>/emsdk_env.sh)" >&2; exit 1; }
found=$(emcc --version | sed -n '1s/.* \([0-9][0-9.]*\) (.*/\1/p')
if [ "$found" != "$EMSCRIPTEN_VERSION" ]; then
    echo "aviso: emcc $found; la versión verificada es $EMSCRIPTEN_VERSION (docs/ENTORNO.md)" >&2
fi

"$ROOT/tools/fetch_sources.sh"
emcmake cmake -S "$ROOT/web/game" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
ninja -C "$BUILD_DIR" smg_headless smg_headless_web

rm -rf "$SITE"
mkdir -p "$SITE"
cp "$ROOT"/web/shell/index.html "$ROOT"/web/shell/app.js "$SITE"/
cp "$BUILD_DIR"/smg_headless_web.js "$BUILD_DIR"/smg_headless_web.wasm "$SITE"/
echo "Sitio en $SITE (sírvelo con tools/serve.py: necesita las cabeceras COOP/COEP)"
