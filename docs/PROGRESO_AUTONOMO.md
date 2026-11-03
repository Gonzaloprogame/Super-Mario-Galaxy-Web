# Progreso del trabajo autónomo

Registro de trabajo para poder retomarlo donde se quedó. Cada bloque dice qué se encontró, por qué, qué se cambió,
cómo se comprobó y qué queda. Nada se marca como hecho sin una prueba que lo respalde.

## Estado de partida (2026-11-03)

- Rama `ccr-8700b376-tetafz`, commit `6a4d025`. 26 parches de Aurora (`web/aurora/patches`, hasta 0026).
- Pruebas: `browser_smoke.mjs` (página, motor, pantalla de carga, vigía), `bridge_smoke.mjs` (puente GX → Aurora),
  `run_thp_video_test.sh`, `run_os_tests.sh`, `run_rvz_tests.sh`, pruebas de consola, audio y servicios del host.
- Comprobado con el disco del usuario: título, archivo, prólogo y sus vídeos, jardín del castillo,
  `HeavensDoorGalaxy`, `AstroGalaxy` (carga), `EggStarGalaxy` (arreglada, parche 0025).
- Abierto: cuelgue en el libro del prólogo en el PC del usuario (no reproducido aquí). 40 de 44 escenarios sin probar.

## Corrección de un dato anterior

`MOTOR_WEB.md` decía que el hilo de la NAND gasta el 100 % de su CPU esperando activamente. **No está comprobado y
probablemente es falso:** el hilo espera con `pthread_cond_wait` (`web/host/console_services.c`, `nand_main`), y el
perfilador del navegador también dio «100 %» a los hilos del reloj dormidos en `_do_futex_wait`: cuenta como ocupado un
hilo bloqueado en una espera.

## Tareas

| # | Tarea | Estado |
|---|---|---|
| 1 | Gira automática de los 44 escenarios (herramienta en el repositorio) | en curso |
| 2 | Mandos físicos (Gamepad API) | hecho con mando simulado; falta un mando real |
| 3 | Cuelgue del libro del prólogo | bloqueado: no se reproduce aquí; falta el registro `[vigía]` del PC del usuario |
| 4 | Arreglar lo que encuentre la gira | pendiente |

## Para reanudar

Se actualiza al cerrar cada bloque. Rutas: `$S` es la carpeta de trabajo local de la sesión (fuera del repositorio:
disco del usuario, NAND, capturas y registros de la gira, que no se suben nunca).

- **En curso:** fallo `RuntimeError: table index is out of bounds` en CocoonExGalaxy, solo tras la secuencia
  BattleShip → BeltConveyerEx → BreakDownPlanet → CannonFleet → CocoonEx (sola no falla en 3000 fotogramas).
  Reproducción: `node tools/galaxy_tour.mjs --disc $S/disc/smg.rvz --stages $S/tour/seq5 --out $S/tour/out_seq5
  --nand $S/drive/nand_RMGE01.bin --frames 900 --hang 180`; la pila queda en `out_seq5/log_end.txt`, y
  `tools/wasm_symbols.py` nombra sus funciones.
- **Después:** `tools/build_web.sh` y `node web/tests/browser_smoke.mjs` (prueba nueva `?hangtest=trap`, sin ejecutar
  todavía); publicar `app.js` y el motor en gh-pages si pasa.
- **Luego:** tandas 01 a 04 de la gira (`$S/tour/batch_01` … `batch_04`, 8 escenarios cada una).
- **Bloqueados:** cuelgue del libro del prólogo (falta el registro `[vigía]` del usuario); PAL y japonesa (Nebula solo
  prepara RMGE01; sin disco para probar); mando real (solo probado con uno simulado).

## Bloques de trabajo

(se añaden abajo, el más reciente al final)

### Bloque 1: gira automática y mandos (2026-11-03)

**Gira automática de escenarios.** `tools/galaxy_tour.mjs` arranca el motor con el disco del usuario en Chromium,
recorre una lista de escenarios con `?tour=` y anota por parada: tiempo de carga, paradas del juego y si se recuperan,
errores y advertencias del renderizador, si siguió dibujando y una captura (solo en la carpeta de salida local: son
imágenes del juego). Se para en el primer cuelgue (una parada que no se recupera en `--hang` segundos) y dice en qué
escenario fue. No dice que un escenario sea jugable: eso hay que comprobarlo jugando.

- Primer intento: ninguna parada registrada en 12 minutos. La herramienta no guardaba el registro mientras corría; se
  añadió `log_tail.txt` en cada lectura.
- Fallo encontrado en la propia herramienta: el registro acaba en salto de línea, así que el último elemento de
  `split('\n')` está vacío y la primera línea nueva de la lectura siguiente ocupaba su sitio; se perdía una línea por
  lectura (por ejemplo, un aviso de bloqueo). Corregido (`seen = lines.length - 1`).
- En el contenedor, sin GPU real, el juego dibuja unos 4 fotogramas por segundo (205 ms por fotograma esperando a la
  GPU por software): cada parada tarda varios minutos.

**Mandos físicos (Gamepad API).** `web/shell/controls.js`: la distribución estándar del navegador (Xbox, PlayStation y
la mayoría de los Bluetooth), leída en cada fotograma mientras el motor corre. Palanca izquierda = Nunchuk; A/✕ = A;
B/○ = B; X, Y y RB = agitar; LT = Z; LB = C; cruceta; Start = +; Select = −; Guía = HOME; clic de las palancas = 1 y 2;
palanca derecha = puntero. Zona muerta 0,18. Prueba nueva en `browser_smoke.mjs` con un mando simulado
(`navigator.getGamepads`): A y LT, la palanca, el puntero y soltarlo todo. **Ejecutada: 44 PASS, 0 FAIL** en
`browser_smoke.mjs` (A+LT = 10240, palanca 63/63, puntero de 5000 a -3000, al soltar 0/0/0). Falta un mando real.

**Dato de la NAND corregido** en `docs/MOTOR_WEB.md` (ver arriba): el hilo duerme en `pthread_cond_wait`
(`web/host/console_services.c`, `nand_main`), no espera activamente.

**Versiones PAL (RMGP01) y japonesa (RMGJ01): bloqueado.** El host ya reconoce las tres (`headless_host.c`, también
las carpetas de idioma de cada una), pero el módulo de juego se compila solo para RMGE01: el propio Nebula lo fija
(`third_party/nebula/native/CMakeLists.txt`, `NEBULA_GAME_VERSION` con RMGE01 como único valor) y la página rechaza
los demás discos para no arrancar código de una región con los datos de otra. Petari tiene ramas por región
(`VERSION`), pero compilarlas sin un disco PAL o japonés con el que probarlas sería código sin comprobar. Queda a la
espera de que Nebula prepare esas regiones o de tener un disco de esas versiones para probar.

### Bloque 2: primera tanda de la gira (2026-11-03)

| Escenario | Carga | Resultado |
|---|---|---|
| BattleShipGalaxy | 5,6 s | cargó y dibujó 900 fotogramas de escena; una parada de 5 s que se recuperó |
| BeltConveyerExGalaxy | 4,1 s | cargó y dibujó |
| BreakDownPlanetGalaxy | 2,7 s | cargó y dibujó |
| CannonFleetGalaxy | 7,6 s | cargó y dibujó |
| CocoonExGalaxy | 4,2 s | **el motor murió** tras unos minutos dibujando: `RuntimeError: table index is out of bounds` |

«Cargó y dibujó» no es «jugable»: nadie ha jugado esos escenarios. Ningún aviso de Aurora en las cuatro primeras.

**El error del motor no traía su pila.** `boot_worker.post.js` mandaba a la página solo el mensaje
(`Error en el motor: …`), así que ni la gira ni el registro que copia un usuario decían dónde fue. Ahora manda también
`error.stack` y la página lo escribe en el registro (`[página] Error en el motor: …; pila:`), como ya hacía con los
abortos. La gira guarda además el registro entero (`log_end.txt`) cuando la página acaba en error.

### Bloque 3: el fallo de CocoonExGalaxy (2026-11-03, en curso)

- **Se reproduce:** con la misma secuencia de cinco escenarios, el motor muere otra vez a los 115 s de CocoonExGalaxy
  (la primera vez, a los 110 s). CocoonExGalaxy sola, 3000 fotogramas de escena (12 min): sin fallo. Depende de lo
  que vino antes.
- **Dónde (pila nueva, `tools/wasm_symbols.py`):** `svc_game_stage` (el host) ← `GameScene::draw()` ←
  `nebula_game_main` ← `boot_thread` ← `fiber_entry`. Desensamblado en el desplazamiento del fallo (`0x625723`):
  el `call_indirect` de un `std::visit` dentro de la exportación de pipelines (parche 0026,
  `web_pipeline_cache_export`, que el host llama al cambiar de escenario y cada 600 escenas). Lee el índice del
  `std::variant` (byte en +2776 de cada elemento de 2784 bytes, un `KnownPipeline`) y toma el puntero de función de
  una tabla de 2 entradas: el índice no es 0 ni 1, así que un elemento está **corrupto**. El 255 (sin valor) habría
  dado `bad_variant_access`, no este error.
- **Descartado:** carrera en el mapa de Aurora (`g_knownPipelines`): todos los accesos van con `g_pipelineMutex`.
  Desbordamiento de la pila: el hilo de arranque tiene 256 KB de pila C (`os_native.c`, `kBootStackSize` 512 KB,
  la mitad para Asyncify), y la ordenación no mantiene pivotes entre niveles.
- **Siguiente medida:** compilación de diagnóstico local (no se sube; `build/site_diag`) que comprueba el índice de
  cada entrada en el mapa, en la copia y tras ordenar, y escribe `[diag]` con sus bytes. Dirá si la corrupción ya
  está en el mapa de Aurora (memoria pisada por otro) o aparece en la exportación.
- **Resultado del diagnóstico:** ningún `[diag]` en el mapa, la copia ni la ordenación, y el mismo fallo en el mismo
  sitio. Los elementos eran válidos: lo corrupto era otra cosa.
- **Causa:** `web/platform/operator_new_wasm.cpp` manda el `new` del hilo del juego al montón del juego (JKRHeap), y su
  regla dice que el host no puede asignar con `new` en las llamadas que el juego le hace en ese hilo. La exportación de
  pipelines (parche 0026, que llama `svc_game_stage` en el hilo del juego) la rompía: sus `std::vector` tomaban memoria
  del juego, y **JKRHeap devuelve nulo cuando su montón actual no tiene sitio**: el vector escribía entonces en la
  dirección 0, encima de los datos estáticos del binario (punteros de función y tablas virtuales incluidos). Con
  cientos de pipelines, cientos de KB. Por eso depende de la historia (cuántos pipelines y qué montón está activo) y el
  fallo es «table index is out of bounds».
- **Arreglo:** un ámbito de asignaciones del host, `nebula_web_host_allocations(1/0)` en `operator_new_wasm.cpp`:
  mientras está abierto, el `new` del hilo del juego va a `malloc`. `smg_bridge_pipeline_cache_export`
  (`web/host/aurora_bridge.cpp`) lo abre alrededor de la exportación (declarado `weak`: `gx_replay` no tiene juego).
  Revisadas las demás funciones del puente que llama el hilo del juego: ninguna asigna.
- **Prueba nueva:** `bridge_smoke.mjs`, `?hostnew=1` (`web/bridge/bridge_main.c`): la exportación desde el hilo del
  juego debe devolver memoria del host (ni nula ni dentro de la arena). **Sin el arreglo falla**: «12 bytes at 0, at
  address 0». Con él: «12 bytes at 0x4de7b50, the host's memory».
