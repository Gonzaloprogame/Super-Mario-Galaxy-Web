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
| 1 | Gira automática de los 44 escenarios (herramienta en el repositorio) | hecho: 44 de 44 cargan y dibujan (sin jugar) |
| 2 | Mandos físicos (Gamepad API) | hecho con mando simulado; falta un mando real |
| 3 | Cuelgue del libro del prólogo | probable causa arreglada (bloque 12, reloj); falta que el usuario lo pruebe |
| 4 | Arreglar lo que encuentre la gira | CocoonExGalaxy arreglada (bloque 3); sigue la gira |

## Para reanudar

Se actualiza al cerrar cada bloque. Rutas: `$S` es la carpeta de trabajo local de la sesión (fuera del repositorio:
disco del usuario, NAND, capturas y registros de la gira, que no se suben nunca).

- **En curso:** jugar a ciegas más tiempo (5400 fotogramas por parada, unos 90 s de juego, los 44 escenarios, con lo
  publicado en `$S/site2`): `timeout 7100 $S/long1/run.sh 9 > $S/long1/run.out 2>&1` (tarea de fondo con `timeout`
  de 2 h). Si se cuelga: `prof_N.txt`, `dump_N/` y `hang_log_N.txt` en `$S/long1`. Mapa de símbolos:
  `build/all/game/smg_headless_web.js.symbols`; índices de tabla: `$S/tools/table_index.py`.
- **Después:** rendimiento del hilo de render (bloque 13: procesar el flujo GX es el 59 % de su trabajo; perfil con
  `$S/drive/prof.mjs` y `$S/tools/prof_total.mjs`, `prof_callees.mjs`, `prof_callers.mjs`, con el mapa de símbolos del
  mismo binario), medidas en el PC/móvil del usuario, y jugar galaxias (lo que la gira no comprueba).
- **Ojo:** las tareas de fondo se cortan a los 30 min si no se les da `timeout` (hasta 2 h); las giras de 44 paradas
  tardan más de 1 h.
- **Bloqueados:** cuelgue del libro del prólogo (pendiente de que el usuario lo pruebe con la versión publicada; tiene
  la firma del cuelgue del reloj arreglado en el bloque 12, sin comprobar que sea el mismo); PAL y japonesa (Nebula solo
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
- **Comprobado en el juego:** la misma secuencia de cinco escenarios con el arreglo: las cinco paradas completas,
  CocoonExGalaxy 166 s (antes moría a los 110–115 s), la gira terminada. La exportación sigue funcionando (300
  pipelines guardados). Publicado en gh-pages (`5e5d471`).
- **Posible relación con el cuelgue del libro:** el fallo escribía sobre datos estáticos del motor a cada cambio de
  escenario y cada 600 escenas, así que pudo también colgar el juego en cualquier sitio. Sin comprobar: hay que
  repetir el prólogo en el PC del usuario con esta versión.

### Bloque 4: detector de escrituras por puntero nulo (2026-11-03)

- **Por qué:** el fallo de CocoonExGalaxy era una escritura en la dirección 0, que WebAssembly no detiene (en una
  consola o un PC habría parado en el acto). Cualquier otra del mismo tipo puede romper el motor más tarde y en otro
  sitio, o colgarlo, como quizá el libro del prólogo.
- **Qué:** por debajo de 1024 la memoria de wasm no contiene nada (los datos de Emscripten empiezan ahí, `GLOBAL_BASE`;
  la arena del juego está en 512 MB). El informe del host (cada 10 s y al parar el juego) la revisa y, si ha cambiado,
  escribe `[host] memory error: N bytes below address 1024 have been written, the first at X (bytes…)`. La gira lo
  anota como problema de la parada.
- **Fallo encontrado de paso:** cuando el juego paraba (`svc_fatal`), su informe final iba por el registro de vigía y
  la página podía ver la salida del motor antes que él: se perdía justo el informe que importa. Ahora va por `stderr`,
  en orden (el hilo del juego es el que está ahí: no está colgado).
- **Pruebas:** `browser_smoke.mjs`: `?hangtest=nullwrite` (el host escribe 32 bytes en la dirección 64) debe dar la
  alarma con esos bytes; un arranque normal no debe darla. 48 PASS, 0 FAIL (con `SMG_SITE`, una copia del sitio,
  porque la gira usaba `build/site`).

### Bloque 5: tanda 01 de la gira (2026-11-03)

Los tres que faltaban de la tanda 00 y los ocho de la 01, con el arreglo del bloque 3 (sin la alarma del bloque 4, que
se compiló a mitad). Once de once cargaron y dibujaron 900 fotogramas de escena; ningún aviso de Aurora.

| Escenario | Carga | Paradas del juego |
|---|---|---|
| CosmosGardenGalaxy | 14 s* | 1, recuperada |
| CubeBubbleExLv1Galaxy | 7,8 s* | 0 |
| CubeBubbleExLv2Galaxy | 5,6 s | 0 |
| DarkRoomGalaxy | 6,2 s | 0 |
| FactoryGalaxy | 15,5 s* | 1, recuperada |
| FishTunnelGalaxy | 5,2 s | 0 |
| FlipPanelExGalaxy | 2,8 s | 0 |
| FloaterOtaKingGalaxy | 4,7 s | 0 |
| HeavenlyBeachGalaxy | 10 s | 0 |
| HellProminenceGalaxy | 10 s | 0 |
| HoneyBeeExGalaxy | 3,1 s | 0 |

\* Coincidieron con compilaciones en el contenedor (4 núcleos): su carga no es representativa.

Llevamos 16 escenarios de la gira, más los 4 comprobados antes: 20 de 44 (corregido: aquí ponía 24). Ninguno jugado.

### Bloque 6: tanda 02 de la gira, con la alarma de puntero nulo (2026-11-03)

Ocho de ocho cargaron y dibujaron 900 fotogramas de escena, sin avisos de Aurora. Primera tanda con el motor del
bloque 4: en 161 informes del host, **ninguna alarma de memoria** (nada escribió por debajo de la dirección 1024).

| Escenario | Carga | Paradas del juego |
|---|---|---|
| HoneyBeeKingdomGalaxy | 9,4 s | 1, recuperada |
| IceVolcanoGalaxy | 9,2 s | 0 |
| KoopaBattleVs1Galaxy | 6,8 s | 0 |
| KoopaBattleVs2Galaxy | 7,6 s | 0 |
| KoopaBattleVs3Galaxy | 10,8 s | 0 |
| KoopaJrShipLv1Galaxy | 8,1 s | 0 |
| OceanFloaterLandGalaxy | 7,9 s | 0 |
| OceanPhantomCaveGalaxy | 11,6 s | 0 |

Llevamos 28 de 44 (24 de la gira y 4 de antes; corregido: aquí ponía 32). Ninguno jugado. Las tandas 03 y 04 van encadenadas
(`$S/tour/out_03`, `out_04`).

### Bloque 7: tanda 03 de la gira (2026-11-03)

Ocho de ocho cargaron y dibujaron, sin avisos de Aurora ni alarmas de memoria (150 informes).

| Escenario | Carga | Paradas del juego |
|---|---|---|
| OceanRingGalaxy | 6,5 s | 1, recuperada |
| PeachCastleFinalGalaxy | 4,9 s | 0 |
| PhantomGalaxy | 10,2 s | 0 |
| ReverseKingdomGalaxy | 9,6 s | 0 |
| SandClockGalaxy | 8 s | 0 |
| SkullSharkGalaxy | 5,3 s | 0 |
| SnowCapsuleGalaxy | 3,4 s | 0 |
| StarDustGalaxy | 6,7 s | 0 |

Llevamos 36 de 44 (32 de la gira y 4 de antes). Ninguno jugado. Falta la tanda 04, en curso.

**Tanda 04, SurfingLv1Galaxy:** cargó (4 s) y dibujó, con el aviso del parche 0022: `a stage reads texcoord 1, but no
texgen makes it (texgen source 21, type 1)`. Es el caso ya tratado (una etapa TEV que lee una coordenada que el juego no
genera, indefinido en la consola; aquí coordenadas 0, en lugar de parar). Se avisa al crear el pipeline, como mucho 8
veces por sesión. Puede ser una diferencia visual pequeña en algún efecto de ese nivel: **pendiente de mirar jugando**.

### Bloque 8: tanda 04 y fin de la gira (2026-11-03)

| Escenario | Carga | Paradas del juego |
|---|---|---|
| SurfingLv1Galaxy | 4 s | 0 (aviso del parche 0022, arriba) |
| SurfingLv2Galaxy | 4,4 s | 0 |
| TamakoroExLv1Galaxy | 3,1 s | 0 |
| TamakoroExLv2Galaxy | 4,1 s | 0 |
| TearDropGalaxy | 4 s | 0 |
| TeresaMario2DGalaxy | 4 s | 0 |
| TransformationExGalaxy | 5,1 s | 1, recuperada |
| TriLegLv1Galaxy | 6,6 s | 0 |

Sin alarmas de memoria (127 informes).

**Resultado de la gira (comprobado contra la lista de 44):** los 40 escenarios de la gira terminaron «cargó y dibujó»
(CocoonExGalaxy, tras el arreglo del bloque 3) y los otros 4 se comprobaron antes: **44 de 44 cargan y dibujan**.
Encontrado y arreglado por el camino: el fallo de memoria de la exportación de pipelines. Queda un aviso conocido
(SurfingLv1). **Ninguno se ha jugado**: el hito «jugable de principio a fin» sigue abierto.

### Bloque 9: en modo diagnóstico el juego no recibía los controles (2026-11-04)

- **Encontrado** al preparar el libro del prólogo en modo diagnóstico (el juego a toda velocidad): el informe decía
  `Wii Remote reports 0` aunque se pulsaba A; el juego seguía en el menú.
- **Causa:** en el navegador cada hilo corre en un worker del conjunto de Emscripten (`PTHREAD_POOL_SIZE`). Uno creado
  más allá necesita un worker nuevo, que solo arranca cuando el hilo que lo crea vuelve al bucle de eventos: el hilo
  del juego no vuelve nunca, así que ese hilo se crea **y no corre nunca, sin error**. La compilación sin renderizador
  tenía 4 workers y el host crea 5 hilos (su reloj, el reloj del SO de Nebula, la NAND, el audio y el mando): el del
  mando, el último (se crea cuando el juego arranca WPAD), no arrancaba. Con gráficos hay 8 para 6: por eso allí iba.
- **Arreglo:** `web/cmake/SmgGame.cmake`, conjunto de 6. Y una comprobación al arrancar (`check_thread_pool`,
  `headless_host.c`): cuenta los workers libres frente a los hilos que el host va a crear y, si no llegan, para con un
  error claro (`the thread pool has N workers and the host needs M`) en lugar de dejar un hilo muerto.
- **Comprobado:** con 4, la comprobación para el motor con ese error; con 6, arranca (`6 workers for the host's 5
  threads`), y con el disco el mando llega (`Wii Remote reports` ~100 por segundo). `browser_smoke.mjs` comprueba la
  línea en los dos modos.
- **Pruebas:** `browser_smoke.mjs` 50 PASS, 0 FAIL; `bridge_smoke.mjs` 56 PASS, 0 FAIL.
- **Libro del prólogo en modo diagnóstico** (ya con mando): partida nueva y 8 minutos pulsando A cada 2,5 s, el juego
  a toda velocidad: sin paradas, sin cuelgue y sin alarmas de memoria. **No se puede confirmar qué página se veía**: sin
  renderizador no hay pantalla, y en ese modo no se decodifican los vídeos, así que el flujo puede no ser igual al del
  PC del usuario. No reproducido; sigue pendiente de su prueba con la versión publicada.

### Bloque 10: cargas medidas, y un cuelgue a toda velocidad (2026-11-04)

**Cargas.** De los registros de la gira con gráficos (40 cargas): media 6,8 s, de los que el disco son 680 ms (10 %) y
los pipelines creados durante la carga, 0. La misma gira en modo diagnóstico (`--mode diagnostic`: el juego a toda
velocidad, sin renderizador): **media 1,27 s** (disco 159 ms), la más lenta OceanPhantomCaveGalaxy, 2,3 s. El trabajo
del propio juego al cargar (objetos, archivos, disco) es rápido; el resto, con gráficos, es esperar al renderizador, que
aquí es una GPU por software que comparte los 4 núcleos con el juego. Con una GPU real hay que medirlo en el PC o el
móvil (`[host] load …` en el registro). Precargar modelos y texturas no ataca lo que se ve aquí.

**Un cuelgue en StarDustGalaxy, en modo diagnóstico.** La parada 32 de 40, nada más cargar: ningún GX en 125 s, el
juego **fuera de cualquier servicio del host**, los hilos **sin cambiar** y la sonda sin ejecutarse: el hilo del juego
da vueltas en su propio código sin ceder. Es la misma firma que el cuelgue del libro en el PC del usuario, y en modo
gráfico (4 fotogramas por segundo) no ha salido nunca. StarDustGalaxy sola: sin cuelgue; la misma secuencia de 32 otra
vez: sin cuelgue. **No es determinista**: una vez en unas 70 cargas a toda velocidad.

- Herramientas: `galaxy_tour.mjs --mode diagnostic` y `--debug-port` (para el perfilador del navegador durante un
  cuelgue); el motor de diagnóstico emite ahora su mapa de símbolos (`smg_headless_web.js.symbols`, no se publica).
- En curso: repetir la gira de diagnóstico hasta cazarlo y perfilar el hilo atascado en ese momento.
- **Cazador:** 8 giras más de diagnóstico de los 40 escenarios (320 cargas a toda velocidad, con `--debug-port` y el
  perfilador listo): **ningún cuelgue**. Es raro (una vez en unas 350 cargas); no se puede contar con reproducirlo.
- **Para que el próximo diga dónde:** el aviso de bloqueo lleva ahora **las últimas llamadas del juego al host**
  (`the game's last calls to the host: …`): las 8 más recientes distintas, con cuántas veces y hace cuánto (el objeto
  que creaba, en Shift-JIS escapado, el escenario, los fotogramas de escena, la GX, la mezcla de audio…). Un bucle del
  juego no llama al host, así que lo último que hizo dice dónde estaba. Visto en un aviso real (BattleShipGalaxy, la
  espera al renderizador al cargar): objetos creados → escenas → GX → audio → cola del renderizador. Prueba nueva en
  `browser_smoke.mjs` (51 PASS, 0 FAIL). El hilo atascado del cuelgue de StarDust era `boot_thread`, el principal del
  juego (índice de tabla 13911, `$S/tools/table_index.py`).
- **Cazador, segunda vuelta** (con el rastro; un reinicio del contenedor por medio): 10 giras más de los 40 escenarios
  a toda velocidad, sin cuelgues. En total, unas 720 cargas en modo diagnóstico y **un solo cuelgue** (StarDustGalaxy).
  Queda vigilado: si vuelve, aquí o en el PC del usuario, el aviso dirá lo último que hizo el juego.

### Bloque 11: jugando a ciegas, el cuelgue sale mucho más (2026-11-04)

`galaxy_tour.mjs --play <semilla>` (palanca y botones al azar cada 0,7 s; comprobado que llegan: las palabras de
control cambian, 1024 = B, 2048 = A, palanca 71/71…). Los 44 escenarios en modo diagnóstico, 30 s de juego cada uno,
semilla 1: **cuelgue en CubeBubbleExLv2Galaxy**, la parada 9. La misma firma que el de StarDust (`boot_thread` en un
bucle propio, sin cambios de hilo, fuera del host), y ya con el rastro:

- los últimos objetos creados, 8,6 s antes (Shift-JIS descodificado): 壊れる籠 (jaula rompible), 壊れる籠壊れモデル (su
  modelo roto), 中継惑星Ｄ y Ａ (planetas de enlace), スターピースディレクターピース (el director de trozos de estrella),
  影描画[ボリューム球] (sombra de una esfera);
- después, 192 fotogramas de escena normales; el último 5,2 s antes del aviso: el bucle empezó dentro de un fotograma,
  unos 3,4 s después de crear esos objetos. No señala a ninguno: es la pista de dónde estaba.

Jugando sale una vez en 9 escenarios, frente a una en ~720 cargas sin jugar: se puede cazar. En curso:
`$S/catch3/run.sh 5` (semillas 1 a 5, con el perfilador al detectar 20 s sin GX).

### Bloque 12: la causa del cuelgue a toda velocidad (2026-11-04)

**Cazado con el perfilador** (jugando a ciegas, `$S/catch3`, tres veces: BreakDownPlanet, KoopaBattleVs2…). El hilo
del juego pasa el 100 % en `nebula_game_main` (el bucle principal, con mucho código del juego en línea), sin llamar a
nada. En su desensamblado, de los 11 bucles sin llamadas, uno no tiene fin si su paso es 0:
`x = G; while (x < 2 * campo) x += G`, con `G` una global en 962800 que vale 670000 al arrancar y luego
`OSGetTick() - anterior`. En el código: **`MainLoopFramework::calcCombinationRatio`** suma `JUTVideo::getVideoInterval()`
hasta pasar dos fotogramas, y **`JUTVideo::preRetraceProc`** guarda ese intervalo en cada retrazado
(`sVideoInterval = tick - sVideoLastTick`).

**Causa:** en la consola dos retrazados están a 16,7 ms; Nebula entrega los que llegaron mientras el juego estaba
ocupado uno tras otro, y en el navegador el reloj monótono (`performance.now()`) tiene microsegundos de resolución: los
dos leen el mismo instante, el intervalo es 0 y el bucle no acaba. Por eso solo a toda velocidad y dependiendo del
momento; y es la firma del cuelgue del libro en el PC del usuario (sin comprobar que sea el mismo).

**Arreglo, en la plataforma (sin tocar el juego):** `web/platform/monotonic_clock_wasm.c`, con
`--wrap=clock_gettime` (`web/cmake/SmgGame.cmake`): el reloj monótono no repite valor; una lectura que no pasa la
anterior devuelve la anterior más 20 ns (algo más de un tick de la consola, 16,5 ns). Como la base de tiempos de la
consola, que nunca se repite.

**Pruebas:** `web/tests/monotonic_clock_test.c` en `run_os_tests.sh`, un millón de lecturas seguidas de `OSGetTick`:
sin el arreglo **396.780 iguales a la anterior**; con él, 0 (PASS). Todas las pruebas del SO pasan; `browser_smoke`
51 PASS, `bridge_smoke` 56 PASS.

**Prueba en el juego** (`$S/catch4/run.sh 4`: giras de diagnóstico de los 44 escenarios jugando a ciegas, 1800
fotogramas de escena cada parada, con el perfilador a la espera). Antes del arreglo, **3 cuelgues en 6 giras** (bloque
11 con la semilla 1; `$S/catch3/first` con la semilla 1; `$S/catch3` con las semillas 1 a 4, cuelgue en la 4). Con el
arreglo, semillas 1 a 4: **0 cuelgues en 4 giras, 176 de 176 paradas cargan y dibujan, ninguna con problemas**. Es una
prueba fuerte pero no definitiva: con la frecuencia de antes, 4 giras limpias saldrían por azar un 6 % de las veces.
Con las semillas 5 a 8 (`$S/catch5`): **otras 4 giras completas, 176 de 176 paradas limpias**. En total, con el
arreglo, **8 giras, 352 de 352 paradas, 0 cuelgues** (antes, 3 en 6; con aquella frecuencia, 8 limpias saldrían por
azar un 0,4 % de las veces).

De dónde salen los retrazados seguidos: la interrupción pendiente ya es un solo bit, como en la consola
(`os_native.c`, `sPending`); es el reloj del host (`clock_main`, `headless_host.c`), que va por plazos absolutos y, si
se despierta tarde, recupera de golpe los retrazados atrasados. Si el juego atiende uno entre dos de ellos, el
intervalo es casi 0. Con el arreglo vale 20 ns y `calcCombinationRatio` da ese fotograma ~1 millón de vueltas (~1 ms);
`mCombinationRatio` no se lee en ninguna otra parte del código del juego, así que no se toca el reloj del host.

Publicado en gh-pages (`f977c04`: `smg_web.wasm`, `smg_headless_web.wasm`, `bridge/gx_bridge.wasm`).

### Bloque 13: el hilo de render, perfilado en el jardín del castillo; la presentación sin copias (2026-11-04)

**Perfil** (`$S/perf1`: el jardín del castillo con gráficos, 20 s con el perfilador del navegador; nombres con
`smg_web.js.symbols` del mismo binario). El trabajo del hilo de render (sin las esperas): procesar el flujo GX 59 %
(`draw_prim_as_written`, `process`, `push`…), terminar el fotograma 16 % (`writeBuffer` 13 %) y **presentar 12 %**: cada
fotograma se copiaba en JS (`slice` de unos 1,2 MB) y el `BroadcastChannel` lo clonaba otra vez.

- De paso: el perfilador dio al hilo de la NAND un 100 % «propio» en `nand_main`. **No gira**: llama a
  `pthread_cond_timedwait` (desensamblado) y el sistema da a cada worker como mucho un 17 % de CPU (`top -H`); es un
  artefacto del perfilador con un hilo bloqueado desde antes de empezar. La corrección del principio sigue en pie.

**Cambio:** los fotogramas van por la memoria compartida (`smg_bridge_present_memory`, `web/host/aurora_bridge.h`): tres
huecos con un número de secuencia (impar mientras se escribe). El hilo de render desentrelaza las filas directamente en
uno y publica solo cuál (`{type: 'frame', slot, sequence}`). La página (`present.js`, `attachMemory`) copia una vez a un
`ImageData` que reutiliza y descarta el fotograma si la secuencia cambió. Solo se usa cuando la página marca los huecos
como leídos: una página antigua (en caché) con un motor nuevo sigue por el camino de antes, y también las páginas del
puente (`gx_bridge.html`, `gx_replay.html`), que no reciben la memoria. Un `SharedArrayBuffer` no se puede mandar por
`BroadcastChannel` (comprobado en este Chromium: el mensaje no llega), por eso la memoria la da el host del motor.

**Comprobado:**
- `browser_smoke.mjs` 54 PASS: 3 nuevas (el motor da los huecos y la página los marca; un fotograma anunciado por su
  hueco se pinta con sus píxeles; uno reescrito se descarta). `bridge_smoke.mjs` 56 PASS (el camino de antes).
- Con el disco, el jardín del castillo, 3000 fotogramas, el mismo escenario (`$S/perf2`): todos los fotogramas por la
  memoria compartida (`350 shown on the page (350 through shared memory)`). Por fotograma, **terminar pasó de 15,2 ms a
  9,8 ms**; procesar el flujo no cambia (17,6 → 16,4 ms, ruido). La parada entera tardó 750 s en vez de 940 s
  (4,1 frente a 3,3 fps en este contenedor, con la GPU por software). La imagen es la misma.
- **Sin medir en el teléfono**: allí terminar costaba unos 8 ms.

**Lo que queda, medido** (`$S/perf2`, con el cambio; trabajo del hilo de render = 100 %):
- procesar el flujo 67 %: `draw_prim` 42 % (unas 39 000 primitivas por fotograma, cada una con su paso por
  `draw_prim_as_written`: 14 % propio, `push_gx_draw` 12 %, del que `hash_texture_source` 2,5 %, índices 12 %), las
  copias del EFB 11 % (al partir el pase se codifican sus dibujos: llamadas a WebGPU, `setBindGroup` 2 %,
  `setIndexBuffer` y `setBlendConstant` 1 % cada una) y el bucle de comandos 10 %;
- `writeBuffer` 15 %: cinco escrituras por fotograma, unos 6 MB (almacenamiento 2,5 MB: las matrices de vértices del
  juego, que se suben otra vez en cada fotograma; uniformes 1,7 MB, unos 2 KB por dibujo). Guardarlas entre fotogramas
  pide un búfer persistente en Aurora con su invalidación (`aurora_guest_written`): ganancia estimada ~6 %, cambio
  grande; pendiente.

**Probado y descartado: hashear cada textura del juego una vez por fotograma.** Las texturas que vienen de los
registros del juego no tienen identidad y Aurora hashea su contenido en cada enlace (`sync_guest_textures`, a
propósito: así ve si el juego la reescribe). Un parche (0027, retirado) guardaba el resultado dentro del fotograma,
olvidándolo si el juego vaciaba su caché sobre esa memoria. Medido en el jardín (`$S/perf4`, con contadores): ahorró
el 24 % de los bytes hasheados (131 MB frente a 413 MB), pero en el perfil el hash pasó del 2,5 % al 1,9 % del hilo
de render y la búsqueda costó otro ~0,5 %: **ganancia neta ~0**, dentro del ruido (procesar 16,4 → 15,7 ms). La
mayoría de las texturas se enlaza una sola vez por fotograma; lo que ahorraría sería guardarlas entre fotogramas, y
eso depende de que el juego vacíe siempre su caché antes de que la GPU lea (sin comprobar para las lecturas del
disco). No se hace.

**El hilo del juego gastaba ~13 % en leer el reloj para el rastro de diagnóstico** (bloque 10, mío). Perfil del jardín
(`$S/perf4`): de las 187 muestras de `svc_gpu_coherent`, 163 eran `clock_gettime` (llamada a JS) y 6 su trabajo; el
juego lo llama miles de veces por fotograma (cada vaciado de caché de `J3DModel::viewCalc` y del audio), y
`SERVICE_ENTER` leía el reloj dos veces. Ahora el rastro y «dentro del servicio desde» usan los retrazados contados
(`retrace_ms`, una lectura atómica, pasos de 16,7 ms) y una llamada repetida sin detalle se cuenta en su sitio.
**Medido** (`$S/perf5`, mismos 20 s del jardín): trabajo del hilo del juego **1729 → 1385 muestras (−20 %)**,
`svc_gpu_coherent` 10,8 % → 1,3 %, el reloj 16,9 % → 6,6 % (lo que queda son lecturas del propio juego, `OSGetTick`, y
dos por subtrama en el mezclador de Nebula, que no se toca). `browser_smoke` 54 PASS (con el aviso de bloqueo y su
rastro). Comprobado de paso en el desensamblado que `OSGetTick` pasa por `__wrap_clock_gettime` (el reloj del
bloque 12): el perfilador pierde ese marco al ponerlo en línea.

**Por qué el teléfono suma y no solapa** (27 fps: 20 ms procesar + 8 terminar + 8 esperando al juego): al final de cada
fotograma el juego espera a que la GPU acabe ese fotograma (`MainLoopFramework::endFrame` → `waitDrawDoneAndSetAlarm`
→ `GXDrawDone`), como en la consola; durante la lógica del fotograma siguiente no sale GX y el hilo de render espera.
Solaparlos pediría contestar el fin de dibujo antes de tiempo, y el juego reescribiría vértices, matrices y listas
mientras Aurora aún los lee: no se hace. Cada ms que se quita al hilo de render es un ms menos por fotograma.
