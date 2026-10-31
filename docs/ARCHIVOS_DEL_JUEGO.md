# Archivos del juego

Este proyecto **no contiene ni distribuye** ningún archivo de Nintendo: ni imágenes de disco, ni `main.dol`, ni
modelos, texturas, música o vídeos. Tampoco los descarga. El motor necesita los archivos de **tu propia copia** del
juego, y los lee en tu navegador sin enviarlos a ningún servidor.

## Qué versión

Super Mario Galaxy para Wii, **Norteamérica (RMGE01), revisión 0**: es la versión para la que se compila el módulo de
juego (`VERSION=1` en `web/game/CMakeLists.txt`). Nebula también admite RMGP01 y RMGJ01 en su módulo nativo; la
compilación web solo se ha preparado para RMGE01.

## Cómo aportarlos legalmente

1. Haz una copia de seguridad de **tu** disco con tu propia consola Wii (por ejemplo con CleanRip en una Wii con
   Homebrew Channel) o usa una imagen que hayas hecho tú de tu disco. No descargues imágenes de terceros.
2. **La forma más sencilla: en formato RVZ.** Convierte tu copia a RVZ con Dolphin (clic derecho sobre el juego →
   «Convertir archivo» → RVZ, con la compresión Zstandard que propone). Selecciona el `.rvz` en la página: el motor lee
   de él la partición del juego, sin convertirlo ni copiarlo, y sin claves de la consola (`RVZ.md`).
3. O genera `game.pak` (la partición del juego, descifrada, con los archivos en sus posiciones) con la herramienta de
   Nebula, en tu equipo:
   ```sh
   cd third_party/nebula
   cargo build --release --locked -p nebula-import
   ./target/release/nebula-import identify /ruta/a/tu/juego.iso     # debe indicar RMGE01
   ./target/release/nebula-import import /ruta/a/tu/juego.iso --output /ruta/de/salida
   ```
   (también admite RVZ o una carpeta extraída; ver `docs/BUILDING.md` de Nebula). En Windows, el instalador de Nebula
   genera el mismo `game.pak`.
4. En la página, selecciona tu `.rvz` o tu `game.pak`. El motor lo lee por trozos dentro del navegador. Las imágenes
   ISO y WBFS no se aceptan directamente, porque guardan la partición cifrada: conviértelas a RVZ.

## Qué no hacer

- No subas `game.pak`, la imagen del disco ni ningún archivo extraído a este repositorio ni al sitio publicado:
  `.gitignore` excluye esos formatos, pero la responsabilidad es de quien publica.
- No publiques un sitio que incluya los archivos del juego.
