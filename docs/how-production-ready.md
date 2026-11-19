# How production ready is Mialu?

## Goals

- A procedural Turing-complete language with similar low-level control as in C with conventient yet powerful structures built in to allow for large scale application development.

## Verdict

Mialu is **not production ready**, and it is early in its life: version 0.4.0, first commit on 2026-10-31. It is a well-specified, carefully built language with a working compiler that is ready for learning, experiments, small tools and its own scripts (the release scripts are programs of Mialu). It cannot yet be used for the "large scale application development" that the goals name. The gaps are not polish. Three of them are fundamental: the language has no way to allocate memory, no generic code, and the native target is one platform.

## What it is today

- A front end (tokenizer, parser, semantic analysis, constant evaluation) of about 7,000 lines of C, and two back ends that write machine output directly: `x64-win32` (a Windows executable with its own assembler and DWARF debug information for gdb) and `wasm`/`wat` (WebAssembly binary and text).
- A specification of about 2,600 lines, a grammar in EBNF that is verified against every example, and documents for the command line and both targets.
- About 90 programs in `test-code` that serve as tests, a browser playground, and VS Code extensions for highlighting, debugging and a language server.
- A small standard library, written in Mialu: `filesystem`, `process`, `math` and `version`, about 400 lines in all.

## Feature checklist

The impact column says how much the feature matters for production readiness: **Critical** (a program of real size cannot do without it), **High**, **Medium** or **Low**. A checked box means the feature is present and documented, not that it is complete.

### Language

| | Feature | Impact | Notes |
|---|---|---|---|
| [x] | Static types, no implicit conversions | High | `Int` is never a `Bool` or a `Byte` without a cast. Removes a large class of C bugs. |
| [x] | Structs, enums, unions with compile-time checked match | High | A field of a union case can only be read where the compiler can prove the case. |
| [x] | Procedures, multiple return values, pointers to procedures | High | Enough for error returns such as `(Int, File_Error)`. |
| [x] | Control flow: if, match (exhaustive for enums), for, labels, break, continue | High | Exhaustive matching catches forgotten cases. |
| [x] | `defer` | High | The clean-up tool for resources, compiled in at each exit. |
| [x] | Checked array and slice bounds | High | Out-of-bounds access traps instead of corrupting memory. |
| [x] | Null checks on pointer use, no pointer arithmetic | High | `nil^` traps. Safer than C by design. |
| [x] | Packages and imports of source files | High | The base for code reuse, but there is no package manager. |
| [x] | Foreign procedures: JavaScript, the C library, DLLs of Windows | Critical | Makes the ecosystem of the system reachable, `malloc` included, in principle. |
| [x] | Strings as UTF-8 views, runes, iteration, slicing, `++` | Medium | Good text model, thin operations. |
| [x] | Checked conversions (`int_to_byte`, `try_str_to_int`) | Medium | Arithmetic itself wraps. |
| [x] | Floats (`F32`, `F64`) with defined behaviour | Medium | IEEE results, no traps. |
| [x] | Constants and compile-time expressions | Medium | Strings joined at compile time. |
| [x] | Reflection for enums and procedures | Low | `type_info_of`. |
| [x] | Attributes, documentation comments | Low | `///` comments reach the editor tooling. |

### Compiler and targets

| | Feature | Impact | Notes |
|---|---|---|---|
| [x] | Native x64 executable for Windows, no linker or assembler needed | High | Zero-dependency toolchain. |
| [x] | WebAssembly target, with a playground | High | A second platform, in the browser. |
| [x] | Debug information (DWARF for gdb), VS Code debugging | High | Rare at this age of a language. |
| [x] | Compiler with fixed memory use and documented limits | Medium | Predictable, but the limits (65,536 tokens, 1 MiB source) rule out big programs. |
| [x] | Errors with line and column, internal errors told apart | Medium | |
| [x] | Versioning (`VERSION`, `--version`) and a verified release workflow | Medium | Tagged, packaged releases for Windows. |
| [x] | `run` command, arguments passed through | Low | |

### Tooling and documentation

| | Feature | Impact | Notes |
|---|---|---|---|
| [x] | Complete specification, grammar verified against the examples | High | Unusually strong for the age of the project. |
| [x] | Editor support: highlighting, language server, debugger | High | |
| [x] | Getting-started guide, examples, playground | Medium | |
| [x] | Standard library (filesystem, process, math, version) | Medium | Present, but tiny. |

## What is missing, by impact

### Critical

- **No memory allocation.** The language has manual memory management, but there is no allocator, no heap and no standard way to get one. Everything lives in frames of at most 512 KiB, in a 64 KiB temporary storage or in global variables of a few simple types, and a pointer to a local does not outlive its procedure. Without growable data (dynamic arrays, hash maps, strings built at run time) most real programs cannot be written. Calling `malloc` through `foreign libc` is the only route, and the standard library does not offer it.
- **No generic code.** Parametric polymorphism is only a possible future feature. Every container would have to be written again for each type.
- **Restrictions on values and types.** A procedure cannot take or return an array, a global variable can only be an `Int`, `Bool`, `Byte`, `Str` or enum, and the sized integer types `I8` to `U64` are partly reserved.
- **No automated tests in CI.** The release workflow checks that the examples match the grammar and compile, but nothing runs a program and compares its output, and nothing checks the errors of the compiler. The WebAssembly examples have `.client.js` files that could be run with Node, but nothing does. A compiler of 40,000 lines of C that is not tested on every change will produce wrong code sooner or later.
- **One native platform.** `x64-win32` is the only native target. There is no Linux, macOS or ARM executable.

### High

- Fixed compiler limits: 1 MiB of source, 65,536 tokens and nodes, 1,024 procedures, 1 MiB for the program.
- No optimizer: the code is a direct translation that keeps values on a stack in memory.
- No package manager, no versioned dependencies, no visibility rules and no project build file.
- Standard library too small: no collections, formatting, time, random numbers, networking, directories, string search, split or trim. What exists works on Windows only (`mu:filesystem` rejects WebAssembly).
- No concurrency: no threads, atomics or async.
- No stability promise: version 0.x, and a specification that changes with the code.

### Medium

- No helper for error handling beyond returning values, so code with many fallible calls is long.
- Arithmetic on `Int` wraps silently, with no checked or saturating form.
- No separate compilation: every build compiles the program and everything that it imports.
- No sanitizers, profiler or coverage. Debugging needs gdb on Windows.
- No security review of a compiler in C that reads untrusted source.

### Low

- No source formatter, documentation generator or linter.
- One example program in `examples`.

## Milestones

Each milestone has an exit criterion that can be checked, and its steps are in the order to do them. A milestone is done when its criterion holds, not when its steps are written.

### M1: Trust the compiler (the next release, 0.5)

*Exit criterion:* every change is tested by CI, and a wrong code generation fails a build.

1. Write a test runner script that builds each program of `test-code/x64` and runs it, and checks its exit code and output against an expected file next to it.
2. Run each `test-code/wasm` program with Node and its `.client.js`, with no browser.
3. Add negative tests: programs that must be rejected, with the expected error message, line and column, for each rule that the specification states.
4. Run the grammar check, both runners and the negative tests on every push and pull request, not only on a release tag.
5. Add the tests that today's examples lack: integer wrap-around, deep expressions at the limit, empty inputs, and the cases of the limits in the CLI document.
6. Run the compiler under an address sanitizer and a fuzzer on the tokenizer and parser, and fix what they find.

### M2: Memory and data (0.6)

*Exit criterion:* a program can build and grow a list, and a string, at run time, and give the memory back.

1. Choose the allocator design and write it down in the specification: an arena and a general allocator in the standard library, over `VirtualAlloc` on x64-win32 and linear memory on WebAssembly.
2. Implement `mu:memory` with `allocate`, `free`, `arena_make` and `arena_reset`, and a variant that reports failure as a value and not a trap.
3. Allow arrays as parameters, as results and as fields of structs, and lift the restriction on the types of global variables.
4. Enable the sized integer types `I8` to `U64` fully on both targets, with conversions between them.
5. Write a string builder and a growable array of `Int` by hand as a test of the allocator, and run both under the tests of M1, including out-of-memory.

### M3: Reuse (0.7)

*Exit criterion:* the standard library has containers that are written once and used for any type.

1. Specify parametric polymorphism: syntax, how a type is inferred, and the errors. The sketch in the specification (`$T`) is the start.
2. Implement it by specializing a procedure per set of types, in the semantic analysis, so that both targets need no change.
3. Allow generic structs, so that a container is one definition.
4. Write `mu:array` (a dynamic array), `mu:map` (a hash map) and `mu:string` (builder, find, split, trim) as generic code.
5. Raise the compiler limits that tests or the library reach, and keep the limits documented.

### M4: Beyond Windows (0.8)

*Exit criterion:* the compiler and a compiled program run on Linux x64, with the same tests passing.

1. Separate what the target needs from the system into one small layer: process exit, standard streams, files, memory, the command line.
2. Add a `x64-linux` target: ELF output, the System V calling convention, system calls or the C library.
3. Make `mu:filesystem` and `mu:process` work on both systems, and WebAssembly where it can.
4. Build the compiler itself on Linux and macOS, and run the CI of M1 on all of them.
5. Package a release for each platform.

### M5: Real programs (0.9)

*Exit criterion:* a program of several thousand lines, written in Mialu and not by the authors of the compiler's own tests, builds, runs and is maintained for a month.

1. Write one real application in Mialu. The best candidate is a part of the compiler itself, starting with the tokenizer, or the tools of `scripts`.
2. Keep a list of every workaround that it needed, and turn each into a language, library or compiler change.
3. Add a project build file and a way to name and fetch dependencies, with versions.
4. Add threads or async if the application needs them, and otherwise write down that they are not planned.
5. Improve the speed of the generated code with register allocation and the removal of redundant moves, measured against C on a set of benchmarks, now that tests keep it correct.
6. Add a source formatter and a linter, so that a project has one style.

### M6: Mialu 1.0

*Exit criterion:* the language and the standard library are stable, and someone else can depend on them.

1. Freeze the language specification and write a policy for breaking changes, deprecation and versions of the standard library.
2. Have a security review of the compiler and of the standard library, and fix what it finds.
3. Document every error message of the compiler, and test each of them.
4. Publish a language tour, a book-length guide and a reference of the standard library, generated from the `///` comments.
5. Release with a long-term support promise for one version, and a place to report problems and a process for answering them.

## Summary

| Area | State |
|---|---|
| Language design and specification | Strong, for the size of the language |
| Evidence that the compiler is correct | Weak: examples compile, nothing runs them in CI |
| Targets | One native platform and WebAssembly |
| Memory and data structures | Missing: no allocator, no generics |
| Standard library | Minimal |
| Tooling and editor support | Good for a new language |
| Stability | Pre-1.0, nothing promised |

Mialu is best called alpha. The first things to change that are tests that run (M1), an allocator (M2) and generics (M3).
