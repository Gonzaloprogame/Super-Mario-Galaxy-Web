# Hoja de ruta

Un hito solo se marca como completado cuando sus criterios de aceptación se han comprobado. Detalle de las pruebas
en `COMPILACION_WASM.md`.

| Hito | Criterio de aceptación | Estado |
|---|---|---|
| 1. Diagnóstico | Informe con comparación, estrategia y riesgos basado en el código | **Completado** (`DIAGNOSTICO.md`) |
| 2. Entorno reproducible | Versiones fijadas, fuentes a revisiones fijas, compilación por script | **Completado** (`ENTORNO.md`, `tools/`) |
| 3. Compilación a WebAssembly | El módulo de juego compila y enlaza a wasm; errores reales registrados | **Completado**: 1.898/1.898 unidades, enlace sin símbolos sin resolver |
| 4. Arranque con el disco real | El juego arranca en el navegador desde la imagen RVZ del usuario | **Completado**: título, elección de archivo y partidas guardadas, comprobado con el disco en escritorio y Android (`MOTOR_WEB.md`) |
| 5. Gráficos con WebGPU | Las escenas del juego se dibujan como en la consola | **Parcial**: el primer nivel se ve completo (parches de Aurora 0009 a 0026, `PUENTE_FIFO.md`); los vídeos THP del prólogo se decodifican en el juego (12.667 fotogramas, 0 rechazados); falta probar más niveles |
| 6. Sonido y controles | Música, efectos y controles del Wiimote con Nunchuk | **Parcial**: controles táctiles, teclado y ratón comprobados en Android; la mezcla de sonido funciona (`MOTOR_WEB.md`), falta oírla en un dispositivo; sin mandos físicos |
| 7. Jugable de principio a fin | Movimiento, cámara, niveles y progreso hasta el final del juego (desglose abajo) | **En curso**: ver «Hito 7, por partes» |
| 8. Rendimiento y publicación | Rendimiento, memoria, estabilidad, tiempos de carga, despliegue | **En curso**: unos 27 fps en Android en el primer nivel; versiones 0.1.0 y 0.1.1 publicadas en GitHub Pages |

El panel del directo (`web/shell/overlay.html`) muestra este estado desde `docs/progreso.json`, con porcentajes
estimados por hito.

## Hito 7, por partes

El hito 7 se da por completado solo cuando lo estén las cuatro partes. Su porcentaje en `docs/progreso.json` sale
de las partes comprobadas: cada parte vale un 25 %, y una a medias, la mitad (hoy A 25 + B 25 + C 12 + D 0 = 62 %).

| Parte | Criterio | Cómo se comprueba | Estado |
|---|---|---|---|
| A. Todos los escenarios | Todas las misiones a las que se puede entrar cargan, dibujan y aguantan jugadas a ciegas | `tools/galaxy_tour.mjs` (con gráficos y `--play`), `tools/routes/misiones.txt` | **Hecho**: la misión 1 de los 44 escenarios (con gráficos) y las 61 misiones 2-5 que existen (modo diagnóstico, a ciegas), sin errores |
| B. Progreso de la historia | Estrellas, Observatorio con 1-6 Grandes Estrellas, jefes y final, sin cuelgues, con la lógica del propio juego | `tools/story_route.mjs` con `tools/routes/historia.txt` (ganchos de prueba de Nebula) | **Hecho** para la ruta actual: 12 de 12 segmentos (`PROGRESO_AUTONOMO.md`, bloque 19); faltan los jefes intermedios |
| C. El final | Tras la última Gran Estrella: los vídeos del final, los créditos y la vuelta a la partida | La ruta, segmento final con gráficos | **En curso**: vídeos del final (10 115 fotogramas, 0 rechazados) y paso al epílogo comprobados; el epílogo aguanta 2 h sin errores; falta ver la vuelta a la partida |
| D. Partida real | Una persona juega y marca la lista | `docs/PRUEBA_DE_PARTIDA.md` | **Pendiente** del usuario |

## Próximos pasos, por prioridad

1. **Ejecutar el arranque con un `game.pak` real** (necesita que el usuario lo aporte en su equipo): `smg_headless`
   en Node debe llegar a la escena del logotipo, con el retrace a 60 Hz y el flujo GX contado. Es la verificación que
   desbloquea todo lo demás y mide el coste real de Asyncify.
2. ~~División con semántica de PowerPC~~: hecho (`tools/wasm_ppc_divide.py`, integrado en la compilación).
3. **Probar el motor con gráficos con el juego real** (`MOTOR_WEB.md`): la cadena está montada y verificada con
   escenas de prueba y una partición sintética; falta el resultado con el `game.pak` del usuario.
4. **Backend WebGPU**: ~~compilar Aurora~~ y ~~conectar `gx_submit`~~ (hecho: `BACKEND_WEBGPU.md`,
   `PUENTE_FIFO.md`). Fase 2 del puente: ~~arrays de vértices, `CALL_DL`, texturas, TLUT, copias del EFB y copia a pantalla~~
   lecturas del EFB del juego y presentación del XFB en la página (hecho), el `OffscreenCanvas` del hilo de render y el cierre ordenado; primer objetivo con datos
   reales: la pantalla del logotipo.
5. **Audio**: mezclador DSP de JAudio de Nebula a wasm + AudioWorklet.
6. **Entrada**: informes de Wii Remote desde teclado, ratón y Gamepad API.
7. **NAND en OPFS**, `game.pak` persistente en OPFS.
8. **Memoria**: reducir la arena (con punteros de 4 bytes sobran los márgenes de 64 bits) y medir el consumo real.
9. **Rendimiento**: reloj de `OSGetTime` sin llamar a JS; evaluar listas de Asyncify o el modelo de un pthread por
   hilo del juego.
