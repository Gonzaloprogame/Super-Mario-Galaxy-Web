# Hoja de ruta

Un hito solo se marca como completado cuando sus criterios de aceptación se han comprobado. Detalle de las pruebas
en `COMPILACION_WASM.md`.

| Hito | Criterio de aceptación | Estado |
|---|---|---|
| 1. Diagnóstico | Informe con comparación, estrategia y riesgos basado en el código | **Completado** (`DIAGNOSTICO.md`) |
| 2. Entorno reproducible | Versiones fijadas, fuentes a revisiones fijas, compilación por script | **Completado** (`ENTORNO.md`, `tools/`) |
| 3. Prueba de compilación | El módulo de juego compila y enlaza a wasm; errores reales registrados | **Completado**: 1.898/1.898 unidades, enlace sin símbolos sin resolver |
| 4. Primera ejecución web | El código compilado se carga en un navegador y la inicialización funciona | **Parcial**: núcleo OS verificado (tests de Nebula en wasm) y módulo cargado en Chromium hasta la carga de datos de `main.dol`; el arranque completo necesita un `game.pak` real |
| 5. Backend gráfico | Una escena 3D real del juego dibujada desde su FIFO GX | **En curso**: Aurora GX compila entera para el navegador (70 unidades, 6 parches pequeños) y dibuja una escena GX de prueba en el WebGPU de Chromium, verificada leyendo el EFB (`BACKEND_WEBGPU.md`). Conectado el FIFO del juego: la GX del propio juego dibuja una escena de prueba que Aurora renderiza y se verifica leyendo el EFB (`PUENTE_FIFO.md`). Fase 2 en curso: arrays de vértices indexados, listas de visualización, texturas, TLUT, copias del EFB a textura y copia a pantalla (`GXCopyDisp`, con su borrado) por dirección física, verificados (parches 0009 a 0012); faltan presentar el XFB y las lecturas del EFB del juego |
| 6. Integración | Recursos, niveles, física, animación, cámara, audio y controles | Pendiente |
| 7. Jugabilidad | Movimiento, saltos, gravedad, interacción, cámara | Pendiente |
| 8. Optimización y publicación | Rendimiento, memoria, estabilidad, tiempos de carga, despliegue | Pendiente |

## Próximos pasos, por prioridad

1. **Ejecutar el arranque con un `game.pak` real** (necesita que el usuario lo aporte en su equipo): `smg_headless`
   en Node debe llegar a la escena del logotipo, con el retrace a 60 Hz y el flujo GX contado. Es la verificación que
   desbloquea todo lo demás y mide el coste real de Asyncify.
2. ~~División con semántica de PowerPC~~: hecho (`tools/wasm_ppc_divide.py`, integrado en la compilación).
3. **Backend WebGPU**: ~~compilar Aurora~~ y ~~conectar `gx_submit`~~ (hecho: `BACKEND_WEBGPU.md`,
   `PUENTE_FIFO.md`). Fase 2 del puente: ~~arrays de vértices, `CALL_DL`, texturas, TLUT, copias del EFB y copia a pantalla~~
   (hecho); faltan presentar el XFB que elige el VI y las lecturas del EFB del juego, el `OffscreenCanvas` del hilo de render y el cierre ordenado; primer objetivo con datos
   reales: la pantalla del logotipo.
4. **Audio**: mezclador DSP de JAudio de Nebula a wasm + AudioWorklet.
5. **Entrada**: informes de Wii Remote desde teclado, ratón y Gamepad API.
6. **NAND en OPFS**, `game.pak` persistente en OPFS.
7. **Memoria**: reducir la arena (con punteros de 4 bytes sobran los márgenes de 64 bits) y medir el consumo real.
8. **Rendimiento**: reloj de `OSGetTime` sin llamar a JS; evaluar listas de Asyncify o el modelo de un pthread por
   hilo del juego.
