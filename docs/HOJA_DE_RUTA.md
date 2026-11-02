# Hoja de ruta

Un hito solo se marca como completado cuando sus criterios de aceptación se han comprobado. Detalle de las pruebas
en `COMPILACION_WASM.md`.

| Hito | Criterio de aceptación | Estado |
|---|---|---|
| 1. Diagnóstico | Informe con comparación, estrategia y riesgos basado en el código | **Completado** (`DIAGNOSTICO.md`) |
| 2. Entorno reproducible | Versiones fijadas, fuentes a revisiones fijas, compilación por script | **Completado** (`ENTORNO.md`, `tools/`) |
| 3. Compilación a WebAssembly | El módulo de juego compila y enlaza a wasm; errores reales registrados | **Completado**: 1.898/1.898 unidades, enlace sin símbolos sin resolver |
| 4. Arranque con el disco real | El juego arranca en el navegador desde la imagen RVZ del usuario | **Completado**: título, elección de archivo y partidas guardadas, comprobado con el disco en escritorio y Android (`MOTOR_WEB.md`) |
| 5. Gráficos con WebGPU | Las escenas del juego se dibujan como en la consola | **Parcial**: el primer nivel se ve completo (parches de Aurora 0009 a 0022, `PUENTE_FIFO.md`); los vídeos THP del prólogo se decodifican en el juego (12.667 fotogramas, 0 rechazados); falta probar más niveles |
| 6. Sonido y controles | Música, efectos y controles del Wiimote con Nunchuk | **Parcial**: controles táctiles, teclado y ratón comprobados en Android; la mezcla de sonido funciona (`MOTOR_WEB.md`), falta oírla en un dispositivo; sin mandos físicos |
| 7. Jugable de principio a fin | Movimiento, cámara, niveles y progreso hasta el final del juego | **En curso**: se juega el primer nivel; el resto, sin probar |
| 8. Rendimiento y publicación | Rendimiento, memoria, estabilidad, tiempos de carga, despliegue | **En curso**: unos 27 fps en Android en el primer nivel; versiones 0.1.0 y 0.1.1 publicadas en GitHub Pages |

El panel del directo (`web/shell/overlay.html`) muestra este estado desde `docs/progreso.json`, con porcentajes
estimados por hito.

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
