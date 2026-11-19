# Tooling

Scripts that help to work on the language. They use only the Python standard library, so there is nothing to install besides [Python](https://www.python.org/downloads/) 3. See [Getting Started](getting-started.md) for the versions that have been tested.

## Grammar

[grammar.ebnf](../grammar.ebnf) describes what the compiler accepts, in EBNF that works on characters. To check that a source file matches it:

```
python scripts/verify_grammar.py test-code/wasm/main-function.mu
```

The script uses only the Python standard library, so there is nothing to install. It prints `OK`, or the line and column where the file stops matching and what the grammar expected there. It exits with 0 when every file matches, 1 when a file does not, and 2 when the grammar or the arguments are wrong.

## Errors

[Error codes](error-codes.md) lists every error that the compiler reports. The programs in `test-code/errors`, each with one error, and the output that the compiler must print for them, in the file next to them with the suffix `.expected`, test the errors. To run the test:

```
python scripts/verify_errors.py
```

The compiler must be built first, see [Getting Started](getting-started.md#building-the-compiler); `--compiler` says where it is when it is not `build/mialuc.exe`. The script checks the output of each program, that each program has the code that its name starts with, and that every code of the compiler has a program, or a reason in the script that it cannot have one. After a change of an error on purpose, `--bless` writes the new output to the `.expected` files, and `git diff` shows what changed. It uses only the Python standard library. It exits with 0 when everything is as expected, and 1 when it is not.

## Language specification

The table of contents of the [language specification](mialu-programming-language.md) is generated from its section titles. After adding or renaming a section, run:

```
python scripts/update_language_spec_table_of_contents.py
```

Add `--check` to only test whether it is up to date. The script uses only the Python standard library.

## Editor support

[editors/vscode](../editors/vscode) is a VS Code extension that highlights the reserved words, the operators, labels, attribute lists such as `#(static)`, strings and calls in `.mu` files. See its [README](../editors/vscode/README.md) for how to install it.

[editors/vscode-debug](../editors/vscode-debug) is a second VS Code extension, next to the first, that debugs Mialu programs with gdb: it builds a `.mu` file with `mialuc build --target x64-win32 --debug` and starts the debug adapter of gdb, so that breakpoints can be set in `.mu` files and the variables can be looked at. See its [README](../editors/vscode-debug/README.md). [editors/vscode-lsp](../editors/vscode-lsp) is a third VS Code extension, with a language server for Mialu, `server.js`, which speaks the Language Server Protocol over the standard input and output. It shows the size and the alignment of a struct above its definition, which it gets from `mialuc dump-struct-sizes`, and the type of a name when the pointer rests on it, which it gets from `mialuc dump-types`. See its [README](../editors/vscode-lsp/README.md). `node --test editors/vscode-lsp/server.test.js` tests the server and needs [Node.js](https://nodejs.org/) 18 or newer. One script installs all three extensions.

## Releasing

Setting the version and releasing it are described in [Releasing](releasing.md).
