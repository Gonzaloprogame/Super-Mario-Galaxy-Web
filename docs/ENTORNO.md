# Entorno de desarrollo (Hito 2)

## Versiones verificadas

| Herramienta | Versión | Uso |
|---|---|---|
| Emscripten (emsdk) | 6.0.13 (`eb132757325583b787fe9414b09afea9442b72ad`) | compilación a wasm |
| CMake | 3.28.3 | proyecto `web/game` |
| Ninja | 1.11.1 | compilación |
| Node.js | 22.22.0 | tests en wasm, prueba de navegador |
| Playwright + Chromium | preinstalados en el entorno | prueba de navegador |
| Python | 3.13 | servidor de desarrollo, fixtures |
| clang (sistema) | 18.1.3 | línea base nativa de los tests de Nebula |
| Rust | 1.97.0 (objetivo `wasm32-unknown-emscripten`) | prueba del importador |

## Fuentes de referencia (`tools/fetch_sources.sh`)

| Proyecto | Revisión |
|---|---|
| Nebula | `a32d9456de82b76cbc25c01108a1afef72bde509` |
| Aurora | `d0ace68f26682caf9e75be552d32e0c7410ed79e` |
| Petari | `470e9cea44760a6c70851207a13c5caa68c4db37` |
| Dependencias de Aurora (`third_party/deps`) | abseil 20240722.0, xxHash 0.8.3, fmt 12.1.0, Tracy 0.14.1, imgui 1.91.9b-docking, magic_enum 0.9.7, zlib 1.3.2, libpng 1.6.58, SDL3 3.4.10 (commits en `tools/fetch_sources.sh`); SQLite 3.54.1 desde `libsqlite3-sys` 0.39.1 (`tools/sqlite/Cargo.lock`) |
| `emdawnwebgpu` (port de Emscripten) | v20260423.175430 |

## Instalación

Además de Emscripten: `git`, `python3`, `cmake` (3.24 o posterior), `ninja` y `cargo` (Rust: `tools/fetch_sources.sh`
descarga con él el *crate* de SQLite, y las pruebas del lector RVZ compilan con él `tools/rvz_fixture`). En Windows,
dentro de WSL2.

```sh
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install 6.0.13 && ./emsdk activate 6.0.13
source ./emsdk_env.sh
```

`tools/build_web.sh` avisa si `emcc` no es la versión verificada.

La prueba del renderizador (`web/tests/aurora_smoke.mjs`) lanza Chromium con `--enable-unsafe-webgpu`; sin GPU usa
SwiftShader. Ver la limitación del canvas en `BACKEND_WEBGPU.md`.

Para la prueba de navegador hace falta el paquete `playwright` de Node con un Chromium; en este entorno ya estaban
instalados (`NODE_PATH=$(npm root -g) node web/tests/browser_smoke.mjs`). En otro equipo: `npm i -g playwright` y
`npx playwright install chromium`.

## Servir el sitio

`python3 tools/serve.py [puerto]` sirve `build/site` con `Cross-Origin-Opener-Policy: same-origin` y
`Cross-Origin-Embedder-Policy: require-corp`, necesarias para SharedArrayBuffer (hilos).
