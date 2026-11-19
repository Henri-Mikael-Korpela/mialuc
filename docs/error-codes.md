# Error codes

Every error of the compiler has a code of its own, `E` and four digits, such as `E0101`. The code is printed in the first line of the error, see [Errors](mialu-cli.md#errors) for how an error is laid out. A code never changes and is never used again for another error, so it can be searched for, and tools can tell the errors apart. Every code has its own text, and an error that has more to tell than its title also has a note, which says why the rule is as it is, and a help, which says what to do.

The text of the errors is all in [report.c](../src/report.c), where each code is an entry of the catalog, and the codes are in [report.h](../src/report.h). The codes are in ranges, one for each part of the compiler:

| Range | What the errors are about |
|---|---|
| E0001 - E0099 | the command, the files and the system |
| E0101 - E0199 | the tokens: comments, indentation, numbers, runes and strings |
| E0201 - E0299 | the syntax |
| E0301 - E0399 | the files that a program imports |
| E0401 - E0449 | names, attributes, labels and the statements that jump |
| E0451 - E0499 | what is known when the program is compiled: constants and array lengths |

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

## E0201 - E0299: the syntax

These are found when the tokens are put together into the syntax tree, see the [grammar](../grammar.ebnf). The compiler stops at the first one, so the syntax error is the first place where the source does not fit the grammar, which is not always the place of the mistake: a missing operand is found at the next token.

| Code | Title | When |
|---|---|---|
| E0201 | more than 65536 syntax tree nodes | The program has more nodes than the compiler allocates. |
| E0202 | unexpected `token` | The token is not what the grammar takes there. The label says what it takes. At the end of a line the token is the first one of the next line, and at the end of the file it is `end of the file`. |

## E0301 - E0399: the files that a program imports

These are found when the files that `import` names are read and put together, see [Imports](mialu-programming-language.md#imports). An error in an imported file, which does not tokenize or parse, is told by the code of that error, with the path and the lines of the file that has it.

| Code | Title | When |
|---|---|---|
| E0301 | the import 'path' is a loop | The file imports itself, directly or through the files that it imports. |
| E0302 | cannot read the file of the import 'path' | The file is not in the directory `lib` next to the compiler or in `src/mialu-std` of the project that the compiler was built from, or it is larger than a source can be. |
| E0303 | the import 'path' is not a path of a library file | The text of an import is not a vendor, a colon and a path of names and slashes, such as `"mu:filesystem"`. |
| E0304 | the imports of 'path' are more than the program can put together | At most 32 files, 8 imports below each other, and 262144 bytes of names. |
| E0305 | 'name' has the name of a definition of the same file | A parameter, a variable or an iterator of an imported file has the name of a definition of that file, which it would hide. |
| E0306 | the vendor of the import 'path' is not known | The text before the colon is not `mu`, which is the standard library of Mialu. |

## E0401 - E0449: names, attributes, labels and the statements that jump

These are found when the names of the program are resolved, see [Scope resolution](mialu-programming-language.md#scope-resolution), [Attributes](mialu-programming-language.md#attributes), [Labels and scopes](mialu-programming-language.md#labels-and-scopes) and [Defer](mialu-programming-language.md#defer).

| Code | Title | When |
|---|---|---|
| E0401 | 'name' is already defined | Two definitions of the module have the same name. |
| E0402 | cannot find 'name' in this scope | A name is not a variable or a parameter of the procedure, a constant or a global variable. |
| E0403 | parameter 'name' is declared twice | |
| E0404 | parameter 'name' has the name of a constant or a global variable | The parameter would hide it. |
| E0405 | variable 'name' is defined twice | In the same scope. |
| E0406 | variable 'name' has the name of a constant, a global variable or a parameter | The variable would hide it. |
| E0407 | the struct already has a field called 'name' | |
| E0408 | the enum has the member 'name' twice | |
| E0409 | 'name' is not a variable, so it cannot be assigned to | A parameter or a constant is assigned to. |
| E0410 | 'name' is the iterator of a for loop, so it cannot be assigned to | |
| E0411 | 'name' is not a procedure, so it cannot be called | |
| E0412 | the attribute 'name' is given twice | |
| E0413 | 'name' is not an attribute of a global variable | The only one is `static`. |
| E0414 | 'name' is not an attribute of a match statement | The only one is `partial`. |
| E0415 | global variable 'name' must have the attribute #(static) | A global variable has static storage duration. |
| E0416 | the initial value of global variable 'name' is not known when the program is compiled | It must be a constant expression, or a field of the information about a type. |
| E0417 | the program already imports foreign procedures from this target | A program has one `import foreign` for each target. |
| E0418 | the foreign procedure 'name' has no 'import foreign' of its target above it | |
| E0419 | 'break' is not inside a loop or a scope | |
| E0420 | 'name' is not inside a loop | A `continue`, or a `break` of a loop, outside one. |
| E0421 | 'name' cannot leave the statement of a 'defer' | A deferred statement runs while its block is being left. |
| E0422 | the label 'name' is already used by a statement around this one | |
| E0423 | 'name' is not the label of a loop, so 'continue' cannot go on with it | |
| E0424 | 'name' is not the label of a statement around this 'break' or 'continue' | |
| E0425 | 'defer' cannot be inside the statement of another 'defer' | |
| E0426 | 'defer' cannot take this statement | It takes an assignment, a call, or an if, for or scope statement. |
| E0427 | 'return' cannot be inside the statement of a 'defer' | |

## E0451 - E0499: what is known when the program is compiled

These are found when the values of [constants](mialu-programming-language.md#constants) are worked out, and when a place needs a value that is known then, such as the length of an array. The place of the error is the name of the definition, or the first name or string of the expression, which is the closest that the syntax tree knows, so the titles that are about an expression say that it is *at* that name.

| Code | Title | When |
|---|---|---|
| E0451 | the constant 'name' is defined in terms of itself | Through any number of other constants. |
| E0452 | the constant expression at 'name' divides by zero | Also for floats, which have an infinity for it in a running program but not in a constant. |
| E0453 | the value of the constant 'name' is not known when the program is compiled | A variable or a call is in the value of a constant. |
| E0454 | an operator of the constant expression at 'name' is given operands that it does not take | Such as `true + 1`, a typed `Byte` in arithmetic, or two strings in a comparison with `<`. |
| E0455 | the constant expression at 'name' has a value that does not fit | An Int overflows, or a float is infinite. Constant arithmetic does not wrap around. |
| E0456 | the strings that '++' joins in the constants of the program, at 'name', are too long together | At most 1 MiB together. |
| E0457 | the constant expression at 'name' has more than 1024 nested operators and constants | |
| E0458 | the constant 'name' has a type that a constant cannot have | A constant is an Int, an I32, an I64, a U32, a U64, a Byte, a Rune, an F32, an F64, a Bool or a Str. |
| E0459 | the value of the constant 'name' is not of the type of the constant | |
| E0460 | the number at 'name' does not fit in a Byte | A Byte is a number from 0 to 255. |
| E0461 | the operands of '++' at 'name' are not known when the program is compiled | `++` joins strings and runes when the program is compiled. |
| E0462 | the length of an array at 'name' is not known when the program is compiled | |
| E0463 | the length of an array at 'name' must be an integer that is not below zero | |
| E0464 | the argument of 'int_to_str' at 'name' is not known when the program is compiled | |
