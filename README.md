<div align="center">

<img src="docs/img/logo_galaxy_web_port.svg" width="720" alt="Galaxy Web Port">

# 🌌 Galaxy Web Port

### Un port experimental de *Super Mario Galaxy* al navegador

**El código original del juego, compilado a WebAssembly y dibujado con WebGPU.**
**Sin instalar nada. Sin emulador. Con tu propia copia del juego.**

[![Estado](https://img.shields.io/badge/estado-experimental-ffb36b?style=for-the-badge)](#-estado-del-proyecto)
[![Versión](https://img.shields.io/badge/versi%C3%B3n-0.1.1--experimental-7b8cff?style=for-the-badge)](#-novedades-de-la-011-experimental)
[![Licencia](https://img.shields.io/badge/licencia-GPL--3.0-b06bff?style=for-the-badge)](LICENSE)

[![WebAssembly](https://img.shields.io/badge/WebAssembly-654FF0?logo=webassembly&logoColor=white)](#-cómo-funciona)
[![WebGPU](https://img.shields.io/badge/WebGPU-005A9C?logoColor=white)](#-requisitos)
[![Chrome](https://img.shields.io/badge/Chrome%20%2F%20Edge-recientes-4cd6a0)](#-requisitos)

### [▶ Probar en el navegador](https://gonzaloprogame.github.io/Super-Mario-Galaxy-Web/)

[Requisitos](#-requisitos) ·
[Copyright](#%EF%B8%8F-aviso-de-copyright-y-legalidad) ·
[Estado y fallos](#-estado-del-proyecto) ·
[Cómo jugar](#-cómo-jugar) ·
[Preguntas](#-preguntas-frecuentes) ·
[Compilar](#%EF%B8%8F-compilar-desde-el-código)

<img src="docs/img/interfaz.png" width="640" alt="La página de Galaxy Web, antes de cargar el juego">

</div>

---

> [!WARNING]
> ### 🚧 Versión experimental: todavía no se puede jugar de principio a fin
> Tiene **fallos conocidos y por descubrir**. Hoy se puede llegar a la pantalla de título, elegir un archivo, ver el
> prólogo con sus vídeos, **jugar el jardín del castillo** y entrar en las **primeras galaxias** (comprobados el observatorio y
> dos galaxias), pero **el juego no se puede completar**: hay un cuelgue conocido en el libro del prólogo, el resto
> de galaxias está sin probar y puede haber errores gráficos, sonidos que fallen o partes que vayan lentas.
>
> **Aun así, es un buen comienzo.** Que el código original del juego arranque, dibuje un nivel completo, suene,
> responda a los controles y guarde la partida dentro de una pestaña del navegador era el paso más difícil. Lo que
> queda es avanzar nivel a nivel, corregir lo que aparezca y ganar velocidad. 🌟

> [!IMPORTANT]
> ### 🛑 Este repositorio no contiene ningún archivo de Nintendo
> Ni ROM, ni ejecutables, ni modelos, texturas, música, vídeos ni logotipos del juego. **Necesitas tu propia
> copia** de *Super Mario Galaxy*, hecha a partir de **tu** disco. Lee el
> [aviso de copyright](#%EF%B8%8F-aviso-de-copyright-y-legalidad) antes de usarlo.

---

## ✨ Qué es

**Galaxy Web** ejecuta **la lógica original de *Super Mario Galaxy*** dentro del navegador. No es un remake, ni un
clon, ni un emulador de Wii:

- 🧠 **El juego:** la decompilación [Petari](https://github.com/SMGCommunity/Petari), en el fork nativo de
  [Nebula](https://github.com/keepvaibin/nebula), compilada a **WebAssembly** con Emscripten. Es el código del juego
  traducido a C++, no una imitación.
- 🎨 **Los gráficos:** cada orden gráfica del juego (el flujo GX de la consola) llega a
  [Aurora](https://github.com/encounter/aurora), que la traduce y la dibuja con el **WebGPU** del navegador.
- 🔊 **El sonido:** el programa de audio del juego (el DSP de JAudio) se mezcla con la implementación de Nebula y
  suena a través de un `AudioWorklet`.
- 💿 **El disco:** la página lee tu imagen **RVZ** de Dolphin **directamente y por trozos**, sin claves de la
  consola ni conversiones. **El archivo nunca sale de tu dispositivo.**
- 💾 **Las partidas:** se guardan en el propio navegador (IndexedDB) y se recuperan al volver.

## ⚙️ Cómo funciona

```mermaid
flowchart LR
  RVZ["💿 Tu imagen .rvz"] --> Lector["Lector RVZ<br/>(bloques de 1 MB)"]
  Lector --> Juego["🧠 Juego: Petari + Nebula<br/>(WebAssembly)"]
  Juego -- "flujo GX" --> Aurora["🎨 Aurora<br/>(WebGPU)"] --> Pantalla["🖥️ Canvas"]
  Juego -- "DSP JAudio + AI" --> Mezclador["🔊 Mezclador de Nebula"] --> Audio["AudioWorklet"]
  Controles["🎮 Táctil / teclado / ratón"] -- "Wiimote + Nunchuk" --> Juego
  Juego -- "partidas (NAND)" --> IDB["💾 IndexedDB"]
```

Todo ocurre **en tu navegador**: no hay servidor que ejecute el juego, no se suben archivos a ninguna parte y la
página funciona como un sitio estático (GitHub Pages).

---

## 🖥️ Requisitos

> [!NOTE]
> Estas cifras salen de lo que se ha medido hasta ahora: un teléfono Android de gama media-alta (unos **27 fps** en
> el primer nivel) y un equipo de escritorio con Chrome. Son **orientativas** y se irán ajustando con más pruebas.
> Si lo pruebas en otro dispositivo, ¡cuéntanos cómo va!

| | 🟡 Mínimos | 🟢 Recomendados |
|---|---|---|
| **Navegador** | Chrome o Edge **121 o posterior** con **WebGPU** activo | La última versión de Chrome o Edge |
| **Sistema** | Windows 10, macOS 12, ChromeOS o **Android 12** con WebGPU | Windows 11, macOS reciente o un Android reciente |
| **Procesador** | 4 núcleos (teléfono de gama media de 2021 en adelante) | 6 núcleos o más, de escritorio o teléfono de gama alta |
| **Memoria (RAM)** | **4 GB** (el motor reserva ~1,1 GB) | **8 GB** o más |
| **Gráficos** | GPU integrada compatible con WebGPU | GPU dedicada o integrada reciente |
| **Almacenamiento** | ~3,3 GB libres para tu archivo `.rvz` | SSD o memoria interna rápida |
| **Conexión** | Solo para cargar la página (unos 15 MB) | Igual: el juego no usa la red al jugar |
| **Controles** | Pantalla táctil, o teclado y ratón | Teclado y ratón, o una tablet en horizontal |

**Imprescindible en cualquier caso:**

- ✅ **WebGPU** (compruébalo en [webgpureport.org](https://webgpureport.org/)).
- ✅ **`SharedArrayBuffer`**: la página lo activa sola con un *service worker* (`coi-serviceworker`); si ves un
  aviso de aislamiento, recarga la página una vez.
- ✅ Una pestaña en **primer plano**: los navegadores frenan las pestañas ocultas y el juego se detiene.

**Sin probar todavía:** Firefox y Safari (su WebGPU es más reciente y no se ha comprobado con el motor) y los
navegadores integrados en otras apps. Sin WebGPU, el motor no puede dibujar.

---

## ⚖️ Aviso de copyright y legalidad

> [!CAUTION]
> ***Super Mario Galaxy*, Mario y todos sus personajes, nombres, imágenes, músicas y marcas son propiedad de
> Nintendo.** Galaxy Web es un proyecto **no oficial**, hecho por aficionados, **sin relación con Nintendo** y **sin
> su aprobación ni respaldo**.

- 📦 **No se distribuye nada del juego.** Este repositorio y la página publicada no contienen ni descargan ROM,
  imágenes de disco, ejecutables, modelos, texturas, música, vídeos ni logotipos de Nintendo. Las capturas de este
  README muestran solo la interfaz, sin el juego.
- 💿 **Tu copia, tu responsabilidad.** Solo debes usar una copia de seguridad hecha **por ti** a partir de **un disco
  original que poseas**. Descargar el juego de internet o compartir tu archivo con otras personas puede ser ilegal en
  tu país. **No pidas ni compartas archivos del juego** en las *issues*, *pull requests* ni en ningún canal del
  proyecto: se borrarán.
- 🔒 **Privacidad.** Tu archivo se lee por trozos dentro del navegador; **no se sube a ningún servidor**. Las partidas
  se guardan solo en tu navegador.
- 🧩 **El código.** Este proyecto es la **capa web**: el host, la página, el lector RVZ y los parches de Aurora. El
  código del juego viene de Petari (CC0) a través de Nebula (GPL-3.0), y se compila desde copias sin modificar en
  `third_party/`. Este proyecto **no genera código de decompilación ni envía cambios a Petari**; las restricciones
  de Petari sobre contribuciones generadas por IA se respetan.
- 📜 **Licencia:** GPL-3.0 ([`LICENSE`](LICENSE)), la de Nebula, del que este trabajo deriva.

Si eres titular de derechos y crees que algo de este repositorio infringe los tuyos, abre una *issue* y se revisará
de inmediato.

---

## 📊 Estado del proyecto

**Versión 0.1.1-experimental, más lo publicado después en la página** (sin número de versión todavía: ver
[novedades](#-novedades-desde-la-011-experimental)). Así está cada parte, con lo que de verdad se ha comprobado (*fecha
de esta tabla: 3 de noviembre de 2026*):

| Parte | Estado | Detalle |
|---|:---:|---|
| Arranque y pantalla de título | ✅ | Comprobado con un disco real, en escritorio y Android |
| Pantalla de carga | ✅ | Pasos reales del arranque y barra de shaders guardados (desde la segunda sesión) |
| Elegir archivo y crear partida | ✅ | Las partidas se guardan y se recuperan |
| Prólogo (el libro de cuentos) | 🧪 | Avanza con A; **en un PC se colgó en la segunda página**; quizá por el fallo de memoria ya arreglado, sin comprobar |
| Vídeos (THP) | ✅ | Los dos vídeos del prólogo, completos: 12.667 fotogramas, ninguno rechazado, ~3,5 ms cada uno |
| Jardín del castillo (primer nivel) | ✅ | Escenario, Mario, cielo, efectos y HUD |
| Primera galaxia (*HeavensDoorGalaxy*) | ✅ | Se juega en un PC; una advertencia de Aurora corregida por el camino (parche 0022) |
| Observatorio (*AstroGalaxy*) | 🧪 | Carga y se mueve; sin jugar a fondo |
| *EggStarGalaxy* | ✅ | Se colgaba siempre; **arreglado** (parche 0025) y comprobado 4 minutos con el disco |
| Gira automática: los 40 escenarios restantes | 🧪 | **Los 40 cargan y dibujan** (900 fotogramas cada uno, sin errores ni alarmas de memoria); **ninguno jugado**. Un aviso conocido en *SurfingLv1* (parche 0022) |
| *CocoonExGalaxy* | 🧪 | El motor moría allí tras varias galaxias: **arreglado** (el host pisaba memoria del motor) y comprobado con la gira; sin jugar |
| Jugar cada galaxia | ⏳ | Cargar y dibujar no es poder jugarla: falta recorrerlas jugando, con sus jefes y sus misiones |
| Controles: táctil, teclado y ratón | ✅ | Palanca, botones, puntero y agitar |
| Sonido (música y efectos) | 🧪 | La mezcla funciona y sus niveles son correctos; sin confirmar en un teléfono |
| Rendimiento | 🧪 | ~27 fps en Android en el primer nivel; mejoras posteriores sin medir allí |
| Cargas de escenario | 🧪 | El disco ya es rápido (~0,4 s por carga); el registro mide cada carga (`[host] load …`) |
| Versiones PAL y japonesa | ⏳ | Solo se ha probado la norteamericana (RMGE01) |
| Jugar de principio a fin | ❌ | **Todavía no** |

### 🐞 Fallos conocidos

- 🧊 **Cuelgue en el libro del prólogo:** en un PC el juego se paró en la segunda página del libro, con todo el juego
  quieto (ni dibujo, ni disco, ni sonido) y fuera de cualquier servicio del motor. Todavía no se reproduce en el
  entorno de desarrollo; el aviso de bloqueo ya dice si el juego espera algo o da vueltas en un bucle, y con ese dato
  se buscará la causa.
- 🐢 **Velocidad:** en teléfonos va por debajo de 30 fps; en escritorio con GPU por software es mucho más lento.
- ⏸️ **Pausas la primera vez:** cada combinación de efectos nueva crea un *pipeline* gráfico en mitad del juego (una
  pausa pequeña). Desde la segunda sesión en el mismo navegador se preparan antes de jugar, en la pantalla de carga.
- 🌠 **Efectos:** puede haber efectos de luz, transparencias o resplandores que no se vean igual que en la consola.
- 🕹️ **Puntero:** apuntar con el dedo funciona, pero a veces hace falta pulsar A dos veces en los menús.
- 💥 **Si algo se para:** la página lo dice. Si el juego se cuelga sin error, el estado avisa de que el hilo del juego
  no responde y los informes siguen llegando al registro (líneas `[vigía]`). El botón «Copiar el registro» guarda todo
  lo necesario para investigarlo.

¿Has encontrado otro? Abre una *issue* con el **registro del motor** (botón «Copiar el registro»), tu dispositivo y
navegador, y qué estabas haciendo. **No adjuntes archivos del juego.**

### 🌟 Por qué es un buen comienzo

Llegar hasta aquí ha supuesto resolver problemas que no tienen nada de trivial:

- 🧩 Compilar **casi 1.900 unidades** de código (juego, JSystem, nw4r y el SDK de Nebula) a WebAssembly, con hilos.
- 🎨 Enseñar a Aurora a entender el flujo gráfico **tal y como lo escribe la consola**: memoria por direcciones
  físicas, copias del EFB, el truco del *clear quad* para limpiar la profundidad, matrices cargadas por índice…
- 🔊 Hacer sonar el programa de audio del juego en una página web.
- 💿 Leer el formato RVZ de Dolphin directamente en el navegador, sin claves ni conversiones.
- ⚡ Medir y optimizar: menos de la mitad de tiempo por fotograma que en las primeras versiones, y un arranque en
  Android que pasaba de más de un minuto de lecturas a unos pocos segundos.
- 🎬 Reproducir los vídeos del juego con el decodificador de Nebula, píxel a píxel igual que en su versión de PC.
- 🔎 Diagnosticar cuelgues que no dejan error: un registro que sigue llegando aunque el juego se pare, y un aviso que
  dice dónde se quedó (en qué servicio, qué orden gráfica, si el juego espera o da vueltas).

---

## 🎮 Cómo jugar

1. Abre **[la página](https://gonzaloprogame.github.io/Super-Mario-Galaxy-Web/)** en Chrome o Edge con WebGPU.
2. Arrastra o elige tu archivo **`.rvz`**, y elige el idioma.
3. Pulsa **▶ Arrancar el motor**. La página baja sola hasta la pantalla y el sonido se activa con ese clic.
4. Espera a que acabe la **pantalla de carga**. La primera vez en un navegador no hay shaders guardados; desde la
   segunda, verás la barra mientras se preparan los de sesiones anteriores.
5. Usa **⛶ Pantalla completa** y el control de **🔊 volumen** si quieres.

| Wiimote + Nunchuk | ⌨️ Teclado | 👆 Táctil / 🖱️ ratón | 🎮 Mando (Xbox, PlayStation…) |
|---|---|---|---|
| **A** / **B** | Espacio / Mayús | Botones A y B · clic / clic derecho | A / ✕ · B / ○ |
| **Palanca** | W A S D | Palanca de la pantalla | Palanca izquierda |
| **Z** / **C** | Z / C | Botones Z y C | LT / L2 · LB / L1 |
| **Agitar** (el giro) | Q o E | Botón «Agitar» | X, Y, RB / R1 |
| **Puntero** | — | Tocar la pantalla o mover el ratón sobre ella | Palanca derecha |
| Cruceta, **+**, **−**, **HOME**, **1**, **2** | Flechas, P, M, H, 1, 2 | Botones de la pantalla | Cruceta, Start, Select, Guía, clic de las palancas |

### 💿 Tu copia del juego

Necesitas una copia de seguridad de **tu** disco de *Super Mario Galaxy* **(Norteamérica, RMGE01)** en formato
**RVZ** de [Dolphin](https://dolphin-emu.org/):

1. Haz la copia de tu disco con las herramientas de tu consola (por ejemplo, CleanRip en una Wii).
2. Ábrela en Dolphin, pulsa con el botón derecho en el juego y elige **«Convertir archivo…»**.
3. Elige el formato **RVZ** con compresión **Zstandard** (la de Dolphin por defecto) o sin compresión.

Las imágenes **ISO** y **WBFS** guardan la partición del juego cifrada: hay que convertirlas a RVZ. Más detalles
en [`docs/ARCHIVOS_DEL_JUEGO.md`](docs/ARCHIVOS_DEL_JUEGO.md).

---

## ❓ Preguntas frecuentes

<details>
<summary><b>¿Es un emulador?</b></summary>

No. Un emulador imita el hardware de la Wii y ejecuta el programa original de la consola. Galaxy Web ejecuta el
**código del juego reconstruido en C++** (la decompilación Petari), compilado para el navegador, y traduce sus
órdenes gráficas y de sonido a tecnologías web.
</details>

<details>
<summary><b>¿Por qué no se incluye el juego?</b></summary>

Porque pertenece a Nintendo. Distribuirlo sería ilegal y pondría en peligro el proyecto. Cada persona usa su propia
copia, y la página la lee sin enviarla a ninguna parte.
</details>

<details>
<summary><b>¿Mi archivo se sube a algún sitio?</b></summary>

No. Se lee por trozos dentro de tu navegador. La página es estática y no tiene servidor que reciba archivos.
</details>

<details>
<summary><b>¿Funciona en iPhone o con Firefox?</b></summary>

No se ha probado todavía. Hacen falta WebGPU y `SharedArrayBuffer`; si tu navegador los ofrece, puedes intentarlo y
contarnos qué pasa.
</details>

<details>
<summary><b>¿Puedo usar un mando?</b></summary>

Sí, en principio: los mandos que el navegador reconoce con la distribución estándar (Xbox, PlayStation y la mayoría
de los Bluetooth). Conéctalo y pulsa un botón; la tabla de controles dice qué hace cada uno. Está comprobado con un
mando simulado en Chromium, **todavía no con uno real**: si el tuyo no responde, cuéntanos cuál es y en qué navegador.
</details>

<details>
<summary><b>¿Se pierden las partidas?</b></summary>

Se guardan en el almacenamiento del navegador para esa página. Si borras los datos del sitio, se borran también.
</details>

---

## 🆕 Novedades desde la 0.1.1-experimental

Publicado en la página, todavía sin número de versión:

- 🎬 **Vídeos del juego:** los vídeos THP se reproducen con el decodificador de Nebula. En WebAssembly no existe su
  instrucción de multiplicar y sumar exacta, así que se calcula de otra forma con el mismo resultado: probado contra
  la biblioteca de C en 3 millones de casos, con los mismos píxeles en el navegador que en nativo y frente a ffmpeg
  con fotogramas reales.
- 💥 **El motor ya no muere tras varias galaxias:** la lista de *shaders* que se guarda para la pantalla de carga se
  creaba en el hilo del juego con su propio gestor de memoria, que a veces devolvía la dirección 0: la lista se
  escribía encima de datos del motor, y tras unas cuantas galaxias el motor moría (`table index is out of bounds`,
  en *CocoonExGalaxy*). Ahora usa la memoria del host; una prueba nueva lo comprueba.
- 🧾 **Los errores del motor traen su pila** en el registro, para saber dónde fue.
- 🎮 **Mandos físicos** (Gamepad API, distribución estándar); probados con un mando simulado, falta uno real.
- 🧊 **EggStarGalaxy ya no se cuelga:** el juego manda una tira de triángulos de 0 vértices, y Aurora reservaba unos
  4 GB para ella (parche 0025).
- 🎨 **Una advertencia en lugar de una parada:** una etapa de textura que lee coordenadas que nadie genera ya no
  detiene el renderizador (parche 0022).
- ⏳ **Pantalla de carga:** pasos reales del arranque y una barra de shaders. Mientras juegas se guarda la lista de
  *pipelines* que usa el juego, y en la siguiente sesión se preparan antes de empezar: al entrar en una galaxia ya
  conocida, se pasa de crear 62 en mitad del juego a 0. Una lista dañada se rechaza y nunca impide arrancar.
- 🔎 **Registro de vigía:** si el juego se cuelga, sus informes siguen llegando a la página por memoria compartida, y
  el aviso de bloqueo dice en qué servicio del motor está el juego, en qué orden gráfica está Aurora, cuántas veces
  cambió de hilo y si espera algo o da vueltas en un bucle.
- 📏 **Medidas:** cada carga de escenario dice cuánto tardó y cuánto fue disco y *pipelines*.
- 🗺️ **Gira de galaxias** (`?tour=Galaxia:misión,…`): el motor cambia de galaxia solo, para buscar fallos galaxia a
  galaxia sin jugar hasta cada una.
- 📺 **Panel para directos:** [`overlay.html`](https://gonzaloprogame.github.io/Super-Mario-Galaxy-Web/overlay.html),
  para Streamlabs u OBS, con el avance del port y la terminal de desarrollo en vivo
  ([`docs/DIRECTO.md`](docs/DIRECTO.md)).

## 🆕 Novedades de la 0.1.1-experimental

Una versión de mantenimiento, a partir de una auditoría del código con
[Ponytail](https://github.com/DietrichGebert/ponytail):

- 💾 **Partidas más seguras:** la copia de la partida se carga entera o nada (antes, una copia dañada podía
  sustituirse en el siguiente guardado por una memoria a medias), y cada sesión guarda aparte la partida de la que
  parte (`RMGE01.anterior` en IndexedDB).
- ⏸️ **Menos parones al cargar:** la cola entre el juego y el renderizador es cuatro veces más grande; al llenarse
  paraba el juego entero, sonido incluido.
- 🧪 **Pruebas nuevas:** la carga de la partida (todo o nada) y el sonido (orden de bytes, vuelta del anillo, anillo
  lleno), ambas comprobadas contra el fallo que vigilan.
- 🧹 **Limpieza:** una sola conexión a IndexedDB, el volumen sin temporizador, CSS que sobraba y un error falso
  (`0x08000000`) que salía en todos los registros.

## 🆕 Novedades de la 0.1.0-experimental

- 🎨 **El nivel se ve completo:** corregidos el *clear quad* del EFB (parche 0016) y las matrices cargadas por índice
  (parche 0018) en Aurora.
- 🔊 **Sonido:** el mezclador JAudio de Nebula y un `AudioWorklet`.
- 🚀 **Arranque rápido:** el lector RVZ lee la imagen en bloques de 1 MB (en Android, las lecturas sumaban 85 s).
- ⚡ **Rendimiento:** los datos de cada fotograma ya no copian 71 MB (parche 0019); procesado del flujo GX un 40 %
  más rápido (parche 0020).
- 💎 **Interfaz nueva:** pantalla completa, volumen, zona para soltar el archivo y registro plegable.

## 🗺️ Próximos pasos

- [ ] Encontrar y arreglar el cuelgue del libro del prólogo.
- [x] Recorrer los 44 escenarios con la gira de prueba y arreglar lo que se pare (los 44 cargan y dibujan).
- [ ] Jugar cada galaxia de principio a fin.
- [ ] Oír y ajustar el sonido en dispositivos reales.
- [ ] Más fotogramas por segundo en teléfonos.
- [x] Vídeos THP.
- [x] Pantalla de carga con los shaders guardados.
- [x] Mandos físicos (Gamepad API); falta probarlos con un mando real.
- [ ] Versiones PAL y japonesa.

La hoja de ruta completa está en [`docs/HOJA_DE_RUTA.md`](docs/HOJA_DE_RUTA.md).

---

## 🛠️ Compilar desde el código

```sh
source <emsdk>/emsdk_env.sh                     # Emscripten 6.0.13
tools/fetch_sources.sh                          # fuentes de referencia en third_party/ (revisiones fijas)
tools/build_web.sh                              # compila y monta el sitio en build/site
python3 tools/serve.py 8080 build/site          # http://127.0.0.1:8080 (con COOP/COEP)
```

Pruebas:

```sh
web/tests/run_os_tests.sh                       # capa nativa en wasm (Node)
web/tests/run_rvz_tests.sh                      # lector RVZ (necesita cargo)
node web/tests/browser_smoke.mjs                # la página y el motor en Chromium
node web/tests/bridge_smoke.mjs build/site/bridge   # GX del juego -> Aurora, en Chromium
```

Versiones y dependencias en [`docs/ENTORNO.md`](docs/ENTORNO.md).

## 📚 Documentación

| Documento | Contenido |
|---|---|
| [`docs/MOTOR_WEB.md`](docs/MOTOR_WEB.md) | El motor con el juego real: hallazgos, correcciones y rendimiento |
| [`docs/ARQUITECTURA_WEB.md`](docs/ARQUITECTURA_WEB.md) | Arquitectura del port |
| [`docs/PUENTE_FIFO.md`](docs/PUENTE_FIFO.md) | El flujo GX del juego dibujado por Aurora, y los parches de Aurora |
| [`docs/BACKEND_WEBGPU.md`](docs/BACKEND_WEBGPU.md) | Aurora sobre el WebGPU del navegador |
| [`docs/RVZ.md`](docs/RVZ.md) | Lectura de la imagen RVZ |
| [`docs/COMPILACION_WASM.md`](docs/COMPILACION_WASM.md) | Compilación a wasm: resultados y errores |
| [`docs/DIAGNOSTICO.md`](docs/DIAGNOSTICO.md) | Análisis de Petari y Nebula |
| [`docs/ARCHIVOS_DEL_JUEGO.md`](docs/ARCHIVOS_DEL_JUEGO.md) | Cómo aportar legalmente los archivos del juego |
| [`docs/ENTORNO.md`](docs/ENTORNO.md) | Herramientas e instalación |
| [`docs/HOJA_DE_RUTA.md`](docs/HOJA_DE_RUTA.md) | Hitos y próximos pasos |
| [`docs/DIRECTO.md`](docs/DIRECTO.md) | El panel para directos y la terminal en vivo |

## 🗂️ Estructura

| Ruta | Contenido |
|---|---|
| `web/shell/` | La página: interfaz, controles, presentación y sonido |
| `web/host/` | El host de `nebula_game_api.h`: disco RVZ, NAND, Wiimote, audio y puente a Aurora |
| `web/aurora/` | Aurora para el navegador: CMake y parches |
| `web/platform/` | Capa de plataforma web (sustitutos de la capa nativa de Nebula) |
| `web/game/`, `web/engine/`, `web/cmake/` | Compilación del módulo de juego y del motor |
| `web/bridge/`, `web/tests/` | Pruebas |
| `tools/` | Descarga de fuentes, compilación, servidor y utilidades |
| `third_party/` | Copias de referencia sin modificar (no versionadas) |

## 🤝 Contribuir

¡Toda ayuda es bienvenida! Probar en tu dispositivo y contar cómo va, informar de fallos con el registro, revisar
la documentación o mejorar el código. Dos normas: **nunca** compartas archivos del juego, y respeta las licencias y
las normas de contribución de los proyectos de los que depende este (en especial, las de Petari).

## 💜 Créditos

- [**Petari**](https://github.com/SMGCommunity/Petari): la decompilación de *Super Mario Galaxy* (CC0 1.0).
- [**Nebula**](https://github.com/keepvaibin/nebula): el fork nativo, su SDK y su mezclador de audio (GPL-3.0).
- [**Aurora**](https://github.com/encounter/aurora): la implementación de GX sobre WebGPU (MIT).
- [**Dolphin**](https://dolphin-emu.org/): el formato RVZ · [**Zstandard**](https://github.com/facebook/zstd).
- [**Emscripten**](https://emscripten.org/): la compilación a WebAssembly.
- [**coi-serviceworker**](https://github.com/gzuidhof/coi-serviceworker) (MIT): aislamiento entre orígenes en
  GitHub Pages.

---

<div align="center">

**Galaxy Web** es un proyecto de aficionados, sin ánimo de lucro y sin relación con Nintendo.
*Super Mario Galaxy* es una marca de Nintendo. Este proyecto no distribuye ningún recurso del juego.

⭐ Si te gusta el proyecto, ¡dale una estrella al repositorio! ⭐

</div>
