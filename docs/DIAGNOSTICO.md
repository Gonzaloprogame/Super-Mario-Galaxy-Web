# Diagnóstico y viabilidad (Hito 1)

Fecha de la inspección: 2026-10-28. Todo lo que aparece como **medido** se ha comprobado en este entorno; lo que
aparece como **según su documentación** procede de los README y documentos de cada proyecto y no se ha podido
verificar aquí (sobre todo, cualquier cosa que requiera el disco del juego).

Revisiones inspeccionadas (fijadas en `tools/fetch_sources.sh`):

| Proyecto | Revisión | Licencia |
|---|---|---|
| [Petari](https://github.com/SMGCommunity/Petari) | `470e9ce` (2026-10-25) | CC0 1.0 |
| [Nebula](https://github.com/keepvaibin/nebula) | `a32d945` (2026-10-28) | GPL-3.0-only |
| [Aurora](https://github.com/encounter/aurora) (referencia adicional) | `d0ace68` (2026-10-25) | MIT |
| [noclip.website](https://github.com/magcius/noclip.website) (referencia adicional) | `e47a8cc` | MIT |

## 1. Petari

**Qué es.** Una decompilación *matching* de Super Mario Galaxy: código C/C++ que, compilado con el compilador original
de la consola (CodeWarrior), reproduce byte a byte el `main.dol` de las versiones RMGJ01, RMGE01, RMGP01 y RMGK01.
No contiene recursos ni ensamblador del juego; necesita una copia del juego para compilar.

**Qué está implementado.** Según su README y decomp.dev, la decompilación está prácticamente completa (Nebula mide
un 99,90 % de funciones de RMGE01 idénticas; la prensa la describe como la primera decompilación completa de un juego
de Wii). Medido en el árbol: `src/Game` 1.609 unidades, `src/JSystem` 169, `src/nw4r` 29, `src/RVLFaceLib` 14,
`src/RVL_SDK` 297, más `MSL_C`, `Runtime` y `MetroTRK`.

**Sistemas disponibles.** Toda la lógica del juego: actores, nervios (máquinas de estado), gravedad, colisiones (KCL),
física de Mario, cámara, escenas, capas de interfaz (nw4r), sonido (JAudio2), carga de archivos (JKernel/JKRArchive),
modelos y animaciones (J3D), partículas (JParticle), además del SDK de Wii (GX, OS, DVD, NAND, WPAD/KPAD, AI/DSP).

**Herramientas.** Python + `configure.py` (decomp-toolkit) + Ninja; compiladores CodeWarrior `GC/3.0a3` y
`GC/3.0a5.x` ejecutados con `wibo` en Linux/macOS; `objdiff` para comparar. Requiere `orig/RMGx01/sys/main.dol`
extraído del disco del usuario.

**Dependencias de Wii.** Todo el SDK de Revolution (`src/RVL_SDK`): GX escribe en el FIFO de hardware, OS usa el
modelo de hilos y de interrupciones de Broadway, DVD/NAND/WPAD hablan con hardware o IOS. Además: ensamblador de
*paired singles* en 49 ficheros (`asm`, `psq_l`, `ps_madd`…), datos que solo existen en el `main.dol`, campos de bits
asignados desde el bit más significativo (CodeWarrior), y estructuras big-endian superpuestas a los ficheros.

**Restricciones de contribución y de IA** (CONTRIBUTING.md y README): solo se aceptan objetos que coincidan 1:1; no se
admite decompilación generada por IA ni salida de decompiladores; la IA solo se permite para nombres, limpieza y
documentación. El README dice explícitamente que el proyecto **no** pretende ser un port a PC (remite a "AstroCore",
un port aún no publicado).

**Utilidad para un port web.** Es la fuente de verdad de la lógica original, pero está escrita para producir un DOL
de PowerPC: compilarla para otra arquitectura exige todo el trabajo de portabilidad que Nebula ya ha hecho (ver §3).

## 2. Nebula

**Qué port implementa realmente.** No es un recompilador ni un emulador: compila el código de Petari como código
nativo (x86-64 y arm64) con clang, dentro de un módulo (`smg_game.dll` / `libsmg_game.so`), y sustituye el SDK de Wii
por una capa nativa (`native/sdk`). Un host (`NebulaNative.exe`) ofrece al módulo los servicios de la consola a través
de una tabla de funciones C (`native/include/nebula_game_api.h`, versión 33). Según su README, el juego es jugable de
principio a fin en Windows; también tiene un host SDL3 (Linux, Android) y una app Android.

**Código y modificaciones.**

- `native/petari`: fork de Petari en la revisión `86ae7a3` con los cambios para 64 bits y little-endian tras
  `TARGET_PC`. **Medido:** de 3.911 ficheros del fork, 2.636 son idénticos a Petari `86ae7a3`, 1.234 están
  modificados y 41 son nuevos; 376 ficheros contienen bloques `TARGET_PC`.
- `native/sdk` + `native/msl`: SDK nativo (OS con planificador propio sobre un solo hilo del host, GX que escribe el
  FIFO de la consola en un anillo, DVD, NAND, VI, WPAD, audio DSP, THP) y la interfaz de C de MSL.
- `runtime/`: renderizador GX (Direct3D 12 y Vulkan, ~98.000 líneas, shaders HLSL), audio (programa DSP de JAudio
  reimplementado en el host, salida XAudio2/SDL), entrada (teclado, ratón, mandos, giroscopio), guardado, mods.
- `crates/nebula-import` (Rust): lee la imagen del disco del usuario (ISO, RVZ… con `nod`) y genera `game.pak`.
- `installer/` (C#/.NET): instalador y lanzador para Windows.

**Proceso de compilación reproducible.** Documentado (`docs/BUILDING.md`) y con dependencias descargadas por CMake
con hash SHA-256, pero orientado a Windows (scripts PowerShell, Visual Studio). **Medido:** los tests de la capa
nativa (`native/sdk/os/tests/run_tests.sh`) compilan y pasan en Linux x86-64 con clang 18, con dos ajustes que el
script no hace en Linux (`-D_GNU_SOURCE` y `-lm`).

**Gráficos, audio, entrada y SO.** Direct3D 12 / Vulkan, XAudio2 / SDL3 audio, Win32 / SDL3 para ventana y entrada,
HID directo para mandos; memoria con `VirtualAlloc`/`mmap` (reserva alineada, páginas de guarda), anillo GX mapeado
dos veces (`memfd_create`/`VirtualAlloc2`), cambio de pila en ensamblador x86-64/arm64, división entera que captura
`SIGFPE`/excepciones para devolver el resultado de PowerPC.

**Cómo se hizo.** Su README declara que el host, el renderizador, el instalador, los tests y la documentación se
escribieron con agentes de IA (Claude), dirigidos y revisados por su autor. Por eso su fork de Petari no se envía a
Petari (que no acepta cambios generados por IA).

## 3. Comparación técnica

| Criterio | Petari | Nebula | Construir desde cero / noclip |
|---|---|---|---|
| Lógica original del juego | Completa (fuente de verdad) | Completa (fork de Petari) | noclip: solo renderizado y parte de actores en TypeScript, sin jugabilidad |
| Compila fuera de la consola | No: CodeWarrior, PowerPC | Sí: clang, x86-64/arm64 | — |
| 64 bits / little-endian | No aplica | Resuelto y auditado (`BE<T>`, `SelfRel<T>`, campos de bits, ~20 auditorías) | — |
| Ensamblador *paired singles* | Presente (49 ficheros) | Sustituido por C nativo | — |
| SDK de Wii | Original (hardware) | Capa nativa con interfaz C estable | — |
| Gráficos | GX de hardware | D3D12/Vulkan + HLSL (no portable al navegador tal cual) | Aurora: GX sobre WebGPU con WGSL (MIT) |
| Audio | DSP de hardware | DSP de JAudio en el host (C++ portable) + XAudio2/SDL | — |
| Archivos del juego | `main.dol` en la compilación | `game.pak` generado por `nebula-import` | — |
| Licencia | CC0 | GPL-3.0-only | Aurora MIT, noclip MIT |
| Riesgo de reescritura | Máximo (hay que repetir todo el port) | Mínimo para el núcleo | Inviable para jugabilidad original |

### Respuestas

1. **Mejor base para el navegador: Nebula.** Medido: sus 1.898 unidades compilan con Emscripten (wasm32) sin
   modificación salvo **una** (el cambio de pila en ensamblador), y el módulo completo enlaza sin símbolos sin
   resolver (ver `COMPILACION_WASM.md`).
2. **Nebula reutiliza Petari** (fork de la revisión `86ae7a3`) **y además lo modifica de forma independiente**
   (1.234 ficheros cambiados, siempre tras `TARGET_PC`), con una capa SDK propia.
3. **Lo que Nebula evita implementar:** todo el trabajo de portabilidad del código decompilado (orden de bytes,
   punteros en datos de 32 bits, campos de bits, *paired singles*, conversiones de coma flotante, división), el SDK
   nativo completo (OS, GX FIFO, DVD, NAND, VI, WPAD, AI/DSP, THP), el mezclador DSP de JAudio, la conversión de
   controles a informes de Wii Remote, el decodificador THP y el importador de discos.
4. **Lo más útil de Petari para entender la lógica original:** su código tal cual se escribió para la consola (sin los
   cambios de `TARGET_PC`), la documentación Doxygen de clases y miembros, los mapas de símbolos de `config/` y la
   correspondencia 1:1 con el binario original, que permite verificar el comportamiento.
5. **Incompatible con WebAssembly** (medido o leído en el código):
   - Cambio de pila en ensamblador (`native/sdk/os/context_switch.c`): wasm no permite mover su pila.
   - Reservas de memoria con `mmap`/`VirtualAlloc` y páginas de guarda con `mprotect`: wasm tiene una única memoria
     lineal sin protección de páginas.
   - Anillo GX mapeado dos veces (`memfd_create`/`shm_open` + `MAP_FIXED`): wasm no puede crear alias de memoria.
   - División entera que captura `SIGFPE` para continuar con el resultado de PowerPC: en wasm la división por cero es
     un *trap* irrecuperable.
   - Renderizadores D3D12/Vulkan con shaders HLSL compilados a DXIL/SPIR-V; FSR3, DLSS, trazado de rayos.
   - XAudio2, HID directo de mandos, Win32, minidumps, instalador .NET.
   - `cdylib` de `nebula-import` (JNI) en Emscripten, por falta de código PIC en liblzma (el `rlib` sí compila).
6. **Cambios necesarios por área:** ver `ARQUITECTURA_WEB.md`. En resumen: gráficos → backend WebGPU (y quizá
   WebGL 2 de respaldo) que consuma el mismo FIFO GX; audio → mezclador DSP de Nebula en un AudioWorklet; entrada →
   API Gamepad + teclado/ratón alimentando los informes de Wii Remote que ya construye Nebula; memoria → arena en la
   memoria lineal (hecho); recursos → `game.pak` leído por trozos desde el `File` del usuario u OPFS (hecho con
   WORKERFS).
7. **Estrategia elegida:** una rama de trabajo que **reutiliza Nebula sin modificarlo** (descargado a una revisión
   fijada) y añade una capa de plataforma web propia; **Aurora** (MIT) es la referencia principal para el backend
   WebGPU, porque ya implementa GX sobre la API `webgpu.h` con shaders WGSL y su `fifo::process()` consume bytes FIFO
   GX como los que produce el módulo de juego de Nebula. Petari se usa como referencia de la lógica original. Las
   licencias son compatibles (CC0 y MIT dentro de una obra GPL-3.0); por eso este repositorio es GPL-3.0.
8. **Alternativa no considerada:** ejecutar Dolphin compilado a wasm (no viable: JIT de PowerPC imposible en wasm y
   rendimiento de intérprete muy insuficiente), o reutilizar el renderizador de noclip.website (MIT; ya dibuja niveles
   de Galaxy en WebGL 2/WebGPU, pero a partir de modelos BMD, no del FIFO GX, y sin la lógica del juego). La opción
   que sí merece considerarse es **Aurora como capa SDK completa** (como hace Dusklight con Twilight Princess) en
   lugar de la de Nebula; no se elige porque Nebula ya resuelve los problemas específicos de Galaxy en su SDK y su
   fork, y Aurora no tiene soporte de Emscripten (búsqueda en su árbol: ninguna referencia).

### Estrategia y evidencias del código

- **Núcleo:** compilar el módulo de juego de Nebula para wasm32. Evidencia: censo de compilación (1.897/1.898
  unidades sin cambios), enlace completo sin símbolos sin resolver, 15.868 funciones en el binario.
- **wasm32 favorece el port:** ILP32 y little-endian. Los punteros vuelven a ocupar 4 bytes, como en la consola, y
  todas las correcciones de orden de bytes de Nebula (pensadas para x86/arm, little-endian) siguen siendo válidas.
- **Capa de plataforma sin tocar Nebula:** cada incompatibilidad se resuelve con un sustituto (`REPLACES:`) o un
  *shim* incluido solo en ese fichero (`web/platform`). Así las copias de referencia quedan intactas y actualizar
  Nebula es cambiar una revisión.
- **Interfaz estable:** el host habla con el juego solo mediante `nebula_game_api.h`; el host web reimplementa esa
  tabla (`web/host/headless_host.c`).

## 4. Implementaciones web existentes

- No se ha encontrado ningún port de Super Mario Galaxy a navegador (búsqueda web y en los repositorios inspeccionados).
- noclip.website renderiza niveles de Galaxy en el navegador (visor, no juego).
- Aurora (base de Dusklight y Metaforce) implementa GX sobre WebGPU para escritorio y móvil, sin build web.
- Hay precedentes de decompilaciones compiladas a wasm (Super Mario 64, pokeemerald), de consolas con gráficos fijos
  mucho más simples.

## 5. Riesgos y obstáculos principales

| Riesgo | Gravedad | Situación / mitigación |
|---|---|---|
| Rendimiento de Asyncify (instrumenta casi todo el juego por las llamadas indirectas) | Alta | Medir con el juego real; alternativas: listas `ASYNCIFY_ONLY/ADD`, un pthread por hilo del juego con relevo (ver arquitectura) o el *stack switching* nativo de wasm cuando esté disponible |
| Backend gráfico: GX completo (TEV, texturas indirectas, copias EFB, lecturas de EFB) sobre WebGPU | Muy alta | Reutilizar Aurora (MIT) en lugar de reescribir; WebGL 2 solo si WebGPU no está disponible |
| División por cero con semántica de PowerPC | Media | Resuelto con un pase post-enlace (`tools/wasm_ppc_divide.py`) para toda división que llegue al binario; donde LLVM ya aprovechó el comportamiento indefinido al compilar, solo sirve corregir la fuente (como hace Nebula con `__nebula_divw`) |
| Memoria: arena de 512 MB alineada a su tamaño → memoria wasm ≥ 1 GB | Media | Con punteros de 4 bytes los márgenes para 64 bits de Nebula sobran: reducir MEM1/MEM2 o mapear la arena en la dirección 0 |
| Sin páginas de guarda: un desbordamiento de pila de un hilo sobrescribe memoria | Media | Canarios (`0xDEADBABE` del SDK) y comprobaciones de profundidad en el cambio de hilo |
| SharedArrayBuffer exige COOP/COEP | Baja | Servidor propio con cabeceras, o `coi-serviceworker` en GitHub Pages |
| Partición de ~4,28 GB: no cabe en memoria wasm32 | Baja | Lectura por trozos (WORKERFS ahora; OPFS con acceso síncrono para persistir) |
| Claves comunes de Wii dentro de `nod` (importador) | Legal | No publicar un importador web sin evaluarlo; el usuario puede generar `game.pak` con la herramienta de Nebula en su equipo |
| `std::` de MSL frente a libc++ en un único binario wasm | Media | Host en C (hecho) o módulos separados (enlace dinámico de Emscripten) cuando el host sea C++ |
| Fallo latente de `wcslen` con `-fshort-wchar` (encontrado aquí) | Resuelto | `-fno-builtin-wcslen`; afecta también a Nebula en Linux con LLVM reciente |
| El juego real no se ha podido ejecutar aquí | — | Sin el disco del usuario, la última etapa verificable es el arranque del módulo hasta la carga de datos del DOL |

## 6. Plan priorizado

Ver `HOJA_DE_RUTA.md` para el estado de cada hito y sus criterios de aceptación.
