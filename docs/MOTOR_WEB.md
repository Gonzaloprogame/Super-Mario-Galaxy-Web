# Probar el motor con tu copia del juego

Estado verificado el 2026-10-28. El motor con renderizador (`smg_web`) reúne el módulo de juego, el host que lee tu
`game.pak` y Aurora sobre el WebGPU del navegador. **No se ha ejecutado nunca con el juego real**: en este entorno no
hay copia del juego y no puede haberla. Lo que sí está verificado está más abajo, y lo que falta comprobar es
precisamente lo que tu prueba puede responder.

## Qué necesitas

- Tu copia de Super Mario Galaxy (RMGE01), de tu propio disco, en formato **RVZ** de Dolphin, o como `game.pak`
  ([`ARCHIVOS_DEL_JUEGO.md`](ARCHIVOS_DEL_JUEGO.md), [`RVZ.md`](RVZ.md)). El repositorio no contiene ni descarga
  ningún archivo del juego.
- Un navegador de escritorio con WebGPU: Chrome o Edge recientes en un equipo con GPU. Firefox y Safari ofrecen WebGPU
  según versión y sistema; no están probados.
- El sitio servido con aislamiento de origen cruzado (COOP/COEP), que ya da `tools/serve.py`.

## Qué instalar en tu equipo

Linux o macOS; en Windows, dentro de WSL2 (los scripts son de shell POSIX). Hace falta: `git`, `python3`, `cmake`
(3.24 o posterior), `ninja` y `cargo` (Rust). `tools/fetch_sources.sh` usa `cargo` para descargar SQLite (el *crate*
`libsqlite3-sys`). Además, Emscripten 6.0.13:

```sh
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk && ./emsdk install 6.0.13 && ./emsdk activate 6.0.13
```

## Cómo arrancarlo

Desde la carpeta del repositorio (`<emsdk>` es la carpeta donde instalaste emsdk; con el ejemplo de arriba,
`~/emsdk`):

```sh
source ~/emsdk/emsdk_env.sh
tools/fetch_sources.sh
tools/build_web.sh
python3 tools/serve.py 8080 build/site
```

La primera compilación tarda: son unas 1.900 unidades del juego más Aurora y sus dependencias. El servidor sigue en
marcha hasta que lo detengas con Ctrl+C.

Abre `http://127.0.0.1:8080` y:

1. Selecciona tu `.rvz` o tu `game.pak`. No sale del navegador: el motor lo lee por trozos dentro de un *worker*.
2. Elige el modo **Con gráficos (Aurora, WebGPU)** y el **idioma de la consola** (de partida, el del navegador).
   «Segundos» en 0 ejecuta sin límite.
3. Pulsa **Arrancar el motor**.

La tarjeta «Pantalla» solo muestra fotogramas que el motor ha enviado: el XFB que el VI del juego tiene seleccionado.
Debajo se indica cuántos van. Si el motor se detiene, el estado muestra su código de salida real y el registro, el
motivo.

## Qué está verificado (sin el juego)

- **El puente gráfico, con una escena de prueba dibujada por la librería GX del propio módulo de juego**
  (`web/tests/bridge_smoke.mjs`, 50 comprobaciones). Cubre vértices directos e indexados, listas de visualización,
  texturas RGB565 y C8 con TLUT, copias del EFB a textura, la copia a pantalla con su borrado, y las lecturas del EFB
  dentro y fuera de una *callback* de token. La comprobación final lee el canvas de la página. Detalle en
  [`PUENTE_FIFO.md`](PUENTE_FIFO.md).
- **La página del motor en los dos modos** (`web/tests/browser_smoke.mjs`), con una partición sintética que no
  contiene datos del juego. Con gráficos, Aurora arranca sobre WebGPU en el *worker* del motor, el módulo de juego
  llega a su cargador de datos de `main.dol`, se detiene porque la partición está vacía, y la página muestra el código
  de salida real (3).
- **El cierre ordenado:** antes de terminar el proceso se detiene el hilo de render y se apaga Aurora. Sin esto,
  `exit()` se quedaba esperando para siempre y la página nunca se enteraba de que el motor había parado.
- **El fin del motor llega a la página aunque Emscripten no lo comunique:** el host lo anuncia en el canal del
  presentador (`[host] exit N`) y la página cierra el *worker*. `browser_smoke.mjs` lo comprueba descartando el
  mensaje de salida de Emscripten: con el anuncio la página termina con el código 3; sin él (comprobado quitándolo) se
  queda en «Motor en marcha», como en el teléfono. `bridge_smoke.mjs` (`?exit=1`) comprueba además que la salida
  tras dibujar llega con su código.
- **El idioma de la consola** (`browser_smoke.mjs`, con una partición sintética que solo tiene carpetas vacías
  `UsEnglish` y `UsSpanish`): el elegido en la página llega al juego si el disco tiene su carpeta; si no, se usa el
  inglés y el registro lo dice.

## Qué no hay todavía

- **Sonido.** El audio avanza a su ritmo, pero no suena.
- **Partidas en otro dispositivo:** se guardan en el navegador (IndexedDB), no se exportan todavía.
- **Presentación directa de WebGPU en un canvas.** La imagen llega por una lectura del XFB y un canvas 2D (1,2 MB por
  fotograma, uno o dos fotogramas de retraso).
- **Rendimiento:** sin medir con el juego. Los dibujos esperan a su *pipeline*, así que la primera aparición de cada
  material detiene el fotograma un momento (`PUENTE_FIFO.md`, copia a pantalla).

## Primera prueba con el juego real (2026-10-29, Android)

Con la imagen RVZ del usuario, en Chrome para Android:

- El motor leyó la partición del `.rvz` (4.286.328.832 bytes, `main.dol` de 6.283.264 bytes) y Aurora arrancó sobre
  la GPU del teléfono.
- **El código del juego se ejecutó:** sus propios mensajes (`original arenaLo …`, `setupAramHeap …`, `D-Wait end`,
  `Table SetupFinish 0`) muestran que cargó sus datos y empezó a arrancar.
- Tras 3.579 bytes de flujo GX, Aurora se detuvo con `unsupported primitive type 240`: leyó 0xF0 como si empezara un
  dibujo, y 0xF0 no es una orden GX. Aurora trata como dibujo cualquier byte ≥ 0x80, así que su lectura del flujo se
  había desalineado antes de ese punto.
- La página siguió diciendo «Motor en marcha»: abortar solo el hilo de render no llegaba a la página. Corregido.

**Causa (encontrada con la traza GX que el usuario copió del registro):** `tools/gx_trace.py`, que lee el flujo con
los tamaños del hardware, independientemente de Aurora, se detuvo en el mismo byte (posición 1775). El flujo no
estaba desalineado: ese 0xF0 es un **marcador de host de Nebula** (`native/msl/include/nebula_gx_fifo.h`) de 12 bytes:
0xF0, el tipo (16, «clear quad»: el siguiente dibujo es el cuadrado que borra el EFB) y sus datos. No es una orden de
la consola. El renderizador de Nebula lo lee, y el juego escribe los de tipo 15 (bloom) y 16 sean cuales sean las
opciones del host. Aurora no lo conocía.

**Corrección:** parche 0014 de Aurora, que salta los marcadores de Nebula. Su autor lo permite: «else the quad draws
as written».

- `gx_replay.html` reproduce la traza del teléfono con el mismo puente y sin el juego. Sin el parche reproduce el
  mismo error, `unsupported primitive type 240`; con él, Aurora lee el flujo entero y dibuja el fotograma (1 de 1).
  La traza es del usuario y no se guarda en el repositorio.
- Prueba de regresión sin datos del juego: la escena de `bridge_smoke.mjs` escribe los mismos marcadores (bloom y
  clear quad) en cada fotograma. Sin el parche falla, como el teléfono; con él pasan todas las comprobaciones.

## Segunda prueba con el juego real (2026-10-29, Android)

Con el parche 0014, Aurora dibujó 6 fotogramas del juego sin errores. Después:

- El juego se detuvo con `OSPanic` en `FileRipper.cpp:112`, «File isn't exist.»: pidió un archivo que el disco no
  tiene.
- **Causa:** el host respondía 0 a todos los ajustes de la consola (`sc_get`). Para el idioma, 0 es japonés, y la
  tabla del juego (`Language.cpp`) lleva a la carpeta `JpJapanese`, que el disco norteamericano no trae (tiene
  `UsEnglish`, `UsFrench` y `UsSpanish`).
- **Corrección:** el host responde los ajustes de la consola como el host de Nebula (`native/host/host_main.cpp`):
  inglés, estéreo, barra de sensores arriba, etc. Hay una diferencia: la relación de aspecto es 4:3 y no 16:9, porque
  la página muestra el XFB de 640×480 tal cual, sin el estiramiento horizontal de un televisor panorámico. El idioma se
  elige en la página. El host solo lo acepta si la tabla de archivos del disco tiene la carpeta que el juego leerá; si
  no, usa el inglés (o el japonés) y lo dice en el registro (`[host] system language: …`).
- La página volvió a quedarse en «Motor en marcha» con el reloj del host informando, aunque el host había llamado a
  `exit(3)`. El `exit` de Emscripten no terminó el *runtime*, y sin él no llega `onExit`. La causa exacta (algo que
  mantiene vivo el *runtime*, o un destructor que espera) no se pudo reproducir sin el juego: con las particiones
  sintéticas y con la escena del puente, `exit` sí llega. **Corrección:** el host anuncia su salida en el canal del
  presentador y después usa `emscripten_force_exit`. La página termina con ese anuncio aunque falte el de Emscripten,
  y cierra el *worker*.
- La tarjeta «Pantalla» mostró 0 fotogramas: el juego todavía no había seleccionado un XFB con imagen para el VI. Hay
  que volver a mirarlo cuando el juego avance.

## Tercera prueba con el juego real (2026-10-29, Android)

- El idioma elegido llegó al juego (`[host] system language: Spanish`, `NebulaText: UsSpanish`). El juego cargó sus
  datos (893 lecturas del disco, 22,6 MB) y no se detuvo.
- **Aurora dibujó el juego de forma continua y la página lo mostró:** 4.538 fotogramas dibujados y 4.532 mostrados en
  unos 3 minutos y 20 segundos (unos 23 por segundo de media; al final, unos 25). En total, 145 MB de flujo GX, sin
  errores del renderizador.
- La pantalla mostraba el **menú HOME** de Nebula, con sus tres filas, sobre fondo negro.
- **Causa:** el host respondía 0 a todas las preguntas del menú HOME (`hbm`). Para `NEBULA_HBM_VIEW`, 0 significa «el
  menú está abierto, con la primera fila seleccionada», así que el juego lo dibujaba en cada fotograma. El host de
  Nebula sin renderizador responde -1 (cerrado). Otras respuestas por defecto también diferían de las suyas: la
  velocidad del texto, la escala de detalle (LOD), el subfotograma del DSP de audio y el alcance de la luz.
- **Corrección:** las respuestas por defecto (`web/host/host_services.c`) son ahora las del host de Nebula sin
  renderizador, salvo la relación de aspecto. `web/tests/host_services_test.c` (en `run_os_tests.sh`) comprueba las
  que cambian lo que hace el juego. Con los valores anteriores falla en 4 comprobaciones, entre ellas la del menú; con
  los nuevos, pasan todas.
- Todavía no hay mandos ni menú HOME propio: el juego no recibe entrada, así que se quedará en la primera pantalla que
  espere un botón.

## Cuarta prueba con el juego real (2026-10-29, Android)

- Sin el menú HOME, la pantalla quedó **negra**, aunque Aurora seguía dibujando y la página mostrando: 3.165
  fotogramas dibujados y 3.160 mostrados.
- Cada fotograma traía unos 5,5 KB de flujo GX, frente a unos 32 KB con el menú. El juego dibujaba poco.
- **Lo que faltaba frente al host de Nebula sin renderizador:**
  - **Mando:** Nebula conecta un Wiimote con Nunchuk en reposo cuando el juego arranca WPAD, y le da un informe
    cada 10 ms. El host web no conectaba ninguno.
  - **Audio:** Nebula tiene un reloj de la interfaz de audio (AI) que toma cada bloque al final de su duración y
    avisa al juego (`nebula_game_ai_interrupt`). El host web nunca avisaba.
  - **NAND:** Nebula responde con un sistema de archivos real. El host web fallaba en todo.
- **Corrección:** `web/host/console_services.c` hace las tres cosas como el host de Nebula: el Wiimote en reposo, el
  reloj del audio y una NAND en memoria, con `/tmp` y la carpeta de partidas del juego creadas. Lo comprueba
  `web/tests/console_services_test.c` (en `run_os_tests.sh`):
  - las operaciones de la NAND y sus códigos de resultado, incluidas la apertura segura, el renombrado y el borrado
    de carpetas;
  - unos 100 informes del mando por segundo, en el formato que pide el juego;
  - un aviso de audio cada 5 ms con bloques de 640 bytes a 32 kHz.
- El informe del host cuenta ahora las operaciones de NAND (y las fallidas), los informes del mando y los bloques de
  audio.
- No está comprobado que esto fuera lo que dejaba la pantalla negra. Era la diferencia que quedaba con el host de
  Nebula, que es el que su autor usa para probar el juego sin renderizador.

## Quinta prueba con el juego real (2026-10-29, Android)

- Con el mando, el reloj del audio y la NAND, **el juego llegó a la selección de partida** (`[host] stage:
  FileSelect`). El mando dio sus informes, el audio avanzó y la NAND respondió: una petición, que falló porque todavía
  no hay partida guardada.
- La pantalla mostró, en español y bien dibujado, el aviso del juego de que **se perdió la conexión con el Nunchuk**,
  que pide pulsar A.
- **Causa:** el orden de la conexión. El juego arranca WPAD (`KPADInit`) en el constructor de `WPadHolder` y registra
  sus funciones de conexión y de extensión al final de ese constructor. El host conectaba el mando 10 ms después de
  arrancar WPAD. En el teléfono, más lento, la conexión se procesó antes de que el juego registrara esas funciones, así
  que nunca supo que había un Nunchuk.
- **Corrección:** el host conecta el mando la primera vez que el juego lee sus mandos (`wpad_sample`, en cada
  fotograma), cuando `WPadHolder` ya está construido. En una consola el mando también se conecta mucho después de
  arrancar WPAD. `console_services_test.c` comprueba que el mando no se conecta al arrancar WPAD y sí en la primera
  lectura. Con el módulo anterior esa comprobación falla.
- **Otros cambios:**
  - durante una carga larga el mando y el audio se pararon 10 segundos; tras una espera así, el mando ya no envía de
    golpe los informes atrasados;
  - el registro solo anota el escenario cuando cambia (el juego lo repite en cada paso).

## Sexta prueba con el juego real (2026-10-29, Android)

- Con los controles, el usuario pasó la pantalla de título (A y B) y la selección de partida con el puntero, y creó
  su archivo. La NAND en memoria funcionó: 20 operaciones, 3 fallidas porque los archivos aún no existían.
- El juego cargó su primer nivel (`[host] stage: PeachCastleGardenGalaxy`) y la pantalla se quedó **negra**.
- Desde ese momento **todo lo del dibujo se paró a la vez**: ni flujo GX (813.307.906 bytes), ni marcadores PE, ni
  fotogramas de Aurora (4.604), ni lecturas del disco. El VI, el mando y el audio seguían. Los marcadores PE, que el
  juego manda en cada fotograma, también pararon: el bucle principal del juego estaba bloqueado, no solo sin dibujar.
- **Causa: sin determinar.** No hay datos para elegir entre las hipótesis:
  - el hilo de render se quedó en un paso (por ejemplo, Aurora rechaza empezar fotogramas si la GPU del teléfono
    pierde el dispositivo), y el juego espera a que le devuelva espacio del FIFO;
  - el juego espera otra cosa. El nivel empieza con una película THP, y el host todavía no las decodifica.
- **Diagnóstico añadido:** si el flujo GX se para 5 segundos mientras el reloj sigue, el host escribe en el registro
  una línea `[host] stall: …` (y la repite cada 30 s). Incluye:
  - si el juego sigue leyendo los mandos (es decir, si su bucle principal corre);
  - cuántos fotogramas de película pidió decodificar;
  - las lecturas de disco y de NAND, y los bloques de audio;
  - qué hace el hilo de render y desde cuándo, su cola y sus contadores.

  Además, la primera vez que el juego pide una película, el registro lo dice.
- **Comprobado:** `bridge_smoke.mjs` comprueba la descripción del hilo de render. **Sin comprobar:** el aviso de bloqueo
  completo. Necesita un bloqueo de verdad, y con las particiones de prueba el juego no llega tan lejos.

## Séptima prueba con el juego real (2026-10-29, Android): el informe de bloqueos

El informe de bloqueos apareció varias veces. Durante el arranque hubo paradas de 6, 6, 20 y 26 segundos, sin
dibujo y sin lecturas del mando, y el juego se recuperó cada vez. La última apareció justo al cargar el nivel:

- el hilo de render **no estaba atascado**: estaba esperando el flujo del juego, con la cola vacía y todos los
  marcadores PE respondidos;
- el bucle principal del juego estaba parado: ninguna lectura del mando;
- **no era la película**: el juego no pidió decodificar ningún fotograma.

**Hipótesis:** el juego no está bloqueado, sino cargando muy despacio. En este puerto los hilos del juego se turnan
sobre un único hilo de wasm, y las lecturas del disco se completan al instante, así que el hilo que carga puede
retener la CPU y el bucle principal no avanza mientras tanto. Si construir el nivel cuesta en el teléfono decenas de
veces más que en un PC, la parada puede durar minutos. El usuario esperó unos 40 s en la sexta prueba. **Sin
comprobar.**

**Diagnóstico añadido:** el informe de bloqueos incluye ahora la actividad del juego mientras dura la parada:

- lecturas del disco;
- órdenes al DSP de audio (el hilo de audio del juego);
- vaciados de caché para la GPU (al cargar recursos);
- objetos nombrados (al construir el nivel).

Si estos contadores suben, el juego está trabajando; si no, está bloqueado.

## Pruebas con el disco del usuario en el entorno de desarrollo (2026-10-29)

El usuario dio acceso a su copia (`.rvz`) para depurar. Estuvo solo en el espacio temporal de la sesión: nunca entró en
el repositorio ni en la página publicada.

La prueba usó la página real en el Chromium sin pantalla del contenedor, con WebGPU por software (sin GPU, unos 6
fotogramas por segundo), manejada con teclado y ratón como lo haría un jugador. El juego llegó al título, a la
selección de partida, creó el archivo (con icono), y al pulsar «Jugar con este archivo» cargó su primer nivel. Sin
gráficos, en Node, llega al título en menos de 10 s y va a 60 fotogramas por segundo.

**Fallo 1, corregido: listas de dibujo leídas tarde.**
- Al cargar el nivel, Aurora se detuvo con `draw vertex data overrun`. Había leído una lista de dibujo de 512 bytes
  llena de un patrón (`ff f5 00 00 00 00 00 00`) que no son órdenes GX.
- `GXCallDisplayList` deja en el flujo solo la dirección de la lista, y Aurora la lee de la memoria del juego cuando
  llega a ella, en su propio hilo. Con el renderizado por software iba segundos por detrás (cientos de elementos en
  cola), y el juego ya había reutilizado esa memoria al cargar el nivel.
- El renderizador de Nebula lee el flujo en cada marcador PE, en el hilo del juego, antes de que este siga.
- Hacer esperar al juego en cada marcador no sirve aquí: todos los hilos del juego comparten un único hilo de wasm, y
  la espera los paraba todos. Así se probó: el arranque se quedó en 17 lecturas de disco.
- **Corrección:** `web/platform/gx_display_list_inline_wasm.c`, enlazado con `--wrap=GXCallDisplayList`, copia las
  órdenes de la lista en el flujo en el momento de la llamada. Una lista son órdenes GX normales (sin llamadas ni
  marcadores de Nebula), así que ejecutarla en el flujo equivale a llamarla.
- **Comprobado:**
  - con el disco del usuario, el nivel cargó sin ese error;
  - la escena del puente sobrescribe su lista con ese mismo patrón justo después de llamarla. Sin la copia,
    `bridge_smoke.mjs` falla (el motor aborta); con ella pasa entera.

**Fallo 2, corregido: el hilo de render abortaba en el nivel.**
- La pila del aborto se pudo leer con el mapa de símbolos que cada compilación genera ahora (`--emit-symbol-map`; se
  traduce con `tools/wasm_symbols.py`). Terminaba en `push_storage` de Aurora: su buffer de *storage* por fotograma
  está mapeado con un tamaño fijo, y al crecer aborta.
- La causa estaba en el parche 0009. Cada `GXInvalidateVtxCache`, que el juego llama entre la mayoría de las formas,
  borraba la subida de cada array. Además, cada dibujo que llegaba más lejos en un array volvía a subirlo entero, y
  las formas de un modelo comparten sus arrays.
- **Corrección, parche 0015 de Aurora:**
  - una subida se reutiliza dentro de su fotograma, hasta que el juego escribe esa memoria para la GPU (sus
    vaciados de caché, que el puente pasa en el orden del flujo: `aurora_guest_written`);
  - si un array tiene que crecer, la nueva subida es al menos el doble de la anterior;
  - un buffer mapeado lleno descarta los datos con un error, en vez de abortar;
  - la lectura del EFB tolera ese caso;
  - el buffer de *storage* pasa de 8 a 16 MiB.
- **Comprobado:**
  - `bridge_smoke.mjs` (`?arrays=1`: 200 dibujos de un array de 96 KB por fotograma) aborta sin el parche y pasa
    con él;
  - con el disco, el pico de *storage* en el nivel bajó a unos 2,4 MB por fotograma (el informe del host lista los
    picos de cada buffer), sin ningún buffer lleno. El juego llegó al nivel y mostró la historia del prólogo, que
    espera a que se pulse A.
- Además, el juego ya no puede adelantarse al renderizador más de 6 marcadores PE (`web/platform/gx_pacing_wasm.c`).
  Espera dentro de su propio sistema operativo, así que sus otros hilos siguen.
- **La página detecta el aborto de un hilo del motor.** `abort()` se enlaza con `--wrap`
  (`web/platform/abort_report_wasm.c`) y avisa a la página desde el propio hilo, por el canal del presentador, con la
  pila de C. La página lo muestra como error y la pila queda en el registro. `bridge_smoke.mjs` (`?abort=1`) lo
  comprueba.

**Siguiente prueba con el disco:**
- Con la NAND restaurada, el juego entró directamente al nivel con el archivo ya creado.
- La historia del prólogo avanzó con A.
- La escena 3D posterior (con bandas de cine) se ve de un azul plano, sin errores de Aurora.
  - Causa: el «clear quad» del EFB (`MainLoopFramework::clearEfb`, `MR::clearZBuffer`). En la consola escribe la
    profundidad máxima desde una textura Z, con la comparación «siempre». Aurora no tiene texturas Z: el quad
    escribía su propia profundidad, cercana, y todo el 3D dibujado después fallaba la prueba de profundidad.
  - **Corrección, parche 0016 de Aurora:** con el marcador de tipo 16 de Nebula, el dibujo siguiente, si es el quad
    esperado (4 vértices, `GX_QUADS`), borra la profundidad de su *viewport* al valor del marcador y pinta solo su
    color. Es lo que el autor del marcador pide al renderizador.
  - **Comprobado:** `bridge_smoke.mjs` (`?clearquad=1`) reproduce el quad del juego. Sin el parche, el cuadrado
    dibujado después queda oculto; con él, se ve, y fuera del *viewport* del quad la profundidad se conserva.
- Con WebGPU por software, cada fotograma 3D tarda unos 5 s en el contenedor.

**El escenario, después del parche 0016:** ya se veían los objetos, pero no el escenario.
- Para encontrarlo, el parche 0017 añade diagnósticos que la página activa mientras el juego corre
  (`globalThis.smgRenderDebug[0]`): una máscara que quita una parte del *pipeline* (niebla, iluminación, todo salvo
  la textura o el color, la prueba de transparencia o la de profundidad), recuentos de los estados que usan las
  dibujadas, y el registro de un fotograma completo (bit 6: cada dibujada, copia y marcador, con sus texturas y si
  son copias del EFB), que llega a la página en un solo mensaje (`globalThis.smgFrameLog`).
- El registro mostró unas 36 000 dibujadas antes de que el *bloom* copie la escena, y esa copia, plana.
- Causa: los modelos del escenario cargan sus matrices por índice (`LOAD_INDX`) desde la memoria del juego. La CPU de
  la consola las escribía en *big-endian*; el juego nativo las escribe en el orden del host, mientras que los datos
  de los archivos siguen en *big-endian*. Aurora las leía en *big-endian*: matrices basura (en el teléfono, polígonos
  estirados de colores planos; en el contenedor, nada). El renderizador de Nebula las lee en el orden del host
  (`runtime/src/gx/fifo_parser.cpp`, `read_indexed_values`).
- **Corrección, parche 0018 de Aurora:** las cargas por índice desde la memoria del juego se leen en el orden del
  host.
- **Comprobado con el disco:** en el primer nivel se ven el escenario, Mario, el cielo y el HUD. `bridge_smoke.mjs`
  (50) y `browser_smoke.mjs` (31) pasan.
- **Sin prueba automática:** una prueba del puente con una matriz cargada por índice no llegó a ser válida (la base
  del *array* de matrices no llegaba a Aurora en el puente, sin causa encontrada) y se quitó.

## Vídeos THP (2026-11-02)

El reproductor del juego (`THPSimplePlayerWrapper`) da al host la parte de vídeo de cada fotograma: un JPEG baseline
4:2:0 sin relleno de bytes. El host del motor (`web/host/thp_video.cpp`) lee su tamaño del segmento SOF0, lo decodifica
con el decodificador de Nebula (`third_party/nebula/runtime/src/native_thp_video.cpp`, compilado sin cambios) en los
planos Y, U y V del juego (texturas I8 en mosaicos de 8×4) y avisa al puente de Aurora de que esos rangos cambiaron
(`smg_bridge_coherent`), porque el juego no vacía la caché sobre ellos. Nebula hace lo mismo en su host.

**Multiplicar y sumar exacto.** El decodificador de Nebula reproduce los píxeles de la consola con operaciones de
multiplicar y sumar con un solo redondeo. Su encabezado usa intrínsecos de x86 y CPUID, que no existen en
WebAssembly, y wasm no tiene esa instrucción (`std::fma` es una rutina por software, demasiado lenta). En el motor web,
`web/host/compat/galaxy/fused_multiply_add.h` ocupa su lugar: el producto de dos float es exacto en double, el error de
la suma se calcula con TwoSum y, si la suma no es exacta, se redondea a impar antes de pasar a float (Boldo y
Melquiond, 2008). El resultado es el de una FMA de verdad.

**Comprobado:**

- `web/tests/run_thp_video_test.sh`: la operación coincide con `fmaf` de la biblioteca de C en 3.000.007 casos, de
  ellos 1.000.000 construidos para caer justo en un punto medio entre dos float (donde una suma en double se equivoca).
  Con el redondeo a impar quitado a propósito, el test falla (500.415 casos distintos).
- El mismo test decodifica 300 fotogramas sintéticos: nativo con el encabezado de Nebula, nativo con el web y
  WebAssembly en Node dan la misma suma de comprobación de los planos.
- Con el disco del usuario, fuera del juego y sin publicar nada: cinco fotogramas de un vídeo del prólogo (640×368)
  decodificados por el camino del host comparados con el decodificador independiente de ffmpeg dan unos **50 dB de
  PSNR** en los tres planos (la mitad de los píxeles idénticos; el resto difiere por la IDCT). En WebAssembly salen
  idénticos al nativo y cuestan **unos 3 ms por fotograma** en el contenedor.
- El motor compila con el decodificador, y el informe del host añade una línea `[host] movies: …` (fotogramas
  decodificados, rechazados y tiempo medio).

**En el juego (prueba del usuario en su PC, 2026-11-02).** Las páginas del libro y el principio del jardín no piden
ningún fotograma THP: los vídeos del prólogo los lanza un objeto del nivel (`MovieStarter`) más adelante. La hipótesis
de la sexta prueba («el nivel empieza con una película THP») era falsa. Al llegar a ese punto, el registro dice
`movie frame 1 (THP, 640x368, …): decoded` y después `movies: 12667 frames decoded, 0 refused, 3.48 ms a frame`: los
dos vídeos del prólogo completos, sin un solo fotograma rechazado, y el juego siguió hasta `HeavensDoorGalaxy`.

Allí Aurora se detuvo con `unhandled tcg src 21`: una etapa TEV leía una coordenada de textura que ningún generador
crea. Lo arregla el parche de Aurora 0022 (`PUENTE_FIFO.md`).

## Cuelgues del hilo del juego: el registro de vigía (2026-11-02)

En la primera galaxia (`HeavensDoorGalaxy`) el juego se colgó en el PC del usuario sin ningún error: el registro
terminaba en un informe normal y la página seguía en «Motor en marcha». El motivo de ese silencio: Emscripten hace
pasar la salida de errores de cada hilo por el hilo principal del worker, que es el del juego, y **espera** a que la
escriba. Con el hilo del juego colgado, el hilo del reloj se quedaba parado en su siguiente informe y el aviso de
bloqueo (`[host] stall: …`) no llegaba nunca.

Ahora los informes del host (`host_log` en `web/host/headless_host.c`) van a un anillo de 32 KB en la memoria
compartida, que la página lee por su cuenta cada segundo (`web/shell/app.js`), sin pasar por el hilo del juego. Si el
resto del registro lleva más de 8 s en silencio mientras el anillo avanza, la página marca esas líneas con `[vigía]` y
dice en su estado que el hilo del juego no responde. Además, el aviso de bloqueo dice en qué servicio del host está el
hilo del juego y desde cuándo (envío al renderizador, lectura del EFB, disco, mezcla de audio, vídeos), o que no está
en ninguno. Sin la página (las compilaciones de Node), los informes siguen yendo a la salida de errores.

**Comprobado:** `browser_smoke.mjs` arranca la página con `?hangtest=1`, que hace que el host cuelgue a propósito el
hilo del juego dentro de un servicio. Los informes y el aviso de bloqueo llegan como `[vigía]`, el aviso nombra el
servicio y la página lo dice en su estado. Con la primera versión, que escribía también en la salida de errores, esa
comprobación fallaba.

La primera versión decidía que el juego estaba colgado cuando el registro llevaba 8 s en silencio; como los
informes ya no pasan por la salida de errores, eso daba falsas alarmas en partidas normales. Ahora la señal es el
**latido** del hilo del juego: un contador en el anillo que sube con cada servicio del host que llama (dibujo, disco,
audio…) y con cada fotograma de escena. En una partida real la página avisó solo cuando empezó el cuelgue, no durante
los minutos previos de carga, título, menús y prólogo.

## Gira de galaxias (2026-11-02)

`?tour=Galaxia:misión,Galaxia:misión,…&tourframes=N` hace que el host cambie de galaxia cada N fotogramas de escena
con el propio cambio de escenario del juego (`NEBULA_TEST_CHANGE_STAGE`), como la gira de prueba del host de Nebula
(`GALAXY_TEST_GALAXY_TOUR`). Sirve para encontrar, galaxia a galaxia, lo que el motor web no sabe dibujar o ejecutar,
sin jugar hasta cada una. La lista de galaxias se da en la URL; no está en el repositorio. Cada parada queda en el
registro (`[host] tour: stop i of N, …`).

Primera tanda, con el disco del usuario en el contenedor (sin GPU real, todo mucho más lento que en un PC):

| Galaxia | Resultado |
|---|---|
| `HeavensDoorGalaxy` | Carga y se juega. Una parada de 9 s al entrar (el hilo de render empezando un fotograma mientras la GPU por software compila *pipelines*), y sigue. |
| `AstroGalaxy` | Igual: paradas de 10 y 18 s al cargar, y sigue. |
| `EggStarGalaxy` | **Se cuelga siempre** (tres de tres, también yendo directamente). |

El cuelgue de `EggStarGalaxy`, según el aviso de bloqueo: el hilo de render lleva todo el tiempo en
`aurora::gx::fifo::drain()` (Aurora interpretando la GX del fotograma) sin avanzar; la cola hacia él se llena
(16.384 de 16.384) y el hilo de audio del juego (`JASAudioThread` → `JASDSPChannel::updateProc`) espera sitio dentro
de `gpu_coherent`. El perfilador del navegador confirma el hilo de audio, pero el hilo de render no le responde: está en
un bucle de WebAssembly que no devuelve el control. **Siguiente paso:** medir dentro de `drain()` qué orden GX lo
atrapa. De paso, el perfil muestra que el hilo de la NAND gasta el 100 % de su CPU esperando activamente.

## Rendimiento

**Medición:** el informe del host da, cada 10 s, el tiempo por fotograma del hilo de render en cada fase
(`[host] render thread:`): esperando al juego, empezando el fotograma, procesando el flujo GX y terminándolo (envío y
presentación). Con `-DSMG_PROFILING_FUNCS=ON` el wasm guarda los nombres de sus funciones para el perfilador del
navegador (no para el sitio publicado).

**Primer nivel, en el contenedor (WebGPU por software), antes:** unos 130 ms por fotograma esperando a la GPU al
empezar, 20 ms procesando el flujo y 29 ms terminando. El perfil del hilo de render puso el 12,7 % de su tiempo (unos
40 % de su trabajo) en `_emwgpuBufferUnmap`: Aurora mapeaba cada fotograma un búfer de 71 MB y, en el navegador,
`Unmap` copia todos los bytes mapeados aunque el nivel use unos 13 MB.

**Parche 0019 de Aurora:** los datos del fotograma van a memoria normal y solo los bytes usados de cada región van
al búfer con `Queue::WriteBuffer`. Cada hueco queda libre cuando la GPU termina su fotograma: el fotograma copia 4
bytes a una «valla» del hueco, que se mapea (la misma contrapresión de antes, sin la copia).
- Sin esa contrapresión la CPU se adelantaba sin límite a la GPU (fallos en `bridge_smoke.mjs`, `table index is out
  of bounds`); con `Queue::OnSubmittedWorkDone` el juego se paraba al arrancar. La valla mapeada no tiene ninguno de
  los dos problemas.
- **Comprobado con el disco:** el primer nivel se ve igual; terminar el fotograma pasó de unos 29 ms a unos 13 ms, y
  el hilo de render de un 30 % a un 20 % de ocupación. La espera a la GPU del contenedor no cambia (es su GPU por
  software). `bridge_smoke.mjs` y `browser_smoke.mjs` pasan.
- Lo siguiente en el perfil es el procesado del flujo GX (unos 22 ms por fotograma en el nivel).

**En el teléfono del usuario (primer nivel, con el parche 0019):** unos 27 fotogramas por segundo; por fotograma,
unos 20 ms procesando el flujo GX, 8 ms terminando, 8 ms esperando al juego y 0,1 ms esperando a la GPU. El límite es
la CPU del hilo de render, no la GPU.

**Parche 0020 de Aurora:** el flujo de un nivel trae unas 39 000 primitivas por fotograma (tiras cortas de las listas
de los modelos). Para cada una, Aurora volvía a calcular el tamaño de cada atributo indexado antes de leer sus índices;
ahora la disposición de los atributos indexados se calcula una vez por formato de vértice (cuando cambia).
- **Comprobado con el disco (contenedor):** procesar el flujo pasó de unos 22,5 ms a unos 13 ms por fotograma; el nivel
  se ve igual. `bridge_smoke.mjs` (50) y `browser_smoke.mjs` (31) pasan.

**Arranque lento en el teléfono:** más de un minuto hasta la pantalla de archivos, con pausas de 6 a 24 s.
- El informe mide ahora el tiempo de las lecturas del disco (`disc reads N (bytes, X ms)`) y, en cada pausa, la
  lectura en curso. En el teléfono, las 1166 lecturas del arranque y el primer nivel (30 MB) sumaban 85 s, unos 73 ms
  cada una; en el contenedor, 1,6 s. Cada acceso al archivo del usuario tiene allí un retraso fijo.
- **Corrección:** el lector RVZ lee la imagen en bloques de 1 MB y guarda los 16 últimos (`web/host/rvz_reader.c`).
  El tamaño sale de repetir esas 1184 lecturas con el lector fuera del navegador: 1 MB × 16 accede al archivo 57
  veces (57 MB); 4 MB × 4, 45 veces pero 179 MB (en el contenedor las lecturas subieron a 4,8 s); 256 KB × 16, 168
  veces.
- **Comprobado en el contenedor:** el juego llega a la pantalla de archivos en menos de 10 s y las lecturas suman
  494 ms (750 ms hasta cargar el nivel). `run_rvz_tests.sh` y `browser_smoke.mjs` pasan. **Sin comprobar todavía en
  el teléfono.**

**Partidas guardadas:** la página guarda la NAND en IndexedDB, por disco, y se la devuelve al motor al arrancar.
## Controles

El juego ve un Wiimote con Nunchuk. La página escribe el estado de los controles directamente en la memoria compartida
del motor (`web/shell/controls.js`, palabras `SMG_INPUT_*` de `web/host/console_services.h`). El hilo del Wiimote del
host construye con ese estado cada informe, cada 10 ms y en el formato que pide el juego.

- **Pantalla táctil:**
  - tocar la pantalla del juego apunta con el puntero, que se queda donde se tocó;
  - debajo están la palanca del Nunchuk, la cruceta, A, B, Z, C, «Agitar» (el giro), −, HOME, +, 1 y 2. Se pueden
    pulsar varios a la vez.
- **Teclado** (los valores por defecto de Nebula): espacio o Intro A, Mayús B, Z o Ctrl Z, C, Q o E agitar, WASD
  palanca, flechas cruceta, P o = más, M o − menos, H HOME, 1 y 2.
- **Ratón:** sobre la pantalla mueve el puntero; el botón izquierdo es A y el derecho B.
- **Puntero:** el informe lleva dos puntos IR colocados como en Nebula (`runtime/src/native_input.cpp`), para que el
  KPAD del propio juego los resuelva en esa posición. También pasa al juego la posición exacta.

**Comprobado:**
- `console_services_test.c` comprueba los bytes de los informes: botones, palanca, C y Z, y los puntos IR del puntero
  en el centro y en una esquina, en los formatos 0x31, 0x33 y 0x37.
- `browser_smoke.mjs` comprueba con el motor real que:
  - la página recibe las palabras del host (marcadas por él con «SMGI»);
  - el teclado, el botón B táctil y el ratón escriben lo que deben.

**Sin comprobar todavía:** que el juego coloque el puntero exactamente donde se toca. Depende del KPAD del juego, y
hay que verlo con el disco.

## Diagnóstico de fallos del renderizador

- Si Aurora se detiene por un error, la página lo muestra como estado de error y detiene el motor. El registro incluye
  la **traza GX**: todo lo que el juego dio a Aurora, en base64, entre `[gx-trace] begin` y `[gx-trace] end` (hasta
  256 KB). «Copiar el registro» la copia junto con el resto.
- `gx_replay.html` (en `bridge/` del sitio, o `node web/tests/replay_trace.mjs build/site/bridge <registro>`) da la
  traza a Aurora con el mismo puente, sin el juego y con su memoria a ceros. Así se reproduce el fallo aquí.
- `python3 -I tools/gx_trace.py <registro copiado>` recorre la traza orden a orden con los tamaños del hardware y el
  formato de vértice que el propio flujo establece. Es independiente de Aurora y se detiene en el primer byte que no
  es una orden, mostrando las órdenes anteriores.
- Verificado con un fallo inyectado (`gx_bridge.html?fault=1`, en `bridge_smoke.mjs`): la página recibe el fallo, el
  registro contiene la traza y `gx_trace.py` llega al byte inyectado.

## Qué puede pasar y qué contar

Es la primera vez que el juego real pasará por esta cadena. Lo esperable es encontrar fallos nuevos. Para cada
prueba, lo útil es:

- el registro del motor completo (se puede copiar de la página);
- el estado final: código de salida, o si sigue en marcha;
- cuántos fotogramas muestra la tarjeta «Pantalla» y qué se ve (una captura de pantalla);
- los errores de la consola del navegador, sobre todo los de WebGPU y los mensajes `[aurora …]`;
- navegador, versión, sistema operativo y GPU.

Algunos casos y qué indicarían:

- **El registro termina en un error `[host] the game stopped: …`:** el módulo de juego se detuvo por su cuenta; el
  texto dice dónde.
- **«Aurora did not start on this browser's WebGPU»:** el navegador no dio un adaptador WebGPU.
- **Errores `[aurora ERROR …]` con direcciones (`… is not guest memory`, `Texture map …`):** recursos que el juego usa
  de una forma que el puente no cubre todavía.
- **La pantalla queda negra con fotogramas contando:** el XFB llega, pero vacío o en negro (`VISetBlack`).
