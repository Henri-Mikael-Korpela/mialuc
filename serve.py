#!/usr/bin/env python3
"""Serves the web pages in tests/wasm: the code playground, the compiler internals page, and index.html,
which loads sum.wasm.

    python serve.py [port]

Browsers refuse to load WebAssembly with `instantiateStreaming` from a file:// URL, and want the
content type application/wasm, so the files have to come from a web server. The server listens on
localhost only. The script has no dependencies besides the Python standard library.

Both pages have a navigation bar on top that goes between them. The compiler internals page,
internals.html, is documentation of the implementation of the compiler: the tokens, the syntax tree
nodes and the reserved words. It takes them from /api/internals, which the server builds from the
sources of the compiler (src/tokenizer.h and src/ast.h) and from the language specification, so the
page cannot go out of date:

    {"tokens": [{"name", "value", "spelling", "category", "description"}],
     "ast_nodes": [{"name", "category", "description"}],
     "reserved_words": [{"word", "status", "category", "token"}],
     "operators": [{"operator", "description", "precedence", "example", "category"}]}

The playground, playground.html, compiles Mialu by posting the source to /compile. The server runs
the compiler, build/mialuc.exe, which must be built first, and answers with JSON:

    {"error": null, "ast": "<syntax tree>", "wasm": "<module in base64>", "wat": "<module text>",
     "asm": "<assembly>", "asm_error": null}
    {"error": "<what the compiler printed>", "ast": "<syntax tree or null>", "wasm": null, "wat": null,
     "asm": null, "asm_error": null}

The syntax tree is the one that `mialuc dump-ast` prints, one node per line, indented by depth. It is
there even when the analysis rejects the program, and null when the source does not parse.

The assembly is the one that `mialuc dump-asm --target x64-win32` prints. The x64-win32 target builds
much less of the language than the WebAssembly targets, so a program that compiles to WebAssembly is
often one that it rejects. Then "asm" is null and "asm_error" is what the compiler printed, which is
not an error of the program as a whole: "error" is only about the WebAssembly targets.

A program that does not compile is a normal answer, so it comes with status 200 too.

The directory test-code/wasm of the project is served under /test-code/wasm/, so that the playground can
offer the example programs that are kept there: a Mialu file, such as sum.mu, and the JavaScript
that calls it, such as sum.client.js. A program that imports JavaScript functions, such as
foreign-functions.mu, has a third file with them, such as foreign-functions.imports.js.
"""

import base64
import functools
import http.server
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

PORT_DEFAULT = 8008
# Localhost only, unless the environment variable MIALU_HOST says otherwise, as the Docker image does.
HOST = os.environ.get("MIALU_HOST", "127.0.0.1")

ROOT_DIRECTORY = Path(__file__).resolve().parent
WEB_DIRECTORY = ROOT_DIRECTORY / "tests" / "wasm"
SPECIFICATION_PATH = ROOT_DIRECTORY / "docs" / "mialu-programming-language.md"
TOKENIZER_HEADER_PATH = ROOT_DIRECTORY / "src" / "tokenizer.h"
AST_HEADER_PATH = ROOT_DIRECTORY / "src" / "ast.h"
EXAMPLES_DIRECTORY = ROOT_DIRECTORY / "test-code" / "wasm"
EXAMPLES_ROUTE = "/test-code/wasm/"
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

    def do_GET(self):
        if self.path.split("?")[0] == "/api/internals":
            try:
                body = json.dumps(read_internals()).encode("utf-8")
            except OSError as error:
                self.send_error(500, f"Cannot read the sources of the compiler: {error}")
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        super().do_GET()

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


# The groups that the keywords are shown in. The specification lists them only as used, literal and
# unused, so the finer groups are kept here. A keyword that is in the specification but not here is
# shown in the group "Other", so that a new one is never lost.
KEYWORD_CATEGORIES = {
    "Control flow": "break case continue defer else for if in match return",
    "Definitions": "const enum foreign import proc struct union",
    "Logical operators": "and not or",
    "Reflection": "type_info_of",
    "Values": "false nil true",
}
UNUSED_KEYWORD_CATEGORIES = {
    "Control flow": "async await catch do finally goto loop switch throw throws try where while yield",
    "Definitions": ("abstract alias class distinct dynamic extern final gen generate impl interface "
                    "macro mutable trait virtual"),
    "Visibility and safety": "private public safe unsafe",
    "Operators and conversions": ("as cast delete is new of operator override ref self super this "
                                  "transmute type_id_of type_of use"),
}

TOKEN_CATEGORIES = {
    "Names and literals": "IDENTIFIER INTEGER_LITERAL STRING_LITERAL LABEL",
    "Layout and comments": "INDENTATION COMMENT DOCUMENTATION_COMMENT END",
}

AST_CATEGORY_SUFFIXES = [
    ("_DEFINITION", "Definition"),
    ("_STATEMENT", "Statement"),
    ("_LITERAL", "Literal"),
    ("_ASSIGNMENT", "Statement"),
]
AST_TYPE_NODES = {"ARRAY_TYPE", "ENUM_TYPE", "POINTER_TYPE", "PROCEDURE_TYPE", "STRUCT_TYPE",
                  "TYPE_NAME"}
AST_OPERATOR_NODES = {"ADDITION", "AND", "COMPARISON", "DIVISION", "MULTIPLICATION", "NOT", "OR",
                      "SUBTRACTION", "ADDRESS_OF", "DEREFERENCE"}
AST_EXPRESSION_NODES = {"CALL", "FIELD_ACCESS", "IDENTIFIER", "SLICE", "SUBSCRIPT", "TYPE_INFO_OF"}


def enum_body(text, name):
    """Returns the lines between `typedef enum {` and `} name;`."""
    text = text.replace("\r\n", "\n")
    match = re.search(r"typedef enum \{\n(.*?)\n\} " + re.escape(name) + ";", text, re.DOTALL)
    return match.group(1).split("\n") if match else []


def read_internals():
    """Builds the answer of /api/internals from the sources of the compiler."""
    return {
        "tokens": read_tokens(TOKENIZER_HEADER_PATH.read_text(encoding="utf-8")),
        "ast_nodes": read_ast_nodes(AST_HEADER_PATH.read_text(encoding="utf-8")),
        "reserved_words": read_reserved_words(SPECIFICATION_PATH.read_text(encoding="utf-8")),
        "operators": read_operators(SPECIFICATION_PATH.read_text(encoding="utf-8")),
    }


def read_tokens(text):
    """The members of `Token_Kind`, each with the comment above it as its description."""
    tokens = []
    comment = []
    for line in enum_body(text, "Token_Kind"):
        line = line.strip()
        if line.startswith("//"):
            comment.append(line[2:].strip())
            continue
        match = re.match(r"TOKEN_KIND_(\w+)(?: = ('.'|\d+))?,", line)
        if match is None:
            comment = []
            continue
        name, value = match.groups()
        description = " ".join(comment)
        # A comment that tells about a whole group of the members is not about the next one.
        if description.startswith("Tokens consisting of"):
            description = ""
        comment = []

        is_character = value is not None and value.startswith("'")
        if is_character:
            spelling = value[1:-1]
        elif name.startswith("KEYWORD_"):
            spelling = name[len("KEYWORD_"):].lower()
        else:
            spelling = ""

        category = "Multi-character"
        if name.startswith("KEYWORD_"):
            category = "Keyword"
        elif is_character:
            category = "Single character"
        for category_name, names in TOKEN_CATEGORIES.items():
            if name in names.split():
                category = category_name
        tokens.append({"name": "TOKEN_KIND_" + name, "value": value, "spelling": spelling,
                       "category": category, "description": description})
    return tokens


def read_ast_nodes(text):
    """The members of `Ast_Node_Type`, with their entries of the big comment above `Ast_Node`."""
    descriptions = {}
    current = None
    for line in text.replace("\r\n", "\n").split("\n"):
        entry = re.match(r"^ \* - `AST_NODE_TYPE_(\w+)`: ?(.*)$", line)
        if entry:
            current = entry.group(1)
            descriptions[current] = entry.group(2).strip()
        elif current and line.startswith(" *   "):
            descriptions[current] += " " + line[5:].strip()
        else:
            current = None

    nodes = []
    for line in enum_body(text, "Ast_Node_Type"):
        match = re.match(r"\s*AST_NODE_TYPE_(\w+),", line)
        if match is None:
            continue
        name = match.group(1)
        category = "Other"
        if name in AST_TYPE_NODES:
            category = "Type"
        elif name in AST_OPERATOR_NODES:
            category = "Operator"
        elif name in AST_EXPRESSION_NODES:
            category = "Expression"
        else:
            for suffix, suffix_category in AST_CATEGORY_SUFFIXES:
                if name.endswith(suffix):
                    category = suffix_category
                    break
        nodes.append({"name": "AST_NODE_TYPE_" + name, "category": category,
                      "description": descriptions.get(name, "")})
    return nodes


def read_reserved_words(text):
    """The reserved words of the language specification, which has them in three code blocks: the
    keywords in use, the literal words and the unused keywords."""
    section = text.replace("\r\n", "\n").split("\n## Reserved words", 1)[-1].split("\n## ", 1)[0]
    blocks = re.findall(r"```\n(.*?)```", section, re.DOTALL)
    statuses = ["Keyword in use", "Literal word", "Unused keyword"]

    words = []
    for status, block in zip(statuses, blocks):
        table = UNUSED_KEYWORD_CATEGORIES if status == "Unused keyword" else KEYWORD_CATEGORIES
        for word in block.split():
            category = next((name for name, names in table.items() if word in names.split()), "Other")
            token = None if status == "Unused keyword" else "TOKEN_KIND_KEYWORD_" + word.upper()
            words.append({"word": word, "status": status, "category": category, "token": token})
    return words


def operator_category(precedence):
    if precedence == 0:
        return "Definition and assignment"
    if precedence <= 3:
        return "Logical"
    if precedence == 4:
        return "Comparison"
    if precedence <= 6:
        return "Arithmetic"
    return "Address, access and pointers"


def read_operators(text):
    """The rows of the table of operators in the language specification, with the links of the
    descriptions reduced to their text."""
    section = text.replace("\r\n", "\n").split("\n## Operators", 1)[-1].split("\n## ", 1)[0]
    operators = []
    for line in section.split("\n"):
        # The header and the line of dashes are not rows of operators, and neither is any other line.
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if not line.startswith("|") or len(cells) != 4 or not cells[2].isdigit():
            continue
        operator, description, precedence, example = cells
        description = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", description)
        operators.append({"operator": operator.strip("`"), "description": description,
                          "precedence": int(precedence), "example": example,
                          "category": operator_category(int(precedence))})
    # The specification lists the assignments last. The table is easier to read from the loosest
    # binding to the tightest, with the operators that are not in expressions at the end.
    operators.sort(key=lambda row: (row["precedence"] == 0, row["precedence"]))
    return operators


def compile_source(source):
    """Builds `source` for both WebAssembly targets and shows its assembly, and returns the answer to
    send as JSON."""
    compiler_path = next((path for path in COMPILER_PATHS if path.is_file()), None)
    if compiler_path is None:
        return failure(f"Cannot find the compiler at {COMPILER_PATHS[0]}. "
                       "Build it first, for example with build-wasm.ps1.")

    # Each request gets its own directory, because the server handles requests in parallel. The
    # compiler runs inside it, so that its messages name the source without a temporary path.
    with tempfile.TemporaryDirectory() as directory:
        Path(directory, SOURCE_NAME).write_bytes(source)

        ast = parse_tree(compiler_path, directory)
        assembly, assembly_error = dump_assembly(compiler_path, directory)

        outputs = {}
        for target in ["wasm", "wat"]:
            output_name = f"playground.{target}"
            command = [str(compiler_path), "build", "--target", target, output_name, SOURCE_NAME]
            try:
                completed = subprocess.run(command, cwd=directory, capture_output=True,
                                           timeout=COMPILE_TIMEOUT_SECONDS)
            except subprocess.TimeoutExpired:
                return failure(f"The compiler did not finish in {COMPILE_TIMEOUT_SECONDS} s.", ast, assembly, assembly_error)

            if completed.returncode != 0:
                message = (completed.stderr or completed.stdout).decode("utf-8", "replace")
                return failure(message.strip() or f"The compiler exited with {completed.returncode}.", ast,
                               assembly, assembly_error)
            outputs[target] = Path(directory, output_name).read_bytes()

    return {
        "error": None,
        "ast": ast,
        "wasm": base64.b64encode(outputs["wasm"]).decode("ascii"),
        "wat": outputs["wat"].decode("utf-8"),
        "asm": assembly,
        "asm_error": assembly_error,
    }


def parse_tree(compiler_path, directory):
    """Returns the syntax tree that `mialuc dump-ast` prints for the source, or None without one."""
    try:
        completed = subprocess.run([str(compiler_path), "dump-ast", SOURCE_NAME], cwd=directory,
                                   capture_output=True, timeout=COMPILE_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        return None

    # The tokens come first, then an empty line, then the tree, which starts with its root.
    # Windows writes the lines of text output with "\r\n".
    output = completed.stdout.decode("utf-8", "replace").replace("\r\n", "\n")
    start = output.find("\n\nMODULE")
    return None if start < 0 else output[start + 2:].rstrip("\n")


def dump_assembly(compiler_path, directory):
    """Returns the assembly that `mialuc dump-asm` prints for the source, and the message that the
    compiler printed instead when it cannot, which is the other of the two that is None.

    The playground runs the same command as the command line does, so that it cannot show anything
    else than the command line does.
    """
    command = [str(compiler_path), "dump-asm", "--target", "x64-win32", SOURCE_NAME]
    try:
        completed = subprocess.run(command, cwd=directory, capture_output=True,
                                   timeout=COMPILE_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        return None, f"The compiler did not finish in {COMPILE_TIMEOUT_SECONDS} s."

    # Windows writes the lines of text output with "\r\n".
    if completed.returncode == 0:
        return completed.stdout.decode("utf-8", "replace").replace("\r\n", "\n"), None
    message = (completed.stderr or completed.stdout).decode("utf-8", "replace").replace("\r\n", "\n")
    return None, message.strip() or f"The compiler exited with {completed.returncode}."


def failure(message, ast=None, assembly=None, assembly_error=None):
    return {"error": message, "ast": ast, "wasm": None, "wat": None, "asm": assembly,
            "asm_error": assembly_error}


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else PORT_DEFAULT
    directory = WEB_DIRECTORY
    handler = functools.partial(Handler, directory=str(directory))

    with http.server.ThreadingHTTPServer((HOST, port), handler) as server:
        print(f"Serving {directory} at http://localhost:{port}/ (Ctrl+C to stop)")
        print(f"Playground: http://localhost:{port}/playground.html")
        print(f"Compiler internals: http://localhost:{port}/internals.html")
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print()


if __name__ == "__main__":
    main()
