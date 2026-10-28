# Puente FIFO: la GX del juego dibujada por Aurora

Resultados medidos el 2026-10-28 (Emscripten 6.0.13, Chromium 153 con WebGPU por software).

## Qué conecta

El módulo de juego escribe el flujo de comandos FIFO de la consola en el anillo de Nebula (`native/sdk/gx/gx_fifo.c`)
y entrega al host cada tramo al vaciarlo (`gx_submit`), con los marcadores de sincronización y de fin de dibujo en su
posición (`gx_pe_marker`). El puente (`web/host/aurora_bridge.cpp`) lleva ese flujo a Aurora, que lo interpreta y lo
dibuja en el WebGPU del navegador:

```
 hilo de simulación (juego)                           hilo de render (Aurora, dueño del dispositivo WebGPU)
 SDK GX del juego → anillo de Nebula → gx_submit ──▶ cola fija ──▶ fifo::write_data → nebula_game_gx_release
                                      gx_pe_marker ─▶            ─▶ fin de dibujo: aurora_end_frame → nebula_game_gx_event
```

Como en Nebula, el renderizador es otro hilo; aquí, además, porque los objetos WebGPU pertenecen al hilo que los crea.
La cola entre ambos tiene tamaño fijo y el lado del juego no asigna memoria (ver "Un solo binario").

## Un solo binario con el juego y Aurora

Nebula mantiene el juego en su propio módulo nativo. En wasm hay un único enlace, y se midieron dos conflictos:

1. **156 símbolos C definidos dos veces** (`llvm-nm` sobre las bibliotecas): la API GX/VI de Aurora (`lib/dolphin`) y
   el SDK de Revolution del juego implementan las mismas funciones. Excluir los ficheros de API de Aurora no sirve:
   comparten fichero con internos que el renderizador necesita (`aurora::gx::copy_tex`, `__gx`,
   `aurora::vi::configured_fb_size`; errores de enlace reales). Solución: en todas las unidades de Aurora esos 156
   nombres se renombran a `aurora_<nombre>` con una cabecera incluida por la fuerza
   (`web/aurora/include/aurora_api_renames.h`, generada por `tools/gen_aurora_renames.py`). El binario tiene así una
   sola API GX, la del juego, y Aurora conserva la suya completa con otro nombre.
2. **`operator new`/`operator delete` globales**: el juego los define (`JKRHeap.cpp`: asignan en el heap JKR actual,
   dentro de la arena). En un enlace estático sustituirían a los de libc++ para todo el binario, y Aurora asignaría en
   los heaps del juego. El enlace dinámico de Emscripten no lo resuelve: es experimental con pthreads (aviso del
   compilador), una prueba mínima falló en ejecución (`RuntimeError: unreachable`), y `operator new` se exporta aunque
   la visibilidad sea oculta. Solución: `-Wl,--wrap` de las cuatro formas que define el juego y
   `web/platform/operator_new_wasm.cpp`:
   - `new` usa JKRHeap solo en el hilo de simulación del juego una vez arrancado (registrado en el primer cambio de
     fiber); en cualquier otro caso, `malloc`;
   - `delete` decide por la dirección: la memoria JKR está siempre dentro de la arena y la de `malloc` nunca.
   Verificado con una prueba mínima: un `std::vector` del host acaba en `malloc` y un objeto del "juego" en su arena.
   Regla que impone: las llamadas del juego al host en su hilo (`gx_submit`, etc.) no usan `new` (el host es C y la
   cola del puente es fija).

`web/CMakeLists.txt` compila todo junto: módulo de juego, Aurora (con `-pthread` en todas sus unidades y
dependencias, necesario para enlazarlas en un binario con memoria compartida) y el puente.

## Adaptaciones de Aurora para el flujo de hardware

La API de Aurora escribe parte del estado con comandos propios; el SDK del juego escribe los registros de hardware tal
cual. Dos diferencias aparecieron al dibujar con la GX del juego:

| Parche | Error real |
|---|---|
| `0007-clamp-viewport-depth` | El SDK escribe el *viewport* XF como escala y desplazamiento en unidades de 24 bits; reconstruido, el rango de profundidad quedaba un redondeo por debajo de 0 y WebGPU rechazaba `SetViewport(…, -0.000000, 1)`. Se acota a [0, 1], como hace el hardware. |
| `0008-hardware-scissor-viewport-offsets` | El hardware **resta** el desplazamiento de caja del *scissor* (el SDK escribe el *scissor* en +342 y el desplazamiento como (x + 342)/2); Aurora lo sumaba, con su API escribiéndolo negado. Con el flujo del juego el *scissor* quedaba en x = 684, fuera del EFB, y no se dibujaba nada. El centro del *viewport* es +342 en el SDK y Aurora usaba 340 (2 píxeles de desfase). Aurora pasa a la convención del hardware en los dos extremos: su API y su reconstrucción. |

## Prueba: `web/tests/bridge_smoke.mjs`

`web/bridge/gx_bridge_scene.c` se compila con las opciones del módulo de juego y dibuja con su GX (Petari) la misma
escena que la demo de Aurora: un triángulo con color de vértice sobre el color de borrado. No se ejecuta código ni
datos del juego: `web/bridge/bridge_main.c` arranca el núcleo OS del módulo (`nebula_os_boot`) con esa escena como
"main" del juego. `GXInit`, la configuración y el dibujo son los del SDK; `GXDrawDone` espera al fin de dibujo que
devuelve el puente.

| Punto del EFB | Esperado | Leído |
|---|---|---|
| Esquina (16, 16) | (24, 32, 96) | `0x00182060` |
| Centro del triángulo (320, 272) | (240, 176, 16) | `0x00F0B010` |
| Fondo (96, 408) | (24, 32, 96) | `0x00182060` |

El alfa es 0 porque el `GXInit` del SDK deja el EFB en un formato sin alfa, como en la consola. La demo de la API de
Aurora (`web/tests/aurora_smoke.mjs`) sigue pasando con el parche 0008.

Error encontrado al escribir la prueba: `exit()` desde el juego ejecuta los destructores estáticos de C++ en el hilo
de la página mientras el hilo de render sigue activo, y uno de ellos espera indefinidamente (pila obtenida pausando el
hilo con el depurador de Chrome: `exitOnMainThread` → `exitRuntime` → `__funcs_on_exit`). La prueba publica el
resultado en la página sin terminar el proceso. **Pendiente:** un cierre ordenado del motor (detener el hilo de render
antes de los destructores).

## Lo que falta: recursos por dirección física (fase 2)

El SDK del juego referencia memoria con direcciones físicas de 32 bits, que Aurora recibe por sus propios comandos
con punteros del host. Aurora rechaza o ignora hoy esas formas de hardware (se ve en el registro de la prueba:
`CP_REG_ARRAYBASE_ID is not supported`, escritas por `GXInit`):

| Forma de hardware | En Aurora | Trabajo |
|---|---|---|
| Base de *array* de vértices (CP 0xA0–0xAB) | `GX_AURORA_LOAD_ARRAYBASE` (puntero, tamaño) | Resolver la dirección con `nebula_game_memory_regions`; el tamaño no está en los registros: acotarlo con el índice máximo de cada dibujo |
| Llamada a lista de visualización (`CALL_DL`) | Se ignora ("nested") | Resolver la dirección y procesar la lista en línea |
| Imagen de textura (BP `image3`) y TLUT (BP 0x64/0x65) | `GX_AURORA_LOAD_TEXOBJ` / `LOAD_TLUT` | Construir la textura desde los registros y la memoria de la arena, con caché por dirección e invalidación por `gpu_coherent` |
| Copia del EFB a memoria (BP 0x49–0x4B, 0x52) | `GX_AURORA_LOAD_COPY_*` | Registrar la copia por dirección de destino, para que una textura en esa dirección sea la copia |
| Lecturas del EFB (`gx_peek_efb`, token) | API de lectura de Aurora (`AuroraReadback*`) | Conectar los *peeks* de Nebula (destello de la lente, puntero) |

Además: el `OffscreenCanvas` del hilo de render para mostrar la imagen en la página (hoy se dibuja fuera de pantalla), y
el ritmo de fotogramas por el retrace (hoy el fotograma termina en cada fin de dibujo).
