# Mialu Programming Language

This is the official compiler implementation for the Mialu programming language.

Mialu is a procedural language with manual memory management. It is influenced by many languages including C, Odin and Python. It aims to be simple to learn yet provide powerful general constructs without a lot of complexity.

## Documentation

### Try it out!

- [Getting Started](docs/getting-started.md): prerequisites, building the compiler, and building and running programs, with a section for each platform.
- [Mialu by Examples](examples): programs to read and run, one directory for each, such as a number guessing game.
- [Playground](docs/playground.md): compile Mialu to WebAssembly in the browser, and run it.
- [Tooling](docs/tooling.md): the grammar check, the table of contents of the specification, and editor support.
- [Editor support](editors/vscode/README.md): the VS Code extension.
- [Releasing](docs/releasing.md): setting the version of the compiler and releasing it.

### Language

- [Grammar](grammar.ebnf): what the compiler accepts, in EBNF.
- [Language specification](docs/mialu-programming-language.md): the most up-to-date description of the language.
- [How production ready is Mialu?](docs/how-production-ready.md): what the language and its compiler can and cannot do yet, a checklist of what is present and how much it matters, what is missing, and the milestones to get there.
- [mialuc](docs/mialu-cli.md): the command line of the compiler: its commands, targets, exit status and errors.

### Build targets

- [WebAssembly](docs/build-target-wasm.md): what the `wasm` and `wat` targets build, how JavaScript uses the module, and what they reject.
- [x64-win32](docs/build-target-x64-win32.md): what the `x64-win32` target builds for Windows on x64, how, and what it rejects, including debugging and the assembly format.

### For developers

- [Programming mindset](docs/programming-mindset.md): how to approach programming in this project.
- [C programming style guide](docs/c-programming-style-guide.md): coding conventions for the compiler.
