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

See the [C programming style guide](docs/c-programming-style-guide.md) for coding conventions.
