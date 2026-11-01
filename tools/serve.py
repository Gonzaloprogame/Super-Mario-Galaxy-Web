#!/usr/bin/env python3
"""Servidor de desarrollo para build/site con aislamiento de origen cruzado (COOP/COEP).

El motor usa hilos (SharedArrayBuffer), que los navegadores solo habilitan en páginas aisladas.
    python3 tools/serve.py [puerto] [directorio]
"""
import functools
import http.server
import os
import sys


class IsolatedHandler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm",
                      ".js": "text/javascript"}

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    root = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "..", "build", "site")
    handler = functools.partial(IsolatedHandler, directory=os.path.abspath(root))
    with http.server.ThreadingHTTPServer(("127.0.0.1", port), handler) as server:
        print(f"http://127.0.0.1:{port}/ ({os.path.abspath(root)})", flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()
