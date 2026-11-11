# mialuc

Mialuc is a tool for compiling and running Mialu source code.

Usage:

```
mialuc <command> [arguments]
```

The commands are:

```
build       compile a source file and write a program for a target
dump-asm    compile a source file and print the assembly of the program
dump-ast    print the tokens and the syntax tree of a source file
run         compile a source file and run the program
```

Use `mialuc --version` to print the version of the compiler.

A command takes exactly one source file, and reads it as a whole. A command that is given anything else, or that is not known, prints its usage to the standard error and ends with exit status 2, without reading any file. A command checks the program in a fixed order, and stops at the first error: the tokens, the syntax, the names, and then, for the commands that build, what the target can build. See [Errors](#errors).

See the [language specification](mialu-programming-language.md) for the language itself, and [Getting Started](getting-started.md) for how to build the compiler.

## Compile a program and write it

Usage:

```
mialuc build --target <target> -o <output file> <source file>
mialuc build --target <target> <output file> <source file>
```

Build compiles the source file for the target, and writes the program to the output file. It writes nothing to the standard output, and does not create the output file when the program has an error.

The `--target` flag says what to write. It is required, and is one of:

```
wasm        a WebAssembly binary module
wat         the same module in the WebAssembly text format
x64-win32   an executable file for Windows on x64
```

See [Targets](#targets).

The `-o` flag says where to write the program. The output file can also be written without it, directly after the target, which is how it was written before there was a flag for it. Either form replaces a file that is already there.

For example:

```
mialuc build --target x64-win32 -o hello.exe test-code/x64/foreign-functions-libc.mu
mialuc build --target wasm -o loops.wasm test-code/wasm/loops.mu
mialuc build --target wat -o loops.wat test-code/wasm/loops.mu
```

## Compile a program and print its assembly

Usage:

```
mialuc dump-asm --target <target> <source file>
```

Dump-asm compiles the source file for the target, and prints the code of the program to the standard output as assembly. The `--target` flag is required, and for now the only target that it takes is `x64-win32`. To see what the `wasm` target writes, build the `wat` target.

The assembly is made from the executable file that `mialuc build --target x64-win32` would write, and not from the syntax tree, so it shows exactly what runs. It has a label for the start of the program, `.start`, and one for each procedure, `.name`, and a comment names what an instruction that is relative to the instruction pointer refers to, such as the imports of the C library:

```
.start:
    sub %rsp, 40
    call .main
    mov %ecx, %eax
    call [%rip - 73681]:u64  // msvcrt.dll: exit
    int3
```

The format is the one of the [inline assembly of the Odin programming language](https://odin-lang.org/docs/inline-asm/):

- One instruction is on each line, with the operand that is written to first: `mov %rbp, %rsp` copies `rsp` to `rbp`.
- A register is written with a `%` in front of it, as in `%rax` and `%r10`.
- A number is written as an integer. A large number, such as the bytes of a string, is written in hexadecimal.
- A memory operand is `[base + displacement]` or `[base - displacement]`, and the displacement is left out when it is zero.
- A label is written `.name:` where it is, and `.name` where it is used. A jump over a breakpoint that checks whether a string fits has a label that is `.skip_` and a number, which is where the jump goes to in the code.
- A comment starts with `//`.

Odin does not show a memory operand that is relative to the instruction pointer, so it is written `[%rip + displacement]`, and the call of an import has the size of an address after it, `[%rip + displacement]:u64`, like the size of a memory operand in Odin, `[base]:T`.

## Compile a program and print its syntax tree

Usage:

```
mialuc dump-ast <source file>
```

Dump-ast reads the source file, and prints what the front end made of it to the standard output, which is for people who work on the language or on its tools. First come the tokens, one on each line, and they include the comments, which the parser leaves out. Then comes an empty line, and then the syntax tree, which starts with its root and has one node on each line, indented by its depth. Then the names are resolved, and a name that is not defined is an error.

The tokens are printed before the source is parsed, and the tree before the names are resolved, so a program with a syntax error still shows its tokens, and a program with an unknown name still shows its tree.

Dump-ast does not build the program, so it does not depend on a target, and it also shows programs that no target can build.

## Compile a program and run it

Usage:

```
mialuc run <source file>
```

Run compiles the source file for the machine that it is run on, and runs the program, which has the standard input, output and error of the compiler. The compiler waits for the program to end, and then ends with the exit status of the program, which is the result of its procedure `main`. The compiler itself prints nothing.

Windows on x64 is the only machine that can run a program for now, so run builds for the `x64-win32` target. On another system it ends with an error that says that running a program is only supported on Windows. Build the `wasm` target to run a program elsewhere, see [Playground](playground.md).

The program is written to a file in the directory for temporary files, and the file is deleted when the program ends. Nothing of the program stays in the compiler, so whatever the program does can only harm itself and the system.

For example:

```
mialuc run test-code/x64/foreign-functions-libc.mu
```

Because the exit status of the program is the exit status of the compiler, a status of 1 or 2 can also be the status of the program, and not an error of the compiler. The compiler prints a message for its own errors.

## Print the version of the compiler

Usage:

```
mialuc --version
```

Prints the name of the compiler and its version, which is a semantic version, such as:

```
mialuc 0.2.0
```

The flag works only on its own. The version is defined in `src/version.h`, and `scripts/bump-version.ps1` sets it, see [Tooling](tooling.md#versioning).

## Targets

A target says what program the compiler writes, and what the program can do. The commands that take one say it with `--target`.

```
wasm        a WebAssembly binary module (.wasm)
wat         the same module in the WebAssembly text format (.wat)
x64-win32   an executable file for Windows on x64 (a PE32+ file for the console)
```

The `wasm` and `wat` targets import their foreign procedures from JavaScript, which are the ones of a `foreign js` block, and they need nothing but the compiler. The `x64-win32` target imports them from the C library, `msvcrt.dll`, which are the ones of a `foreign libc` block, and the file that it writes needs nothing but Windows. A target rejects what it cannot build, and does not write a program that is built wrongly.

Which commands take which target:

```
            wasm   wat    x64-win32
build       yes    yes    yes
dump-asm    no     no     yes
run         no     no     yes
```

What a target builds, and how, is in the [language specification](mialu-programming-language.md#compilation-targets). The programs in `test-code/wasm` are for the first two, and the ones in `test-code/x64` for the third.

## Exit status

```
0   the command did what it was asked to
1   an error: the source is not valid or cannot be built for the target, a file cannot be
    read or written, or a program cannot be started
2   the command line is not valid, and the usage is printed
```

The exception is `run`, which ends with the exit status of the program when it started it, see [Compile a program and run it](#compile-a-program-and-run-it).

## Errors

An error is printed to the standard error, and has the form:

```
<source file>:<line>:<column>: <message>
```

The line and the column are counted from 1, and they point at the place in the source that the error is about. An error that is about the whole program, such as a program that has no procedure `main`, or one that is too large, has no line and column: `<source file>: <message>`. The compiler stops at the first error.

An error that says `internal error` is about the compiler and not about the program, because the compiler cannot read what it wrote itself. It should be reported as a bug.

## Limits

The compiler allocates what it needs once, when it starts, so that its use of memory is known, and the size of what it reads and writes is limited:

```
source file                    1 MiB
tokens                         65536
syntax tree nodes              65536
program                        1 MiB
assembly or WebAssembly text   16 MiB
procedures (x64-win32)         1024
```

A source file or a program that is larger is an error that says so.
