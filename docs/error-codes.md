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
| E0501 - E0599 | what both targets reject: types, calls, structs, matches and unions |
| E0601 - E0699 | what the x64 target rejects, and its limits |
| E0701 - E0799 | what the wasm target rejects, and its limits |
| E0901 - E0999 | the text of a program as WebAssembly text and as assembly |

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
| E0119 | | Not used any more: a backslash in a string was an error before strings had escape sequences, and a code is never used again. |
| E0120 | a string must be valid UTF-8 | |
| E0121 | string has no closing quote | The string does not end on its line. |
| E0122 | more than 65536 tokens | The compiler reads at most 65536 tokens of a file. |
| E0123 | unexpected character 'c' | A character that cannot start a token. |
| E0124 | this is not an escape sequence of a string | A backslash in a string that does not make an escape sequence, such as `"a\qb"`, or one with a number that is not a Unicode character. A backslash is written `\\`. |

## E0201 - E0299: the syntax

These are found when the tokens are put together into the syntax tree, see the [grammar](../grammar.ebnf). The compiler stops at the first one, so the syntax error is the first place where the source does not fit the grammar, which is not always the place of the mistake: a missing operand is found at the next token.

| Code | Title | When |
|---|---|---|
| E0201 | more than 65536 syntax tree nodes | The program has more nodes than the compiler allocates. |
| E0202 | unexpected `token` | The token is not what the grammar takes there. The label says what it takes. When the line ends where the grammar wants more, as when a foreign procedure has no `->` and `...`, the token is `end of the line` and the error is at the end of that line, and at the end of the file it is `end of the file`. |

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

## E0501 - E0599: what both targets reject

These are found when a target builds the program, and are the same mistake in the program for the `x64-win32` and the `wasm` targets, which is why they have one code. They are about the types of the values, see [Primitive data types](mialu-programming-language.md#primitive-data-types), and about calls, structs, matches and unions. The place of an error is the name that the syntax tree knows, as for [the constants](#e0451---e0499-what-is-known-when-the-program-is-compiled), and the title says so with *at*.

| Code | Title | Details |
|---|---|---|
| E0501 | an operand of arithmetic at 'name' is not of a type that it takes | An operand of arithmetic is an Int, an I32, an I64, a U32, a U64 or a float, and is of the type of the other operand. |
| E0502 | the element at 'name' is not of the type of the elements of the array |  |
| E0503 | 'name' is not an array or an array slice | Not an array or an array slice. |
| E0504 | the array 'name' must be given an array literal | An array is not copied, so it is defined with an array literal and is not assigned to as a whole. Write an array literal, such as [const]Int { 1, 2 }. |
| E0505 | the array of 'name' has a different number of elements than the length in its type |  |
| E0506 | the call of 'name' has a different number of arguments than the procedure has parameters | A different number of arguments. |
| E0507 | an argument of the call of 'name' is not of the type of its parameter |  |
| E0508 | the procedure 'name' returns no value, so its call cannot be used as one |  |
| E0509 | 'name' does not hold a pointer to a procedure, so it cannot be called |  |
| E0510 | the value cannot be cast to 'name' | A Byte, a Bool, an I32, an I64, a U32, a U64, an Int, a Rune or a float can be cast to Int or I64, an integer or a Rune to a U64, a Byte, a Bool, a U32 or a Rune to a U32, a Byte, a Bool, an I32 or a Rune to an I32, a Byte, an I32, an I64, a U32, an Int or a float to a float, a Byte or a Rune to Rune, and a Rune to [4]Byte. |
| E0511 | 'name' cannot be compared here | Both operands of a comparison must be Int, or both must be I32, or both must be I64, or both must be U32, or both must be U64, or both must be floats of one type, or both must be Bool, Str, arrays or array slices of Byte, or a Byte and an ASCII rune for == and not=, or pointers of one type for == and not=. |
| E0512 | the condition at 'name' is not a Bool | An Int is not converted to a Bool. |
| E0513 | the enum has no member 'name' | Not a member of the enum. |
| E0514 | the enum of 'name' is not known here | Write the member with its enum, as in 'Color.RED', or give it to something that has an enum type. |
| E0515 | a field of 'name' cannot be assigned to | 'name' is a parameter or an iterator, which cannot be assigned to, so neither can its fields. A field of what a pointer points to can be. |
| E0516 | 'name' is a type and not a value | This is a type. |
| E0517 | 'name' is not an Int, so it cannot be an index | Not an Int. |
| E0518 | 'name' is not a Bool, so it cannot be used with 'and', 'or' or 'not' | Not a Bool. |
| E0519 | this case matches a value that a case above it matches already | It could never run for that value. |
| E0520 | 'name' only means something for a match of an enum that has no default case | A match of an enum that has no default case is the only match that must have a case for every value. |
| E0521 | the pattern of the case is not known when the program is compiled | The pattern is a literal, a constant or a member of an enum. |
| E0522 | the pattern of the case is not of the type of what is matched | A string is not an Int, a member is of one enum only, a Byte is from 0 to 255 and a range is only for an Int or a Byte. |
| E0523 | the range of the case has no numbers in it, so it could never match | An empty range. |
| E0524 | 'name' cannot be matched | A match takes an integer, a Str or an enum. |
| E0525 | 'nil' does not say what 'name' points to | Write the type of the variable, as in 'p : ^Int = nil'. |
| E0526 | 'name' is not a pointer, so '^' cannot follow it | Not a pointer. |
| E0527 | the value assigned through 'name' is not of the type that it points to |  |
| E0528 | 'name' is not an Int, so it cannot be the start or the end of a range | Not an Int. |
| E0529 | the number of names of 'name' is not the number of values that the call gives |  |
| E0530 | 'name' returns a number of values that is not the number of its return types |  |
| E0531 | 'name' returns T, but its return type is U | The title names both types, and the label says which was expected and which was found. A call of a procedure that returns nothing is `no value`. |
| E0532 | 'name' returns a value but has no return type | This has no return type. |
| E0533 | the field 'name' is given a value twice | Given again here. |
| E0534 | the value of the field 'name' is not of the type of the field |  |
| E0535 | the struct has no field called 'name' | Not a field of the struct. |
| E0536 | the struct with the field 'name' contains itself | A struct that contains itself would never end. Give it a pointer to itself instead. |
| E0537 | type 'name' is not supported by the x64 or wasm target | Not supported. |
| E0538 | the case 'name' is not a member of the enum of the field that the union is matched against | Not a member of the enum. |
| E0539 | 'name' cannot be matched by a union | The field that a union is matched against is a field of the struct, outside of the cases, and of an enum type. |
| E0540 | the field 'name' is in a case of a union, but the struct object is given another member for the field that the union is matched against | This field does not exist there. |
| E0541 | the field 'name' is in a case of a union, so it can only be used where the field that the union is matched against is known to be the member of the case | Use it inside 'if x.kind == .MEMBER ->', and do not change 'x' or 'x.kind' in it. |
| E0542 | the field 'name' is in a case of a union, so the struct object must give the field that the union is matched against a member of the enum that is written out | Write the member, such as '.MEMBER', and not another value. |
| E0543 | variable 'name' is given a value that is not of its type |  |

## E0601 - E0699: what the x64 target rejects

These are about the `x64-win32` target, see [x64-win32](build-target-x64-win32.md): what it does not build, and its limits. A mistake that both targets reject is in [E0501 - E0599](#e0501---e0599-what-both-targets-reject).

| Code | Title | Details |
|---|---|---|
| E0601 | 'name' has no address | A variable, an element and what a pointer points to have an address, but not a parameter, an iterator, a value that is computed, or an array. |
| E0602 | the parameter 'name' cannot be an array | An array is in the frame of its procedure, so a parameter takes an array slice. Write the type as '[]Type'. |
| E0603 | more than N constants, procedures and strings | The x64 target keeps at most N of them, so that the memory that it needs is known when it starts. |
| E0604 | 'name' has an expression nested deeper than N |  |
| E0605 | the field 'name' is accessed on something that is not a struct or a pointer to one | A struct is held by a variable, a parameter, an element or a pointer. |
| E0606 | the foreign procedure 'name' is declared in a 'foreign js' block | The x64 target imports from 'foreign libc' and from the libraries of the system that an 'import foreign' names. |
| E0607 | the library of the foreign procedure 'name' is not written as a library of the system | There is room for a few libraries. Write it as "system:Name.lib" in its import. |
| E0608 | 'name' must be 'main :: proc() Int ->' | 'main' is a procedure without parameters that returns an Int, which is the exit code. |
| E0609 | a program needs a procedure 'main' to start with | Define it, as in 'main :: proc() Int ->'. |
| E0610 | the match has no case for a member of the enum, and no default case | Add a case for each member, or a default case 'case ->', or write 'match #(partial)' to leave members out. |
| E0611 | the program is larger than N bytes | The compiler allocates the memory for the program once, when it starts. |
| E0612 | 'name' is not a struct that is defined, or the literal without a name is not where a struct is wanted |  |
| E0613 | the literal of 'name' gives some fields and not all of them | A literal gives either every field or none. |
| E0614 | the information about 'name' is not a value | Read a field of it right away, such as '.members' or '.name'. |
| E0615 | 'type_info_of' at 'name' is about a type that the x64 target gives no information about | The x64 target gives information about an enum only. |
| E0616 | 'name' has something that the x64 target does not build yet |  |

## E0701 - E0799: what the wasm target rejects

These are about the `wasm` and `wat` targets, see [WebAssembly](build-target-wasm.md): what they do not build, and their limits. A mistake that both targets reject is in [E0501 - E0599](#e0501---e0599-what-both-targets-reject).

| Code | Title | Details |
|---|---|---|
| E0701 | cannot take the address of 'name' | Only a variable of a procedure, a field or an element of one, and what a pointer points to have an address, and not a parameter, an iterator, a global variable, a value, an array or an array slice. |
| E0702 | an array of 'name' cannot be the type here | A parameter can be an array slice, as in []name, and a global variable can be neither. |
| E0703 | the built-in procedure 'name' is not supported by the wasm target |  |
| E0704 | 'name' is reserved for the memory of the wasm module | Choose another name. |
| E0705 | an expression in 'name' has too many nested operators |  |
| E0706 | the field 'name' is read or assigned through a value that is not a variable, a parameter or an element of an array |  |
| E0707 | the foreign procedure 'name' is declared in a 'foreign libc' block or in one of a library of the system | The wasm target imports from 'foreign js'. |
| E0708 | the match has no case for the member 'name' of the enum | Add a case for it, a default case 'case ->', or write 'match #(partial)' to leave members out. |
| E0709 | the WebAssembly module is larger than N bytes | The compiler allocates the memory for the module once, when it starts. |
| E0710 | 'name' is not a struct or a pointer to one, so it has no fields | Not a struct. |
| E0711 | the field 'name' is not given a value | Either every field is given a value, or the braces are empty and every field has its default value. |
| E0712 | type_info_of('name') exists only when the program is compiled | A global variable can be given that and nothing more. Write a field after it right away, as in type_info_of(name).members. |
| E0713 | the wasm target has information about enums only, and 'name' is not one |  |

## E0901 - E0999: the text of a program

These are about the `wat` target and `dump-asm`, which format the module or the executable that the compiler wrote as text, see [mialuc](mialu-cli.md). An error that says `internal error` is about the compiler and not about the program: the formatter reads what the compiler wrote itself, so what it cannot read is a bug, which should be reported with the program that has it.

| Code | Title | When |
|---|---|---|
| E0901 | internal error: the WebAssembly module is malformed at byte N | A bug of the compiler. |
| E0902 | internal error: the WebAssembly module cannot be formatted as text at byte N | A bug of the compiler. |
| E0903 | the WebAssembly text is larger than N bytes | The text of the module is more than 16 MiB. |
| E0904 | internal error: the executable is malformed at byte N | A bug of the compiler. |
| E0905 | internal error: the executable cannot be formatted as assembly at byte N | A bug of the compiler. |
| E0906 | the assembly is larger than N bytes | The assembly is more than 16 MiB. |
