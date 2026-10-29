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

- **Sonido y mandos.** El juego no recibe entrada, así que no pasará de las pantallas que no la piden.
- **NAND:** las partidas guardadas fallan como si no hubiera sistema de archivos.
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
