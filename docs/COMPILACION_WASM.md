# Compilación a WebAssembly: resultados (Hitos 2, 3 y 4)

Todos los resultados de este documento se han medido en este entorno (Linux x86-64, 4 núcleos, 15 GB) el
2026-10-28, con Emscripten 6.0.13, Node.js 22.22 y el Chromium preinstalado de Playwright. Las versiones están en
`ENTORNO.md`.

## Cómo reproducirlo

```sh
source <emsdk>/emsdk_env.sh          # Emscripten 6.0.13
tools/fetch_sources.sh               # Nebula, Aurora y Petari en third_party/, a revisiones fijadas
web/tests/run_os_tests.sh            # tests de la capa nativa de Nebula, en wasm (Node)
tools/build_web.sh                   # módulo de juego + hosts (Node y navegador) → build/site
node web/tests/browser_smoke.mjs     # prueba en Chromium (necesita playwright; ver ENTORNO.md)
```

## Hito 2: entorno

- `tools/fetch_sources.sh` descarga cada proyecto a una revisión fija en `third_party/` (ignorado por git) y se niega a
  continuar si una copia tiene cambios locales: las copias de referencia quedan intactas.
- Línea base nativa: los tests de Nebula (`native/sdk/os/tests/run_tests.sh`) pasan en Linux x86-64 con clang 18,
  con `CC="clang -D_GNU_SOURCE -Wl,--no-as-needed -lm"` (el script, pensado para Windows, no añade esas opciones en
  Linux). Medidas nativas: `OSDisableInterrupts+OSRestoreInterrupts` 3,0 ns, `OSLockMutex+OSUnlockMutex` 20,7 ns,
  `OSGetTime` 35,6 ns.

## Hito 3: compilación del módulo de juego

### Censo de compilación (sin ninguna adaptación)

`web/game/CMakeLists.txt` reproduce la selección de fuentes y las opciones de `native/CMakeLists.txt` de Nebula.
Primera compilación con `em++`/`emcc` (Release, `ninja -k 0`, 2 min 5 s):

| | Unidades |
|---|---|
| Total | 1.898 |
| Compilan sin cambios | 1.897 |
| Fallan | 1: `native/sdk/os/context_switch.c` (`#error "nebula_switch_stack: no implementation for this architecture"`) |

### Adaptaciones (en `web/platform`, sin modificar Nebula)

1. **Cambio de hilo** (`context_switch_wasm.c`, sustituye a `os/context_switch.c`): fibers de Emscripten con Asyncify.
   Cada región de pila preparada contiene, de abajo arriba: la pila C, el búfer de Asyncify y el registro del fiber.
   - *Error real encontrado:* en la primera versión el registro del fiber estaba en la dirección más baja de la región.
     Para el hilo de arranque esa dirección es `OSThread::stackEnd`, donde el SDK escribe el canario de desbordamiento
     `0xDEADBABE`; el registro se corrompía y `_asyncify_stop_unwind` acababa en `unreachable`. Corregido moviendo el
     registro al extremo superior.
2. **Memoria** (`memory_shim_wasm.c` + `include/web_memory_shim.h`, incluido solo en `sdk/memory.c`): la reserva
   "2× tamaño para alinear al tamaño" devuelve directamente la arena de 512 MB alineada a 512 MB; `munmap` y
   `mprotect` no tienen efecto (no hay páginas en wasm); `dladdr` informa de la base 0, de modo que la ventana de
   imagen de Nebula cubre los datos estáticos al principio de la memoria lineal.
3. **Anillo GX** (`gx_fifo_shim_wasm.c` + `include/web_gx_fifo_shim.h`, incluido solo en `sdk/gx/gx_fifo.c`): un
   búfer de 2× tamaño sin alias. Razonamiento en el propio fichero: el espejo solo sirve para que un tramo que cruza el
   final sea contiguo; nunca se lee a través del alias.
4. **`pthread_setname_np`** (`posix_shim_wasm.c`): Emscripten lo declara pero no lo implementa (error de enlace real).
5. **`-fno-builtin-wcslen`** en todas las unidades. *Error real encontrado:* a `-O2`, LLVM reconoce el bucle de
   `nebula_msl_wcslen` y lo sustituye por una llamada a `wcslen` de la libc, cuyo `wchar_t` es de 4 bytes en
   Emscripten, mientras que el juego usa `wchar_t` de 2 bytes (`-fshort-wchar`, texto UTF-16 del disco). Resultado:
   `wcslen(L"abc") == 5` y 6 tests de MSL fallando. Tras el cambio no queda ninguna referencia a funciones `wcs*` de
   la libc en el módulo. Este fallo latente afectaría también a Nebula en Linux/Android (glibc y bionic usan
   `wchar_t` de 4 bytes) con un LLVM que aplique esa transformación.

6. **División entera con la semántica de PowerPC** (`tools/wasm_ppc_divide.py`, sustituye a
   `os/divide_native.c`, que captura `SIGFPE`). En la consola `divw`/`divwu` no hacen *trap*; en wasm, dividir por
   cero (o `INT_MIN / -1`) aborta el programa sin posibilidad de continuar, y Binaryen ya no tiene pases
   `trap-mode-*`. El pase desensambla el módulo con `wasm-dis`, convierte cada `i32/i64 div_s, div_u, rem_s, rem_u` en
   una llamada a una función auxiliar con los resultados de Nebula (cociente `a < 0 ? -1 : 0` con signo, `0` sin
   signo; resto `a - q·b`) y lo vuelve a ensamblar con `wasm-as`. Se ejecuta tras cada enlace (CMake `POST_BUILD`).
   - Medido: 704 divisiones reescritas en el módulo (i32: 223 `div_s`, 152 `div_u`, 175 `rem_s`, 123 `rem_u`; i64:
     31), 75 s, +68 KB (9.123.568 → 9.192.187 bytes). El módulo reescrito se comporta igual con los *fixtures*.
   - Test `web/tests/ppc_divide_test.c`: sin el pase, `RuntimeError: divide by zero`; con él, todos los casos dan el
     resultado de PowerPC y las divisiones normales no cambian.
   - *Limitación encontrada al escribir el test:* a `-O2`, LLVM transforma `x / y == 0` (sin signo) en `x < y`, lo que
     es válido para él porque dividir por cero es indefinido en C. En esos casos la división ni siquiera llega al
     binario, así que ningún pase posterior (ni el manejador de *traps* nativo de Nebula) puede corregirla; solo un
     cambio en la fuente, como los `__nebula_divw` de Nebula en los casos conocidos.

### Enlace

- `smg_headless` (Node) y `smg_headless_web` (navegador) enlazan el módulo completo **sin ningún símbolo sin
  resolver**.
- `smg_headless.wasm`: 9,1 MB (código 8,0 MB, datos 0,8 MB), **15.868 funciones**; el archivo estático contiene los
  1.901 objetos del módulo.
- Opciones de enlace: `-sASYNCIFY -pthread -sALLOW_MEMORY_GROWTH -sMAXIMUM_MEMORY=4GB`; la arena alineada obliga a
  crecer la memoria hasta ~1 GB (el registro muestra el crecimiento de 64 MB a 1.074.003.968 bytes).

## Hito 4: ejecución

### Tests de la capa nativa de Nebula en wasm (`web/tests/run_os_tests.sh`, Node.js)

| Test | Nativo (Linux x86-64) | wasm (Node 22) |
|---|---|---|
| `context_switch_tests` (3 pilas, 1.000 rondas, estado de coma flotante) | pasa | **pasa** |
| `os_scheduler_tests` (prioridades, mensajes, alarmas, decrementador) | pasa | **pasa** |
| `mtx_tests` | pasa | **pasa** |
| `msl_runtime_tests` (formateadores de MSL) | pasa | **pasa** (tras `-fno-builtin-wcslen`) |
| `ppc_divide_tests` (división de PowerPC; propio) | — | **pasa** (tras `tools/wasm_ppc_divide.py`) |

Medidas de rendimiento (orientativas, una ejecución):

| | Nativo | wasm |
|---|---|---|
| `OSDisableInterrupts+OSRestoreInterrupts` | 3,0 ns | 12,9 ns |
| `OSLockMutex+OSUnlockMutex` | 20,7 ns | 56,1 ns |
| `OSGetTime` | 35,6 ns | 205,5 ns (llama a `performance.now()` en JS) |
| Alarma de 1 ms, hilo dormido: retraso medio / peor | 246 / 1.126 µs | 263 / 995 µs |

`OSGetTime` es 6× más lento: el juego lo consulta a menudo, así que conviene un reloj propio (p. ej. contador en
memoria compartida actualizado por el host).

### Módulo de juego completo (`smg_headless`, Node.js)

Sin el disco del usuario no se puede ejecutar el juego. Pruebas realizadas:

| Entrada | Resultado |
|---|---|
| Sin argumentos | Uso del programa |
| Fichero inexistente | `error: … cannot be opened` |
| Partición sintética con otro id (`web/tests/make_fixture.py`) | `holds ABCD01, not Super Mario Galaxy…` |
| Partición sintética RMGE01 con `main.dol` vacío | El módulo se acopla al host, arranca y su cargador de datos del DOL se detiene: `cStoryProgressTable (0x8053C620, 0x1E0 bytes) is not in the disc's main.dol` (código 3) |

Las particiones sintéticas se escriben enteras en `make_fixture.py`; no contienen ningún byte de Nintendo.

### Navegador (`web/tests/browser_smoke.mjs`, Chromium headless)

Todas las comprobaciones pasan:

- La página servida con COOP/COEP está aislada (`crossOriginIsolated`).
- El programa se ejecuta en un worker dedicado con pthreads en workers anidados; el `game.pak` se lee por trozos
  mediante WORKERFS (`/game/fixture_emptydol.pak: RMGE01, 16384 bytes…`).
- El módulo de juego se acopla, arranca y explica el fallo del DOL; la interfaz muestra el código de salida real (3).
- Una partición de otro juego se rechaza.
- Sin COOP/COEP la página no arranca el motor y explica que falta SharedArrayBuffer con aislamiento.
- *Error real encontrado:* esta versión de Emscripten crea los workers de pthreads con la URL del script que se
  ejecuta en el worker (`_scriptName`) y no admite `mainScriptUrlOrBlob`; con un `boot_worker.js` aparte, los
  pthreads cargaban ese fichero. Corregido haciendo que `smg_headless_web.js` sea el propio script del worker, con
  la lógica de arranque añadida por `--extern-post-js` (`web/shell/boot_worker.post.js`).

### Importador de discos (`nebula-import`) para wasm

- `wasm32-unknown-unknown`: falla (las dependencias C de `nod` —bzip2, zstd, liblzma— no tienen libc en ese objetivo).
- `wasm32-unknown-emscripten`: el `rlib` compila, incluidas las dependencias C; el `cdylib` (pensado para JNI de
  Android) falla al enlazar porque liblzma no se compiló como PIC. Un *wrapper* propio de tipo `staticlib` o `bin`
  es el camino para importar el disco en el navegador (ver el riesgo legal de las claves en `DIAGNOSTICO.md`).

## Última etapa que funciona

El módulo de juego completo compila, enlaza y se ejecuta en Node.js y en Chromium hasta la carga de datos de
`main.dol`, que es el primer punto en el que necesita los archivos del juego del usuario. Las siguientes etapas
(arranque del SDK y del juego, escena del logotipo) requieren un `game.pak` real, que este repositorio no puede ni
debe incluir.
