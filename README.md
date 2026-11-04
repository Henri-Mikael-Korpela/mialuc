# Mialu Programming Language

Compiler for the Mialu programming language. See the [language specification](docs/mialu-programming-language.md).

## Build

```
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc
cmake --build build
build/mialuc.exe run <source file>
build/mialuc.exe build --target wasm <output file> <source file>
build/mialuc.exe build --target wat <output file> <source file>
```

`run` prints the tokens and the syntax tree of a source file, and then resolves its names (see scope resolution in the specification). `build` writes a program for a target: `wasm` is a WebAssembly binary module, and `wat` is the same module in the WebAssembly text format. See [build-wasm.ps1](build-wasm.ps1) and [run.ps1](run.ps1).

## Playground and WebAssembly test site

Build the compiler first, for example with `build-wasm.ps1`, which also writes `tests/wasm/sum.wasm`. Then start the server:

```
python tests/wasm/serve.py
```

Open http://localhost:8008/playground.html for the playground: Mialu on the left, compiled as you type, and JavaScript below it that calls the procedures and prints what they return with `console.log`, such as `console.log("Sum:", sum(10, 15))`. Run it with Ctrl+Enter; the console output is on the right, with two tabs below it: "Parsed code", the syntax tree as a hierarchy that can be folded, and "WebAssembly text". The server compiles with `build/mialuc.exe`. The examples in the list at the top are the programs in [test-code](test-code): each is a Mialu file and the JavaScript next to it that calls it, such as `sum.mu` and `sum.client.js`.

A `Str` is two numbers, the address and the length of its bytes in the memory of the module, so a procedure that returns one gives an array. The playground has a function `Str` that reads it as a JavaScript string: `Str(get_name())`.

http://localhost:8008/ calls `sum.wasm` directly. See [tests/wasm](tests/wasm).

## Grammar

[grammar.ebnf](grammar.ebnf) describes what the compiler accepts, in EBNF that works on characters. To check that a source file matches it:

```
python scripts/verify_grammar.py test-code/main-function.mu
```

The script uses only the Python standard library, so there is nothing to install. It prints `OK`, or the line and column where the file stops matching and what the grammar expected there. It exits with 0 when every file matches, 1 when a file does not, and 2 when the grammar or the arguments are wrong.

## Editor support

[editors/vscode](editors/vscode) is a VS Code extension that highlights the reserved words, the operators, labels, attribute lists such as `#(static)`, strings and calls in `.mu` files. See its [README](editors/vscode/README.md) for how to install it.

See the [C programming style guide](docs/c-programming-style-guide.md) for coding conventions.

## License

See [LICENSE](LICENSE). The software is provided as is, without warranty or liability, and may be redistributed unmodified with attribution. Derivative works are not permitted.
