# Hoja de ruta

Un hito solo se marca como completado cuando sus criterios de aceptación se han comprobado. Detalle de las pruebas
en `COMPILACION_WASM.md`.

| Hito | Criterio de aceptación | Estado |
|---|---|---|
| 1. Diagnóstico | Informe con comparación, estrategia y riesgos basado en el código | **Completado** (`DIAGNOSTICO.md`) |
| 2. Entorno reproducible | Versiones fijadas, fuentes a revisiones fijas, compilación por script | **Completado** (`ENTORNO.md`, `tools/`) |
| 3. Prueba de compilación | El módulo de juego compila y enlaza a wasm; errores reales registrados | **Completado**: 1.898/1.898 unidades, enlace sin símbolos sin resolver |
| 4. Primera ejecución web | El código compilado se carga en un navegador y la inicialización funciona | **Parcial**: núcleo OS verificado (tests de Nebula en wasm) y módulo cargado en Chromium hasta la carga de datos de `main.dol`; el arranque completo necesita un `game.pak` real |
| 5. Backend gráfico | Una escena 3D real del juego dibujada desde su FIFO GX | Pendiente |
| 6. Integración | Recursos, niveles, física, animación, cámara, audio y controles | Pendiente |
| 7. Jugabilidad | Movimiento, saltos, gravedad, interacción, cámara | Pendiente |
| 8. Optimización y publicación | Rendimiento, memoria, estabilidad, tiempos de carga, despliegue | Pendiente |

## Próximos pasos, por prioridad

1. **Ejecutar el arranque con un `game.pak` real** (necesita que el usuario lo aporte en su equipo): `smg_headless`
   en Node debe llegar a la escena del logotipo, con el retrace a 60 Hz y el flujo GX contado. Es la verificación que
   desbloquea todo lo demás y mide el coste real de Asyncify.
2. **División con semántica de PowerPC**: pase post-enlace sobre el `.wasm` que sustituya `i32.div_s/div_u/rem_s/rem_u`
   por funciones auxiliares (Nebula documenta al menos un caso real: `FlipPanel::endClipped`).
3. **Backend WebGPU**: compilar Aurora con Emscripten + `emdawnwebgpu`, conectarlo a `gx_submit` y a las regiones de
   memoria del módulo; primer objetivo: la pantalla del logotipo.
4. **Audio**: mezclador DSP de JAudio de Nebula a wasm + AudioWorklet.
5. **Entrada**: informes de Wii Remote desde teclado, ratón y Gamepad API.
6. **NAND en OPFS**, `game.pak` persistente en OPFS.
7. **Memoria**: reducir la arena (con punteros de 4 bytes sobran los márgenes de 64 bits) y medir el consumo real.
8. **Rendimiento**: reloj de `OSGetTime` sin llamar a JS; evaluar listas de Asyncify o el modelo de un pthread por
   hilo del juego.
