# Panel para el directo

`overlay.html` (publicada en GitHub Pages) es una fuente de navegador para Streamlabs u OBS que muestra el
avance del port con datos reales del repositorio.

## Añadirlo en Streamlabs u OBS

1. Fuentes → **+** → **Navegador** (Browser Source).
2. URL: `https://gonzaloprogame.github.io/Super-Mario-Galaxy-Web/overlay.html`
3. Ancho **1920**, alto **1080**. El panel se escala solo a cualquier tamaño.

Parámetros opcionales, añadidos a la URL (`?a=1&b=2`):

| Parámetro | Efecto |
|---|---|
| `fondo=transparente` | Sin fondo, para ponerlo sobre la cámara o el juego |
| `cada=<segundos>` | Cada cuánto se leen el progreso y los commits (mínimo 60, por defecto 120) |
| `terminal=<segundos>` | Cada cuánto se lee la terminal (mínimo 5, por defecto 10) |
| `rama=<rama>` | Rama de la que se leen el progreso y los commits |

## De dónde salen los datos

| Apartado | Fuente |
|---|---|
| Probando ahora, comprobaciones, hoja de ruta | `docs/progreso.json` de la rama de desarrollo |
| Último commit y últimos avances | API pública de GitHub (60 consultas por hora sin sesión) |
| Terminal en directo | `terminal.json` de la rama `directo` |

Si algo no se puede leer, el pie del panel lo dice y se reintenta; no se muestran datos inventados.

## La terminal en directo

Mientras Claude Code trabaja en el repositorio, unos *hooks* de la sesión ejecutan
`tools/directo/terminal_feed.py` después de cada orden de terminal y de cada archivo editado. El script añade una
línea a `terminal.json` y lo publica en la rama `directo` como mucho cada 15 segundos. Al terminar cada turno el
estado pasa a «en espera».

Qué se publica y qué no:

- Solo la orden (su primera línea, hasta 180 caracteres) y su descripción, o la ruta del archivo editado.
- **Nunca la salida de las órdenes**: puede contener datos del juego.
- Se ocultan tokens, contraseñas, credenciales en URLs y correos, y las rutas locales se acortan (`~/repo`, `$S`).

Para activarlo en una sesión nueva:

```bash
tools/directo/setup.sh            # crea la copia de la rama directo y los hooks en .claude/settings.local.json
```

`.claude/settings.local.json` no se sube al repositorio. Si los hooks no se activan, abre `/hooks` una vez o
reinicia la sesión. Para desactivarlo, quita las entradas de `terminal_feed.py` de ese archivo.
