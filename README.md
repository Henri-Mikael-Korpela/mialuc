# Mialu Programming Language

Compiler for the Mialu programming language. See the [language specification](docs/mialu-programming-language.md).

## Build

```
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc
cmake --build build
build/mialuc.exe <source file>
```

## Grammar

[grammar.ebnf](grammar.ebnf) describes what the compiler accepts, in EBNF that works on characters. To check that a source file matches it:

```
python scripts/verify_grammar.py test-code/main-function.mu
```

The script uses only the Python standard library, so there is nothing to install. It prints `OK`, or the line and column where the file stops matching and what the grammar expected there. It exits with 0 when every file matches, 1 when a file does not, and 2 when the grammar or the arguments are wrong.

## Editor support

[editors/vscode](editors/vscode) is a VS Code extension that highlights the reserved words in `.mu` files. See its [README](editors/vscode/README.md) for how to install it.

See the [C programming style guide](docs/c-programming-style-guide.md) for coding conventions.

## License

See [LICENSE](LICENSE). The software is provided as is, without warranty or liability, and may be redistributed unmodified with attribution. Derivative works are not permitted.
