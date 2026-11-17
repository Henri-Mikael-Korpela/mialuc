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
dump-struct-sizes
            print the size and the alignment of each struct of a source file
dump-types  print the type of each constant, variable and parameter of a source file
run         compile a source file and run the program
```

Use `mialuc --version` to print the version of the compiler.

A command takes exactly one source file, and reads it as a whole. A command that is given anything else, or that is not known, prints its usage to the standard error and ends with exit status 2, without reading any file. A command checks the program in a fixed order, and stops at the first error: the tokens, the syntax, the names, and then, for the commands that build, what the target can build. See [Errors](#errors).

See the [language specification](mialu-programming-language.md) for the language itself, and [Getting Started](getting-started.md) for how to build the compiler.

## Compile a program and write it

Usage:

```
mialuc build --target <target> [--debug] -o <output file> <source file>
mialuc build --target <target> [--debug] <output file> <source file>
```

Build compiles the source file for the target, and writes the program to the output file. It writes nothing to the standard output, and does not create the output file when the program has an error.

The `--target` flag says what to write. It is required, and is one of:

```
wasm        a WebAssembly binary module
wat         the same module in the WebAssembly text format
x64-win32   an executable file for Windows on x64
```

See [Targets](#targets).

The `--debug` flag, right after the target, adds debug information to the program, so that it can be debugged with gdb, also from VS Code, see [Debugging](#debugging). Only the `x64-win32` target has it, and the flag is a usage error with another target.

The `-o` flag says where to write the program. The output file can also be written without it, directly after the target, which is how it was written before there was a flag for it. Either form replaces a file that is already there.

For example:

```
mialuc build --target x64-win32 -o hello.exe test-code/x64/foreign-functions-libc.mu
mialuc build --target x64-win32 --debug -o hello.exe test-code/x64/foreign-functions-libc.mu
mialuc build --target wasm -o loops.wasm test-code/wasm/loops.mu
mialuc build --target wat -o loops.wat test-code/wasm/loops.mu
```

### Debugging

A program that is built with `--debug` has the same code as one built without it, and runs the same, but it also has sections that gdb reads, also from VS Code. See [Debugging](build-target-x64-win32.md#debugging) of the x64-win32 target, and the VS Code extension in [editors/vscode-debug](../editors/vscode-debug/README.md).

## Compile a program and print its assembly

Usage:

```
mialuc dump-asm --target <target> <source file>
```

Dump-asm compiles the source file for the target, and prints the code of the program to the standard output as assembly. The `--target` flag is required, and for now the only target that it takes is `x64-win32`. To see what the `wasm` target writes, build the `wat` target.

The assembly is made from the executable file that the `x64-win32` target writes. Its format is described in [Assembly](build-target-x64-win32.md#assembly).

## Compile a program and print its syntax tree

Usage:

```
mialuc dump-ast <source file>
```

Dump-ast reads the source file, and prints what the front end made of it to the standard output, which is for people who work on the language or on its tools. First come the tokens, one on each line, and they include the comments, which the parser leaves out. Then comes an empty line, and then the syntax tree, which starts with its root and has one node on each line, indented by its depth. Then the names are resolved, and a name that is not defined is an error.

The tokens are printed before the source is parsed, and the tree before the names are resolved, so a program with a syntax error still shows its tokens, and a program with an unknown name still shows its tree.

Dump-ast does not build the program, so it does not depend on a target, and it also shows programs that no target can build.

## Print the sizes of the structs

Usage:

```
mialuc dump-struct-sizes <source file>
```

Dump-struct-sizes reads the source file, checks it up to the names, as dump-ast does, and prints a line for each struct that is defined as a constant of the module, in the order of the source. A line has four fields separated by tabs: the line of the definition, counted from 1, the name of the struct, its size in bytes, and its alignment in bytes. For `test-code/x64/structs.mu`:

```
23	Person	24	8
28	Point	16	8
32	Line	32	8
36	Info	24	8
42	Node	16	8
```

The sizes are those of the `x64-win32` target, see [Structs](build-target-x64-win32.md#structs): every field is made of whole words of 8 bytes, a `Str` is two words, and a struct has no padding, so the alignment is always 8. A struct that is defined where a field is has no name, and is not printed, but it is part of the size of the struct that has it. The command does not build the program, so it also shows programs that the target cannot build. It is what the [language server](../editors/vscode-lsp/README.md) shows above the structs in an editor.

## Print the types of the names

Usage:

```
mialuc dump-types <source file>
```

Dump-types reads the source file, checks it up to the names, as dump-ast does, and prints a line for each place where a constant, a global variable, a parameter or a variable is defined or used, in the order of the nodes of the syntax tree. A line has five fields separated by tabs: the line and the column of the name, both counted from 1 with columns that count bytes, the length of the name in bytes, the name, and its type. The iterator of a for statement over an array or a slice has a sixth field, the array or slice that it is an element of, written as it is declared, such as `people: []Person`, or only its type when it is not a name. For a few lines of `test-code/x64/conditions.mu`:

```
14	1	5	LIMIT	untyped Int
22	1	4	sign	proc(n: Int) Int
31	5	6	result	Int
```

The type is written as it is in the source when the definition gives one, as for a parameter or `LIMIT : Byte : 100`. Otherwise it is the type of the value: a constant that is known when the program is compiled, such as `LIMIT :: 100`, which is `untyped Int` for a constant and `Int` for a variable that is given it, a struct literal, another name, arithmetic of them, or a call of a procedure that returns one value. A procedure shows its signature. A member of an enum has the enum as its type, where it is defined, in `Color.RED` and in `.RED`, whose enum is not written, so it is shown only when exactly one enum has a member of that name. A field of a struct has the type that the struct gives it, where it is defined, where a struct literal gives it a value, and where it is read, as in `p.age`, `p^.age` and `line.end.x`, which need the type of what is read from to be known, and may be behind a pointer. A field that is a struct of its own, written in place, has no type that can be written, and shows nothing. An array whose length is left to its elements, as in `a := [const]Int { 1, 2, 3 }`, has the length that the compiler counted, which is known when the program is compiled, so its type is `[3]Int`. An array that is given its length, `[3]Int`, and a slice, `[]Int`, are written as they are, and an element of an array has the type of the elements. A range of an array or of a slice, as in `a[1:3]`, is a slice of the same elements, `[]Int`, whose length is known only when the program runs, so it has none. The iterator of a for statement has the type of the elements when the statement goes over an array or a slice, as `person` has `Person` in `for person in people`, and `Int` when it goes over a range, as in `for i in 0..<3`. A place whose type is not known this way, and the structs and enums, which are types and not values, are left out. The command does not build the program, so it also shows programs that the target cannot build. It is what the [language server](../editors/vscode-lsp/README.md) shows when the pointer rests on a name in an editor.

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

What a target builds, and how, is in [Build target: WebAssembly](build-target-wasm.md) and [Build target: x64-win32](build-target-x64-win32.md), and the [language specification](mialu-programming-language.md#compilation-targets) links to them. The programs in `test-code/wasm` are for the first two, and the ones in `test-code/x64` for the third.

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
nested constant expression     1024 operators and constants
nested expression (x64-win32)  1024 operators and operands
frame (x64-win32)              512 KiB for a procedure, half of the stack of 1 MiB
```

A source file or a program that is larger is an error that says so.
