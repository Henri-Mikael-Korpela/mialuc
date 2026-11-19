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

## E0101 - E0199: the tokens

These are found first, when the source is split into tokens. See the [specification](mialu-programming-language.md) for the rules of [comments](mialu-programming-language.md#comments), [indentation](mialu-programming-language.md#lines-spaces-and-indentation), [integers](mialu-programming-language.md#integers), [floating-point numbers](mialu-programming-language.md#floating-point-numbers), [characters](mialu-programming-language.md#characters) and [strings](mialu-programming-language.md#strings).

| Code | Title | When |
|---|---|---|
| E0101 | a comment must be valid UTF-8 | A comment has a byte that is not valid UTF-8. |
| E0102 | a documentation comment must be on lines of its own | A `///` comes after code on its line. Use `//` there. |
| E0103 | indentation must use spaces, not tabs | A line is indented with a tab. |
| E0104 | indentation must be a multiple of 4 spaces | A line is indented by a number of spaces that is not a multiple of four. |
| E0105 | 'c' is not a digit of a float literal | A character after the dot of a float literal is not a digit, such as the `f` of `1.5f`. There is no exponent and no suffix. |
| E0106 | a digit separator '_' must be between two digits | A `_` of a float literal is not between two digits, as in `1.5_`. |
| E0107 | float literal is larger than the largest F64 | The value of a float literal is above about 1.8e308. A float literal has at most 128 characters, so this one cannot be made. |
| E0108 | float literal has more than 128 characters | |
| E0109 | 'c' is not a digit of an integer literal | A character is not a digit of its base: `0b102`, `0xfg` or `12a`. |
| E0110 | integer literal has no digits after its prefix | A `0b` or a `0x` without digits. |
| E0111 | a digit separator '_' must be between two digits | A `_` of an integer literal is not between two digits, as in `1__000`, `1_` or `0x_FF`. |
| E0112 | integer literal is larger than the largest Int | The value is above 9223372036854775807. |
| E0113 | a label cannot be a reserved word | A `:` directly followed by a reserved word. |
| E0114 | a rune literal has no character | `''`. |
| E0115 | this is not an escape sequence of a rune | A backslash in a rune that does not make an escape sequence, such as `'\q'`, or one with a number that is not a Unicode character. |
| E0116 | the character of a rune must be valid UTF-8 | |
| E0117 | a rune literal has more than one character | `'ab'`. Write more than one character as a string. |
| E0118 | a rune literal has no closing quote | The rune does not end on its line. |
| E0119 | a string cannot contain a backslash | Escape sequences are not supported in strings yet, and the backslash is kept for them. |
| E0120 | a string must be valid UTF-8 | |
| E0121 | string has no closing quote | The string does not end on its line. |
| E0122 | more than 65536 tokens | The compiler reads at most 65536 tokens of a file. |
| E0123 | unexpected character 'c' | A character that cannot start a token. |
