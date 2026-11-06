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

## Playground

The playground compiles Mialu to WebAssembly in the browser: Mialu on the left, compiled as you type, and JavaScript below it that calls the procedures and prints what they return with `console.log`, such as `console.log("Sum:", sum(10, 15))`. It runs in a Docker container that builds the compiler and serves the page, so the only thing to install is Docker.

### Linux

Install [Docker Engine](https://docs.docker.com/engine/install/) with the Compose plugin, then in the root of the repository:

```
docker compose up --build
```

If your user is not in the `docker` group, put `sudo` before the command.

### Windows

Install [Docker Desktop](https://docs.docker.com/desktop/setup/install/windows-install/) and start it. Then in PowerShell, in the root of the repository:

```
docker compose up --build
```

### Using it

The first run builds the image, which takes a minute. Open http://localhost:8008/playground.html when the log says the server is running. Stop it with Ctrl+C, or run `docker compose down` if it runs in the background (`docker compose up --build -d`). The port is reachable from your computer only.

Run the JavaScript with Ctrl+Enter; the console output is on the right, with two tabs below it: "Parsed code", the syntax tree as a hierarchy that can be folded, and "WebAssembly text". The examples in the list at the top are the programs in [test-code](test-code): each is a Mialu file and the JavaScript next to it that calls it, such as `sum.mu` and `sum.client.js`.

A program that imports JavaScript functions with `import foreign js "functions"`, such as the `foreign functions` example, gets them from the "JavaScript imports" section under the JavaScript one, which is the file next to the program that ends in `.imports.js`. Its code defines `imports`, an object with a property of the name in the import, which has a function for each foreign procedure: `const imports = { functions: { alert: (address, length) => { ... } } }`. The code can use `Str`, which reads a string from the address and the length that a `Str` parameter is, as in `Str(address, length)`, and `instance`, whose `instance.exports.memory` is the memory of the program. The section stays empty for a program that imports nothing.

A `Str` is two numbers, the address and the length of its bytes in the memory of the module, so a procedure that returns one gives an array. The playground has a function `Str` that reads it as a JavaScript string: `Str(get_name())`. An array slice, such as the `[]Int` parameter of `sum` in the `arrays` example, is two numbers as well: the address of its first element and the number of elements. The playground has a variable `memory`, the memory of the module when it has one, so JavaScript can write the elements there: `new BigInt64Array(memory.buffer, 0, 3).set([1n, 2n, 3n])` and then `sum(0, 3)`. A struct is the values of its fields one after the other, so a procedure that returns one, as in the `structs` example, gives an array of them, and a parameter of a struct type is as many parameters. A pointer, such as the `^Int` parameter of `increment` in the `pointers` example, is one number: the address in the memory of the module of the value it points to, so JavaScript can write a value to the memory and pass the address of it: `new BigInt64Array(memory.buffer)[1] = 41n` and then `increment(8)`. Address 0 is `nil`, the null pointer, and following it traps.

The image contains a copy of the sources, so run `docker compose up --build` again after changing the compiler, the playground or the examples. Without Docker, build the compiler as described above and run `python tests/wasm/serve.py`; see [tests/wasm](tests/wasm).

## Grammar

[grammar.ebnf](grammar.ebnf) describes what the compiler accepts, in EBNF that works on characters. To check that a source file matches it:

```
python scripts/verify_grammar.py test-code/main-function.mu
```

The script uses only the Python standard library, so there is nothing to install. It prints `OK`, or the line and column where the file stops matching and what the grammar expected there. It exits with 0 when every file matches, 1 when a file does not, and 2 when the grammar or the arguments are wrong.

## Language specification

The table of contents of the [language specification](docs/mialu-programming-language.md) is generated from its section titles. After adding or renaming a section, run:

```
python scripts/update_language_spec_table_of_contents.py
```

Add `--check` to only test whether it is up to date. The script uses only the Python standard library.

## Editor support

[editors/vscode](editors/vscode) is a VS Code extension that highlights the reserved words, the operators, labels, attribute lists such as `#(static)`, strings and calls in `.mu` files. See its [README](editors/vscode/README.md) for how to install it.

See the [C programming style guide](docs/c-programming-style-guide.md) for coding conventions.

## License

See [LICENSE](LICENSE). The software is provided as is, without warranty or liability, and may be redistributed unmodified with attribution. Derivative works are not permitted.
