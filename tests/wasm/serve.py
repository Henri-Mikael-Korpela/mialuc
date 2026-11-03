#!/usr/bin/env python3
"""Serves the directory of this script, so that index.html can load sum.wasm in a browser.

    python tests/wasm/serve.py [port]

Browsers refuse to load WebAssembly with `instantiateStreaming` from a file:// URL, and want the
content type application/wasm, so the files have to come from a web server. The server listens on
localhost only. The script has no dependencies besides the Python standard library.
"""

import functools
import http.server
import sys
from pathlib import Path

PORT_DEFAULT = 8008


class Handler(http.server.SimpleHTTPRequestHandler):
    # Older Pythons on Windows take the content types from the registry, which may not know .wasm.
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
    }

    def end_headers(self):
        # The module is rebuilt often, and a stale one in the cache would be confusing.
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else PORT_DEFAULT
    directory = Path(__file__).resolve().parent
    handler = functools.partial(Handler, directory=str(directory))

    with http.server.ThreadingHTTPServer(("127.0.0.1", port), handler) as server:
        print(f"Serving {directory} at http://localhost:{port}/ (Ctrl+C to stop)")
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print()


if __name__ == "__main__":
    main()
