# Backend gráfico: Aurora GX sobre el WebGPU del navegador (Hito 5)

Resultados medidos en este entorno el 2026-10-28 (Emscripten 6.0.13, port `emdawnwebgpu` v20260423.175430,
Chromium 153 de Playwright con WebGPU por software, SwiftShader).

## Por qué Aurora

El módulo de juego escribe el flujo de comandos FIFO de GX de la consola. El renderizador de Nebula (D3D12/Vulkan,
shaders HLSL) no puede usarse en un navegador. [Aurora](https://github.com/encounter/aurora) (MIT) implementa GX sobre
la API `webgpu.h` de Dawn, genera WGSL e interpreta bytes FIFO GX (`aurora::gx::fifo::process`). El port
`emdawnwebgpu` de Emscripten ofrece esa misma API sobre el WebGPU del navegador. Aurora no tenía soporte de
Emscripten.

## Cómo se compila

- `web/aurora/CMakeLists.txt` compila la capa GX de Aurora (las 52 unidades que lista su `cmake/aurora_gx.cmake`) y su
  núcleo (18 unidades de `cmake/aurora_core.cmake`), más `main.cpp` y `vi.cpp`.
- Aurora se copia al directorio de compilación y se le aplican los parches de `web/aurora/patches/` con `patch -p1`;
  `third_party/aurora` no se toca.
- Dependencias en las versiones que fija Aurora, obtenidas con `tools/fetch_sources.sh`: abseil, xxHash, fmt, Tracy
  (solo cabeceras; sin perfilado), imgui, magic_enum, zlib, libpng, SDL3. SQLite sale del *crate* `libsqlite3-sys`
  0.39.1 (SQLite 3.54.1; Aurora usa 3.51.3, la API C es compatible). El proxy de esta sesión rechaza los archivos
  comprimidos de GitHub y sqlite.org, de donde descargan los ports `zlib`, `libpng`, `sdl3` y `sqlite3` de
  Emscripten; `emdawnwebgpu` (un asset de *release* de Dawn) sí se descarga.

## Censo de compilación

| Paso | Unidades que compilan |
|---|---|
| Aurora GX sin adaptar, sin cabeceras de SDL3 | 31 / 52 (las 21 restantes: cabeceras de SDL3) |
| Con cabeceras de SDL3 | 49 / 52 |
| Con los parches web | **52 / 52** (GX) y **18 / 18** (núcleo) |

## Adaptaciones

Parches (`web/aurora/patches/`, en orden; cada uno es pequeño y explica su motivo en el código):

| Parche | Motivo (error real encontrado) |
|---|---|
| `0001-immediates-as-uniform` | Aurora pasa 64 bytes por draw con los datos inmediatos de Dawn (`var<immediate>`, `RenderPassEncoder::SetImmediates`), que el WebGPU del navegador no tiene. Pasan a un `uniform` con offset dinámico en el grupo 3, escrito con el `push_uniform` existente; el *bind group* enlaza solo 64 bytes para que un offset cerca del final del búfer de 24 MiB sea válido. |
| `0002-device-logging-dawn-only` | `wgpu::LoggingType` y `Device::SetLoggingCallback` son de Dawn nativo. |
| `0003-canvas-surface-usages` | `wgpuSurfaceGetCapabilities` de `emdawnwebgpu` no rellena `usages` (queda 0) y Aurora rechazaba la superficie ("surface is incompatible with the active adapter"). Un canvas admite siempre `RENDER_ATTACHMENT`. |
| `0004-single-thread-browser` | Aurora lanza hilos (`thread constructor failed`) y, en un navegador, los objetos WebGPU pertenecen al hilo que los crea. Sin *render worker*: el único hilo es el *worker* y el trabajo se ejecuta en línea. Sin caché persistente de *pipelines* por ahora (su escritor es un hilo y su base necesita almacenamiento persistente). |
| `0005-yield-to-browser-while-waiting` | Las esperas de Aurora (`wait_for_gpu_progress`) duermen con `std::this_thread::sleep_for`, que no devuelve el control al bucle de eventos: el `MapAsync` de los búferes de *staging* nunca terminaba y la página se congelaba. Ahora ceden con `emscripten_sleep` (Asyncify). |
| `0006-canvas-presents-itself` | `wgpuSurfacePresent` no existe en el navegador ("use requestAnimationFrame"): el canvas se presenta al volver al bucle de eventos. |

Sustitutos (`web/aurora/platform/`):

- `backend_binding_web.cpp` (en lugar de `lib/dawn/BackendBinding.cpp`): la superficie de una ventana SDL es su
  `<canvas>` (`EmscriptenSurfaceSourceCanvasHTMLSelector`).
- `imgui_web.cpp` (en lugar de `lib/imgui.cpp`): la capa de depuración de imgui queda desactivada; el backend WebGPU de
  la imgui que fija Aurora (1.91.9b) solo admite la antigua API WebGPU de Emscripten, no `emdawnwebgpu`.

Opciones de compilación: sin `WEBGPU_DAWN` (Aurora ya aísla tras esa macro los *toggles* y la caché de Dawn nativo),
con `ENABLE_BACKEND_WEBGPU`; `texture_replacement.cpp` con `-Wno-c++11-narrowing` (Aurora asume `size_t` de 64 bits:
dos tamaños de textura `uint64_t` inicializan un `ByteBuffer` entre llaves).

## Prueba: `web/tests/aurora_smoke.mjs`

La demo `web/aurora/demo/gx_demo.c` dibuja una escena GX deliberadamente simple: el color de borrado de la copia y un
triángulo en el centro del EFB de 640×480, con una etapa TEV. Dos variantes del color del triángulo: el color de
vértice rasterizado (`GX_PASSCLR`) y el registro TEV `C0` (`?tev=reg`). La escena pasa por las mismas piezas que el
flujo del juego: procesador de comandos, búferes de vértices y *uniforms*, generador de shaders TEV (WGSL), caché de
*pipelines*, EFB y lectura del EFB.

La comprobación lee el EFB desde el propio wasm con la API de lectura de Aurora (`AuroraReadbackCapturePeeks`), la
misma ruta que toman `GXPeekARGB`/`GXPeekZ` del juego (el destello de la lente):

| Punto del EFB | Esperado | Leído (Debug y Release, ambas variantes) |
|---|---|---|
| Esquina (16, 16) | color de borrado (24, 32, 96) | `0xFF182060` |
| Centro del triángulo (320, 272) | color del triángulo (240, 176, 16) | `0xFFF0B010` |
| Fondo (96, 408) | color de borrado | `0xFF182060` |

Más de 30 fotogramas GX por ejecución, sin errores de WebGPU (el *callback* de errores no capturados de Aurora
aborta ante cualquiera).

Errores encontrados al escribir la demo (míos, no de Aurora): el triángulo en z = +0,5 quedaba recortado (la cámara
GX mira hacia −z; la proyección lleva z a −z − 1 y GX recorta fuera de [−1, 0]); y sin `GXInit` las escrituras TEV
iban al registro BP 0, porque las copias en sombra de los registros que actualizan las funciones `GXSet*` llevan la
dirección del registro y es `GXInit` quien la pone. Todo juego llama a `GXInit` al arrancar.

## Limitación del entorno: canvas WebGPU en Chromium headless

En este entorno, **cualquier** dispositivo WebGPU que presenta en un `<canvas>` se pierde ("Device was destroyed"),
también con una página de JavaScript puro que solo limpia el canvas, en los dos modos headless de Chromium y con
cualquier formato. El registro interno de Chromium indica `Could not find SharedImageBackingFactory … WebgpuRead`: no
hay imagen compartida para el canvas con el adaptador por software. El renderizado fuera de pantalla con lectura
funciona (comprobado también en JavaScript puro). Por eso la prueba usa la superficie *headless* de Aurora
(`?headless=1`).

**Sin verificar aquí:** la presentación en el canvas en un navegador con GPU (`SMG_CANVAS=1 node
web/tests/aurora_smoke.mjs build/site/gx` la comprueba por captura de pantalla). Con Aurora pidiendo un fotograma,
presentando en el canvas, se llegaron a dibujar 4 fotogramas antes de que el navegador perdiera el dispositivo.

## Medidas

- `gx_demo.wasm`: 4,5 MB (Release, con Asyncify).
- La memoria wasm crece hasta unos 90 MB en los primeros fotogramas: los búferes de *staging* de Aurora (24 MiB solo
  de *uniforms*) tienen copia en la memoria de wasm al mapearse.

Parches posteriores para el flujo de hardware del juego (0007 a 0011): ver `PUENTE_FIFO.md`.

## Siguiente paso

Conectar el módulo de juego: `gx_submit` del host → `aurora::gx::fifo::process`, con las direcciones físicas de los
comandos (bases de arrays, texturas, listas de visualización) resueltas con `nebula_game_memory_regions`, y los
marcadores de Nebula (tokens de sincronización, lecturas del EFB) sobre la API de lectura de Aurora. El juego y Aurora
deben compartir un hilo (WebGPU no se comparte entre workers), lo que en el worker del motor requiere un
`OffscreenCanvas`.
