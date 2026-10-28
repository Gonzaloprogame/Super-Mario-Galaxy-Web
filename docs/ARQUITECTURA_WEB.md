# Arquitectura del port web

El principio: **el juego no cambia**. El módulo de juego de Nebula (Petari más su SDK nativo) se compila a wasm32 tal
cual; lo que cambia es la plataforma debajo (`web/platform`) y el host que le ofrece los servicios de la consola
(`nebula_game_api.h`). Cada pieza marcada como *hecho* está verificada en `COMPILACION_WASM.md`.

```
 Página (hilo principal)                         Worker del motor (smg_*_web.js)
 ┌──────────────────────────┐   postMessage     ┌──────────────────────────────────────────────────┐
 │ UI: carga, estado, errores│ ───────────────▶ │ Host web (C)  ── nebula_game_api.h ──▶ Módulo de  │
 │ Teclado / Gamepad API     │ ◀─────────────── │  disc_read, fs_request, vi_retrace,      juego    │
 │ <canvas> → OffscreenCanvas│                  │  wpad_report, gx_submit, audio_dsp     (Petari +  │
 └──────────────────────────┘                   │                                         SDK de    │
        ▲                                        │ Plataforma web: fibers (Asyncify),     Nebula)   │
        │ AudioWorklet (SharedArrayBuffer)       │  arena en memoria lineal, anillo GX              │
        │                                        └─────┬──────────────┬─────────────────────────────┘
        │                                              │ FIFO GX      │ pthreads (workers anidados)
        │                                     ┌────────▼─────────┐  ┌─▼──────────────────────────┐
        └──────────────────────────────────── │ Backend WebGPU   │  │ Reloj (decrementador),     │
                     mezcla DSP de JAudio     │ (Aurora, WGSL)   │  │ retrace 60 Hz, disco       │
                                              └──────────────────┘  └────────────────────────────┘
```

## Motor del juego

- **Código independiente de la plataforma** (sin cambios): `native/petari` de Nebula (Game, JSystem, nw4r,
  RVLFaceLib) y las partes del RVL SDK que Nebula reutiliza (GX salvo el FIFO, GD, MEM, KPAD, dvdfs, arc, wenc,
  OSThread/OSMutex/OSMessage/OSAlarm/OSTime/OSArena/OSAlloc). *Hecho: compila y enlaza.*
- **Específico de la plataforma** (`web/platform`, sustituye o adapta ficheros de `native/sdk` sin modificarlos):

  | Pieza nativa | Sustituto web | Estado |
  |---|---|---|
  | `os/context_switch.c` (ensamblador) | `context_switch_wasm.c`: fibers de Emscripten (Asyncify) con la misma interfaz | Hecho, tests pasan |
  | `memory.c` (`mmap` alineado, `mprotect`, `dladdr`) | `memory_shim_wasm.c`: arena alineada en la memoria lineal; ventana de imagen en la dirección 0 | Hecho, tests pasan |
  | `gx/gx_fifo.c` (anillo mapeado dos veces) | `gx_fifo_shim_wasm.c`: un búfer de 2× tamaño sin alias | Hecho, enlaza; sin probar con el juego real |
  | `os/divide_native.c` (captura `SIGFPE`) | `tools/wasm_ppc_divide.py`: pase post-enlace que convierte cada `div`/`rem` en una llamada con la semántica de PowerPC | Hecho, test pasa (704 divisiones en el módulo) |
  | `pthread_setname_np` | `posix_shim_wasm.c` (sin efecto) | Hecho |

- **Hilos.** Los hilos del juego (OSThread) se ejecutan uno a uno en un solo hilo del host, como en la consola; el
  cambio de hilo es un cambio de fiber. Los relojes del host (decrementador, retrace) son pthreads (workers).
  Alternativa a evaluar si Asyncify resulta demasiado lento: un pthread por OSThread con un "testigo" que solo uno
  posee a la vez (misma semántica, sin instrumentación; cada cambio cuesta un `Atomics.notify` + `Atomics.wait`).

## Renderizado 3D

- El módulo de juego produce el **flujo de comandos FIFO de GX** de la consola (big-endian, direcciones físicas de
  32 bits); el host lo recibe por `gx_submit` en tramos contiguos. El renderizador de Nebula (D3D12/Vulkan + HLSL)
  no es portable al navegador sin traducir HLSL en tiempo de ejecución.
- **WebGPU como objetivo principal**, a través de `emdawnwebgpu` (la API `webgpu.h` de Dawn sobre el WebGPU del
  navegador). Motivos: shaders WGSL generados en tiempo de ejecución (uno por configuración TEV), *compute* para
  conversiones de texturas y copias EFB, y es la API que ya usa Aurora.
- **Aurora (MIT) como base del backend:** `aurora::gx::fifo::process(data, size)` interpreta bytes FIFO GX, genera
  WGSL (`lib/gx/shader.cpp`) y gestiona texturas, copias EFB y lecturas. Trabajo necesario: compilarlo con
  Emscripten (no tiene soporte hoy), resolver direcciones físicas con las regiones del módulo
  (`nebula_game_memory_regions`), y conectar los marcadores de Nebula (tokens de sincronización, lecturas de EFB).
- **WebGL 2 como respaldo** solo si WebGPU no está disponible: exigiría un generador GLSL ES 3.0 propio (noclip.website,
  MIT, tiene uno para GX que sirve de referencia).
- Preservación: modelos, texturas, materiales TEV, iluminación, animaciones y efectos salen del propio juego; el
  backend solo tiene que emular GX con fidelidad, sin reinterpretar los recursos.

## Audio

- JAudio2 programa el DSP directamente; Nebula ya ejecuta el programa DSP de JAudio en C++ en el host
  (`runtime/src/jaudio_host_mixer.cpp`, portable). Plan: compilarlo a wasm y entregar los bloques del AI a un
  **AudioWorklet** mediante un búfer circular en SharedArrayBuffer; el reloj del dispositivo marca el ritmo del AI
  (como hace Nebula con XAudio2).
- La reproducción automática exige un gesto del usuario: el AudioContext se crea o reanuda al pulsar "Arrancar".

## Controles

- Nebula convierte teclado, ratón y mandos en informes de Wii Remote + Nunchuk (`runtime/src/native_input.cpp`,
  `wiimote_protocol.cpp`); se reutiliza esa lógica alimentada por eventos del navegador:
  - **Teclado** (`KeyboardEvent.code`, independiente de la distribución) y **ratón** como puntero (Pointer Lock para
    el giro de cámara cuando haga falta).
  - **Gamepad API**: mando estándar; giroscopio no disponible en la API estándar (el giro se mapea a botones o al
    stick derecho).
  - **Remapeo** guardado en `localStorage` (preferencia por usuario).
  - **Puntero y movimiento del Wii Remote**: puntero por ratón o stick derecho; agitar (giro de Mario) en un botón,
    como hace Nebula; inclinación (bola, manta raya) en el stick.

## Recursos y almacenamiento

- **Archivos del juego:** el usuario aporta su `game.pak` (ver `ARCHIVOS_DEL_JUEGO.md`). El motor lo lee **por
  trozos** sin copiarlo a memoria (la partición ocupa ~4,28 GB; wasm32 tiene 4 GB en total). Hecho con WORKERFS
  (lectura síncrona del `File` en el worker). Siguiente paso: copiarlo una vez a **OPFS** y leerlo con
  `FileSystemSyncAccessHandle`, para no pedir el archivo en cada visita.
- **Partidas guardadas (NAND):** `fs_request` sobre un directorio en OPFS con la misma estructura que la NAND de Wii
  (como Nebula), exportable/importable como zip. IndexedDB solo como respaldo si OPFS no existe.
- **Nada de Nintendo en el repositorio ni en el sitio publicado**; `.gitignore` excluye los formatos del disco.

## Interfaz web

- Hecho: comprobación previa de requisitos (WebAssembly, SharedArrayBuffer con aislamiento, Workers), selección de
  `game.pak`, estado, registro del motor y código de salida real (`web/shell`). La interfaz no simula progreso: el
  estado solo cambia con mensajes del motor.
- Pendiente: `<canvas>` transferido al worker (OffscreenCanvas), pantalla completa, controles configurables, aviso
  de memoria y de navegador no compatible con WebGPU.

## Publicación

- Sitio estático (`build/site`) con las cabeceras COOP/COEP. GitHub Pages no permite cabeceras: hace falta
  `coi-serviceworker` o un hosting que las admita (Cloudflare Pages, Netlify).
