# Galaxy Web Port: guía para Claude

Port de Super Mario Galaxy (RMGE01) al navegador: el código del juego de Petari a través de Nebula, compilado a
WebAssembly con Emscripten, y renderizado con Aurora sobre WebGPU.

**Antes de nada, lee `docs/PROGRESO_AUTONOMO.md`, sección «Para reanudar»: dice dónde se quedó el trabajo y qué toca.
No repitas lo ya hecho.**

## Reglas del usuario (siempre)
- Habla en **español** con el usuario. Los commits también van en español.
- Respeta las licencias (este repositorio es GPL-3.0, como Nebula; Petari es CC0; Aurora es MIT).
  - **Nunca** escribas ni aportes código de descompilación generado por IA: Petari lo prohíbe.
  - El juego ya está descompilado (Nebula compila el 99,90 % de las funciones de RMGE01).
- **Nunca** subas, incluyas ni distribuyas archivos de Nintendo: ni el disco (`.rvz`), ni la partida guardada, ni
  capturas del juego, ni registros. Todo eso va solo en la carpeta temporal de la sesión (`$S`).
  - No mandes capturas del juego al usuario y no uses logotipos de Nintendo.
- **No inventes resultados.** No declares un hito hecho sin comprobar sus criterios, y no digas que algo funciona sin
  haberlo probado. La interfaz no oculta errores ni simula que el juego funciona.
- `third_party/` (Petari, Nebula, Aurora) se queda **intacto**.
  - Los cambios van en `web/`.
  - Los de Aurora, como parches en `web/aurora/patches/NNNN-*.patch`, aplicados con `patch -p1` a una copia en
    `build/all/aurora/aurora-patched`.
- No borres pruebas. Cada arreglo lleva su prueba.

## Dónde está cada cosa
- `web/host/`: el host del motor (`headless_host.c`: servicios de Nebula, giras de prueba, informes) y el puente a
  Aurora (`aurora_bridge.cpp`).
- `web/platform/`: sustituye partes del SDK de Nebula; cada archivo lleva una línea `REPLACES:`.
- `web/shell/`: la página (`index.html`, `app.js`, `present.js`, `controls.js`, `fps.js`, `servir.py`).
- `web/aurora/patches/`: los parches de Aurora.
- `web/tests/`: las pruebas.
- `tools/`:
  - `build_web.sh`: compila y monta el sitio.
  - `galaxy_tour.mjs`: gira por los escenarios con el disco.
  - `perf/`: perfiles.
  - `code_index.sh`: análisis C/C++ local con clangd y clang-tidy.
- `docs/`:
  - `PROGRESO_AUTONOMO.md`: el registro de trabajo, por bloques.
  - `HOJA_DE_RUTA.md`: los hitos y sus criterios.
  - `MOTOR_WEB.md`
  - `progreso.json`: el panel de la página.

## Compilar, probar y publicar
```sh
source /home/user/tools/emsdk/emsdk_env.sh          # Emscripten 6.0.13
tools/build_web.sh                                   # compila y monta build/site (¡lo borra antes!)
ninja -C build/all smg_headless_web smg_web gx_bridge gx_replay gx_demo   # solo compilar
node web/tests/browser_smoke.mjs                     # la página (SMG_SITE=<otra copia> para no usar build/site)
node web/tests/bridge_smoke.mjs build/site/bridge    # GX -> Aurora
web/tests/run_index_writer_test.sh                   # índices de Aurora (parche 0027)
tools/code_index.sh check <archivo>                  # diagnósticos de clangd como wasm32
```
- **Publicar:** hay un worktree de la rama `gh-pages` en `$S/gh-pages`. Copia ahí lo que cambió, haz commit y
  `git push origin HEAD:gh-pages`.
- **Rama de trabajo:** `ccr-8700b376-tetafz`. Se sube con `git push -u origin ccr-8700b376-tetafz`. Nada de force push
  ni de PR sin pedirlo.

## Trampas conocidas
- `tools/build_web.sh` borra `build/site`: no lo lances mientras una gira lo esté usando. Monta otra copia del sitio y
  pásala con `--site` o `SMG_SITE`.
- Tareas en segundo plano: dales `timeout` (como mucho 2 h). Si no, se cortan a los 30 min. Una gira de 44 escenarios
  tarda más de una hora.
- No uses `pkill -f <patrón>` si el patrón también casa con tu propia orden: te mata la shell. Mata por PID.
- Perfiles: usa el mapa de símbolos del **mismo** binario (`build/all/engine/smg_web.js.symbols`) y cópialo antes de
  recompilar.
- La GPU de este contenedor es emulada: el tiempo de terminar un fotograma tiene mucho ruido. Compara siempre con
  varias tandas alternas.
