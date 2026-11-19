# Error codes

Every error of the compiler has a code of its own, `E` and four digits, such as `E0101`. The code is printed in the first line of the error, see [Errors](mialu-cli.md#errors) for how an error is laid out. A code never changes and is never used again for another error, so it can be searched for, and tools can tell the errors apart. Every code has its own text, and an error that has more to tell than its title also has a note, which says why the rule is as it is, and a help, which says what to do.

The text of the errors is all in [report.c](../src/report.c), where each code is an entry of the catalog, and the codes are in [report.h](../src/report.h). The codes are in ranges, one for each part of the compiler:

| Range | What the errors are about |
|---|---|
| E0001 - E0099 | the command, the files and the system |

## E0001 - E0099: the command, the files and the system

These errors have no place in a source: they are about a file as a whole, or about the system.

| Code | Title | When |
|---|---|---|
| E0001 | cannot read the source file | The file does not exist, cannot be opened, or is larger than 1 MiB. |
| E0002 | cannot write the output file | The output file of `build` cannot be written, for example because its directory does not exist. |
| E0003 | cannot find the absolute path of the source file | A `--debug` build names the source file by its absolute path, and the system could not resolve it. |
| E0004 | out of memory | The system did not give the compiler the memory that it allocates when it starts. |
| E0005 | cannot write the assembly to the standard output | `dump-asm` could not write to the standard output. |
| E0006 | cannot write the program to the directory for temporary files | `run` writes the program to a file there, and could not. |
| E0007 | the system cannot start the program | `run` made the file, but the system refused to start it. |
| E0008 | the arguments of the program are longer than the system takes | The command line of the program, with its name, is longer than 32767 characters. |
| E0009 | running a program is only supported on Windows | `run` builds for the `x64-win32` target only. Build the `wasm` target to run a program elsewhere, see [Playground](playground.md). |
