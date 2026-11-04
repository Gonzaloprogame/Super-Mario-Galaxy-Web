# Herramientas de perfil del motor

Perfilan los workers del motor en Chromium con el perfilador del navegador (CDP) y leen el resultado con los nombres
de las funciones de wasm. Usadas en los bloques 13 y siguientes de `docs/PROGRESO_AUTONOMO.md`.

1. Arranca Chromium con `--remote-debugging-port=<puerto>` y el motor jugando (por ejemplo, con `tools/galaxy_tour.mjs`
   o a mano).
2. `PROF_DUMP=<carpeta> node tools/perf/prof.mjs <puerto> <segundos> [top]`: muestrea cada worker; imprime lo más caro
   por tiempo propio y total, y con `PROF_DUMP` guarda cada perfil (`.cpuprofile`).
3. Copia el mapa de símbolos del mismo binario (`build/all/engine/smg_web.js.symbols`) **antes de recompilar**: un mapa
   de otro binario da nombres falsos.
4. Lectura:
   - `node tools/perf/prof_named.mjs <carpeta> <símbolos> [top]`: tiempo propio por función, sin las esperas.
   - `node tools/perf/prof_total.mjs <perfil> <símbolos> [top]`: tiempo inclusivo por función.
   - `node tools/perf/prof_callees.mjs <perfil> <símbolos> <nombre>`: lo que cuesta cada función llamada desde esa.
   - `node tools/perf/prof_callers.mjs <perfil> <símbolos> <nombre>`: quién llama a esa función.
   - `node tools/perf/prof_js_under.mjs <perfil> <símbolos> <nombre>`: el JavaScript (WebGPU) que hay debajo.

Ojo: el perfilador cuenta como ocupado un hilo bloqueado en una espera (`_do_futex_wait`); compruébalo con `top -H`.
En la salida de `wasm-dis`, la función `$N` es el símbolo `N - 320` (en el binario de noviembre de 2026 había 320 importaciones;
compruébalo en el tuyo).
