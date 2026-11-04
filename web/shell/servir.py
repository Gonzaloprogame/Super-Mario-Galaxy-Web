#!/usr/bin/env python3
"""Sirve en tu ordenador la copia descargada del sitio (la rama gh-pages, o build/site), con el aislamiento de origen
cruzado (COOP/COEP) que el motor necesita para sus hilos, y abre el navegador.

    python3 servir.py            # http://127.0.0.1:8080
    python3 servir.py 9000       # otro puerto

Solo escucha en 127.0.0.1: nadie más en la red ve la página. Tu copia del juego no pasa por aquí: la eliges en la
página, que la lee desde tu disco. Ctrl+C para pararlo.
"""
import functools
import http.server
import os
import sys
import threading
import webbrowser


class IsolatedHandler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm",
                      ".js": "text/javascript"}

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, format, *args):
        pass  # quiet: the page's own log says what happens


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    root = os.path.dirname(os.path.abspath(__file__))
    if not os.path.exists(os.path.join(root, "smg_web.wasm")):
        sys.exit(f"No encuentro smg_web.wasm junto a este archivo ({root}): pon servir.py en la carpeta del sitio.")
    handler = functools.partial(IsolatedHandler, directory=root)
    with http.server.ThreadingHTTPServer(("127.0.0.1", port), handler) as server:
        url = f"http://127.0.0.1:{port}/"
        print(f"Galaxy Web en {url} (carpeta {root}). Ctrl+C para parar.", flush=True)
        if "--sin-navegador" not in sys.argv:
            threading.Timer(0.5, lambda: webbrowser.open(url)).start()
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
