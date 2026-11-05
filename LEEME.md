# Sitio publicado (generado) · versión 1.0.0-rc.1

Esta rama solo contiene el sitio compilado de Galaxy Web, servido por GitHub Pages. Versión 1.0.0-rc.1, candidata a
la 1.0.0 (commit `ef52a54` de la rama de desarrollo). No la edites a mano: se genera con
`tools/build_web.sh` desde la rama `ccr-8700b376-tetafz` (el código fuente y las instrucciones están en esa rama).

## Usarlo en tu ordenador

1. Descarga esta rama como zip y descomprímela.
2. Dentro de la carpeta: `python3 servir.py` (en Windows, `py servir.py`). Abre `http://127.0.0.1:8080` con el
   aislamiento de origen cruzado (COOP/COEP) que el motor necesita; solo escucha en tu ordenador.
3. En la página, elige tu `.rvz` (Super Mario Galaxy de Norteamérica, RMGE01). Se lee desde tu disco; no se copia.

Necesitas Python 3 y Chrome o Edge con WebGPU. Las partidas y los shaders guardados son por dirección: los de
`127.0.0.1:8080` no son los de la página publicada.

## Qué contiene

No contiene ningún archivo de Nintendo: el motor necesita tu propia copia del juego
(`docs/ARCHIVOS_DEL_JUEGO.md` en la rama de desarrollo). `coi-serviceworker.js` (MIT, `coi-serviceworker.LICENSE`) da
el aislamiento de origen cruzado que GitHub Pages no permite configurar.
