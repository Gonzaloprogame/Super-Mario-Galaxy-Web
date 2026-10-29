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
  (`web/tests/bridge_smoke.mjs`, 43 comprobaciones). Cubre vértices directos e indexados, listas de visualización,
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
- **NAND persistente:** la NAND está en memoria. Lo que el juego guarda se pierde al cerrar o recargar la página.
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

**Fallo 2, sin resolver: el hilo de render aborta en el nivel.**
- Ya en `PeachCastleGardenGalaxy`, el hilo de render abortó sin ningún mensaje de Aurora, mientras terminaba un
  fotograma: la consola mostró `RuntimeError: Aborted()`, y el informe de bloqueos, «ending a frame» durante minutos.
- Desde entonces el juego espera al renderizador: no dibuja, pero el reloj, el mando y el audio siguen. Es el mismo
  cuadro que en el teléfono.
- La página no se entera y sigue en «Motor en marcha». Tiene que detectarlo.
- La causa del aborto está por averiguar. Un enlace con `-sASSERTIONS` para verla no llegó a arrancar en este montaje
  (un módulo en un worker con hilos), así que habrá que buscarla por otro camino.

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
