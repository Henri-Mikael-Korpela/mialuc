# Playground

The playground provides a code editor and is able to compile Mialu to WebAssembly in the browser real-time. It is part of mialudoc, a small web server that is written in Mialu, which also shows the documents of the project as pages and has the page of the [internals of the compiler](#compiler-internals). The top bar of every page goes between them: Documentation, Playground, Compiler internals, Architecture and Source statistics.

There are two ways to run it:

- On Windows, run `mialudoc`, which is built together with the compiler. It needs no other tool than the ones that build the compiler. **This is the recommended way on Windows.**
- On other systems, or on Windows if you would rather not build anything, run a Docker container with Docker Compose.

See [Getting Started](getting-started.md) for the prerequisites of each platform.

The panel below the console has three tabs: the parsed code (what `mialuc dump-ast` shows), the x64 assembly (what `mialuc dump-asm --target x64-win32` shows) and the WebAssembly text. The `x64-win32` target builds a program that starts with `main :: proc() Int ->` and imports from the C library instead of JavaScript, so most programs of the playground, which have no `main` and call JavaScript, have no assembly. Add a `main` to one to see it. The tab then says so, and shows what the compiler said.

## Launching the playground on Windows

On Windows use `mialuc` and `mialudoc`, and not Docker Compose: there is no container to build and no virtual machine to start, so it is faster, it uses the compiler that you built, and so it shows a change to the compiler as soon as it is built.

1. Build the project as described in [Getting Started](getting-started.md#building-the-compiler), which has [how to build only mialudoc](getting-started.md#building-mialudoc) too. The build makes `build/mialuc.exe`, the compiler, and then uses it to make `build/mialudoc.exe` from [src/mialudoc/mialudoc.mu](../src/mialudoc/mialudoc.mu), so a single build is all that is needed:

    ```
    cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc
    cmake --build build
    ```

2. In the root of the repository, start the server:

    ```
    build/mialudoc.exe
    ```

3. Open http://localhost:8080/tools/playground in your browser. Stop the server with Ctrl+C.

The server can be given a port, the directory of the project and the address to listen on, in this order, and each of them can be left out when the ones after it are: `build/mialudoc.exe 8000`, `build/mialudoc.exe 8000 D:/mialuc`. The port is 8080 by default, and the directory is the current one. It listens on `127.0.0.1`, so only your computer can reach it, unless an address is given, such as `0.0.0.0` for all of them, which a container needs.

The server runs the compiler, `build/mialuc.exe` of the directory of the project, for each program that the playground compiles, and answers with the syntax tree, the WebAssembly module, its text and the assembly. A change to the compiler is seen by building it again, and there is nothing to restart. A change to `mialudoc.mu` is seen by building the project and starting the server again.

## Launching the playground using Docker

mialudoc is a program for Windows, so the container runs it, and the compiler that it starts, with [Wine](https://www.winehq.org/). The image is built from the [Dockerfile](../Dockerfile): the compiler is built with GCC for Linux to build mialudoc, and with MinGW-w64 for Windows to be the compiler that mialudoc runs.

On Linux, install [Docker Engine](https://docs.docker.com/engine/install/) with the Compose plugin, then in the root of the repository:

```
docker compose up --build
```

If your user is not in the `docker` group, put `sudo` before the command.

On Windows, Docker is not needed, see above. If you want it anyway, install [Docker Desktop](https://docs.docker.com/desktop/setup/install/windows-install/), start it, and run the same command in PowerShell.

The first build takes a few minutes, as the image has Wine. Once it is complete, open http://localhost:8008/tools/playground in your browser. The container publishes the port on the loopback of your computer only. To stop the server, press Ctrl+C, or run `docker compose down` if you started the container in the background (`docker compose up --build -d`).

The container has the compiler of the time that the image was built, so build the image again with `docker compose up --build` to see a change to the compiler or to the playground.

## Compiler internals

The navigation bar on top of the page goes to the compiler internals page, `/tools/internals` of the server, which documents the implementation of the compiler: the tokens, the syntax tree nodes, the reserved words and the operators, each with a search. mialudoc builds it from `src/tokenizer.h`, `src/ast.h` and the [language specification](mialu-programming-language.md) when the page asks for it, at `/api/internals`, so it follows the code.

## Architecture

The navigation bar also goes to the architecture of the compiler, `/tools/architecture` of the server, which is a diagram of the stages that `mialuc` runs: the tokenizer, the packages, the parser, the semantic analysis, the specializer and the targets, with the files of `src` that hold them. A stage can be chosen to read what it takes, what it gives and where its code is. The page is a static file, `tests/wasm/architecture.html`, so it needs no request to the server but the page itself.

## Source statistics

The navigation bar also goes to the source statistics, `/tools/stats` of the server, which shows how many lines of code the project has in C and in Mialu, as a pie chart. The lines can be counted in two ways, which a button chooses: *lines*, which are all of the lines of the files, and *source lines*, which leave out the blank lines and the lines that have only a comment, so a line of code that ends with a comment is still one. The pie can be split by language, or by the part of the project: the sources and the headers of the compiler, the standard library, mialudoc, the scripts and the examples, and the tests of the targets and of the errors. Under the chart a table has the numbers of each part, with the number of files, the lines, the source lines, the lines of comments and the blank lines. The page works as the one of the internals does: mialudoc counts the files of the project, in Mialu, when the page asks for them at `/api/stats`, so it follows the code. The comments that are counted are `//` in both languages, and `/* */` in C, also of many lines.

## What the server has

| Address | What it is |
|---|---|
| `/` | The index page, which lists the documents of `docs`. |
| `/<name>` | The document `docs/<name>.md` as a page, such as `/getting-started`. |
| `/tools/playground` | The playground, `tests/wasm/playground.html`. |
| `/tools/internals` | The page of the compiler internals, `tests/wasm/internals.html`. |
| `/api/internals` | The tokens, the syntax tree nodes, the reserved words and the operators, as JSON. |
| `/tools/architecture` | The page of the architecture of the compiler, `tests/wasm/architecture.html`. |
| `/tools/stats` | The page of the source statistics, `tests/wasm/stats.html`. |
| `/api/stats` | The files, lines, source lines, comment lines and blank lines of each part of the project, as JSON. |
| `/compile` | Posting the source of a program to it compiles it, and gives the answer as JSON. |
| `/test-code/wasm/<file>` | The examples of the playground, which are kept in `test-code/wasm`. |
| `/tests/wasm/<file>` | The files of `tests/wasm`, such as `sum.wasm`. |

The server answers `GET` and `HEAD`, and `POST` for `/compile` only, one request at a time. See the comments at the start of [mialudoc.mu](../src/mialudoc/mialudoc.mu) for what Mialu was missing for it, which are the features that it has workarounds for.
