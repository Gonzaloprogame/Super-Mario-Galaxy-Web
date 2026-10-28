# Super Mario Galaxy Web (experimental)

Investigación y desarrollo de un port de Super Mario Galaxy a navegador mediante WebAssembly, a partir del código
existente: la decompilación [Petari](https://github.com/SMGCommunity/Petari) (CC0) en el fork nativo de
[Nebula](https://github.com/keepvaibin/nebula) (GPL-3.0). No es un clon: el objetivo es ejecutar la lógica original
del juego en el navegador.

**No contiene ningún archivo de Nintendo.** Necesitas tu propia copia del juego (`docs/ARCHIVOS_DEL_JUEGO.md`).
No está afiliado a Nintendo ni respaldado por Nintendo.

## Estado actual

**Todavía no se puede jugar.** Lo que funciona y está verificado (`docs/COMPILACION_WASM.md`):

- El módulo de juego completo (1.898 unidades: juego, JSystem, nw4r, RVLFaceLib y SDK nativo de Nebula) compila y
  enlaza a wasm32 sin símbolos sin resolver.
- La capa de plataforma web resuelve lo que wasm no permite (cambio de pila, reservas de memoria, anillo GX mapeado
  dos veces) sin modificar el código de Nebula. Los tests de la capa nativa de Nebula pasan en wasm.
- El motor se carga en Chromium dentro de un worker con hilos, lee el `game.pak` del usuario por trozos y se ejecuta
  hasta la carga de datos de `main.dol`, el primer punto que requiere los archivos reales del juego.

- El renderizador elegido, la capa GX de Aurora, compila entera para el navegador y dibuja en el WebGPU de Chromium
  (`docs/BACKEND_WEBGPU.md`).
- El FIFO GX del juego está conectado a Aurora, con los recursos por dirección física que usa el juego: vértices
  indexados, listas de visualización, texturas y TLUT, copias del EFB, copia a pantalla y lecturas del EFB. Una
  escena de prueba dibujada por la GX del propio módulo de juego llega hasta el canvas de la página
  (`docs/PUENTE_FIFO.md`).
- La página tiene un modo **con gráficos**: el motor completo con Aurora. Arranca y se cierra de forma verificada
  con una partición sintética; **no se ha probado con el juego real**, porque requiere tu copia
  (`docs/MOTOR_WEB.md` explica cómo probarlo).

No hay audio ni controles. Hoja de ruta: `docs/HOJA_DE_RUTA.md`.

## Documentación

| Documento | Contenido |
|---|---|
| `docs/DIAGNOSTICO.md` | Análisis de Petari y Nebula, comparación, estrategia, riesgos |
| `docs/ARQUITECTURA_WEB.md` | Arquitectura del port: motor, gráficos, audio, controles, recursos, interfaz |
| `docs/COMPILACION_WASM.md` | Resultados reales de compilación y ejecución, errores encontrados |
| `docs/BACKEND_WEBGPU.md` | Aurora GX sobre el WebGPU del navegador: parches, pruebas y limitaciones |
| `docs/PUENTE_FIFO.md` | El FIFO GX del juego dibujado por Aurora: un binario con ambos, pruebas y fase 2 |
| `docs/MOTOR_WEB.md` | Cómo probar el motor con gráficos con tu copia del juego, y qué contar |
| `docs/ENTORNO.md` | Versiones de las herramientas e instalación |
| `docs/ARCHIVOS_DEL_JUEGO.md` | Cómo aportar legalmente los archivos del juego |
| `docs/HOJA_DE_RUTA.md` | Hitos, criterios de aceptación y próximos pasos |

## Compilar y probar

```sh
source <emsdk>/emsdk_env.sh            # Emscripten 6.0.13
tools/fetch_sources.sh                 # fuentes de referencia en third_party/ (revisiones fijas)
web/tests/run_os_tests.sh              # tests de la capa nativa en wasm (Node)
tools/build_web.sh                     # compila y monta el sitio en build/site
python3 tools/serve.py 8080 build/site # http://127.0.0.1:8080 (con COOP/COEP)
node web/tests/browser_smoke.mjs       # prueba del motor en Chromium
node web/tests/aurora_smoke.mjs build/site/gx   # prueba del renderizador (WebGPU) en Chromium
node web/tests/bridge_smoke.mjs build/site/bridge   # GX del juego -> Aurora, en Chromium
```

Diagnóstico en Node con tu partición: `node build/all/game/smg_headless.js /ruta/a/game.pak 10`.
Con gráficos en el navegador: `docs/MOTOR_WEB.md`.

## Estructura

| Ruta | Contenido |
|---|---|
| `web/game/` | CMake de Emscripten para el módulo de juego y los hosts |
| `web/platform/` | Capa de plataforma web (sustitutos y *shims* de la capa nativa de Nebula) |
| `web/host/` | Host que implementa `nebula_game_api.h` (por ahora sin gráficos) |
| `web/aurora/` | Aurora (GX sobre WebGPU) para el navegador: CMake, parches, sustitutos y demo |
| `web/bridge/` | Prueba del puente: escena con la GX del juego, host y página |
| `web/cmake/` | Opciones de compilación del módulo de juego, compartidas |
| `web/shell/` | Interfaz web y arranque del worker |
| `web/tests/` | Tests en wasm, prueba de navegador, particiones sintéticas |
| `tools/` | Descarga de fuentes, compilación, servidor de desarrollo |
| `third_party/` | Copias de referencia sin modificar (no versionadas) |

## Licencia

GPL-3.0 (`LICENSE`), la licencia de Nebula, del que este trabajo deriva. Petari es CC0 1.0; Aurora y noclip.website,
usados como referencia, son MIT. Las restricciones de Petari sobre contribuciones generadas por IA se aplican a
contribuciones a Petari; este proyecto no envía cambios a Petari ni genera código de decompilación: su código es la
capa de plataforma web.
