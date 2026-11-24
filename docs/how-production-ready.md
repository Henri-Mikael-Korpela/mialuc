# How production ready is Mialu?

## Goals

- A procedural Turing-complete language with similar low-level control as in C with convenient yet powerful structures built in to allow for large scale application development.
- A language that makes web development more pleasant, see [Problems Mialu addresses](problems.md).

## Verdict

Mialu is **not production ready**. It is version 0.5.0, and the first commit is from 2026-10-31. It is a well-specified, carefully built language with a compiler that is good for learning, experiments, small tools and its own programs: the release scripts and `mialudoc`, the server of the documentation and the playground, are written in it. It cannot yet be used for the "large scale application development" that the goals name, and it cannot yet write a web service, which is the use that matters most.

What exists is not listed again here, see the [specification](mialu-programming-language.md), the [command line](mialu-cli.md) and the documents of the [targets](build-target-x64-win32.md). In short: a front end and two back ends written in about 42,000 lines of C (`x64-win32` and WebAssembly), a verified grammar, a debugger and a language server for VS Code, a playground, and a standard library of four files. What follows is what is missing.

## What is missing, by impact

The impact says how much the gap matters for production readiness: **Critical** (a program of real size cannot do without it), **High**, **Medium** or **Low**.

### Critical

- **No heap.** `[dynamic]Int` grows, but in the temporary storage, which a procedure gives back when it returns, so a list cannot outlive the procedure that made it. An array cannot be a parameter, a result, a field of a struct or a global variable. There is no allocator in the library, and `malloc` through `foreign libc` is the only route. Without data that grows and lives on (lists, maps, strings built at run time, a cache) a service cannot be written.
- **No generic code.** Parametric polymorphism is only a possible future feature. Every container would be written again for each type, and no library of containers is possible.
- **No networking and no concurrency.** `mialudoc` declares Winsock by hand and answers one connection at a time, because there are no threads, no async and no way to wait on many sockets. A web server that answers many clients is the first thing that a web language is asked for.
- **No tests that run in CI.** Releases check the grammar and that every example compiles, and only on a version tag. Nothing runs a program and compares its output, and nothing runs `scripts/verify_errors.py` on a push. A compiler of this size that is not tested on every change will produce wrong code sooner or later.
- **One native platform.** `x64-win32` is the only native target. Servers run on Linux, and developers on macOS and ARM.

### High

- **Text at run time.** `++` and `int_to_str` work on constants only, there is no formatting, split, trim, find or builder, and no way to read the bytes of a `Str`. HTTP, JSON and logs are all text built at run time.
- **Standard library too small.** No collections, time, random numbers, sockets, process start, directory listing or file size. What exists works on Windows only.
- **No package manager and no project file.** Packages exist, but there are no versioned dependencies, no visibility rules and no build description. Every build compiles the program and everything it imports.
- **Integers.** `I8`, `I16` and `U16` are reserved, an `I32` takes 8 bytes in a struct, and there is no remainder operator. Protocols and file formats need exact sizes and byte order.
- **No optimizer.** Code is a direct translation that keeps values on a stack in memory.
- **Fixed compiler limits.** 1 MiB of source, 65,536 tokens and nodes, 1,024 procedures.
- **No stability promise.** Version 0.x, and a specification that changes with the code.

### Medium

- **Error handling is by hand.** Values are returned as `(Int, File_Error)`, and there is nothing that shortens the chain of fallible calls, so code with many of them is long.
- **Arithmetic wraps silently.** There is no checked or saturating form.
- **No sanitizers, profiler or coverage.** Debugging needs gdb on Windows.
- **No security review** of a compiler in C that reads untrusted source.

### Low

- No source formatter, linter or documentation generator.
- One example program in `examples`.

## Features that would make Mialu worth using

A language that only catches up with C, a better C, competes with Zig and Rust, which are far ahead. The reasons to choose Mialu have to come from problems that are still hard in the languages that people use. Each feature below is missing, and each is chosen because it solves a problem of [problems.md](problems.md) in a way that the languages of the comparison do not, or only with a library and a convention.

### 1. Memory that is safe without a borrow checker: allocators in the language

**Problem:** memory bugs (use after free, leaks, dangling pointers) are the largest class of security holes, and the long-running processes of a web service suffer from them and from the pauses of garbage collection.

**Idea:** the allocator is a value that is passed, as in Zig, but the language makes it hard to misuse: an arena per request that is freed as a whole, a pointer that cannot outlive its arena (checked at compile time, by a region that is part of the type), and a trap, not a corruption, on use after free in a debug build. `defer` already exists as the tool for release.

**Compared:** C and C++ leave it to the programmer and tools. Java, C#, Kotlin and Python use a garbage collector, safe but with pauses and a memory use that is hard to predict. Rust is safe at compile time at the price of lifetimes in every signature. Zig has explicit allocators but no checks of lifetimes. A region check without the full burden of Rust would be a middle path that no language of the seven takes.

### 2. Generics that are plain

**Problem:** reuse of containers and algorithms, without the cost of run time dispatch.

**Idea:** `$T` that specializes a procedure or a struct per set of types in the semantic analysis, so that neither target changes, with errors that name the type and the place. Constraints are written as ordinary compile-time checks, not as a second language of traits.

**Compared:** C has none. C++ templates are the most powerful and give the worst errors. Java and Kotlin erase types at run time. C# keeps them. Rust traits are strong and complex. Zig uses `comptime`, which is simple and strong but not checked until a type is used. Python is dynamic. Mialu already has the base of what the others lack, errors designed as an interface ([Errors as interface](errors-as-interface.md)), so generics with readable errors would be a visible advantage.

### 3. Concurrency without data races, and async that is cheap

**Problem:** distributed systems, real-time communication, background jobs and many clients at once are the daily work of a server, and data races are the hardest bugs to find.

**Idea:** lightweight tasks with their own small stacks, and channels, with a rule that a pointer sent over a channel is moved and the sender cannot use it again (a compile-time check that is simpler than a borrow checker, because the check only concerns a send). `defer` and cancellation tied together, so that a task that is cancelled releases what it holds. Structured concurrency: a task cannot outlive the block that started it.

**Compared:** C and C++ give threads and a manual discipline. Java has threads and, lately, virtual threads. Kotlin has coroutines with structured concurrency, the model to follow. C# has `async`/`await`. Python has the GIL and `asyncio`. Rust has `Send` and `Sync` that prove no data race, with the colored functions of async. Zig has no stable async at all. A language with structured tasks and moved sends, small enough to learn in a day, would be rare.

### 4. Web in the language: one source for the server and the browser

**Problem:** the front end and the back end are written in two languages, with types written twice and an API that drifts, and **cross-browser compatibility, state and frontend architecture** all come from that split. Mialu already has a WebAssembly target and a playground.

**Idea:** the same procedure, struct and enum compile to the server (native) and to the browser (Wasm), and a request and its response are one type that both sides share, with the serialization generated from the type by reflection (`type_info_of` exists for enums and procedures, and would be extended to structs). Access to the DOM and `fetch` is a small, typed library over `foreign` procedures. The compiler checks that an API change breaks both sides at compile time.

**Compared:** Kotlin Multiplatform, and Rust with `wasm-bindgen`, come the closest, with a heavy toolchain. C# has Blazor with a large runtime. C++ has Emscripten. Java has no good browser story. Python has Pyodide, large and slow. Zig builds Wasm but has no web library. A small Wasm module with no runtime and no garbage collector is something that Mialu can already do and that most of these cannot.

### 5. Security by type: untrusted data that has to be checked

**Problem:** injection (SQL, HTML, command), unchecked input and forged identities, in **web application security** and **authentication**.

**Idea:** a type that marks a string as `Untrusted`, `Html` or `Sql`, with no implicit conversion (Mialu has none anyway), so that the only way from raw input to a query is through a checking or escaping procedure. Secrets as a type that cannot be printed or logged, and constant-time comparison in the library. Bounds and null already trap.

**Compared:** no language of the seven does this in the language. Rust, Kotlin and C# can do it with newtypes, by convention. A language where a distinct type costs one line and nothing at run time makes the convention easy, and the compiler's existing refusal of implicit conversions does the rest.

### 6. State, data and errors that can be reasoned about

**Problem:** **managing complex application state**, and failures that are lost or hidden.

**Idea:** a short form for a fallible call that returns early with the error (the `?` of Rust, or `try` of Zig) and works with the multiple results that Mialu has, so no exceptions are needed. Immutable by default values, and matches over unions that the compiler proves complete, which Mialu has, to model a state machine so that an impossible state cannot be written. Structured logging and errors with a trace, from the types.

**Compared:** C has error codes by hand. C++, Java, C# and Python use exceptions, which are invisible in a signature. Kotlin has both. Rust (`Result` and `?`) and Zig (error unions and `try`) show the target. Mialu is already close, because it has multiple results and exhaustive matches, and only the short form is missing.

### 7. Data and files: streams that cannot be forgotten

**Problem:** **file processing** of huge files, background jobs with retries and duplicates, and a database that has to be reached.

**Idea:** a reader that gives a slice at a time from a buffer, so a file of any size is processed in constant memory, closed by `defer`. A database driver and a migration tool as libraries over the C library that exist on every platform, with queries checked against the schema when the program is built.

**Compared:** Java and C# have streams with a garbage collector that hides the cost. Python is easy and slow. Rust and Zig give control, with libraries to find. Checking a query at compile time is available in Rust (`sqlx`) and in few others.

### 8. Tools that run from one file

**Problem:** the setup of a project (a package manager, a bundler, a linter, a formatter, a test runner) is a large part of the cost of web development.

**Idea:** the compiler is also the formatter, the test runner (`mialuc test`, with the tests in the same file), the documentation generator (from `///`) and the package fetcher, and it needs no other program. This is already the way of the toolchain, which has no linker or assembler to install. Reproducible builds, since the compiler is deterministic and has no dependencies.

**Compared:** Rust (`cargo`) and Zig (`zig build`, `zig test`, `zig fmt`) are the standard. Go set it. C and C++ have no standard at all. Java, Kotlin and C# need Maven, Gradle or MSBuild. Python has too many tools. For Mialu this is cheap, and it removes a reason not to try it.

## How Mialu compares today

| | C++ | C# | Java | Kotlin | Python | Rust | Zig | Mialu |
|---|---|---|---|---|---|---|---|---|
| Memory safety by default | No | Yes | Yes | Yes | Yes | Yes | Partly | Partly: bounds and null trap, no heap |
| Generics | Templates | Yes | Erased | Yes | Dynamic | Traits | `comptime` | **No** |
| Concurrency | Threads | Tasks | Threads | Coroutines | GIL, `asyncio` | `Send`, async | Threads | **None** |
| Allocators | Yes | GC | GC | GC | GC | Yes | Explicit | **None** |
| Package manager | Several | NuGet | Maven | Gradle | pip | Cargo | Yes | **None** |
| Runs in the browser | Emscripten | Blazor | No | Kotlin/Wasm | Pyodide | Wasm | Wasm | Wasm, no runtime |
| Standard library | Large | Large | Large | Large | Large | Medium | Medium | **4 files** |
| Native platforms | Many | Many | Many | Many | Many | Many | Many | **One** |
| Toolchain to install | Large | Large | Large | Large | Medium | Medium | One file | One file |
| Compiler is tested in CI | Yes | Yes | Yes | Yes | Yes | Yes | Yes | **No** |

Mialu leads in the size and simplicity of its toolchain and in the quality of its specification and its error messages. It trails in everything that a program of real size needs. The way to be worth using is not to match the standard library of a language that is thirty years old, but to be the language where the features above (regions, plain generics, structured tasks, one source for both sides of the web) are small and fit together.

## Milestones

Each milestone has an exit criterion that can be checked, and its steps are in the order to do them. A milestone is done when its criterion holds, not when its steps are written.

### M1: Trust the compiler (0.6)

*Exit criterion:* every change is tested by CI, and a wrong code generation fails a build.

1. Write a test runner script that builds each program of `test-code/x64` and runs it, and checks its exit code and output against an expected file next to it.
2. Run each `test-code/wasm` program with Node and its `.client.js`, with no browser.
3. Add a test for each rule that the specification states: every error code has one (`scripts/verify_errors.py`, `test-code/errors`), but a code may cover more than one rule.
4. Run the grammar check, both runners and the error tests on every push and pull request, not only on a release tag.
5. Add the tests that today's examples lack: integer wrap-around, deep expressions at the limit, empty inputs, and the cases of the limits in the CLI document.
6. Run the compiler under an address sanitizer and a fuzzer on the tokenizer and parser, and fix what they find.

### M2: Memory and data (0.7)

*Exit criterion:* a program can build and grow a list, and a string, at run time, give the memory back, and no pointer outlives its arena in a checked program.

1. Choose the allocator design and write it down in the specification: an arena and a general allocator in the standard library, over `VirtualAlloc` on x64-win32 and linear memory on WebAssembly, and the rule for the lifetime of a pointer to arena memory.
2. Implement `mu:memory` with `allocate`, `free`, `arena_make` and `arena_reset`, and a variant that reports failure as a value and not a trap.
3. Make `[dynamic]` use an allocator, so that it can outlive a procedure.
4. Allow arrays as parameters, as results and as fields of structs, and lift the restriction on the types of global variables.
5. Enable `I8`, `I16` and `U16`, with the sizes in memory that their names say, and a remainder operator.
6. Write the text tools: join and the text of a number at run time, a builder, find, split, trim, and the bytes of a `Str`.

### M3: Reuse (0.8)

*Exit criterion:* the standard library has containers that are written once and used for any type.

1. Specify parametric polymorphism: syntax, how a type is inferred, and the errors. The sketch in the specification (`$T`) is the start.
2. Implement it by specializing a procedure per set of types in the semantic analysis, so that both targets need no change.
3. Allow generic structs, so that a container is one definition.
4. Write `mu:array`, `mu:map` and `mu:string` as generic code.
5. Add a short form for returning an error early.
6. Raise the compiler limits that tests or the library reach, and keep the limits documented.

### M4: Beyond Windows (0.9)

*Exit criterion:* the compiler and a compiled program run on Linux x64, with the same tests passing.

1. Separate what the target needs from the system into one small layer: process exit, standard streams, files, memory, sockets, the command line.
2. Add an `x64-linux` target: ELF output, the System V calling convention, system calls or the C library.
3. Make `mu:filesystem` and `mu:process` work on both systems, add `mu:network`, and WebAssembly where it can.
4. Build the compiler itself on Linux and macOS, and run the CI of M1 on all of them.
5. Package a release for each platform.

### M5: Concurrency and the web (0.10)

*Exit criterion:* a web service that answers many clients at once, with the same types shared with a Wasm client, runs for a week.

1. Design tasks, channels and cancellation, and the rule for sending a pointer, and write it down.
2. Rewrite `mialudoc` to answer many connections, and keep the list of its `MISSING:` comments as the list of what is left.
3. Extend reflection to structs, and generate serialization from it, so that the server and the browser share a type.
4. Add a typed library for the DOM and `fetch`.
5. Add the types for untrusted text and secrets.

### M6: Real programs (0.11)

*Exit criterion:* a program of several thousand lines, written in Mialu and not by the authors of the compiler's own tests, builds, runs and is maintained for a month.

1. Write one real application in Mialu. The best candidate is a part of the compiler itself, starting with the tokenizer.
2. Keep a list of every workaround that it needed, and turn each into a language, library or compiler change.
3. Add a project build file and a way to name and fetch dependencies, with versions.
4. Improve the speed of the generated code with register allocation and the removal of redundant moves, measured against C on a set of benchmarks, now that tests keep it correct.
5. Add a source formatter, a test command and a linter, so that a project has one style.

### M7: Mialu 1.0

*Exit criterion:* the language and the standard library are stable, and someone else can depend on them.

1. Freeze the language specification and write a policy for breaking changes, deprecation and versions of the standard library.
2. Have a security review of the compiler and of the standard library, and fix what it finds.
3. Keep every error message of the compiler documented and tested, as [Error codes](error-codes.md) does today.
4. Publish a language tour, a book-length guide and a reference of the standard library, generated from the `///` comments.
5. Release with a long-term support promise for one version, and a place to report problems and a process for answering them.

## Summary

| Area | State |
|---|---|
| Language design and specification | Strong, for the size of the language |
| Evidence that the compiler is correct | Weak: examples compile, nothing runs them in CI |
| Targets | One native platform and WebAssembly |
| Memory, generics, concurrency | Missing |
| Standard library | Minimal |
| Web development | A Wasm target and a playground, no networking library |
| Stability | Pre-1.0, nothing promised |

Mialu is best called alpha. The first things to change that are tests that run (M1), an allocator with checked lifetimes (M2) and generics (M3). The things that would make it worth choosing over the languages above are structured concurrency and one source for the server and the browser (M5).
