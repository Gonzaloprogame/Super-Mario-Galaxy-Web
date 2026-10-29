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
2. Elige el modo **Con gráficos (Aurora, WebGPU)**. «Segundos» en 0 ejecuta sin límite.
3. Pulsa **Arrancar el motor**.

La tarjeta «Pantalla» solo muestra fotogramas que el motor ha enviado: el XFB que el VI del juego tiene seleccionado.
Debajo se indica cuántos van. Si el motor se detiene, el estado muestra su código de salida real y el registro, el
motivo.

## Qué está verificado (sin el juego)

- **El puente gráfico, con una escena de prueba dibujada por la librería GX del propio módulo de juego**
  (`web/tests/bridge_smoke.mjs`, 38 comprobaciones). Cubre vértices directos e indexados, listas de visualización,
  texturas RGB565 y C8 con TLUT, copias del EFB a textura, la copia a pantalla con su borrado, y las lecturas del EFB
  dentro y fuera de una *callback* de token. La comprobación final lee el canvas de la página. Detalle en
  [`PUENTE_FIFO.md`](PUENTE_FIFO.md).
- **La página del motor en los dos modos** (`web/tests/browser_smoke.mjs`), con una partición sintética que no
  contiene datos del juego. Con gráficos, Aurora arranca sobre WebGPU en el *worker* del motor, el módulo de juego
  llega a su cargador de datos de `main.dol`, se detiene porque la partición está vacía, y la página muestra el código
  de salida real (3).
- **El cierre ordenado:** antes de terminar el proceso se detiene el hilo de render y se apaga Aurora. Sin esto,
  `exit()` se quedaba esperando para siempre y la página nunca se enteraba de que el motor había parado.

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

## Diagnóstico de fallos del renderizador

- Si Aurora se detiene por un error, la página lo muestra como estado de error y detiene el motor. El registro incluye
  la **traza GX**: todo lo que el juego dio a Aurora, en base64, entre `[gx-trace] begin` y `[gx-trace] end` (hasta
  256 KB). «Copiar el registro» la copia junto con el resto.
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
