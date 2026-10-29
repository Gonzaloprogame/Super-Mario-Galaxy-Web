# Sitio publicado (generado)

Esta rama solo contiene el sitio compilado de Galaxy Web, servido por GitHub Pages. No la edites a mano: se genera con
`tools/build_web.sh` desde la rama `ccr-8700b376-tetafz`, commit `b89842bf8a2040ef0e94ca04e56a31763174e19e`. El código fuente y las instrucciones están en
esa rama.

No contiene ningún archivo de Nintendo: el motor necesita el `game.pak` de tu propia copia del juego
(`docs/ARCHIVOS_DEL_JUEGO.md`). `coi-serviceworker.js` (MIT, `coi-serviceworker.LICENSE`) da el aislamiento de
origen cruzado que GitHub Pages no permite configurar.
