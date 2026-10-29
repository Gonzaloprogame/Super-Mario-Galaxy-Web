#!/bin/sh
# Compila todo el proyecto web (web/CMakeLists.txt: módulo de juego y hosts, Aurora, puente) y monta el sitio en
# build/site/.
#   tools/build_web.sh            (requiere emcc 6.0.13 en el PATH: source <emsdk>/emsdk_env.sh)
# Variables: BUILD_TYPE (Release), BUILD_DIR (build/all).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
EMSCRIPTEN_VERSION=6.0.13
BUILD_TYPE=${BUILD_TYPE:-Release}
BUILD_DIR=${BUILD_DIR:-$ROOT/build/all}
SITE=$ROOT/build/site

command -v emcc >/dev/null || { echo "error: emcc no está en el PATH (source <emsdk>/emsdk_env.sh)" >&2; exit 1; }
found=$(emcc --version | sed -n '1s/.* \([0-9][0-9.]*\) (.*/\1/p')
if [ "$found" != "$EMSCRIPTEN_VERSION" ]; then
    echo "aviso: emcc $found; la versión verificada es $EMSCRIPTEN_VERSION (docs/ENTORNO.md)" >&2
fi

"$ROOT/tools/fetch_sources.sh"
emcmake cmake -S "$ROOT/web" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
ninja -C "$BUILD_DIR" smg_headless smg_headless_web smg_web gx_demo gx_bridge gx_replay

rm -rf "$SITE"
mkdir -p "$SITE/gx" "$SITE/bridge"
# The engine's page (the game module, headless for now).
cp "$ROOT"/web/shell/index.html "$ROOT"/web/shell/app.js "$ROOT"/web/shell/present.js "$ROOT"/web/shell/controls.js \
   "$SITE"/
# COOP/COEP on hosts without custom headers (GitHub Pages), with its licence (web/shell/vendor/README.md)
cp "$ROOT"/web/shell/vendor/coi-serviceworker.js "$ROOT"/web/shell/vendor/coi-serviceworker.LICENSE "$SITE"/
cp "$BUILD_DIR"/engine/smg_web.js "$BUILD_DIR"/engine/smg_web.wasm "$SITE"/
cp "$BUILD_DIR"/game/smg_headless_web.js "$BUILD_DIR"/game/smg_headless_web.wasm "$SITE"/
# Aurora's GX on WebGPU, with its own API (docs/BACKEND_WEBGPU.md).
cp "$ROOT"/web/aurora/demo/gx_demo.html "$BUILD_DIR"/aurora/gx_demo.js "$BUILD_DIR"/aurora/gx_demo.wasm "$SITE/gx"/
# The game's GX stream through Aurora (docs/PUENTE_FIFO.md).
cp "$ROOT"/web/bridge/gx_bridge.html "$BUILD_DIR"/bridge/gx_bridge.js "$BUILD_DIR"/bridge/gx_bridge.wasm \
   "$ROOT"/web/shell/present.js "$SITE/bridge"/
cp "$ROOT"/web/bridge/gx_replay.html "$BUILD_DIR"/bridge/gx_replay.js "$BUILD_DIR"/bridge/gx_replay.wasm "$SITE/bridge"/
echo "Sitio en $SITE (sírvelo con tools/serve.py: necesita las cabeceras COOP/COEP)"
