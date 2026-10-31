# Código de terceros servido con el sitio

| Archivo | Origen | Licencia |
|---|---|---|
| `coi-serviceworker.js` | [coi-serviceworker](https://github.com/gzuidhof/coi-serviceworker) 0.1.7 (paquete npm, sin cambios) | MIT, `coi-serviceworker.LICENSE` |

`coi-serviceworker.js` da aislamiento de origen cruzado (COOP/COEP, necesario para los hilos del motor) en
alojamientos que no permiten cabeceras propias, como GitHub Pages: un *service worker* las añade a las respuestas del
propio sitio y recarga la página una vez. Si el servidor ya las envía (`tools/serve.py`), no hace nada.
