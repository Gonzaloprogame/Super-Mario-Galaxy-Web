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
