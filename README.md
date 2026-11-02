# Mialu Programming Language

Compiler for the Mialu programming language. See the [language specification](docs/mialu-programming-language.md).

## C standard

The project is written in **C99** (ISO/IEC 9899:1999). CMake builds it with `-std=c99`, GNU extensions off, and `-Wpedantic -Werror`, so any use of a newer or non-standard feature fails the build.

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

See the [C programming style guide](docs/c-programming-style-guide.md) for coding conventions.
