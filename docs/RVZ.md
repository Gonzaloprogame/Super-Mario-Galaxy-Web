# Leer el juego desde una imagen RVZ

Resultados medidos el 2026-10-29. La página acepta directamente la copia de seguridad del disco en formato **RVZ** de
Dolphin: lee de ella la partición del juego, que es lo que `game.pak` contiene, sin convertir el archivo ni copiarlo.

## Por qué funciona sin claves de la consola

`game.pak` es la partición del juego del disco, descifrada, con cada archivo en su posición (`nebula-import`,
`crates/nebula-import/src/lib.rs`). En un disco o en una ISO esa partición está cifrada, y descifrarla requiere la
clave común de la Wii. El formato RVZ (descrito en `docs/WiaAndRvz.md` del repositorio de Dolphin) **guarda la
partición ya descifrada y sin sus hashes**, para comprimirla mejor. Basta con descomprimir. La página no contiene
ninguna clave ni dato de Nintendo.

Por la misma razón, ISO y WBFS no se aceptan: guardan la partición cifrada. El motor lo detecta y explica cómo
convertir el archivo (a RVZ en Dolphin, o a `game.pak` con el importador de Nebula).

## Cómo se lee (`web/host/rvz_reader.c`)

- RVZ divide los datos en grupos que se comprimen cada uno por separado. Cada sector de 0x8000 bytes del disco ocupa
  0x7C00 bytes de datos. Para leer N bytes en una posición de la partición, el lector calcula en qué grupo están,
  descomprime solo ese grupo (Zstandard 1.5.7, solo el descompresor) y guarda los últimos ocho en caché.
- La partición del juego se localiza con la tabla de particiones del disco (tipo 0), que RVZ guarda sin cifrar, y la
  cabecera de la partición.
- Se omiten las listas de excepciones de hashes, que solo sirven para reconstruir el disco cifrado. El relleno
  pseudoaleatorio guardado como semilla («RVZ packing») se regenera con el generador de Fibonacci retardado de los
  discos.
- Compresiones: Zstandard (la de Dolphin por defecto) y ninguna. bzip2, LZMA y LZMA2 se rechazan con una explicación;
  WIA (el formato del que deriva RVZ), también.
- El motor lo usa en lugar del archivo: `read_at` de `web/host/headless_host.c`, con la misma lectura por trozos de un
  `File` del navegador (WORKERFS).

## Verificación (sin el juego)

No hay imágenes del juego en este entorno. Las pruebas usan imágenes sintéticas: todos sus bytes los genera la prueba.

1. `tools/rvz_fixture` (Rust) escribe tres imágenes RVZ según la especificación: Zstandard con bloques de 128 KiB,
   Zstandard con 2 MiB, y sin compresión. Cada una contiene un disco falso con tabla de particiones, cabecera de
   partición, una partición dispuesta como el `game.pak` de prueba de la página, datos pseudoaleatorios, un grupo de
   ceros, relleno empaquetado (también a través del límite entre dos grupos), listas de excepciones y grupos sin
   comprimir entre los comprimidos.
2. **`nod`**, el lector de RVZ que usa el importador de Nebula y una implementación independiente de la mía, lee cada
   imagen. Devuelve exactamente la partición esperada. La comparación detecta un solo byte cambiado: hay un
   autocontrol que lo comprueba.
3. **El lector** (compilado a wasm, en Node) devuelve los mismos bytes: la partición entera en orden, con tamaños de
   lectura que cruzan los límites entre grupos y segmentos, y 3.000 lecturas al azar. Rechaza WIA, bzip2 y archivos
   que no son RVZ. **Control negativo:** con el desplazamiento del generador de relleno cambiado de 18 a 16 (el
   detalle sobre el que avisa la especificación), las comparaciones fallan en las tres imágenes.
4. **El motor** (Node y página, con gráficos) lee la imagen sintética y llega al mismo punto que con `game.pak`.

```sh
web/tests/run_rvz_tests.sh        # 1–3 (necesita cargo)
node web/tests/browser_smoke.mjs  # 4, si run_rvz_tests.sh generó las imágenes
```

Sin verificar: una imagen RVZ real del juego, que solo puede aportar el usuario.
