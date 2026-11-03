#!/usr/bin/env python3
"""Serves the directory of this script: the code playground, and index.html, which loads sum.wasm.

    python tests/wasm/serve.py [port]

Browsers refuse to load WebAssembly with `instantiateStreaming` from a file:// URL, and want the
content type application/wasm, so the files have to come from a web server. The server listens on
localhost only. The script has no dependencies besides the Python standard library.

The playground, playground.html, compiles Mialu by posting the source to /compile. The server runs
the compiler, build/mialuc.exe, which must be built first, and answers with JSON:

    {"error": null, "wasm": "<module in base64>", "wat": "<module in the text format>"}
    {"error": "<what the compiler printed>", "wasm": null, "wat": null}

A program that does not compile is a normal answer, so it comes with status 200 too.

The directory test-code of the project is served under /test-code/, so that the playground can
offer the example programs that are kept there: a Mialu file, such as sum.mu, and the JavaScript
that calls it, such as sum.client.js.
"""

import base64
import functools
import http.server
import json
import subprocess
import sys
import tempfile
from pathlib import Path

PORT_DEFAULT = 8008

ROOT_DIRECTORY = Path(__file__).resolve().parents[2]
EXAMPLES_DIRECTORY = ROOT_DIRECTORY / "test-code"
EXAMPLES_ROUTE = "/test-code/"
# The name of the executable depends on the platform that CMake built it for.
COMPILER_PATHS = [ROOT_DIRECTORY / "build" / "mialuc.exe", ROOT_DIRECTORY / "build" / "mialuc"]

# The compiler rejects larger sources anyway, see SOURCE_SIZE_BYTES_MAX in src/main.c, so a larger
# request is refused before it is read.
SOURCE_SIZE_BYTES_MAX = 1024 * 1024
# The compiler takes milliseconds. A longer run is a bug in it, and should not hold a thread.
COMPILE_TIMEOUT_SECONDS = 10
# The name that compiler messages show for the source, such as "playground.mu:1:5: ...".
SOURCE_NAME = "playground.mu"


class Handler(http.server.SimpleHTTPRequestHandler):
    # Older Pythons on Windows take the content types from the registry, which may not know .wasm.
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
        ".mu": "text/plain; charset=utf-8",
    }

    def end_headers(self):
        # The module is rebuilt often, and a stale one in the cache would be confusing.
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def translate_path(self, path):
        # The base class resolves a path against `self.directory`, and refuses to leave it, so
        # the examples are served by pointing it at their directory for this request. A handler
        # lives for one request, so the change does not reach any other.
        if path.startswith(EXAMPLES_ROUTE):
            self.directory = str(EXAMPLES_DIRECTORY)
            path = path[len(EXAMPLES_ROUTE) - 1:]
        return super().translate_path(path)

    def do_POST(self):
        if self.path != "/compile":
            self.send_error(404)
            return

        size_bytes = int(self.headers.get("Content-Length", "0"))
        if size_bytes > SOURCE_SIZE_BYTES_MAX:
            self.send_error(413, f"The source is larger than {SOURCE_SIZE_BYTES_MAX} bytes.")
            return

        source = self.rfile.read(size_bytes)
        body = json.dumps(compile_source(source)).encode("utf-8")

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def compile_source(source):
    """Builds `source` for both WebAssembly targets, and returns the answer to send as JSON."""
    compiler_path = next((path for path in COMPILER_PATHS if path.is_file()), None)
    if compiler_path is None:
        return failure(f"Cannot find the compiler at {COMPILER_PATHS[0]}. "
                       "Build it first, for example with build-wasm.ps1.")

    # Each request gets its own directory, because the server handles requests in parallel. The
    # compiler runs inside it, so that its messages name the source without a temporary path.
    with tempfile.TemporaryDirectory() as directory:
        Path(directory, SOURCE_NAME).write_bytes(source)

        outputs = {}
        for target in ["wasm", "wat"]:
            output_name = f"playground.{target}"
            command = [str(compiler_path), "build", "--target", target, output_name, SOURCE_NAME]
            try:
                completed = subprocess.run(command, cwd=directory, capture_output=True,
                                           timeout=COMPILE_TIMEOUT_SECONDS)
            except subprocess.TimeoutExpired:
                return failure(f"The compiler did not finish in {COMPILE_TIMEOUT_SECONDS} s.")

            if completed.returncode != 0:
                message = (completed.stderr or completed.stdout).decode("utf-8", "replace")
                return failure(message.strip() or f"The compiler exited with {completed.returncode}.")
            outputs[target] = Path(directory, output_name).read_bytes()

    return {
        "error": None,
        "wasm": base64.b64encode(outputs["wasm"]).decode("ascii"),
        "wat": outputs["wat"].decode("utf-8"),
    }


def failure(message):
    return {"error": message, "wasm": None, "wat": None}


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else PORT_DEFAULT
    directory = Path(__file__).resolve().parent
    handler = functools.partial(Handler, directory=str(directory))

    with http.server.ThreadingHTTPServer(("127.0.0.1", port), handler) as server:
        print(f"Serving {directory} at http://localhost:{port}/ (Ctrl+C to stop)")
        print(f"Playground: http://localhost:{port}/playground.html")
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print()


if __name__ == "__main__":
    main()
