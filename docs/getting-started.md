# Getting Started

This document explains how to get the Mialu compiler and how to build and run Mialu programs. The steps depend on the operating system, so each platform has a section of its own:

- [Windows](#windows): build the compiler yourself and use all of its targets.
- [Linux and other operating systems](#linux-and-other-operating-systems): try the language in the playground, which runs in Docker.

See the [language specification](mialu-programming-language.md) for the language itself.

## Windows

Windows is the only operating system that the compiler is built on locally for now. It is also the only one that can build and run programs for the `x64-win32` target.

### Prerequisites

Windows 10 x64 has been used as the primary operating system. It is required for building and running Mialu code locally as `x64-win32` target. Windows 11 x64 should work but this is untested.

- [MinGW-w64](https://github.com/niXman/mingw-builds-binaries/releases) GCC, with `mingw32-make`, on the `PATH`.
    - For compiling the C99 source of the compiler.
    - Tested with GCC 16.2.0 (MinGW-Builds, `x86_64-win32-seh`), GNU Make 4.4.1.
- [CMake](https://cmake.org/download/) 3.20 or newer, on the `PATH`.
    - For generating the build files.
    - Tested with CMake 4.4.4.

The following are optional:

- [Python](https://www.python.org/downloads/) 3
    - For running `scripts/verify_grammar.py`, `scripts/update_language_spec_table_of_contents.py` and `tests/wasm/serve.py`. See [Tooling](tooling.md).
    - No dependencies, the standard library is enough.
    - Tested with Python 3.11.9.
- [VS Code](https://code.visualstudio.com/)
    - For Mialu programming language editor support, which includes syntax highlighting. See the [README of the extension](../editors/vscode/README.md).
- Windows PowerShell 5.1
    - For running `run.ps1`, `build-wasm.ps1` and `scripts/install_vscode_extension.ps1`.
    - Tested with PowerShell 5.1.19041.6456.
- [Docker Desktop](https://docs.docker.com/desktop/setup/install/windows-install/)
    - For running the [playground](playground.md) without building the compiler yourself.
    - Tested with Docker 29.8.1.

The compiler has no libraries to install: it only uses the C standard library, and the Windows API in [src/process.c](../src/process.c) to run the programs of `run`. The `x64-win32` target writes executables that need nothing but Windows, and the `wasm` and `wat` targets need nothing besides the compiler.

### Building the compiler

In the root of the repository:

```
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc
cmake --build build
```

This writes `build/mialuc.exe`.

### Building and running programs

Mialu compiler allows you to both build and run your programs. After a successful compiler build, you can build a Mialu source file and output it to an executable file as follows:

```
build/mialuc.exe build --target x64-win32 -o <output file> <source file>
```

Instead of building and then running your program, you can also run Mialu source file directly using the following commands:

```
build/mialuc.exe run <source file>
```

`run` builds the program for the machine it is run on, which is `x64-win32` for now, runs it and ends with its exit code. See [test-code/x64](../test-code/x64) for programs of that target.

To see what the front end makes of a source file, print its tokens, which include the comments, and its syntax tree, after which its names are resolved:

```
build/mialuc.exe dump-ast <source file>
```

To see the assembly that the compiler writes for the `x64-win32` target, use `dump-asm`. It builds the program and prints the code of the executable as text, with a label for the start of the program and for each procedure, and with comments that name the imports of the C library:

```
build/mialuc.exe dump-asm --target x64-win32 <source file>
```

The assembly is in the format of the [inline assembly of Odin](https://odin-lang.org/docs/inline-asm/): one instruction on each line, the operand that is written to first, registers written as `%rax`, memory operands written as `[%rbp - 8]`, labels written as `.name:` and used as `.name`, and comments after `//`. Odin's documentation does not show a memory operand that is relative to the instruction pointer, so it is written as `[%rip - 73681]`, and the call of an import has the size of an address after it, as in `[%rip - 73681]:u64`, like Odin's `[base]:T`. The text is made from the executable file that `build` would write, so it shows exactly what runs.

For building WebAssembly files (`.wasm` and `.wat`) using the Mialu compiler, you can run the following commands:

```
build/mialuc.exe build --target wasm -o <output file> <source file>
build/mialuc.exe build --target wat -o <output file> <source file>
```

`wasm` is a WebAssembly binary module, and `wat` is the same module in the WebAssembly text format. See [build-wasm.ps1](../build-wasm.ps1) and [run.ps1](../run.ps1) for examples, and [Playground](playground.md) for trying the result in the browser.

## Linux and other operating systems

The compiler is not built locally on these systems for now, and the `x64-win32` target is for Windows only. If you don't need to build the compiler yourself and you just want to try stuff out, you can use the [playground](playground.md) for testing the language. You can run it via Docker in the browser. The Docker image builds the compiler on Linux with GCC 14, see the [Dockerfile](../Dockerfile), which is how the `wasm` and `wat` targets are used outside of Windows for now.

### Prerequisites

- [Docker Engine](https://docs.docker.com/engine/install/) with the Compose plugin on Linux.
    - Tested with Docker 29.8.1 (on Windows, as Docker Desktop).
- A web browser that supports WebAssembly (Wasm).

Other operating systems that can run Docker, such as macOS, should work but this is untested.

### Running the playground

In the root of the repository:

```
docker compose up --build
```

If your user is not in the `docker` group, put `sudo` before the command. Then open http://localhost:8008/playground.html. See [Playground](playground.md) for how to use it.
