# Probar el motor con tu copia del juego

Estado verificado el 2026-10-28. El motor con renderizador (`smg_web`) reúne el módulo de juego, el host que lee tu
`game.pak` y Aurora sobre el WebGPU del navegador. **No se ha ejecutado nunca con el juego real**: en este entorno no
hay copia del juego y no puede haberla. Lo que sí está verificado está más abajo, y lo que falta comprobar es
precisamente lo que tu prueba puede responder.

## Qué necesitas

- Tu `game.pak` de Super Mario Galaxy (RMGE01), obtenido de tu propio disco como explica
  [`ARCHIVOS_DEL_JUEGO.md`](ARCHIVOS_DEL_JUEGO.md). El repositorio no contiene ni descarga ningún archivo del juego.
- Un navegador de escritorio con WebGPU: Chrome o Edge recientes en un equipo con GPU. Firefox y Safari ofrecen WebGPU
  según versión y sistema; no están probados.
- El sitio servido con aislamiento de origen cruzado (COOP/COEP), que ya da `tools/serve.py`.

## Cómo arrancarlo

```sh
source <emsdk>/emsdk_env.sh
tools/fetch_sources.sh
tools/build_web.sh
python3 tools/serve.py 8080 build/site
```

Abre `http://127.0.0.1:8080` y:

1. Selecciona tu `game.pak`. No sale del navegador: el motor lo lee por trozos dentro de un *worker*.
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
