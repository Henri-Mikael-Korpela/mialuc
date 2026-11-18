# Mialu Programming Language Specification

## Table of contents

- [Introduction](#introduction)
- [Source code representation](#source-code-representation)
- [Packages](#packages)
- [Imports](#imports)
  - [Where the standard library is](#where-the-standard-library-is)
  - [Packages and names](#packages-and-names)
- [Lines, spaces and indentation](#lines-spaces-and-indentation)
- [Comments](#comments)
- [Reserved words](#reserved-words)
- [Identifiers](#identifiers)
- [Primitive data types](#primitive-data-types)
  - [Booleans](#booleans)
  - [Characters](#characters)
  - [Integers](#integers)
  - [Bytes](#bytes)
  - [Floating-point numbers](#floating-point-numbers)
  - [Strings](#strings)
- [Operators](#operators)
  - [Arithmetic operators](#arithmetic-operators)
  - [Assignment and compound assignment operators](#assignment-and-compound-assignment-operators)
  - [Comparison operators](#comparison-operators)
  - [Logical operators](#logical-operators)
  - [Address of and dereference operators](#address-of-and-dereference-operators)
  - [Casts](#casts)
- [Constants](#constants)
- [Variables](#variables)
- [Control flow structures](#control-flow-structures)
  - [If statements](#if-statements)
  - [Match statements](#match-statements)
  - [For statements](#for-statements)
  - [Labels and scopes](#labels-and-scopes)
  - [Break and continue](#break-and-continue)
  - [Defer](#defer)
- [Arrays](#arrays)
  - [Array literals](#array-literals)
  - [Elements](#elements)
  - [Comparing bytes](#comparing-bytes)
  - [Array slices](#array-slices)
- [Custom data types](#custom-data-types)
  - [Enumerations](#enumerations)
  - [Structs](#structs)
  - [Unions](#unions)
- [Reflection](#reflection)
  - [Enumeration type info](#enumeration-type-info)
  - [Procedure type info](#procedure-type-info)
- [Pointers](#pointers)
  - [Taking an address](#taking-an-address)
  - [Following a pointer](#following-a-pointer)
  - [Fields of a pointer](#fields-of-a-pointer)
  - [Null and lifetime](#null-and-lifetime)
  - [Pointers in structs and arrays](#pointers-in-structs-and-arrays)
- [Procedures](#procedures)
  - [Calling procedures](#calling-procedures)
  - [Multiple return values](#multiple-return-values)
  - [Pointers to procedures](#pointers-to-procedures)
- [Built-in procedures](#built-in-procedures)
  - [length_of](#length_of)
  - [int_to_byte](#int_to_byte)
  - [int_to_i32](#int_to_i32)
  - [int_to_str](#int_to_str)
  - [str_to_c_str](#str_to_c_str)
  - [try_str_to_int](#try_str_to_int)
  - [utf8_decode](#utf8_decode)
- [Scope resolution](#scope-resolution)
- [Attributes](#attributes)
- [Foreign procedures](#foreign-procedures)
  - [Foreign procedures in WebAssembly](#foreign-procedures-in-webassembly)
  - [Foreign procedures in the libraries of the system](#foreign-procedures-in-the-libraries-of-the-system)
  - [Foreign procedures in the C library](#foreign-procedures-in-the-c-library)
- [Compilation targets](#compilation-targets)
  - [WebAssembly](#webassembly)
  - [x64 for Windows](#x64-for-windows)
- [Possible future features](#possible-future-features)
  - [Parametric polymorphism](#parametric-polymorphism)
  - [Converting a string to bytes](#converting-a-string-to-bytes)
- [Rejected ideas](#rejected-ideas)
  - [Interfaces](#interfaces)

## Introduction

This document describes the Mialu programming language, its syntax, semantics and compilation targets.

Mialu is a procedural programming language with straight-foward and familiar syntax, built with simplicity and performance in mind. It is influenced by multiple programming languages including, but not limited to, C, C++, Go, Odin and Python, fully embracing the fact that it comes after many of them, so it has learned from their mistakes and successes.

The fact that there's no mention of functional programming, object-oriented programming or metaprogramming does not mean the author is unfamiliar with or blissfully ignorant of such concepts. The author has made an intention choice to not focus on such paradigms that much in the design of this language. In the future, the overall reasoning will become more evident as the language progresses and new posts documenting certain design decisions emerge.

Where does the name Mialu come from? It comes from the first syllables of my children's names, Miriam, Amira and Lukas. That's my personal touch to such a technical tool.

## Source code representation

Mialu source code is a sequence of Unicode characters, encoded in UTF-8. Source code files should have the `.mu` extension.

## Packages

Every Mialu source file belongs to a package, and says which one with a package definition:

```
package helloworld

main :: proc() Int ->
    return 0
```

The package definition is `package` and the name of the package, on a line of its own, written in the first column. It is required: a source file without it is an error, and so is a source file that has more than one. It is the first thing in the file, with the exception of [comments](#comments) and blank lines, which can be above it and below it, so everything else, including `import foreign` and every definition, comes after it:

```
// Greets the world.

package helloworld // The name of the package.

/// Returns the answer.
answer :: proc() Int ->
    return 42
```

`package` is a [keyword](#reserved-words), so it cannot be used as a name, and the name of the package is an [identifier](#identifiers), so it follows the rules of one: it has alphanumeric characters and underscores, does not begin with a digit, and is not a reserved word. The convention is to write it in lowercase with underscores, such as `helloworld` or `secret_project`, and to name the package after what the code in it is for. The name of the package of the program has no effect on the program that is built, and the compiler does not check it any further: it is kept in the syntax tree, which `mialuc dump-ast` prints on the first line, `MODULE package helloworld`. The package name of a file that is [imported](#imports) is the name that its definitions have outside of the file.

The code examples of this specification that show a part of a program, such as a single procedure, leave the package definition out to keep the example short. A whole program, which can be compiled as it is, has it.

## Imports

An import statement brings the definitions of another source file into the program. It is `import` and a path in quotes, on a line of its own, written in the first column, below the [package definition](#packages) and above the definitions that use it:

```
package program

import "mu:filesystem"

main :: proc() Int ->
    buffer := [64]Byte { .. = 0 }
    file, error := mialu.file_open("data.txt", mialu.File_Open_Mode.READ, buffer[0:64])
    if error not= mialu.File_Error.NONE ->
        return 1
    return cast(Int)mialu.file_close(file)
```

The text in quotes is the vendor, a colon and the path under the vendor. The vendor is the library that the file is in: `mu` is the standard library of Mialu, and it is the only vendor for now. The path is the file below the directory of the library, without `.mu`, with names and slashes, so `mu:filesystem` is the file `filesystem.mu` and `mu:text/ascii` would be `text/ascii.mu`. A path that has anything else, such as a dot, is an error, and so is a vendor that is not known and a file that is not there. `import` is a [keyword](#reserved-words). The other kind of import, `import foreign`, brings in procedures that are not written in Mialu, see [Foreign procedures](#foreign-procedures).

### Where the standard library is

The standard library is source code, and the compiler reads it when a program imports it. A release of the compiler has it in the directory `lib` that is next to `mialuc.exe`, in the zip file that it is distributed in, which is looked in first. A compiler that is built from the sources of the project finds it in the directory `src/mialu-std` of the project, which is also where the files of the library are kept, so a debug and a release build of the project use the files that are in the project without having them copied. A file that is in neither is an error that says where it looked.

### Packages and names

All the definitions of the imported file are part of the program, but they are used with the name of their package, written before the name, with a dot and nothing between: `file_open` that is defined in a file whose package is `mialu` is `mialu.file_open` everywhere outside of that file. That goes for every kind of definition: a procedure, a constant, a global variable, a struct and an enum, and so a type is written as `mialu.File`, a member of an enum as `mialu.File_Error.NOT_FOUND`, and a struct literal as `mialu.File { handle = 5 }`. The name of the package is the one that the imported file has in its [package definition](#packages), and it is not the name of the file, so every file of the standard library has `mialu`, which makes the definitions of all of them `mialu.` something, and no two may have the same name.

The program's own names are never the names of a package, and the other way round, so a program that defines a `File` of its own, or `min`, or `CP_UTF8`, has no conflict with the library that has the same, and uses its own without the package before it. Inside the file that defines it a definition is written with its plain name, as everywhere else. A name of a package is not found without the package: `file_open` alone, where `mialu.file_open` is meant, is an unknown name. A parameter or a variable of a program may have the name of any definition of a package, which is not a definition of the program, but one of an imported file may not have the name of a definition of the same file, as in the program.

The files that an imported file imports are imported too, once, before it, in the order that they are written, and a file is only read once however many files import it. Files that import each other, directly or through others, are an error. There are at most 32 files, and the imports go at most 8 files deep.

The definitions of an imported file are part of the program that is built: a procedure that nothing calls is in it, so a target has to be able to build all that a file has. A file of the standard library that uses what a target does not have, such as `filesystem`, which is for Windows and calls the system, does not build for the others. [WebAssembly](build-target-wasm.md#imports) does not export the procedures of the imported files, as the name of one has a dot, which a caller cannot write.

## Lines, spaces and indentation

Mialu doesn't use curly brackets for defining blocks. Mialu borrows its indentation-based syntax from Python, so the structure of the code is defined by its indentation, like this

```
foo :: proc() Int ->
    x := 4
    if x > 5 ->
        return 1
    return 2
```

instead of this:

```
foo :: proc() Int {
    x := 4
    if x > 5 {
        return 1
    }
    return 2
}
```

A line that is indented deeper than the previous one starts a block, and a line that is indented less ends it. Instead of using colon (`:`) to start a block, Mialu uses `->` after the header of a statement to starts a block or scope.

The reason for using indentation-based approach for blocks is to both make the code more readable and to reduce the amount of vertical space, which is a problem in languages that use curly brackets (and especially if you use language like C# where a general convention is to set each curly bracket on its own line).

Indentation is made of spaces only. Tabs are not allowed in indentation. Each level of indentation is exactly 4 spaces, so the number of leading spaces on a line must be a multiple of 4. Blank lines, including lines of only spaces, are ignored, and so are lines of only a [comment](#comments).

Mialu doesn't need semicolons (`;`) to separate statements. Each statement is on its own line, and a line can have at most one statement.

## Comments

A comment is text for the reader of the code, which does not change what the program does. There are two kinds, and both are made of lines, because Mialu is line-based:

```
// A comment on a line of its own.

/// A documentation comment describes the definition below it.
/// It can go on over many lines, and each of them starts with three slashes.
foo :: proc() Int ->
    // A comment inside a block.
    x := 4 // A comment can also follow code on the same line.
    return x
```

- `//` starts a comment that goes on to the end of the line. It can be on a line of its own or after code.
- `///` starts a documentation comment. It must be on a line of its own, with nothing but spaces before it, because it documents the definition that comes after it, so a `///` after code is an error. A documentation comment is the lines in a row that each start with `///` (after their indentation). A blank line, a line of code, or a `//` comment ends it, and a new `///` after that starts another one.
- There are no comments that start and end in the middle of a line, and none that nest. This keeps a comment from hiding code that is on the same line, and a reader never has to look for where a comment ends.
- A line that has only a comment is like a blank line for the structure of the code. It does not have to be indented by a multiple of 4 spaces, it does not end a block, and it can be between the lines of a block or of a multi-line literal.
- Comments hold UTF-8 text, as all source code does. The compiler reports a comment that has bytes that are not valid UTF-8.

The compiler does not ignore comments: the tokenizer makes each comment a token (`COMMENT` for `//` and `DOCUMENTATION_COMMENT` for a documentation comment), which holds the text exactly as it is in the source, including the slashes. A documentation comment of many lines is one token, which holds the lines with their line breaks. The parser leaves the comment tokens out, so a comment never changes the syntax tree or the program that is built. The tokens are there for tools, such as formatters and generators of documentation, which need to know where the comments are. `mialuc dump-ast` prints them with the other tokens, see [mialuc](mialu-cli.md#compile-a-program-and-print-its-syntax-tree).

The reason for `///` is that documentation should be found by tools and not guessed from the `//` comments around it, so it has a form of its own, which is also the form that editors can color apart. What a documentation comment is attached to is not yet used by the compiler.

## Reserved words

Mialu has reserved words that cannot be used as names for identifiers. Reserved words are case-sensitive, so `If` is an identifier and `if` is not. A longer identifier that only starts with a keyword, such as `returned` or `cannot`, is an identifier.

Reserved words are categorized into used and unused keywords and literal words. Mialu has the following keywords in use, in alphabetical order:

```
and
break
case cast const continue
defer
else enum
for foreign
if import in
match
not
or
package proc
return
struct
type_info_of
union
```

There are also literal words, which look like keywords but are actually literals, because each one is a value and not a part of the syntax of a statement. In alphabetical order they are:

```
false nil true
```

`false` and `true` are the two values of a [`Bool`](#booleans), and `nil` is the [null pointer](#null-and-lifetime), a value of every pointer type.

Mialu also defines the following unused keywords. They are reserved for possible future use:

```
abstract alias as async await
catch class
delete distinct do dynamic
extern
final finally
gen generate goto
impl interface is
loop
macro mutable
new
of operator override
private public
ref
safe self switch super
this throw throws trait transmute try type_id_of type_of
unsafe use
virtual
where while
yield
```

## Identifiers

Identifiers can contain alphanumeric characters and underscores. Case matters for identifiers in Mialu are case-sensitive. An identifier cannot begin with a digit. A [keyword](#keywords) or a [literal word](#literal-words) cannot be used as an identifier.

## Primitive data types

Every value has a type, which is written as an identifier. Mialu has the following primitive data types:

| Type  | Description                                                  |
|-------|--------------------------------------------------------------|
| `Bool` | A boolean value, which is `true` or `false`. See [Booleans](#booleans). |
| `Byte` | An unsigned 8-bit integer, from 0 to 255. It is the unit of memory and of binary data. See [Bytes](#bytes). |
| `Rune` | A Unicode character, which is its 32-bit code point. Written between single quotes, such as `'A'`, `'芋'` or `'🚀'`. See [Characters](#characters). |
| `Int` | A signed 64-bit integer. Written as a number, such as `42`, `0b101010` or `0x2A`. See [Integers](#integers). |
| `I32` | A signed 32-bit integer, from -2147483648 to 2147483647. It is the `int` of the C library. See [32-bit integers](#32-bit-integers). |
| `I64` | A signed 64-bit integer, the size of an `Int` and a type of its own. It is the `long long`, `int64_t` and `size_t` of the C library. See [64-bit integers](#64-bit-integers). |
| `U32` | An unsigned 32-bit integer, from 0 to 4294967295. It is the `unsigned int` and the `DWORD` of the C library and of Windows. See [Integers](#integers). |
| `U64` | An unsigned 64-bit integer, from 0 to 18446744073709551615. It is the `unsigned long long` and the `QWORD`. A constant of it is at most 9223372036854775807. See [Integers](#integers). |
| `F32` | A 32-bit floating point number. Written as a number with a fractional part, such as `1.42`. See [Floats](#floating-point-numbers). |
| `F64` | A 64-bit floating point number. Written as a number with a fractional part, such as `1.42`, and the type of one where nothing says. See [Floats](#floating-point-numbers). |
| `Str` | A string, which is text encoded in UTF-8. Written in quotes, such as `"Bob"`. |
| `C_Str` | A null-terminated C string, the address of bytes that end with a zero. It is for passing text to and from code written in C, to a parameter of type `char *` or `char const *`. See [C strings](#c-strings). |

The following primitive data types are reserved for possible future use:

| Category | Types |
| -------- | ----- |
| Signed integer | `I8 I16` |
| Unsigned integer | `U8 U16` |

### Booleans

There's only one boolean value type, `Bool`, which is either `true` or `false`, which are [literal words](#literal-words):

```
ready : Bool = true
```

Unlike C, integer types are not implicitly converted into boolean values. A `Bool` is never converted to or from another type. An `Int` is not a condition, so `if n ->` is an error and has to be written `if n not= 0 ->`, and `true` is not `1`: `x : Int = true` and `b : Bool = 1` are errors.

> **Lessons learnt from C** In C, any integer or pointer can be used as a condition, and a comparison gives an `int`. So `if (n)`, `if (p)` and `if (x = 0)` all compile, and a value that was meant to be a flag can quietly be 2 or -1 and be unequal to `true`. In Mialu a condition must be a `Bool`, so what is meant has to be written out, such as `n not= 0`, and a mistake of this kind is an error at compile time.

A comparison such as `x > 5` is an expression of type `Bool`, see [Comparison operators](#comparison-operators). The logical operators `and`, `or` and `not` combine `Bool` values, see [Logical operators](#logical-operators).

### Characters

A `Rune` is one Unicode character. Underneath it is a 32-bit number, the code point of the character, which goes from 0 to 0x10FFFF, so it holds a letter of any alphabet, a kanji, an emoji or a control character alike. A rune literal is a character between single quotes:

```
letter : Rune = 'A'
kanji : Rune = '芋'
emoji : Rune = '🚀'
inferred := 'é'
```

A character that cannot be written as itself, or is hard to see, is written as an escape sequence: a backslash and what stands for the character, as in `'\n'` for a new line and `'\u0123'` for U+0123.

```
newline : Rune = '\n'
quote : Rune = '\''
unicode_char : Rune = '\u0123'
rocket : Rune = '\U0001F680'
```

The source of a program is UTF-8, and a character in it can take one to four bytes, which a rune does not care about: `'A'` takes one byte and is 65, `'é'` takes two and is 233, `'芋'` takes three and is 33419, and `'🚀'` takes four and is 128640. What a rune holds is the number of the character and not its bytes, so it is the same whatever the encoding of the text that the character is in. A rune is one character, which is one code point: a letter with an accent that is written as a letter and a combining mark is two code points, and so is two runes, and a literal of both is an error, while the one code point of the letter, `'é'`, is a rune. A character that is not valid UTF-8 is an error, and so is a number that is not a Unicode character: a surrogate, U+D800 to U+DFFF, and a number above 0x10FFFF.

A rune literal has the type `Rune`, and it is not an untyped number: `'A'` is not an `Int` and not a `Byte`, `x : Int = 'A'` is an error and so is `x : Rune = 65`. A `Rune` is converted to an `Int`, to the 4 bytes of its UTF-8 encoding, and from a `Byte` with a [cast](#casts), as in `cast(Int)'A'`, which is 65. A rune literal is one literal, so it is a constant, which can be a [constant](#constants) with or without its type, `FIRST :: 'A'` and `LAST : Rune : 'Z'`, and a global variable, a parameter, a field, an element of an array and what a pointer points to can be one. The default value of a `Rune` is the character with the code point 0.

Runes have an order, the order of their code points, so all six [comparison operators](#comparison-operators) work on two runes, and `character >= 'A' and character <= 'Z'` is true for an upper case letter of the ASCII range. Which characters come where in the order is the order of Unicode and not of an alphabet, so it is not the order of a dictionary, and the characters of other alphabets and the emojis are above the letters of ASCII. A rune has no arithmetic, because a sum of two characters is not a character: `'a' + 1` is an error.

The escape sequences are:

| Escape sequence | Character | Code point |
|-----------------|-----------|------------|
| `\a` | bell | 7 |
| `\b` | backspace | 8 |
| `\e` | escape | 27 |
| `\f` | form feed | 12 |
| `\n` | new line, which is a line feed | 10 |
| `\r` | carriage return | 13 |
| `\t` | horizontal tab | 9 |
| `\v` | vertical tab | 11 |
| `\\` | backslash | 92 |
| `\'` | single quote | 39 |
| `\"` | double quote | 34 |
| `\` and three octal digits, such as `\101` | the code point of the number, which is at most 255, `\377` | 0 to 255 |
| `\x` and two hexadecimal digits, such as `\x41` | the code point of the number | 0 to 255 |
| `\u` and four hexadecimal digits, such as `\u0123` | the code point of the number | 0 to 0xFFFF |
| `\U` and eight hexadecimal digits, such as `\U0001F680` | the code point of the number | 0 to 0x10FFFF |

The digits of a hexadecimal number are of either case, so `'\u00e9'` and `'\u00E9'` are the same rune. A number has all of its digits, so `'\x4'` is an error, and so is a number that is not a Unicode character: `'\ud800'` is a surrogate, and `'\U00110000'` is above the last code point. The octal form takes exactly three digits, so the character with the code point 0 is `'\000'` or `'\x00'`, and not `'\0'`. A backslash followed by anything else, such as `'\q'`, is an error. The single quote is written `'\''` in a rune literal, because a quote that is not escaped ends it, while the double quote, which is no problem there, can be written either way. A character that is a line break cannot be written as itself, because a literal is on one line, so a new line is `'\n'`.

An escape sequence is a way to write one character, and the ones that are in the ASCII range, such as `'\n'`, `'\t'` and `'\x41'`, are one byte in UTF-8, so a rune that has one is compared with a [`Byte`](#bytes) with `==` and `not=`. An escape sequence is a way to write one character, so a rune literal that has one is the same rune as the character: `'\u0041'`, `'\x41'`, `'\101'` and `'A'` are all 65. The escape sequences are only for rune literals now: a string has no escape sequences yet, and a backslash in one is an error.

> **Lessons learnt from C** In C, a character literal such as `'A'` is an `int`, and the type `char` is a byte that may or may not be signed, so text in UTF-8 is a sequence of numbers that are not characters, and a character that takes several bytes cannot be one literal. In Mialu a character is a `Rune`, which is as wide as the largest code point, and its literal is one character in any alphabet.

### Integers

The integer type that numbers are by default is `Int`, a signed 64-bit integer: it holds the numbers from -9223372036854775808 to 9223372036854775807. A [`Byte`](#bytes), an [`I32`](#32-bit-integers) and an [`I64`](#64-bit-integers) are the other integers. An integer literal is written in one of three bases:

- decimal, such as `42`,
- binary, after the prefix `0b`, such as `0b101010`, whose digits are `0` and `1`,
- hexadecimal, after the prefix `0x`, such as `0x2A`, whose digits are `0` to `9` and `a` to `f`, in either case, so `0x2a` is the same number.

The prefixes are lowercase only, so that a literal has one spelling, and there is no octal literal. An underscore `_` is a digit separator, which makes a long number easier to read and does not change its value: `1_000_000`, `0b1111_0000` and `0xFF_FF` are 1000000, 240 and 65535. A separator must be between two digits, so `1__000`, `1_` and `0x_FF` are errors, and `_1` is an identifier and not a number. The digits can be grouped in any way, but the convention is three digits in decimal and four in binary and hexadecimal.

```
count : Int = 42
million := 1_000_000
flags := 0b1111_0000
mask := 0xFF_FF
```

A literal is the same untyped integer whatever its base, which is an `Int` where nothing else is wanted, see [Untyped constants](#untyped-constants), so the bases can be mixed, and `0b1010 + 10 + 0xA` is 30. A literal that does not fit in an `Int` is an error in every base, so the largest hexadecimal literal is `0x7FFF_FFFF_FFFF_FFFF`, and `0xFFFF_FFFF_FFFF_FFFF` is an error and not -1. A literal is signed with a minus sign directly before its digits, with no space between them, so `-5`, `-112` and `-0x10` are negative literals, and the sign is part of the literal and not an operator. The lowest `Int`, -9223372036854775808, has no literal, because its digits alone do not fit, so the lowest literal is `-9_223_372_036_854_775_807`. A literal must not be directly followed by a letter or an underscore, so `12a`, `0b102` and `0xFG` are errors that name the character that is not a digit of the literal.

A negative number is written as a signed integer literal: a `-` directly followed by the digits of an integer literal, in any base, with no space between them. `-5`, `-112`, `-0x10` and `-0b101` are signed literals, whose values are -5, -112, -16 and -5.

```
temperature := -5
offset : Int = -112
deltas := [const]Int { -1, 0, 1 }
```

A signed literal is one literal, not the operator `-` applied to a number, so it is an operand like any other and has the same type, an untyped integer that is an `Int` where nothing else is wanted, see [Untyped constants](#untyped-constants). It can be written wherever an operand can: as the value of a variable or a constant, an argument, a returned value, an element of an array, a bound of a range, the case of a match statement, and the operand of an operator, so `3 * -4` is -12 and `a - -2` is `a + 2`.

Whether a `-` is part of a literal depends on where it is. A `-` where an operand is expected, which is at the start of an expression and after an operator, a comma, `(`, `[`, `{`, `in`, `case`, `return` or an assignment, starts a signed literal. A `-` between two operands is the subtraction, so `a - 5`, `a -5` and `a-5` all subtract 5 from `a`, and `a - -5` adds 5. A signed literal needs the digits directly after the `-`: `- 5` is an error, and so is `-x`, because there is no negation of a name, which is written `0 - x`. A literal that does not fit is an error also when it is signed, and a signed literal is not a `Byte`, so `b : Byte = -1` is an error.

> **Lessons learnt from C** In C, a literal that starts with `0`, such as `010`, is octal, so a number that is padded with a zero to line up with others quietly becomes another number. In Mialu a leading zero changes nothing, `010` is ten, and a base other than decimal is always spelled out with a letter, `0b` or `0x`.

The arithmetic operators `+`, `-`, `*` and `/` take two `Int` values and give an `Int`, see [Operators](#operators), and so do the assignment operators `+=`, `-=`, `*=` and `/=`, see [Assignment](#assignment). Addition, subtraction and multiplication wrap around on overflow, so the largest `Int` plus 1 is the smallest. Division truncates toward zero, so `-7 / 2` is -3. Dividing by zero, or dividing the smallest `Int` by -1, whose result does not fit, is an error at run time, see [WebAssembly](#webassembly).

> **Lessons learnt from C** In C, the size of `int` and `long` depends on the platform, so the same code can overflow on one machine and not on another, and overflow of a signed integer is undefined behavior, which lets a compiler assume that it never happens and remove checks that a programmer wrote against it. Integer types of different sizes and signs are also converted into each other silently, so a comparison of a signed and an unsigned number can give a surprising answer. In Mialu there is one `Int` of 64 bits whose overflow wraps around in a defined way, nothing is converted implicitly, and the cases of division that have no result are traps and not undefined behavior.

An `Int` is not converted implicitly: it is not a `Bool`, so `if n ->` is an error, it is not a `Byte`, and an operand of arithmetic that is not an `Int` is an error. A literal or a constant without a type is not an `Int` yet, so it becomes a `Byte` where one is wanted without being converted, see [Untyped constants](#untyped-constants). The integer types `I8`, `I16`, `I32`, `I64` and `U8`, `U16`, `U32`, `U64` are reserved for possible future use, see [Primitive data types](#primitive-data-types).

An `I32` is a signed 32-bit integer: it holds the numbers from -2147483648 to 2147483647. It is the type of the `int` of C, which is what it is for: a procedure of the C library that takes or gives an `int` is declared with an `I32`, see [Foreign procedures in the C library](#foreign-procedures-in-the-c-library), and so is a procedure of JavaScript that takes numbers of 32 bits, which are plain numbers there and not a `BigInt`, see [Foreign procedures in WebAssembly](#foreign-procedures-in-webassembly). The default value of an `I32` is `0`.

```
level : I32 = 3
limit : I32 : 2147483647

add :: proc(a: I32, b: I32) I32 ->
    return a + b
```

An `I32` is a type of its own, and nothing is converted to it or from it implicitly, as for a `Byte`: an `Int` is not an `I32`, so `n : I32 = count` is an error when `count` is an `Int`, and so is `a + count` for an `I32` `a`. A literal or a constant without a type is not an `Int` yet, so it becomes an `I32` where one is wanted, as long as its value fits in 32 bits, see [Untyped constants](#untyped-constants): `level : I32 = 3` needs no conversion, and `n : I32 = 2147483648` is an error. A constant can have the type, `LIMIT : I32 : 100`, and arithmetic of constants of it is exact, and so is an error when the result does not fit, as for an `Int`.

The arithmetic operators `+`, `-`, `*` and `/` take two `I32` values, or an `I32` and a literal that fits in one, and give an `I32`, and so do `+=`, `-=`, `*=` and `/=`. The comparison operators take two `I32` values with all six operators, and the order is the one of the numbers with their signs. Addition, subtraction and multiplication wrap around in 32 bits, as those of an `Int` do in 64, so the largest `I32` plus 1 is the smallest, and `46341 * 46341` is -2147479015. A division goes toward zero, and traps when the divisor is zero, and when the smallest `I32` is divided by -1, since the result, 2147483648, is not an `I32`, as for an `Int`.

An `I32` is made an `Int` or a float with a [cast](#casts), `cast(Int)n`, which never loses anything and keeps the sign, and a `Byte`, a `Bool` or a `Rune` is made an `I32` the same way. An `Int` or an `I64` is not cast to an `I32`, because it may not fit: the built-in procedure [`int_to_i32`](#int_to_i32) makes an `I32` of an `Int`, and stops the program when the number is not from -2147483648 to 2147483647.

An `I32` takes the same place in memory as an `Int` does, which is 8 bytes in a struct or an array, so a pointer to an `I32`, `^I32`, is not a pointer to an `int` of C, and an array of `I32` values is not an array of `int`. An `I32` is passed and returned by value, which is what C does with an `int`.

An `I64` is a signed 64-bit integer: it holds the numbers from -9223372036854775808 to 9223372036854775807, which are the numbers of an `Int`. It is not another name for `Int`, but a type of its own with the same size, in the way that an `I32` is of its own: nothing is converted to it or from it implicitly, so `n : I64 = count` is an error when `count` is an `Int`, and so is `a + count` for an `I64` `a`. It is for what has a size that is said, which is the `long long`, the `int64_t` and the `size_t` of the C library, and a number of 64 bits of JavaScript, which is a `BigInt` there, as an `Int` is, see [Foreign procedures](#foreign-procedures). A procedure that is declared with an `I64` says what it takes and gives, where an `Int` says that it is a number like any other of the program. The default value of an `I64` is `0`.

```
size : I64 = 5_000_000_000
limit : I64 : 9223372036854775807

total :: proc(a: I64, b: I64) I64 ->
    return a + b
```

A literal or a constant without a type is not an `Int` yet, so it becomes an `I64` where one is wanted, whatever its value, because every one fits, see [Untyped constants](#untyped-constants). A constant can have the type, `LIMIT : I64 : 100`, and arithmetic of constants of it is exact, and so is an error when the result does not fit, as for an `Int`.

The arithmetic operators `+`, `-`, `*` and `/` take two `I64` values, or an `I64` and a literal, and give an `I64`, and so do `+=`, `-=`, `*=` and `/=`. The comparison operators take two `I64` values with all six operators, and the order is the one of the numbers with their signs. The operations are the ones of an `Int`: addition, subtraction and multiplication wrap around in 64 bits, and a division goes toward zero and traps when the divisor is zero and when the smallest number is divided by -1.

An `I64` and an `Int` are each made the other with a [cast](#casts), `cast(Int)n` and `cast(I64)n`, which keep the number, and so are an `I32`, a `Byte`, a `Bool` and a `Rune`, which are made an `I64` the same way, and a float, which is made one as it is made an `Int`, with its fraction dropped. An `I64` is made an `I32` by [`int_to_i32`](#int_to_i32), which takes an `Int` or an `I64`, and stops the program when the number does not fit. An `I64` takes the same place in memory as an `Int`, so a `^I64` is a pointer to an `Int` in all but its type, and an array of `I64` values is the array of 64-bit numbers that C has.

A `U32` is an unsigned 32-bit integer: it holds the numbers from 0 to 4294967295, and a `U64` is an unsigned 64-bit integer, which holds the numbers from 0 to 18446744073709551615. Each is a type of its own, as an `I32` and an `I64` are: nothing is converted to them or from them implicitly, so `n : U32 = count` is an error when `count` is an `Int`, and so is `a + count` for a `U32` `a`. They are for what has a size and no sign, which is the `unsigned int`, the `DWORD`, the `unsigned long long`, the `QWORD` and the flags and sizes of the system libraries, see [Foreign procedures](#foreign-procedures). The default value of both is `0`.

```
GENERIC_READ : U32 : 0x80000000
size : U64 = 5_000_000_000

total :: proc(a: U32, b: U32) U32 ->
    return a + b
```

A literal or a constant without a type becomes a `U32` where one is wanted when it is from 0 to 4294967295, and a `U64` when it is not negative, see [Untyped constants](#untyped-constants). A constant can have the type, `MASK : U32 : 4294967295`, and arithmetic of constants of it is exact, so it is an error when the result is below 0 or does not fit. A constant of a `U64` goes as far as an `Int` does, 9223372036854775807, because the constants are 64-bit numbers with a sign, but the value of a variable goes up to 18446744073709551615.

The arithmetic operators `+`, `-`, `*` and `/` take two `U32` values, or a `U32` and a literal, and give a `U32`, and so do `+=`, `-=`, `*=` and `/=`, and the same goes for a `U64`. The comparison operators take two values of the type with all six operators, and the order is the unsigned one, so `3000000000 > 5` is true for a `U32`, and `0 - 1 > 5` is true for a `U64`. Addition, subtraction and multiplication wrap around in the bits of the type, so `0 - 1` is 4294967295 for a `U32` and 18446744073709551615 for a `U64`, and a division goes down, and traps when the divisor is zero. There is no unary minus.

A `Byte`, a `Bool` and a `Rune` are made a `U32` with a [cast](#casts), because they always fit, and every integer and a `Rune` is made a `U64`, a negative number as the number that has its bits, so `cast(U64)-1` is 18446744073709551615. An `Int` and an `I32` are not cast to a `U32`, because they may not fit, and a float is cast to neither. A `U32` is made an `Int`, an `I64` or a float by a cast, and a `U64` is made an `Int` or an `I64`, which keep its bits. A `U32` takes the same place in memory as an `Int` does, which is 8 bytes in a struct or an array, and is passed and returned by value as C does an `unsigned int`. JavaScript gets a `U32` as a number and a `U64` as a `BigInt`, and since WebAssembly has no unsigned values, a number above 2147483647 or 9223372036854775807 is the negative number that has the same bits there.

### Bytes

A `Byte` is an unsigned 8-bit integer: it holds the numbers from 0 to 255. It can be used to represent individual bytes of memory. The default value for `Byte` is `0`.

A `Byte` is made an `Int` with a [cast](#casts), `cast(Int)b`, which never loses anything, and the built-in procedure [`int_to_byte`](#int_to_byte) makes a `Byte` of an `Int`.

An untyped integer from 0 to 255, which is a literal, a constant without a type or a constant expression of them, is a `Byte` where a `Byte` is wanted, so `b : Byte = 200`, `b >= 65` and `[3]Byte { 1, 2, 3 }` need no conversion, and `b : Byte = 300` is an error, see [Untyped constants](#untyped-constants). An `Int` is never a `Byte`, whatever its value, because its value is known only when the program runs.

A `Byte` can be compared with `==` and `not=` to a rune, which is how a character is looked for in bytes: `b == 'A'` holds when the byte is 65, and `b == '\n'` when it is the new line, which is a rune that is written as an [escape sequence](#characters). The rune must be known when the program is compiled, so a rune literal or a constant, and it must be in the ASCII range, from 0 to 127, because a character in that range is one byte in UTF-8, and is the byte that has its number, which no other character is. A rune above 127, such as `'é'`, takes more than one byte, so `b == 'é'` is an error, and so is comparing a `Byte` with a `Rune` that is a variable, which can be any character. The order of the two is not defined, so `b < 'z'` is an error, and a rune can come on either side. A `Byte` above 127 is never the same as a rune in the range:

```
is_line_end :: proc(b: Byte) Bool ->
    return b == '\n' or b == '\r'
```

A `Byte` has no arithmetic: `a + b` for two bytes is an error, and the arithmetic is done on `Int` values, as in `cast(Int)a + cast(Int)b`.

### Floating-point numbers

There are two floating point types: `F32` is a 32-bit floating point number, the IEEE 754 binary32 type that is a `float` in C, which holds numbers of about 7 decimal digits, from about 1.2e-38 to 3.4e38 in size, and `F64` is a 64-bit one, the binary64 type that is a `double` in C, which holds about 15 decimal digits, from about 2.2e-308 to 1.8e308 in size. Both also hold infinity, minus infinity and not a number. They are written as float literals, see below, and their default value is `0.0`. An `F64` is the type of a float literal where nothing says, as in `x := 1.42`, and an `F32` is written out: `x : F32 = 1.42`.

A number with a fractional part is a float literal: decimal digits, a dot and decimal digits, such as `1.42`, `42.4445` or `0.5`.

```
my_float := 1.42
other_float := 42.4445
negative := -0.5
```

There are digits on both sides of the dot, so `1.` and `.5` are not literals: write `1.0` and `0.5`. An underscore is a digit separator between two digits, on either side of the dot, as it is in an integer literal, so `1_000.000_1` is one number. A float literal has no exponent, no suffix and no other base, so `1e3`, `1.5f` and `0x1.8` are errors, and a literal that has a letter after its digits is an error that names the character, as for an integer. The dot of a range, as in `0..<5`, is not followed by a digit, so it is never part of a float literal.

A float literal is not an integer: `1.0` and `1` are different literals, and an integer is not converted to a float or the other way round without `cast`. The value of a literal is the number that its digits are nearest to, as a 64-bit floating point number (a `double` in C), so `1.42` is stored as the nearest number to it that the type can hold, which is not exactly 1.42 for most numbers, as in every language that has floats. A literal that is larger than the largest 64-bit float, about 1.8e308, is an error. A `-` directly before the digits makes a signed float literal in the same places where it makes a signed integer literal, see [Integers](#integers), so `-0.5` is one literal and `a - 0.5` subtracts.

A float literal is untyped, like an integer literal, and it is an `F32` or an `F64` where one is wanted, and an `F64` where nothing says, as in `x := 1.42`, which is the default type of a float, see [Untyped constants](#untyped-constants). A literal that is too large for an `F32`, above about 3.4e38, is an error where an `F32` is wanted, and it is rounded to the nearest number of the type, so `0.1` is not exactly 0.1 in either. Arithmetic and comparisons of float literals are known when the program is compiled, as they are for integer literals, so `1.5 + 2.25` is the untyped float 3.75, and dividing by zero is an error there.

```
scale :: proc(x: F32, factor: F32) F32 ->
    return x * factor + 1.0

average :: proc(a: F32, b: F32) F32 ->
    sum := a + b
    return sum / 2.0
```

The arithmetic operators `+`, `-`, `*` and `/` take two floats of one type and give a float of that type, and so do the assignment operators `+=`, `-=`, `*=` and `/=`, see [Operators](#operators). The comparison operators take two floats of one type, see [Comparison operators](#comparison-operators). Nothing is converted implicitly, so an `Int` is not a float, an `F32` is not an `F64`, and an operand that is a mixture of them is an error, and `cast` converts, see [Casts](#casts): `cast(F32)count` makes an `F32` of an `Int`, `cast(Int)ratio` makes an `Int` of a float, dropping its fraction, and `cast(F64)small` and `cast(F32)large` change the size. An untyped float, which a float literal is, becomes an `F32` or an `F64` where one is wanted, see [Untyped constants](#untyped-constants).

An operation of floats follows IEEE 754, so it never traps and never wraps around: a result that is too large is infinity, `1.0 / 0.0` is infinity when the program runs, and `0.0 / 0.0` is not a number. A comparison with not a number is false for all of `==`, `<`, `<=`, `>` and `>=`, and true for `not=`, so a number that is not equal to itself is not a number. Every result is rounded to the nearest number of its type, so `0.1 + 0.2 == 0.3` is not true in the way that it is not in any language that has floats, in either type, and a comparison for equality of results that are computed is usually wrong: compare the difference with a small number instead. The result of a division by zero is known when the program is compiled when both operands are constants, which is an error, see [Constants](#constants).

The result of an `F32` is the same on every target, because the operations are the ones of the standard and nothing in between is kept in more digits than the type has. A procedure of the C library that takes a `float` is declared with an `F32` and one that takes a `double` with an `F64`, see [Foreign procedures in the C library](#foreign-procedures-in-the-c-library).

> **Lessons learnt from C** In C, an integer is converted to a float and a float to a double without being asked to, so `1 / 2 * x` is 0 and `1.0 / 2 * x` is not, and a `float` that is passed to a procedure that takes a variable number of arguments is converted to a `double`, which a format that expects a `float` reads wrongly. In Mialu a float is never converted to another type unless `cast` says so, so `1 / 2.0` is an error, and what a procedure receives is of the type that is written.

### Strings

A string literal is the text between a pair of double quotes, on one line:

```
"Hello, world"
```

A `Str` is encoded in UTF-8: its bytes are always a valid UTF-8 encoding of text, as defined by [RFC 3629](https://www.rfc-editor.org/rfc/rfc3629). A string can hold any Unicode character, such as `"héllo"` or `"日本語"`, and each character takes one to four bytes.

A string is length-based, not null-terminated. It is its bytes together with the number of them, so the length is known without looking for an end, and a string can hold a zero byte. The bytes are what a string is: it is stored, copied and given to the C library as bytes, and nothing in it is converted or normalized. The empty string `""` has no bytes. The operations that go by the position of a character, which are the subscript and `length_of`, read the bytes as UTF-8 and count characters, see below. A string is immutable, and does not own its bytes: a string literal is a view of bytes that are part of the program, so returning one from a procedure is safe.

> **Lessons learnt from C** In C, a string is a pointer to bytes that end with a zero, so its length is found by walking over it every time, a string cannot hold a zero byte, and a function that copies or appends to one has to be trusted to have enough room, which is the source of a great many buffer overflows. A missing terminator makes code read on past the end of the string. In Mialu a `Str` knows its length, so none of that is needed, and the null-terminated form exists only as the separate type `C_Str`, for interoperating with C code.

The compiler rejects a string literal that is not valid UTF-8. A string cannot contain a double quote or a line break, because they would end it, and cannot contain a backslash. There are no escape sequences.

A `C_Str` (short for "C string") represents a null-terminated string. Unlike `Str`, it doesn't store length, it must determined by a null-terminator, which is a byte with value `0`.

A `C_Str` is for interoperating with existing C code, as in [foreign procedures](#foreign-procedures), the equivalent type in C being `char*` or `char const*` Nothing can be done with a `C_Str` but to keep it in a variable or a parameter, pass it on, return it, and hand it to a foreign procedure.

A `Str` value is never converted to a `C_Str` implicitly. A string literal, and a constant that is an untyped string, is not a `Str` yet: it is a `C_Str` where one is wanted, which is the same as `str_to_c_str` of it, see [Untyped constants](#untyped-constants). The built-in procedure [`str_to_c_str`](#str_to_c_str) makes a `C_Str` of a `Str` explicitly, and how long the `C_Str` is good for is there too.

A character is a Unicode code point, which is what a [`Rune`](#characters) holds, and it takes one to four bytes. A string is subscripted by the character, and not by the byte. The subscript `s[i]` is the `Rune` of the character at the index `i`, which is counted from 0, and `length_of(s)` is the number of characters, see [length_of](#length_of):

```
s := "äiti"
first := s[0]
second := s[1]
count := length_of(s)
```

`first` is `'ä'`, which takes two bytes, `second` is `'i'`, and `count` is 4, though the string has 5 bytes. So `"héllo"` has 5 characters and takes 6 bytes. An index that is negative or not below the number of characters stops the program, as an index outside of an array does. The subscript is a value and not a place, so a character cannot be assigned to, `s[0] = 'x'` is an error, since a string is immutable, and the address of one cannot be taken. The index must be an `Int`.

A part of a string is taken with a range, `s[low:high]`, which is a string and not a `Rune`. The range is half-open, like the range of a [for statement](#for-statements) and the slice of an [array](#array-slices): it has the characters from `low` up to, and not including, `high`, so its length is `high - low`, and like the subscript, the bounds count characters and not bytes:

```
text := "Hello, World!"

a := text[0:5]
b := text[7:12]
```

`a` is `"Hello"` and `b` is `"World"`. Unlike the subscript of a single character, which gives a `Rune`, the range gives another string, which can be subscripted, sliced again, compared, joined to a `C_Str` with `str_to_c_str` and given anywhere a `Str` is wanted. The string that it makes is not a copy: like a string literal, it is a view of bytes that are not its own, the ones of the string that it is a slice of, so making one allocates nothing, and one that is returned from a procedure stays good as long as the bytes that it views do, which is for ever when they are the bytes of a literal. A slice of a character in the middle of the string is on the bytes of whole characters, since the bounds are characters, so the result is always valid UTF-8, as every `Str` is. Both bounds must be written, and both must be `Int` values. `low` must not be above `high`, and `high` must not be above the number of characters, and a program that has other bounds is stopped, as for an array. The range `s[2:2]` has no characters, and is the empty string.

`array_slice[low:high]` makes a view of the elements of an array in the same way, see [Array slices](#array-slices): the difference is that the bounds of the elements of an array are found at once, and the bounds of the characters of a string are found by walking over the bytes from the start, like the subscript.

The characters of a string are gone through by a [for statement](#for-statements) as well, with the iterator a `Rune`, which does not walk over the string again for each one, as the subscript does:

```
for character in "Héllo" ->
    ...
```

Characters are found by walking over the string from its start, because the bytes of the characters before one say how far it is, so `s[i]` and `s[low:high]` take as long as the index is large, and not the same time for every index as the subscript of an array does. The characters are code points: a character that a person sees can be more than one of them, such as an `e` with an accent that is written as two code points, and a string is never normalized, so `"é"` of one code point and of two are different strings, with different lengths.

A program that needs the bytes of a string, and not its characters, should convert the `Str` to a `[]Byte`, and work on that. This is the recommendation whenever one wants to go through a string by byte, or to know how many bytes it has: `length_of` of the `[]Byte` is the length in bytes, a for statement over it gives each `Byte`, and a subscript of it takes the same time for every index and is the byte at that place. That is what the bytes are for, such as a size that is written to a file or sent over a network, a checksum, a parser of a format that is made of bytes and not of text, or a test for one ASCII character, and it is not what the operations of the string do, which go by the character. The `[]Byte` is a view of the bytes of the string, and not a copy, as an [array slice](#array-slices) is a view of the elements of an array, so the conversion allocates nothing and costs nothing, and the `[]Byte` cannot be used to change a string, which is immutable. Counting the bytes by `length_of` of the string, which counts characters, or going through a string by `s[i]` to get at its bytes, takes more time and gives the wrong answer for a string that is not ASCII, so it is not the way. See [Converting a string to bytes](#converting-a-string-to-bytes), which says that the language does not have this conversion yet.

Strings are joined with `++`, which has the same precedence as `+` and `-` and is left associative. It takes two strings, or a string and a rune on either side, and a rune is joined as its UTF-8 bytes, so `"Hello" ++ ' ' ++ "world" ++ '🚀'` is the string `"Hello world🚀"`. Both operands must be constants, see [Constants](#constants), because the joining is done when the program is compiled: the program has the joined text as one string, like a string literal, and nothing is joined when it runs. `++` does not allocate, and an operand that is known only when the program runs, such as a variable or the result of a call, is an error. Two runes are not joined, and neither is a number, so `'a' ++ 'b'` and `"count: " ++ 3` are errors. The result is an untyped string, so it can be given to a `C_Str` too, unless an operand is a constant that was given the type `Str`, and then the result is a `Str`. The joined text of all the constants of a program is limited in size, and a program that has more is rejected.

```
GREETING :: "Hello"
SEPARATOR :: ','
TITLE : Str : '«' ++ GREETING ++ SEPARATOR ++ " world" ++ '»'

main :: proc() Str ->
    return "<" ++ TITLE ++ '>'
```

## Operators

Mialu supports the following operators. A higher precedence binds tighter:

| Operator | Description | Precedence | Example |
|----------|-------------|------------|---------|
| `or`     | Logical or, see [Logical operators](#logical-operators). | 1 | `a or b` |
| `and`    | Logical and. | 2 | `a and b` |
| `not`    | Logical not. It is written before its operand, and may be repeated. | 3 | `not a` |
| `==`     | Equal to. A comparison, see [Comparison operators](#comparison-operators). | 4 | `x == 3` |
| `not=`   | Not equal to. A comparison. `not` and `=` are written together, without a space. | 4 | `x not= 3` |
| `<`      | Less than. A comparison. | 4 | `x < 3` |
| `<=`     | Less than or equal to. A comparison. | 4 | `x <= 3` |
| `>`      | Greater than. A comparison. | 4 | `x > 3` |
| `>=`     | Greater than or equal to. A comparison. | 4 | `x >= 3` |
| `+`      | Addition. | 5 | `x := 1 + 2` makes `x` 3 |
| `-`      | Subtraction. | 5 | `x := 5 - 1` makes `x` 4 |
| `++`     | Joins strings. It joins two strings, or a string and a rune, into one string when the program is compiled. See [Joining strings](#strings). | 5 | `"Hello" ++ '!'` is `"Hello!"` |
| `*`      | Multiplication. | 6 | `x := 2 + 3 * 4` makes `x` 14 |
| `/`      | Division. The result is truncated toward zero. | 6 | `x := 20 - 12 / 4` makes `x` 17 |
| `%`      | Address of. It is written before its operand. See [Address of and dereference operators](#address-of-and-dereference-operators). | 7 | `%x` is a pointer to `x` |
| `cast`   | Cast. `cast(Type)` is written before its operand, and converts its value to the type, where nothing is lost. It binds like `%`. See [Casts](#casts). | 7 | `cast(Int)b` is the `Int` of the `Byte` `b` |
| `[ ]`    | Subscript. The index of an element between brackets. It is written after the name of an array or of an array slice, and binds tighter than every operator. See [Elements](#elements). | 8 | `a[2]` is the element at index 2 of `a` |
| `.`      | Field access. The name of a field after a struct object, which is a name, an element of an array, what a pointer points to, or another field. See [Fields](#fields). | 8 | `p.age` is the field `age` of `p` |
| `[ : ]`  | Slice. The two bounds of an array slice between brackets, separated by a colon. It includes the low bound and excludes the high bound. See [Array slices](#array-slices). | 8 | `a[1:4]` has the elements 1 through 3 |
| `^`      | Dereference. It is written after its operand. See [Address of and dereference operators](#address-of-and-dereference-operators). | 8 | `p^` is the value that `p` points to |
| `+=`     | Addition assignment. It adds the value to a variable that is defined already. | 0 | `x += 2` adds 2 to `x` |
| `-=`     | Subtraction assignment. It subtracts the value from a variable. | 0 | `x -= 2` makes `x` 2 less |
| `*=`     | Multiplication assignment. It multiplies a variable by the value. | 0 | `x *= 3` triples `x` |
| `/=`     | Division assignment. It divides a variable by the value, truncating toward zero. | 0 | `x /= 2` halves `x` |
| `:`      | Type separator. It separates a name from its type. | 0 | `x : Int = 5` |
| `::`     | Defines a constant. It is two subsequent `:`. | 0 | `NAME :: "Bob"` |
| `:=`     | Defines a variable, deducing its type from the value. It is `:` followed by `=`, with the type left out. | 0 | `x := 4` |
| `=`      | Sets the initial value of a variable whose type is written. It also assigns a new value to a variable, an element or a field, and copies to a range of elements. | 0 | `x : Int = 5`, `x = 6`, `a[0] = 5`, `p.age = 5` |

A binary operator is written between its two operands, such as `a + b`. The operands are integer literals, float literals, rune literals, string literals, `true`, `false`, names, [calls](#calling-procedures) or [array literals](#array-literals), which can have subscripts, or the results of operators of a higher precedence. An operand can have `%` or `cast(Type)` before it, and a name can have `^` after it. `not` is a unary operator, written before its operand. A comparison takes two `Int` values, or two `Bool` values or two pointers of one type for `==` and `not=`, and its result is a `Bool`.

> **Lessons learnt from C** In C, `a < b < c` is valid, and it does not mean what it means in mathematics: it compares the `0` or `1` of `a < b` with `c`. Mialu makes the comparison of a comparison an error, so the mistake is found at compile-time.

Operators of the same precedence are left associative, and an operator of a higher precedence binds tighter, see [Arithmetic operators](#arithmetic-operators). A comparison binds looser than `+` and `-`, and tighter than `not`, `and` and `or`, so `a + 1 > b * 2` means `(a + 1) > (b * 2)`. A comparison is an expression of type `Bool`, but its operands are not comparisons, so it cannot be an operand of another operator, and `a < b < c` and `a == b == c` are errors.

The operators of precedence 0 define names or assign to them, and are not part of an expression. Each one is written after the name that it defines or assigns to, which can be followed by subscripts, fields and carets, as in `a[0] = 1`, `p.age = 1` and `p^ = 1`, and takes everything up to the end of the line as its right-hand side, so `x := a + b` means `x := (a + b)`, and `x += a * b` means `x += (a * b)`. Spaces may be written between the characters of `::` and `:=`, because they are separate tokens. An assignment operator such as `+=`, `-=`, `*=` or `/=` is one token, so no space may be written between its characters.

See [Variables](#variables) for variable definitions and [Constants](#constants) for constants.

### Arithmetic operators

Addition, subtraction, multiplication and division are defined for [`Int`](#integers) and for the [float types](#floating-point-numbers), so both operands must be `Int` or both must be of one float type, and the result is of the same type, and `a + 1.5` is an error when `a` is an `Int`, and so is the sum of an `F32` and an `F64`. A [`Byte`](#bytes) has no arithmetic, and is converted with [`cast(Int)`](#casts) first.

| Operator | Meaning | Precedence | Example |
|----------|---------|------------|---------|
| `a + b`  | Addition. | 5 | `x := 1 + 2` makes `x` 3 |
| `a - b`  | Subtraction. | 5 | `x := 5 - 1` makes `x` 4 |
| `a * b`  | Multiplication. | 6 | `x := 2 + 3 * 4` makes `x` 14 |
| `a / b`  | Division. The result is truncated toward zero. | 6 | `x := 20 - 12 / 4` makes `x` 17 |

`+` and `-` have the same precedence, and so do `*` and `/`, and operators of the same precedence are left associative. So `a + b + c` means `(a + b) + c`, `a - b + c` means `(a - b) + c`, `5 - 1 + 2` is 6 and not 2, and `8 / 2 * 3` is 12 and not 1. An operator of a higher precedence binds tighter, so `2 + 3 * 4` means `2 + (3 * 4)`, which is 14 and not 20, and `20 - 12 / 4` means `20 - (12 / 4)`, which is 17 and not 2. Parentheses are not part of the language, so the order is changed by naming a part of the expression in a variable.

Division of `Int` values truncates toward zero, so `7 / 2` is 3 and `-7 / 2` is -3, while division of floats is not truncated, so `7.0 / 2.0` is 3.5. Dividing by zero is an error at run time, see [WebAssembly](#webassembly), and an error at compile-time when both operands are constants, see [Constants](#constants). Arithmetic that goes beyond the range of an `Int` wraps around when the program runs. The arithmetic can be combined with an assignment, see [Assignment and compound assignment operators](#assignment-and-compound-assignment-operators).

### Assignment and compound assignment operators

An assignment changes the value of a variable that is defined already. It is a statement that has the name of the variable, an assignment operator and a value, which is an expression.

> **Lessons learnt from C** In C, assignment is an expression. While it can be useful in certain conditions and loops, it has often produced logical errors due to the fact that people have mistyped equality operator `==`, typing in `=` instead. This, combined with implicit conversions in C, leads into situations where an assignment expression ends up having a truthy value more than necessary. In Mialu, this mistake cannot happen because assignment is never an expression.

The plain assignment `=` gives the variable a new value of its type, whatever the type is: `x = 5` for an `Int`, and `p = Person { age = 30, name = "Bob" }` for a struct object, which replaces all of its fields. The value must be of the type of the variable, so `x = true` is an error for an `Int`. An array is not assigned to as a whole.

```
foo :: proc() Int ->
    x := 4
    x = 5
    return x
```

`foo` returns 5. The value is evaluated first, with the value that the variable has before the assignment, so `x = x * 2 + 1` doubles `x` and adds 1. It is a statement and not an expression, so a value cannot be assigned to two variables in a row, and each assignment keeps the value that the variable has until the next one: a variable that is assigned to twice has the last value. A variable can be given the value of another one, and two variables are swapped with the help of a third:

```
swap :: proc() Int ->
    a := 3
    b := 7
    temp := a
    a = b
    b = temp
    return a * 10 + b
```

`swap` returns 73. An assignment can be in a block of an if or a for statement, and the variable that it assigns to can be defined above the block. A variable that is defined with a type and no value, such as `x : Int`, is given its value with `=` as well. The name must stand for a variable, so `n = 2` is an error for a parameter `n`, and so is assigning to a constant or to the iterator of a for statement. The other assignments work on an `Int` or a float. `+=` is the addition assignment: it adds the value to the variable and stores the sum in the variable, so `x += 2` leaves `x` 2 greater than it was.

A compound assignment operator applies an arithmetic operator to the variable and the value, and stores the result in the variable, so `x += 2` means `x = x + 2`. The variable and the value must be both `Int` or both floats of one type.

| Operator | Meaning |
|----------|---------|
| `x += v` | adds `v` to `x` |
| `x -= v` | subtracts `v` from `x` |
| `x *= v` | multiplies `x` by `v` |
| `x /= v` | divides `x` by `v`, truncating toward zero |

`+=` is the addition assignment: it adds the value to the variable and stores the sum in the variable, so `x += 5` leaves `x` 5 more than it was:

```
foo :: proc() Int ->
    x := 4
    x += 5
    return x
```

`foo` returns 9. `/=` is the division assignment: it divides the variable by the value and stores the quotient in the variable, so `x /= 2` leaves `x` halved, truncated toward zero like the `/` operator:

```
foo :: proc() Int ->
    x := 20
    x /= 4
    return x
```

`foo` returns 5. `*=` is the multiplication assignment: it multiplies the variable by the value and stores the product in the variable, so `x *= 3` leaves `x` tripled:

```
foo :: proc() Int ->
    x := 6
    x *= 7
    return x
```

`foo` returns 42. `-=` is the subtraction assignment: it subtracts the value from the variable and stores the difference in the variable, so `x -= 2` leaves `x` 2 less than it was:

```
foo :: proc() Int ->
    x := 9
    x -= 4
    return x
```

`foo` returns 5. An element of an array can be assigned to as well, such as `a[0] += 2`, see [Elements](#elements). Dividing by zero is an error at run time, see [WebAssembly](#webassembly). The value is evaluated first, with the value that the variable has before the assignment, so `x += x` doubles `x`, `x += 2 * 3` adds 6, `x -= 1 + 1` subtracts 2, `x *= 1 + 2` multiplies `x` by 3, and `x /= 1 + 1` divides `x` by 2. The name must be a variable of the procedure, defined above the assignment, or a global variable: a parameter or a constant cannot be assigned to. The variable and the value must be an `Int`, because addition, subtraction, multiplication and division are defined for `Int` only.

### Comparison operators

A comparison takes two operands and is an expression of type `Bool`, so it can be the value of a variable, a returned value or the condition of an if statement:

| Operator | Result |
|----------|--------|
| `a == b` | `true` when `a` is equal to `b` |
| `a not= b` | `true` when `a` is not equal to `b`. `not` and `=` are written together, without a space |
| `a < b` | `true` when `a` is less than `b` |
| `a <= b` | `true` when `a` is less than or equal to `b` |
| `a > b` | `true` when `a` is greater than `b` |
| `a >= b` | `true` when `a` is greater than or equal to `b` |

```
is_positive :: proc(n: Int) Bool ->
    return n > 0

is_even :: proc(n: Int) Bool ->
    return n / 2 * 2 == n
```

Both operands must have the same type, because nothing is converted. Two [`Int`](#integers) values can be compared with all six operators, and the comparison is signed. Two [`Rune`](#characters) values can be compared with all six operators too, and the comparison is of their code points. Two [floats](#floating-point-numbers) of one type can be compared with all six operators too, and an untyped float is of that type there, so `x < 0.5` is allowed whether `x` is an `F32` or an `F64`. A comparison with not a number is false, except `not=`, which is true. `Int`, `Byte`, `Rune` and the floats are the only types that have an order, so `<`, `<=`, `>` and `>=` are defined for them only. Two [`Bool`](#booleans) values can be compared with `==` and `not=`, so `a not= b` holds when exactly one of them is `true`. Two pointers can be compared with `==` and `not=` as well, and so can two [pointers to procedures](#pointers-to-procedures), see [Null and lifetime](#null-and-lifetime). Two members of one [enum](#enumerations) can be compared with `==` and `not=` too. Two [`Byte`](#bytes) values can be compared with all six operators, and the comparison is of the numbers from 0 to 255; an untyped integer from 0 to 255 is a `Byte` when the other operand is one, so `b >= 65` is allowed, but a `Byte` is never compared with an `Int`. Two [strings](#strings) can be compared with `==` and `not=`: they are the same when they have the same length and the same bytes, so `"go"` is not the same as `"gone"` or `"Go"`, and `""` is the same as the empty string only. A string has no order, so `<`, `<=`, `>` and `>=` are errors for strings, and so is the comparison of a [`C_Str`](#c-strings), which has no length that is known, with anything. An operand is a `Str` or an untyped string, such as a literal, which is a `Str` there. The comparison of two constants is a constant, which is worked out when the program is compiled, so `SAME :: "a" == "b"` is the untyped truth value `false`. Strings that are known only when the program runs are compared then, byte by byte, and the comparison stops at the first byte that differs. Two arrays or array slices of `Byte` can be compared with `==` and `not=` too, see [Comparing bytes](#comparing-bytes). A `Byte` can be compared with `==` and `not=` to a rune that is known when the program is compiled and is in the ASCII range, see [Bytes](#bytes). Example:

```
is_help :: proc(command: Str) Bool ->
    return command == "help" or command == "-h"
```

A comparison binds looser than `+` and `-`, and tighter than `not`, `and` and `or`, see [Operators](#operators).

### Logical operators

`and`, `or` and `not` are keywords, and their operands must be `Bool`: an `Int` or a string is an error, because nothing is converted to a `Bool`. The result is a `Bool`.

| Operator | Result |
|----------|--------|
| `a and b` | `true` when both `a` and `b` are `true` |
| `a or b` | `true` when at least one of `a` and `b` is `true` |
| `not a` | `true` when `a` is `false`, and `false` when `a` is `true` |

```
in_range :: proc(n: Int, low: Int, high: Int) Bool ->
    return n >= low and n <= high

out_of_range :: proc(n: Int, low: Int, high: Int) Bool ->
    return n < low or n > high

can_enter :: proc(age: Int, has_ticket: Bool, is_banned: Bool) Bool ->
    return age >= 18 and has_ticket and not is_banned
```

A comparison binds tighter than all three, so `n >= low and n <= high` needs no grouping. `not` binds tighter than `and`, and `and` binds tighter than `or`, so `a or b and c` means `a or (b and c)`, and `a and not b or not a and b` is true when exactly one of `a` and `b` is. `not` binds looser than a comparison, so `not a == b` means `not (a == b)`, and it may be repeated: `not not a` is `a`. `and` and `or` are left associative. There are no parentheses yet, so the precedence is the only way to group.

Both operands of `and` and `or` are always evaluated: there is no short circuit. That makes no difference for a value that is already computed, but in `d not= 0 and n / d > 1` the division still happens, and traps when `d` is zero.

A word must be separated from what is next to it by whitespace, unlike a symbol, so `a and b` cannot be written `aandb`. `not=` is one token for the inequality, and the keyword `not` is a `not` that is not directly followed by `=`.

### Address of and dereference operators

| Operator | Result |
|----------|--------|
| `%a` | a pointer to `a`, of type `^T` when `a` is of type `T` |
| `p^` | what the pointer `p` points to, which can be assigned to |

`%` is the address of operator. It is written before its operand, which must have an address, such as a variable, a field, an element, or what a pointer points to, and gives a pointer to it. It binds looser than what follows a name, so `%p.x` is the address of `p.x` and `%a[1]` is the address of the element, and tighter than every operator. `^` is the dereference operator. It is written after an expression whose value is a pointer, and gives what the pointer points to, so a chain of pointers reads from left to right, as in `pp^^`. It binds like `.` and `[ ]`, and `p^` can be assigned to, with `=` and with the compound assignment operators.

```
increment :: proc() Int ->
    x := 4
    p := %x
    p^ += 1
    return x
```

`increment` returns 5. Which operands have an address, what a pointer to a field or an element is, and how the pointers behave with `nil` and with structs, is described in [Pointers](#pointers), see [Taking an address](#taking-an-address) and [Following a pointer](#following-a-pointer).

### Casts

A cast converts a value to another type with `cast(Type)` before the value: `my_int := cast(Int)my_byte`. Nothing is converted implicitly in Mialu, so a cast is how a value of one type is used as a value of another, and it is written wherever the conversion happens, which makes it easy to find.

A cast to `Int` of a `Byte`, a `Bool`, an `I32`, an `I64` or a `Rune` never loses anything, and neither does a cast of those, or of an `Int`, to an `I64`. A cast of a `Byte`, a `Bool`, a `Rune` or an `I32` to an `I32` never does either, but an `Int` or an `I64` is not cast to an `I32`, see [`int_to_i32`](#int_to_i32), a cast of a `Byte` to a `Rune` never does either, and a cast of a `Rune` to the 4 bytes of its UTF-8 encoding, `[4]Byte`, never does, because no character takes more than 4 bytes. A cast of an `Int` to a `Rune` is not allowed, since most numbers are not Unicode characters. and a cast of an `Int` to a float rounds to the nearest number of the type when the number has more digits than it holds, and a cast of a float to an `Int` drops the fraction, truncating toward zero, and stops the program for a number that is not in the range of an `Int` and for not a number, since there is no `Int` for it. A cast of an `F32` to an `F64` never loses anything, and a cast of an `F64` to an `F32` rounds, to infinity for a number that is too large. Today these casts are allowed, and every other one is an error:

| From | To | Result |
|------|----|--------|
| `Byte` | `Int` | The number of the byte, from 0 to 255. |
| `Bool` | `Int` | 1 for `true` and 0 for `false`. |
| `I32` | `Int` | The same number, with its sign, from -2147483648 to 2147483647. |
| `I64` | `Int` | The same number. |
| `Int` | `Int` | The same number. |
| `Int`, `I32`, `Byte`, `Bool` or `Rune` | `I64` | The same number. |
| `I64` | `I64` | The same number. |
| `Byte`, `Bool` or `Rune` | `U32` | The same number, which fits. |
| `U32` | `U32` | The same number. |
| `Int`, `I32`, `I64`, `U32`, `Byte`, `Bool` or `Rune` | `U64` | The same number, which for a negative one is the number that has its bits, so `-1` is 18446744073709551615. |
| `U64` | `U64` | The same number. |
| `U32` | `Int`, `I64` or `F32` or `F64` | The same number, exactly, or the nearest float. |
| `U64` | `Int` or `I64` | The number that has the same bits, which is negative above 9223372036854775807. |
| `Byte`, `Bool` or `Rune` | `I32` | The same number, which fits, since a `Rune` is at most 0x10FFFF. |
| `I32` | `I32` | The same number. |
| `Rune` | `Int` | The code point of the character, from 0 to 0x10FFFF. |
| `Byte` | `Rune` | The character with that code point, one of the first 256, which are the ones of Latin-1: 65 is `'A'`, and 233 is `'é'`. |
| `Rune` | `Rune` | The same character. |
| `Rune` | `[4]Byte` | The UTF-8 encoding of the character, which is 1 to 4 bytes, followed by as many zero bytes as it takes to make 4, see below. |
| `F32` or `F64` | `Int` | The number with its fraction dropped, truncated toward zero, so `-3.75` is -3. The program stops for a number that is not in the range of an `Int`, and for not a number. |
| `Byte` | `F32` or `F64` | The number of the byte, exactly. |
| `I32` | `F32` | The nearest `F32`, which is the number itself when it has at most 24 significant bits. |
| `I32` | `F64` | The number, exactly. |
| `I64` | `F32` or `F64` | The nearest float, as for an `Int`. |
| `F32` or `F64` | `I64` | The number with its fraction dropped, as for an `Int`, and the program stops for a number that is not in the range. |
| `Int` | `F32` | The nearest `F32`, which is the number itself when it has at most 24 significant bits. |
| `Int` | `F64` | The nearest `F64`, which is the number itself when it has at most 53 significant bits. |
| `F32` | `F64` | The same number, exactly. |
| `F64` | `F32` | The nearest `F32`, which is infinity or minus infinity for a number that is too large. |
| `F32` | `F32` | The same number. |
| `F64` | `F64` | The same number. |

```
count := cast(Int)small + cast(Int)flag
```

The cast of a rune to bytes makes an array of 4 bytes, which is used like the array that an array literal makes: it has a length, 4, it can be subscripted, gone through by a for statement and passed as an array slice, and it is in the frame of the procedure that makes it, so it does not outlive that. The bytes are those of the character in UTF-8, so `cast([4]Byte)'A'` is `[0x41, 0, 0, 0]`, `cast([4]Byte)'é'` is `[0xC3, 0xA9, 0, 0]`, `cast([4]Byte)'芋'` is `[0xE8, 0x8A, 0x8B, 0]` and `cast([4]Byte)'🚀'` is `[0xF0, 0x9F, 0x9A, 0x80]`. The bytes after the encoding are zero, so the array of an ASCII character is a C string of one character, and the number of bytes of the encoding is found by counting the bytes before the first zero, except for the character with the code point 0, which is one byte that is zero.

```
encode :: proc(character: Rune) Int ->
    bytes := cast([4]Byte)character
    length := 0
    for byte in bytes ->
        if byte not= 0 ->
            length += 1
    return length
```

`encode('🚀')` returns 4, `encode('é')` returns 2 and `encode('A')` returns 1. A rune is made of a `Byte` with `cast(Rune)`, and a `Rune` is made an `Int` with `cast(Int)`, which are the casts of a character to and from its number, and nothing makes a rune of an `Int` or of bytes, because those are not always characters.

The other direction, to a type that holds fewer values, is not a cast, because a value may not fit: [`int_to_byte`](#int_to_byte) makes a `Byte` of an `Int` and stops the program when the number is not from 0 to 255, see [Bytes](#bytes). A `Bool` is never made out of an `Int`, which is written as a comparison, `n not= 0`, see [Booleans](#booleans).

A cast binds like `%`: it is written before an operand, which can have subscripts, fields and carets after it, and it binds tighter than every operator, so `cast(Int)a + b` is `(cast(Int)a) + b` and `cast(Int)a[2]` is the cast of the element. A cast cannot follow a cast, which would do nothing, and a cast is not a constant, so `LIMIT :: cast(Int)SMALL` is an error. The type is a name, and the word `cast` is a [keyword](#reserved-words).

> **Lessons learnt from C** In C, a cast is written `(int)x` whatever it does, so one spelling converts a number, throws away the high bits of another, and reinterprets a pointer as an integer, and it is easy to confuse with a parenthesized expression. In Mialu a cast is a keyword that is found by searching for it, and it is allowed only where nothing is lost.

## Constants

A constant is an immutable value with a name, and it is visible in the whole module, also above where it is defined. It is defined by placing two subsequent colons after an identifier:

```
CONSTANT_NAME :: value
```

Constants are commonly used to define procedures:

```
sum :: proc(a: Int, b: Int) Int ->
    return a + b
```

A constant can also be a [struct](#structs) or an [enum](#enumerations), which are types, the [member of an enum](#enumerations), or a value that is known when the program is compiled, which is written on its own line:

```
LIMIT :: 100
GREETING :: "Hello, world"
DEBUG :: true
```

The value of such a constant is a constant expression: a literal, the name of another constant, or the arithmetic, comparison and logical operators applied to them, such as `DOUBLE_LIMIT :: LIMIT * 2` or `VERBOSE :: DEBUG and LIMIT > 50`, and `++` applied to strings and runes, such as `HELLO :: "Hello" ++ ' ' ++ NAME`, see [Strings](#strings). The compiler works the value out, so a constant costs nothing when the program runs: a use of its name is its value, as if the literal were written there. A constant can be used above the line where it is defined, like any constant, and it can be defined in terms of constants below it, but not in terms of itself, so `A :: B` together with `B :: A + 1` is an error. A call, a variable, a parameter and a global variable have a value only when the program runs, so `START :: clock()` is an error.

Arithmetic in a constant expression is exact. A step whose result does not fit in an `Int`, such as `0x7FFF_FFFF_FFFF_FFFF + 1`, is an error, and so is a division by zero, such as `1 / 0`, where the same operation on variables would wrap around or trap when the program runs. That goes for the parts of any expression that are constants, so `x + 1 / 0` is an error as well.

A literal has no type of its own. `200` is an untyped integer: a number without a size, which becomes the type that the place where it is used wants, when its value fits in that type. The same goes for every constant that is defined without a type, such as `LIMIT :: 200`, and for every constant expression of them, such as `LIMIT / 2`. So one constant can be given to an `Int` and to a `Byte`:

```
LIMIT :: 200

both :: proc() Int ->
    count : Int = LIMIT
    small : Byte = LIMIT
    return count + cast(Int)small
```

An untyped constant becomes a value of its type when the program is compiled, so nothing is converted while the program runs, and only where its value fits: `b : Byte = 300` and `b : Byte = LIMIT + 100` are errors, because 300 is not a `Byte`. There are four untyped kinds. Each has a default type, which is the type that an untyped constant has where its place does not say which type it wants, such as the value of a variable without a type, `x := LIMIT`:

| Kind | Literals | Becomes | Default type |
| ---- | -------- | ------- | ------------ |
| Untyped integer | `42`, `0b101010`, `0x2A` | `Int`, an `I64`, a `U64` when it is not negative, an `I32` when it is from -2147483648 to 2147483647, a `U32` when it is from 0 to 4294967295, or a `Byte` when it is from 0 to 255 | `Int` |
| Untyped float | `1.42`, `0.5` | `F32`, when it is not above the largest `F32`, or `F64` | `F64` |
| Untyped boolean | `true`, `false` | `Bool` | `Bool` |
| Untyped string | `"Hello"` | `Str`, or a `C_Str` | `Str` |

`nil` is untyped in the same way: it is a pointer of any type, see [Null and lifetime](#null-and-lifetime).

Two untyped operands give an untyped result, so `LIMIT * 2` is the untyped integer 400. A comparison of constants, such as `LIMIT > 100`, gives an untyped boolean, and so do `and`, `or` and `not` of untyped booleans. An untyped integer next to an `Int` that is known only when the program runs, as in `x + 1`, becomes an `Int`, and one that is compared with a `Byte`, as in `b == 65`, becomes a `Byte`, which it must fit in.

An untyped string is a `C_Str` where one is wanted, such as for a parameter of a [foreign procedure](#foreign-procedures), and it is then what `str_to_c_str` of it gives, see [Strings](#strings). So `puts("Hello")` passes the string to the C library without a call of `str_to_c_str`.

A constant can be given a type, which is written between its two colons, `name : Type : value`. A typed constant has that type, like a variable, and it is not converted, so it is used only where a value of its type is:

```
MASK : Byte : 0x0F
COUNT : Int : 3
NAME : Str : "Mialu"
```

The type of a constant is `Int`, `I32`, `I64`, `U32`, `U64`, `Byte`, `Rune`, `F32`, `F64`, `Bool` or `Str`, and its value must be a constant expression that has that type, or that is untyped and fits in it, so `MASK : Byte : 256` and `COUNT : Int : "three"` are errors. A typed constant follows the rules of its type: `MASK + 1` is an error, because a `Byte` has no arithmetic, and so is `b : Byte = COUNT`, because an `Int` is not a `Byte`. A typed and an untyped operand give a value of the type, so `COUNT * 2` is the `Int` 6.

That is how a literal is written once and used with every type that can hold it: no type is chosen for it until it is used, so it needs no conversion, which a [cast](#casts) and `int_to_byte` are for values that are known only when the program runs.

> **Lessons learnt from C** In C, every literal has a type, `int` by default, so `200` is an `int` that is converted to a `char` when it is given to one, whether it fits or not: `unsigned char b = 300;` compiles and gives 44. A `#define` has no type and no scope, and it is checked nowhere but where it is pasted in, while a `const int` is a variable, which cannot be used everywhere that C wants a constant. In Mialu a constant has a name with a scope and a value that the compiler works out exactly, and it has no type until it is used, where it becomes a value of the type that is wanted only when it fits, which is checked when the program is compiled.

## Variables

A variable is a name for a value that is local to a procedure. It is defined by a name followed by `:=` and its initial value, which is an expression:

```
x := 4
```

The type of the variable is deduced from the initial value. It is implicit. `x` above is of type `Int`, because `4` is an untyped integer, whose default type is `Int`, see [Untyped constants](#untyped-constants).

The type can also be written explicitly, between the `:` and the `=`:

```
x : Int = 5
```

When the type is written, the value can be left out. The variable then has the default value of its type, and it can be given another one with an assignment later:

```
x : Int
p : Person
```

The default value is 0 for an `Int`, `false` for a `Bool`, the empty string `""` for a `Str`, an array slice with no elements for an array slice, `nil`, the null pointer, for a [pointer](#null-and-lifetime), and for a [struct](#structs) the default value of each of its fields. An array has no default value, since it is made by an array literal, so `a : [3]Int` is an error.

> **Lessons learnt from C** In C, a local variable that is defined without a value holds whatever was in that memory before, and reading it is undefined behavior. The bug depends on what the program did earlier, so it can come and go with a change that has nothing to do with it. In Mialu every variable has a value from its definition on, either the one that is written or the default value of its type. In the future, the compiler may leave out default value initialization since if it can guarantee the type is initialized later that prevents undefined value from being read.

A variable is defined once, with its initial value, and every variable has one. It can be used in the statements below its definition, but not above it and not in its own initial value. The variables of a procedure are not visible outside of it, so two procedures can have a variable of the same name.

The values of a call of a procedure that returns several values are defined together, with a name for each, as in `quotient, remainder := divide(31, 4)`, see [Multiple return values](#multiple-return-values).

A variable that is defined outside of every procedure is a [global variable](#global-variables).

A global variable is a variable that is defined at file scope, outside of every procedure. It is written like a variable, with the attribute `static` before the `:`, and it starts in the first column like a constant:

```
counter #(static) := 0

next :: proc() Int ->
    counter += 1
    return counter
```

A global variable has static storage duration, which is what the `static` attribute says: it exists for as long as the program runs, and not only for as long as a call of a procedure does. It is initialized once, before any procedure is called, and it keeps the value that the procedures assign to it, so the first call of `next` returns 1, the second returns 2, and so on. Every global variable must have the attribute `static`, because storage of this kind is the only one that global variables can have.

The type of a global variable is deduced from its initial value, or written between the `:` and the `=`, as for any variable:

```
ready #(static) : Bool = false
```

The initial value is a constant expression, such as `0`, `"calls"`, `true` or `LIMIT * 2`, see [Constants](#constants), or else a field of [type information](#static-reflection), such as `type_info_of(Color).members`, which is known when the program is compiled too. It is stored when the program starts, before any code runs, so it cannot be a call, a variable or another global variable. An untyped value becomes the type that is written for the global variable, when it fits, or else the variable has its default type. A global variable can be used and assigned to in every procedure of the module, also in the ones that are written above its definition, because it is in the module scope, see [Scope resolution](#scope-resolution). Like a constant, it has a name of its own in that scope, so it cannot have the name of a constant or of another global variable, and a parameter or a variable of a procedure cannot have its name.

## Control flow structures

### If statements

An if statement runs a block of statements only when a condition holds. It is written like in C, but the condition has no parentheses. The `->` after the condition starts the block, which is the lines that are indented one level deeper than the `if`, so an indented block takes the place of the curly braces:

```
foo :: proc() Int ->
    x := 4
    if x > 5 ->
        return 1
    return 2
```

Here `foo` returns 2, because `x > 5` does not hold, so the block of the `if` is skipped and the statements after it run.

> **Lessons learnt from C** In C, a block is optional after `if`, so `if (a) b; c;` looks like it has two statements in the branch when only one is, and adding a line to an unbraced branch has caused serious security bugs. With a `->` and an indented block, the shape of the code is the shape of the program, and a branch always has a block.

A condition is an expression of type `Bool`: a comparison such as `x + 1 > y * 2`, which can use everything that an expression can, or a `Bool` that is stored in a variable or a parameter, or `true` and `false`. There is no implicit conversion to `Bool`, as in C: a condition that is an `Int`, such as `if x ->` or `if 1 ->`, is an error, and so is a string.

```
foo :: proc(n: Int) Int ->
    big := n > 100
    if big ->
        return 2
    return 1
```

An `else` branch runs when the condition does not hold. An `else if` is an `else` that has an `if` in it, so there can be any number of them, and the first condition that holds picks the block. The branches are tried in the order that they are written, and at most one of them runs. An `else` that comes last takes the case that none of the conditions holds:

```
foo :: proc() Int ->
    x := 4
    if x > 5 ->
        return 1
    else if x < 3 ->
        return 2
    else ->
        return 3
```

`foo` returns 3. `else` must be on a line of its own at the indentation of the `if` that it belongs to, directly below the block of the previous branch, and there is at most one `else`, which is the last branch. A block must have at least one statement, and a line that is indented deeper than the block it is in, without a header that asks for it, is an error.

If statements can be inside the blocks of other if statements, as many as 32 levels deep:

```
foo :: proc() Int ->
    x := 4
    if x > 5 ->
        return 1
    else ->
        if x < 3 ->
            return 2
        else ->
            return 3
```

A block is a scope, see [Scope resolution](#scope-resolution): a variable that is defined in a block can be used in the rest of that block, but not after it. To compare one value with many others, a [match statement](#match-statements) is shorter, and it computes the value once.

### Match statements

A match statement compares one value, which is called its subject, with the patterns of the cases below it, and runs the block of the first case whose pattern the value matches. It is the `switch` of C, but it matches strings, enums and ranges as well as numbers. After `match` comes the subject, which is any expression, and the `->` after it starts the cases, which are indented one level deeper than the `match`. Each case is the word `case`, a pattern and `->`, and its block is indented one level deeper still:

```
describe :: proc(x: Int) Str ->
    match x ->
        case 5 ->
            return "five"
        case ->
            return "something else"
```

`case ->` without a pattern is the default case, which runs when no other case matches. It catches everything that the cases above it leave, so it can only be the last case, and a case after it is an error. At most one case runs, and then the statement after the match runs next, as after an if statement, so there is no falling through from one case into the next, and no `break` is needed to stop it. When no case matches and there is no default case, nothing runs. The subject is computed once, before the first case, so a call in it runs once however many cases there are, and the cases all compare the same value.

> **Lessons learnt from C** In C, the cases of a `switch` fall through into each other unless each ends with `break`, and a forgotten `break` is a well-known bug that compilers can only warn about. A Mialu case never falls through: its block is its own, like the branch of an `if`.

The subject is an integer, which is an `Int`, a `Byte`, an `I32`, an `I64`, a `U32` or a `U64`, a `Str` or an enum. A pattern is known when the program is compiled: a literal, a [constant](#constants), or arithmetic of them such as `LIMIT + 1` or `0 - 1`, or a signed literal such as `-1`. It must be of the type of the subject, and an untyped one becomes it when it fits, so a pattern of a `Byte` is a number from 0 to 255. A pattern that is a variable or a call is an error, because what it matches would not be known until the program runs. A string matches a pattern when it has the same bytes, so `"go"` does not match `"gone"` or `"Go"`, and `""` matches the empty string only:

```
command_number :: proc(command: Str) Int ->
    match command ->
        case "go" ->
            return 1
        case "stop" ->
            return 2
        case ->
            return 0
```

An integer can be matched with a range, which is written as in a [for statement](#for-statements): `first..<end` has the numbers from `first` up to, and not including, `end`, and `first..<=end` includes `end`. Both ends are constants, and a range with no numbers, such as `5..<5`, is an error, because it could never match:

```
classify :: proc(n: Int) Str ->
    match n ->
        case 0 ->
            return "zero"
        case 1..<10 ->
            return "small"
        case 10..<=99 ->
            return "medium"
        case ->
            return "large"
```

No two cases may match a value in common: a pattern that is the same as one above it, a range that overlaps one above it, and a number that is in a range above it are errors, because the case could never run for that value. So the order of the cases does not change what a match does.

A case that has no block, because the next line is another case, runs the block of the next case that has one. That is how cases share a block, as cases without statements in between do in C. The last case must have a block. It is the same for every type of subject, and the patterns are constants of the type, such as a code that a library gives as a `U32`:

```
is_weekend :: proc(day: Int) Bool ->
    match day ->
        case 6 ->
        case 7 ->
            return true
        case ->
            return false
```

```
ERROR_FILE_NOT_FOUND : U32 : 2
ERROR_PATH_NOT_FOUND : U32 : 3
ERROR_ACCESS_DENIED : U32 : 5

error_from :: proc(code: U32) File_Error ->
    match code ->
        case ERROR_FILE_NOT_FOUND ->
        case ERROR_PATH_NOT_FOUND ->
            return .NOT_FOUND
        case ERROR_ACCESS_DENIED ->
            return .ACCESS_DENIED
        case ->
            return .OTHER
```

The subject can be an enum. The patterns are then members of that enum, written with a dot only, such as `.RED`, because the enum is the one of the subject, or with the name of the enum, such as `Color.RED`. The member of another enum is an error, and so is a range, as members have no order. A match of an enum must have a case for every member of the enum, or a default case, so a member that is added to the enum later is an error in every match that does not handle it, instead of a value that the match skips without a word:

```
Color :: enum Int ->
    RED
    GREEN
    BLUE

color_name :: proc(color: Color) Str ->
    match color ->
        case .RED ->
            return "red"
        case .GREEN ->
            return "green"
        case .BLUE ->
            return "blue"
```

A match of an enum that is meant to leave members out has the [attribute](#attributes) `partial`, which is written after `match`. Then nothing runs for the members that it has no case for:

```
warm_color_name :: proc(color: Color) Str ->
    match #(partial) color ->
        case .RED ->
            return "red"
    return "not warm"
```

`partial` is the only attribute of a match statement, and it is an error on a match whose subject is not an enum, or which has a default case, because such a match has nothing to check: a match of a number or of a string never has a case for every value, and a default case handles every member that is left.

The block of a case is a block like that of an `if`: it is a scope, see [Scope resolution](#scope-resolution), so two cases can define variables of the same name, it can have any statement, also another match statement, and a `defer` in it runs when the case ends. A match statement is not a loop or a scope that a `break` leaves, so a `break` or a `continue` in a case belongs to the loop around the match:

```
sum_of_multiples_of_three :: proc(limit: Int) Int ->
    total := 0
    for i in 0..<limit ->
        match i - i / 3 * 3 ->
            case 0 ->
                total += i
            case ->
                continue
    return total
```

A match statement can be [deferred](#defer), like an if statement, with `defer match`, and the cases follow it as without `defer`.

### For statements

A for statement runs a block of statements once for each number in a range. There is also a [infinite loop](#infinite-loops), and a loop can be left early or skipped ahead with [`break` and `continue`](#break-and-continue). After `for` comes the name of the iterator, then the keyword `in`, and then the range. The `->` after the range starts the block, which is indented one level deeper than the `for`, like the block of an `if`:

```
sum_to_nine :: proc() Int ->
    total := 0
    for i in 1..<10 ->
        total += i
    return total
```

Here `sum_to_nine` returns 45, because the block runs for `i` equal to 1, 2, and so on up to 9. The range is written `first..<end`, where `first` is the number that the iterator starts at and the loop stops before it reaches `end`, so the end is not part of the range, which is an exclusive range. `first..<=end` is an inclusive range, which also runs the block for `end`:

```
sum_to_ten :: proc() Int ->
    total := 0
    for i in 1..<=10 ->
        total += i
    return total
```

`sum_to_ten` returns 55. The range `0..<n` has `n` numbers, and `0..<=n` has one more. When `end` is below `first` the range is empty, and so is an exclusive range where they are equal, and the block does not run at all. An inclusive range where they are equal has one number.

The first number and the end are expressions of type `Int`, so they can be numbers, names, calls and arithmetic, and they are read once, before the loop starts, in the order they are written. So changing a variable that the end came from inside the block does not change the number of times that the block runs. `..<` and `..<=` are single tokens, like `->`, so no space may be written inside them, and a range needs the whole of one: `0..3` is an error.

```
sum_between :: proc(low: Int, high: Int) Int ->
    total := 0
    for i in low..<=high ->
        total += i * i
    return total
```

The iterator is a variable of type `Int` that exists only in the block of the statement. It counts up by one for every pass. It cannot be assigned to, so `i += 1` in the block is an error, and it is not in scope in the range itself, so `for i in 0..<i ->` is an error too. The iterator follows the rules for the names of variables, see [Scope resolution](#scope-resolution): a for statement cannot have an iterator with the name of a constant, a parameter or a variable that is in scope. Two for statements one after another can use the same name, because the first iterator is gone when its block ends.

The block of a for statement is a block like that of an `if`: it can define variables, which are in scope in the rest of the block, and it can have if statements, `return`, `break` and `continue` statements and other loops. A `return` in the block ends the procedure at once. For statements and if statements can be inside each other, as many as 32 levels deep:

```
count_pairs :: proc(n: Int) Int ->
    count := 0
    for row in 0..<n ->
        for column in 0..<=row ->
            if row not= column ->
                count += 1
    return count
```

The elements of an array or of an array slice can be gone through as well. Instead of a range, the expression after `in` is the array or the slice, and the block runs once for each element, in order from the first. The iterator is a copy of the element, which has the type of the elements, an `Int` for an array of `Int`:

```
sum_of_elements :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    total := 0
    for x in a ->
        total += x
    return total
```

`sum_of_elements` returns 15. A for statement is told to go through a range by the `..<` or `..<=` in it, so one without such an operator must have an array, a slice or a string after `in`, and `for x in 5 ->` is an error. The address and the length of what is gone through are read once, before the loop, but the elements are read as the loop goes on, so a change to an element that has not been reached yet in the block is seen by it. The iterator is a copy of an element, so it cannot be used to change one, and the same rules as for the iterator of a range hold: it cannot be assigned to, it is not in scope in the expression, and `break` and `continue` work as in any loop. An array with no elements runs the block no times. To change the elements, write the index of each:

```
fill :: proc() Int ->
    a := [5]Int { .. = 0 }
    for i in 0..<5 ->
        a[i] = i * i
    return a[4]
```

The characters of a string can be gone through the same way. The expression after `in` is a `Str`, and the block runs once for each character, in order from the first, with the iterator a `Rune`: the code point of the character, which is what the subscript of a string gives too, see [Strings](#strings):

```
count_vowels :: proc(text: Str) Int ->
    count := 0
    for character in text ->
        if character == 'a' or character == 'e' or character == 'ä' ->
            count += 1
    return count
```

`for character in "Héllo"` runs 5 times, though the string has 6 bytes, with the `Rune` values `'H'`, `'é'`, `'l'`, `'l'` and `'o'`. This is the way to go through a string by character: the loop decodes the UTF-8 as it goes, from the first byte to the last, so it takes as long as the string is long, and a loop over the indexes of `length_of(text)` that subscripts `text[i]` walks over the string from its start for every index, and takes as long as the square of that. The address and the length of the string are read once, before the loop, and the string is immutable, so nothing in the block changes what is gone through. The iterator is a copy, which cannot be assigned to, and `break` and `continue` work as in any loop. A string with no characters, such as `""`, runs the block no times. To go through a string by byte, see [Strings](#strings).

A for statement with no iterator and no range is an infinite loop, like `for(;;)` and `while(true)` in C. It is written `for ->`, and the block, which is indented one level deeper, runs again and again until a `break` or a `return` leaves it. Example:

```
count_up_to :: proc(limit: Int) Int ->
    count := 0
    for ->
        if count >= limit ->
            break
        count += 1
    return count
```

`count_up_to(5)` returns 5. A loop without a way out is not an error, but it never ends, and neither does a program that runs it.

A for statement with only a condition is a loop that goes on while the condition is true, like `while` in C. It is written `for` and a condition of type `Bool`, such as `for length > 0 ->`, and the block, which is indented one level deeper, runs again and again as long as the condition is true. The condition is tested before each pass, so the block does not run at all when it is false from the start, and a `continue` goes back to the test. A `break` and a `return` leave the loop as in any other, and so does the condition becoming false. Example:

```
count_up_to :: proc(limit: Int) Int ->
    count := 0
    for count < limit ->
        count += 1
    return count
```

The condition is worked out again for each pass, so it is not a constant, and it is not a `Bool` conversion of a number: `for count ->` is an error, as `if count ->` is. A condition that starts with a name and `in` is read as the iterator of a for statement, so it is written another way.

### Labels and scopes

A scope statement is a block of statements that is not the branch of an `if` or the body of a loop. It is written `->` alone on a line, and the block, which is indented one level deeper, runs once, from the first statement to the last:

```
add_eleven :: proc(x: Int) Int ->
    total := x
    ->
        extra := 10
        total += extra
        total += 1
    return total
```

A scope statement is a scope like the block of an `if`, see [Scope resolution](#scope-resolution): `extra` is in scope in the rest of the block and not after it, so two scopes one after another can each define a variable of the same name. Its use is to leave it early with a [`break`](#break-and-continue), and to limit where its variables can be used.

A scope can have a label, which is written before the `->`. A label is the character `:` directly followed by a name, with no space in between, such as `:outer`:

```
find_first :: proc(limit: Int) Int ->
    found := 0
    :search ->
        for i in 1..<=limit ->
            if i * i > 50 ->
                found += i
                break search
    return found
```

The name of a label follows the rules of an [identifier](#identifiers), so a keyword or a literal word cannot be one, and `: outer` with a space is an error. Labels are a kind of name of their own: they cannot be used as variables, and a variable or a procedure with the same name does not clash with them. A statement cannot have the label of a statement that it is inside of, so a label has one meaning wherever it can be used, but statements that are not inside each other can have the same label.

An if statement, a for statement and a [infinite loop](#infinite-loops) can have a label as well. It is written before the `if` or the `for` on the same line, with a space after it:

```
:check if n > 5 ->
    ...
else ->
    ...

:rows for row in 0..<n ->
    ...

:again for ->
    ...
```

The label belongs to the whole statement. For an if statement that is the `if` with all of its `else if` and `else` branches, and only the first line has the label: `:check else ->` is an error. A label does not change what the statement does. It lets a [`break` or a `continue`](#break-and-continue) name it.

### Break and continue

`break` leaves the innermost loop or scope that it is in, and the statement after it runs next. `continue` ends the current pass of the loop and starts the next one: in an infinite loop that is the first statement of the block again, and in a for statement with a range it is the next number of the range, or the end of the loop when there are no more. The iterator is stepped after a `continue` the same way as after a block that runs to its end, so an inclusive range that ends at the largest `Int` also ends. Both are statements of one word on a line of their own:

```
sum_of_odd_numbers :: proc(n: Int) Int ->
    total := 0
    for i in 1..<=n ->
        if i / 2 * 2 == i ->
            continue
        total += i
    return total

first_multiple :: proc(factor: Int, limit: Int) Int ->
    found := 0
    for i in 1..<=limit ->
        if i / factor * factor == i ->
            found += i
            break
    return found
```

They work in infinite loops and in for statements with a range alike, and they can be in the blocks of if statements that are inside the loop. They belong to the innermost loop only, so a `break` in an inner loop does not leave the outer one. The block of an `if` is not a scope to leave, so a `break` in it leaves the loop or scope around the `if`:

```
count_pairs :: proc(n: Int) Int ->
    pairs := 0
    for row in 0..<n ->
        for column in 0..<n ->
            if column > row ->
                break
            pairs += 1
    return pairs
```

A [scope statement](#scopes) can be left with a `break` as well, and so can a labeled if statement, see below. A `break` without a label leaves the innermost loop or scope, whichever is nearer, so in a scope inside a loop it leaves the scope and the loop goes on. `continue` is only for loops: it goes on with the innermost loop, also when scopes are in between.

```
break_early :: proc() Int ->
    x := 1
    ->
        if x < 10 ->
            x += 1
        else ->
            break
        x += 2
    return x
```

`break_early()` returns 4. A `break` can name a statement to leave by the name of its label, written after `break` without the `:`. It leaves that scope, loop or if statement, and everything in between, so it can leave several at once:

```
break_early_with_label :: proc() Int ->
    x := 1
    ->
        :my_block ->
            if x > 10 ->
                x += 1
            else ->
                break my_block
            x += 2
        x += 5
    return x
```

`break_early_with_label()` returns 6: `break my_block` leaves the labeled scope, and `x += 5` runs next. A `break` writes the label without the `:`, which is for the line that declares it only, and `break :my_block` is an error. The label must be one of a statement that the `break` is inside of.

A `break` with a label can leave a [labeled if statement](#scopes) from any of its branches, also from a loop inside it, and then the statement after the if statement runs next. A `break` without a label passes an if statement, labeled or not, and leaves the loop or scope around it, because the block of an `if` is not a scope:

```
classify :: proc(n: Int) Int ->
    x := 0
    :check if n > 5 ->
        x += 1
        if n > 10 ->
            break check
        x += 10
    else if n > 2 ->
        x += 100
    else ->
        x += 1000
    x += 10000
    return x
```

`classify(11)` returns 10001, because `break check` skips `x += 10` and the other branches, and `classify(7)` returns 10011.

A labeled loop is left with `break` and its label from anywhere inside it, also from an inner loop. `continue` and the label of a loop goes on with the next pass of that loop, ending the inner loops on the way, which is what a plain `continue` cannot do from an inner loop:

```
count_triangle :: proc(n: Int) Int ->
    total := 0
    :rows for row in 0..<n ->
        for column in 0..<n ->
            if column > row ->
                continue rows
            total += 1
    return total
```

`count_triangle(3)` returns 6. The label of a `continue` must be the one of a for statement or an infinite loop that it is inside of. A scope or an if statement cannot be continued, so `continue` with the label of one is an error.

A `break` that is not inside a loop or a scope, and a `continue` that is not inside a loop, are errors, and so is one in a procedure that has no loop, even if the procedure is only called from a loop. A `break` or a `continue` with a label that no statement around it has is an error as well. They are not expressions, so they have no value and nothing can follow them on the line, except a label. The statements that come after them in the same block are never run.

### Defer

A defer statement is the word `defer` and a statement after it on the same line. The statement does not run where the `defer` is, but when the block that the `defer` is in is left, which is why it is called a deferred statement. Its use is to keep what undoes something next to what does it, such as closing a file right below the line that opens it, so that no way out of the block forgets it:

```
copy_file :: proc(source: C_Str, target: C_Str) Int ->
    input := open_file(source)
    defer close_file(input)
    output := open_file(target)
    defer close_file(output)
    if copy_bytes(input, output) == false ->
        return 1
    return 0
```

Both files are closed whichever `return` leaves `copy_file`. A block is left in four ways, and each of them runs the deferred statements of the block:

- at its end, after its last statement,
- by a `break` that leaves it, or a loop or a scope around it,
- by a `continue`, which leaves the block of the pass of the loop, and the blocks inside it that it is in,
- by a `return`, which leaves every block that it is in, out to the body of the procedure.

The deferred statements of a block run in the reverse order of their `defer`s, the last one first, so what was set up last is undone first. When a jump leaves several blocks at once, the deferred statements of the innermost block run first, then the ones of the block around it, and so on out to the block that it leaves:

```
trace #(static) := 0

note :: proc(digit: Int) ->
    trace = trace * 10 + digit

return_runs_every_defer :: proc(n: Int) Int ->
    defer note(1)
    ->
        defer note(2)
        for i in 0..<n ->
            defer note(3)
            if i == 1 ->
                return 100
    note(4)
    return 0
```

`return_runs_every_defer(3)` returns 100 and notes 3321: the first pass of the loop runs its deferred statement at the end of its block, and in the second pass the `return` runs the deferred statements of the block of the loop, of the scope and of the procedure, in that order. `return_runs_every_defer(0)` notes 241.

A `defer` belongs to the block that it is in, and not to the procedure, unlike in Go, so a `defer` in the block of a loop runs on every pass, and one in a scope or in the branch of an if statement runs when that block ends. A `defer` takes effect when it is reached: a block that is left before its `defer` does not run the deferred statement, and an early `return` above a `defer` runs only the deferred statements above it.

A deferred statement reads the variables when it runs, not when the `defer` is reached, so it sees what the block did after the `defer`:

```
last_value :: proc() ->
    x := 1
    defer note(x)
    x = 7
```

`last_value()` notes 7. A `return` computes its value first, and then the deferred statements run, so they cannot change the value that is returned, but what they do to something outside the procedure, such as what a pointer points to or a global variable, is seen after the call:

```
return_value_comes_first :: proc() Int ->
    x := 1
    defer x += 10
    return x

increment_after_return :: proc(counter: ^Int) Int ->
    defer counter^ += 1
    return counter^
```

`return_value_comes_first()` returns 1, and `increment_after_return` returns the value that `counter` pointed to before it was increased.

The statement after `defer` is an assignment, a call, or an if statement, a match statement, a for statement, an infinite loop or a scope statement, with or without a label. A deferred if statement has its `else if` and `else` lines below it as usual, and the `defer` is written on the `if` line only. `defer ->` defers a scope, which is how a block of statements is deferred together:

```
defer_a_block :: proc() ->
    defer ->
        note(3)
        note(4)
    note(1)
    note(2)

defer_if_statement :: proc(n: Int) ->
    defer if n > 5 ->
        note(2)
    else ->
        note(3)
    note(1)
```

A deferred statement runs while its block is being left, maybe by a `return` or a `break`, so it cannot leave anything itself. These are errors:

- a variable definition after `defer`, because the variable would be gone at once, and a `return`, a `break` or a `continue` after `defer`,
- a `return` inside a deferred statement,
- a `break` or a `continue` inside a deferred statement that leaves it, or goes on with a loop around it; one that stays inside it, such as a `break` of a deferred scope or a `continue` of a deferred loop, is allowed,
- a `defer` inside a deferred statement, because the deferred statement is already what runs last.

The names in a deferred statement are looked up where the `defer` is, like the names of any statement there, so a variable that is defined below the `defer` cannot be used in it. A deferred statement runs before the block ends, so every variable that it can use still exists when it runs.

There is nothing about a `defer` that is left for the program to do when it runs: which deferred statements run where is known when the program is compiled, and the code of a deferred statement is put at each place where its block is left. So a `defer` costs as much as writing its statement out by hand at each of those places, which makes the program larger when a block has many ways out.

> **Lessons learnt from Go** In Go, `defer` belongs to the function, and a deferred call runs when the function returns, so a `defer` in a loop piles up calls until the end of the function, and the resources of each pass are held until then. The arguments of the call are evaluated where the `defer` is, while the rest of the function goes on, which surprises as often as it helps. Mialu takes `defer` from Odin instead: it belongs to the block, it runs where the block ends, and it is one more statement there, which reads the variables as they are at that point.

## Arrays

An array is a fixed number of values of one type, which are called its elements. The elements are one after the other, and the number of them is the length of the array. The length is known when the program is compiled, and it never changes. An array lives on the stack of the procedure that defines it: it takes no allocation, and it is gone when the procedure returns. The type of an array is its length between square brackets, followed by the type of its elements, so `[5]Int` is an array of five `Int` values. Elements are numbered from zero, so the elements of `[5]Int` have the indexes 0, 1, 2, 3 and 4. The elements can be `Int`, `Bool`, `Byte` or `Str` values, [struct objects](#structs-in-arrays) or [pointers](#pointers-in-structs-and-arrays), and not arrays.

### Array literals

An array literal gives the elements of an array. It is the type of the array followed by the elements between braces, which are separated by commas:

```
a := [5]Int { 1, 2, 3, 4, 5 }
```

When the length of the type is the keyword `const` instead of a number, the length is the number of elements in the literal. This is the way to write a literal without counting the elements:

```
a := [const]Int { 1, 2, 3, 4, 5 }
```

`a` above is of type `[5]Int`, which is deduced from the value like the type of any variable. A literal that has a number as its length must have exactly that many elements, so `[3]Int { 1, 2 }` is an error. A literal must have a number or `const` as its length, so `[]Int { 1, 2 }` is an error: that is the type of an array slice, which has no elements of its own, see [Array slices](#array-slices). The elements are expressions of the type of the elements, and there may be none, so `[const]Int { }` is an array of length zero. The elements can be on lines of their own, and a comma can come after the last one:

```
names := [const]Str {
    "Bob",
    "Alice",
}
```

An array whose elements all have the same value is written with `..` in place of the elements, as `.. = value`, which says that the value is the value of every element. This is the way to make an array of zeros, or a buffer, without writing every element:

```
zeros := [5]Int { .. = 0 }
buffer := [64]Byte { .. = 0 }
flags := [8]Bool { .. = true }
```

`zeros` has the five elements that its length says, every one of them 0, and `buffer` has 64 bytes that are 0. The `..` means the whole range of the array, which is why the length must be written as a number: `const` is the number of the elements that are written, so with only `.. = 0` it would have none to count, and `[const]Int { .. = 0 }` is an error. The value is an expression of the type of the elements, and it is worked out once, not once for each element, so `[4]Int { .. = next() }` calls `next` one time and gives all four elements what it returned. Each element is a copy, so a struct or a string that is given is put in every element, and changing one element changes no other. The length can be 0, which has no elements, and the literal, `.. = value` and a comma after it, can be on lines of its own, as the elements of any other literal can. A literal that fills has nothing but `.. = value` between its braces: `{ .. = 0, 1 }` is an error, and so is `{ .. 0 }`, which has no `=`.

A variable of an array type is defined with an array literal, and not with another array or a call: `b := a` is an error, because an array is not copied by a definition. The type can be written as well, as long as the literal has the number of elements that it says:

```
a : [3]Int = [const]Int { 7, 8, 9 }
```

An array literal is an expression, so it can also be used without a variable, for example as an argument of a call. Each time that a literal is evaluated it puts its elements in their place again, so a literal in the block of a loop makes a new array in every pass.

> **Lessons learnt from C** In C, an array turns into a pointer to its first element as soon as it is passed to a procedure, and the length is lost on the way, so `sizeof` gives the wrong answer there and the callee has to be told the length by another parameter that can be wrong. A parameter that is written like an array is really a pointer. In Mialu, an array is not passed at all: a procedure takes an array slice, which carries its length, and an index outside of it traps and does not read or write the memory next to the array.

An array cannot be the type of a parameter, of a global variable (except for the [members of an enum](#static-reflection), which are constant) or of a returned value, because its elements are in the frame of the procedure that defines it: a parameter would have to be a copy, and the elements of a returned array would be gone when the procedure returns. A procedure that works on the elements of an array takes an [array slice](#array-slices) of them.

### Elements

The element at an index is written with the index between square brackets after the array, which is called a subscript. The index is an expression of type `Int`:

```
array_element_access :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    return a[0] + a[a[0] + 1]
```

`array_element_access` returns 4, which is `1 + a[2]`. An index that is not in the array, which is one that is negative or not below the length, is an error at run time: the program traps, see [WebAssembly](#webassembly).

An element is a place that can be assigned to. The plain assignment `=` works on an element like on a variable, and the assignment operators `+=`, `-=`, `*=` and `/=` work on an element of type `Int` like they do on a variable:

```
array_element_assignment :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    a[0] = 10
    a[1] += 5
    return a[0] + a[1]
```

`array_element_assignment` returns 17. The index and the array are evaluated before the value, and the value must be of the type of the elements.

### Comparing bytes

Two arrays or array slices of [`Byte`](#bytes) can be compared with `==` and `not=`: they are the same when they have the same number of bytes and the same bytes, in the same order. Each can be an array or a slice, in any mix, so an array is compared with a slice, and two arrays of different lengths are compared too, and are not the same:

```
is_header :: proc(bytes: []Byte) Bool ->
    magic := [const]Byte { 0x7F, 0x45, 0x4C, 0x46 }
    if length_of(bytes) < 4 ->
        return false
    return bytes[0:4] == magic

is_empty :: proc(bytes: []Byte) Bool ->
    nothing := [const]Byte { }
    return bytes == nothing
```

The comparison is of the bytes as the numbers from 0 to 255 that they are, and a byte that differs, or a different number of them, is enough to say that they are not the same. It stops at the first byte that differs, so it takes as long as the bytes that are the same at the start of the two. A `Byte` has an order, but a sequence of them has none here, so `<`, `<=`, `>` and `>=` are errors, and so is comparing arrays or slices of any other element type, such as `Int`, or a sequence of bytes with a [string](#strings), whose bytes are text. Two views of the same bytes are the same, and the bytes are read when the comparison runs, so a change that was made before it is seen.

### Array slices

An array slice is a view of some of the elements of an array. Its type is `[]` followed by the type of the elements, so `[]Int` is an array slice of `Int` values. It does not own the elements: it only knows where the first one is and how many there are, which is not known until the program runs. So a change to an element of a slice is a change to the element of the array that it views.

A slice is made of an array, or of another slice, by writing two indexes, the low bound and the high bound, separated by a colon, between the square brackets. It is a half-open range: it includes the element at the low bound and excludes the one at the high bound. So the slice `a[1:4]` has the elements 1 through 3 of `a`, and its length is 3, which is the high bound minus the low bound:

```
sum_of_middle :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    return sum(a[1:4])
```

`sum_of_middle` returns 9, which is 2 + 3 + 4. Both bounds are written, and they are expressions of type `Int`. The low bound must not be above the high bound, and the high bound must not be above the length of what is sliced, or else the program traps. So `a[2:2]` is a slice with no elements, and `a[0:5]` has all five elements of `a`. The indexes of a slice start at zero again, so `a[1:4][0]` is `a[1]`, and a slice of a slice is relative to the first one. A slice can be the value of a variable, and then its type is deduced, or it can be written:

```
middle := a[1:4]
everything : []Int = a
```

An array can be given where a slice is wanted: the slice then has all the elements of the array. That is the case for an argument of a call and for the value of a variable with a slice type. This is how a procedure takes some elements, whatever the length of the array they come from is. When the length of the array between the brackets of a parameter is left out, the parameter is a slice, not an array:

```
sum :: proc(elements: []Int) Int ->
    total := 0
    for x in elements ->
        total += x
    return total
```

`sum(a)` adds all the elements of `a`, and `sum(a[0:3])` the first three. A slice can be passed on to another procedure, and it can be sliced again. Because a slice views the elements of the frame of the procedure that defines the array, it cannot outlive it, so a slice cannot be the type of a global variable or of a returned value either. A `for` statement can go through the elements of a slice, see [For statements](#for-statements).

A range of elements is assigned to with `=` and a slice on the left. The value on the right is an array or a slice, and it must have as many elements as the range, or else the program traps. The elements are copied, and the copy is right also when the two ranges overlap:

```
array_element_range_assignment :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    a[0:3] = [const]Int { 10, 20, 30 }
    return sum(a)
```

`array_element_range_assignment` returns 69. The elements of the value must be of the type of the elements of the range. The assignment operators such as `+=` are for one element, so `a[0:3] += 1` is an error.

The number of elements of an array or an array slice is given by the built-in procedure [`length_of`](#length_of), as an `Int`: `length_of(a)` for the array `a`.

## Custom data types

Besides the [primitive data types](#primitive-data-types), a program can define data types of its own: [structs](#structs), which group values of other types into one, and [enumerations](#enumerations), which are a fixed set of named members. A custom data type is a [constant](#constants) with a name, and it is used wherever a type is written.

### Enumerations

An enumeration, or enum, is a type with a fixed set of named members with values. You define it by writing

- a constant denoted by `enum`,
- an integer type (like `Int`) for values of its members and
- `->` to mark the beginning of enum members on each subsequent line, indented one level deeper:

```
Color :: enum Int ->
    RED
    GREEN
    BLUE
```

Enum members must have name but values are optional. An enum must have at least one member, and no two members have the same name. If the first enum member does not have a value, it has value `0`. Every subsequent member without an explicit value continues from the previous member value by `1` so in the example above, RED = 0, GREEN = 1 and BLUE = 2.

The integer types that are reserved for the future, such as `I8` and `U16`, will be the others. 

A member can be referenced by writing the name of its enum and a dot, such as `Color.RED`:

```
color := Color.RED
DEFAULT_COLOR :: Color.RED
```

For convenience, the name of the enum can be automatically deduced and therefore left out in certain cases:

- In a constant or variable initialization if explicit enum type is given:
    - Example: `color : Color = .BLUE`
- In a return statement with enum type as the return type:
    - Example return statement: `return .GREEN`
- In a struct object initialization:
    - Example: `car_model := Car_Model { color = .RED, }`

> **Lessons learnt from C** In C, an enum is an integer in disguise: any `int` can be given to a variable of an enum type, members of different enums can be compared and mixed, and a member can be used in arithmetic. So the type says very little about what a value can be. In Mialu, an enum is a type of its own that is not converted to or from an `Int`, so a value of an enum type is always one of its members.

Enum members don't have any order, so comparsing enum values with `<`, `<=`, `>` and `>=` results in an error. An enum is not converted to or from an `Int` or any other type.

The default value for any enum is the first member of the enum.

In the future, the language spec and implementation will address the following issues:

- An enum cannot be the type of a global variable, because its initial value is a constant expression, and no constant expression is a member.
- A member has no value of its own, such as a number or a string, that is written in the definition.
- A member cannot be the value of a variable without a type written, as above, and a member is not called or followed with `^`.

### Structs

A struct is a record type: a name for a fixed set of fields, each with a name and a type. A struct is defined as a [constant](#constants) whose value is `struct ->` followed by its fields, one on each line, indented one level deeper than the line of the definition. A field is its name, a colon and its type:

```
Person :: struct ->
    age: Int
    name: Str
```

The type of a field can be `Int`, `Bool`, `Byte`, `Str`, another struct or a [pointer](#pointers) but it cannot be an [array or an array slice](#arrays).

The fields of a struct must have names unique to the fieldset. A struct cannot contain itself, since it would never end, as in a field `next: Person` of `Person`, and not through other structs. A pointer to the struct is not the struct, so a field `next: ^Person` is fine because it's a pointer whose size can be determined without the knowing the full struct definition.

A value of a struct type is called a struct object. It is constructed by a struct literal: the name of the struct and then the fields that are given a value between braces, each of them the name of the field:

```
p := Person { age = 30, name = "Bob" }
```

The fields can be given in any order, and each field at most once. A comma can come after the last one (trailing comma), which is the convention when the fields are on lines of their own. The literal can be spread over several lines, as long as every line is indented by a multiple of 4 spaces like any line:

```
p := Person {
    age = 30,
    name = "Bob",
}
```

> **Lessons learnt from C** In C, a struct that is initialized with fewer values than it has fields quietly sets the rest to zero, and a struct that is not initialized holds garbage, including the padding between its fields, which can leak memory that was never meant to be seen. A struct that has a field added later keeps compiling in all the places that do not set it. In Mialu a struct has no padding that a program can see, fields are named in a literal, and either all of them are given or none.

Either every field is given a value or none is. A literal with empty braces, `Person {}`, gives each field its default value. 
A variable can still be defined with a struct type and without a value, such as `p : Person`, and then it has the default value, see [Variables](#variables). This will be removed in the future, though.

> **Rationale** As soon as one field is given a value, the rest of the fields must be given one too, because otherwise someone may forget to set a field.

A field that
    
- does not exist in the struct or
- is given a value twice and/or
- is not given a value of the type of its field

result in an error.

Two struct types are different types even if they share the same fields of the same type.

A field of a struct object is read by writing a dot and its name after the object, such as `p.age`. Example:

```
p := Person { age = 30, name = "Bob" }
p.age = 31
```

A field is read or assigned through a variable, a parameter, an element of an array, like `persons[0].age`, or a [pointer](#pointers) to a struct, such as `p.age`, and not through the value of a call: `birthday(p).age`. This results in an error, and has to be written with a variable that is given the result first.

A field can be of another struct type:

```
Point :: struct ->
    x: Int
    y: Int

Line :: struct ->
    start: Point
    end: Point
```

or an anonymous struct, which is defined where the field is:

```
Info :: struct ->
    name: Str
    age: Int
    address: struct ->
        area_name: Str
        house_no: Int
        district: Str
```

A struct that is defined where the field cannot be referred to by name. Fields can still be accessed via dot operator, like in the following example:

```
create_nested_struct_object :: proc() Str ->
    info := Info {
        name = "Bob",
        age = 30,
        address = {
            area_name = "Downtown",
            house_no = 123,
            district = "Central",
        },
    }
    return info.address.area_name
```

The elements of an [array](#arrays) or array slice can be struct objects. Example:

```
persons := [const]Person {
    Person { age = 30, name = "Bob", },
    Person { age = 25, name = "Alice", },
    Person { age = 40, name = "Charlie", },
}
```

### Unions

Union is a type consisting of members whose storage overlaps. Only one of the members is used at a time. A union is only as big as necessary to store its largest member, not as big as the combination of all its members.

In Mialu, a union is not a separate named type but a group of fields in a [struct](#structs). It is tagged: an enum field called tagger of the containing struct says which member is used at a given time.

Because the tagger is always there, a program never has to guess which member is stored, and the compiler can check it instead. This check is based on a `match` construct. It is denoted by `union match`, followed by the field name and by `case`s for each value of the field of the enumeration type. Example:

```
Token_Kind :: enum Int ->
    IDENTIFIER
    INTEGER_LITERAL
    OPERATOR_ADD

Token :: struct ->
    kind: Token_Kind
    union match kind ->
        case .IDENTIFIER ->
            identifier: struct ->
                text: Str
        case .INTEGER_LITERAL ->
            integer_literal: struct ->
                value: Int
```

> **Lessons learnt from C** In C, a union can be read as the wrong member and give garbage. Here a field of a case is only read or assigned where the compiler can tell that the kind is that case, and a literal can only give the fields of the case that its kind names. A mistake is an error at compile time and not a bug at runtime.

A field of a case is read or assigned only where the compiler can tell from the program which member it is. The compiler can narrow this down using `if` statement whose condition compares the field that is matched of that object with the member of the case using `==`, with the member on either side. Example narrowing for the token example above:

```
if token.kind == .INTEGER_LITERAL ->
    return token.integer_literal.value
else if token.kind == .IDENTIFIER ->
    return 1
return 0
```

## Reflection

Reflection is about a program looking at its own types. Mialu uses static reflection, which does it at compile-time.

Static reflection is based on obtaining information about types. For this purpose, there's an operator `type_info_of` takes the name of a type between parentheses and gives the information about it, as a struct object. What the information is depends on the type that is asked about, and each kind of type has a struct of its own for it.

### Enumeration type info

For an [enum](#enumerations), `type_info_of` gives an `Enum_Type_Info` object. It has the following fields:

- `name`, a `Str` with the name of the enum, such as `"Color"`,
- `members`, an array with an `Enum_Member_Type_Info` for each member of the enum, in the order of the definition. The length of the array is the number of members, which is part of its type, so it is a compile-time array.

An `Enum_Member_Type_Info` is an ordinary [struct](#structs) that has these fields:

- `name`, a `Str` with the name of the member, such as `"GREEN"`,
- `value`, an `Int` with the number of the member, which is its place among the members, counted from 0.

```
Color :: enum Int ->
    RED
    GREEN
    BLUE

Color_Members #(static) := type_info_of(Color).members

get_enum_members_count :: proc() Int ->
    return length_of(Color_Members)

member_name :: proc(index: Int) Str ->
    return Color_Members[index].name

sum_of_numbers :: proc() Int ->
    total := 0
    for member in type_info_of(Color).members ->
        total += member.value
    return total
```

`get_enum_members_count` returns 3, `member_name(1)` is `"GREEN"` and `sum_of_numbers` returns 3, which is 0 + 1 + 2.

The `Enum_Type_Info` exists only when the program is compiled, because the length of its `members` is a part of its type, and a value of a type that depends on the enum has nowhere to be kept when the program runs. So a field has to be read from it right away: `type_info_of(Color).members` and `type_info_of(Color).name` are values, and `info := type_info_of(Color)` and passing `type_info_of(Color)` to a procedure are errors. The array of the members is a constant of the program, so a [global variable](#global-variables) can be given it for its initial value, as `Color_Members` above, which is the only expression other than a literal that a global variable can be given. A global variable or a local variable that is given the members holds the address of the array and does not copy it, which is the one case where a variable of an array type is not defined with an array literal. It has the type `[3]Enum_Member_Type_Info` here, which can also be written. Its elements are indexed, sliced and gone through by a `for` statement like those of any array, and `length_of` gives their number. The elements are information, so a program should not assign to them; the compiler does not stop it.

`Enum_Member_Type_Info` is a struct that the compiler adds to a program that uses `type_info_of`, as if the program ended with its definition, so a program cannot define a struct with that name as well. The compiler gives no information about a type that is not an enum for now, and asking for it is an error.

Not everything can be done with the information yet, which a later version of the language may allow:

- `type_info_of` can only be asked about the name of an enum. The information about a struct or a primitive type is not available.
- The information is not a value that a variable or a parameter can hold, as above.
- `Enum_Type_Info` and `Enum_Member_Type_Info` are not written as the type of a variable or a parameter, because the name of the first is not a type that a program can use. The second is, so `[]Enum_Member_Type_Info` is the type of a slice of the members.

### Procedure type info

For a [procedure](#procedures), `type_info_of` gives a `Procedure_Type_Info` object.

It has the following fields:

- `name`, a `Str` with the name of the enum, such as `"create_token"`,
- `parameters`, an array slice of  `Procedure_Parameter_Type_Info`. If no parameters exist, the length is `0`.
- `return_types`, an array slice of `Procedure_Return_Type_Info`. If no return types exist, the length is `0`.

## Pointers

A pointer is the address of a value: it says where the value is, so that the value can be read and changed by code that does not have the variable that holds it, such as another procedure. The type of a pointer is a caret, `^`, followed by the type of the value that it points to, so `^Int` is a pointer to an `Int`, `^Str` is a pointer to a `Str`, `^Bool` is a pointer to a `Bool` and `^Person` is a pointer to a [struct object](#struct-objects). What is pointed to can be a pointer again, with one more caret: `^^Int` is a pointer to a pointer to an `Int`. A pointer cannot point to an [array or an array slice](#arrays), because those are addresses of their elements already. The type of a pointer is written where any type is: for a variable, a parameter, a field of a struct, the elements of an array, as in `[const]^Int` and `[]^Int`, and the return type of a procedure. Spaces may be written after a caret, but the convention is none.

```
pointer_with_explicit_type :: proc() Int ->
    x := 4
    p : ^Int = %x
    p^ += 1
    return x
```

`pointer_with_explicit_type` returns 5. `%x` takes the address of the variable `x`, which is a pointer to it, and `p^` is the value that `p` points to, so `p^ += 1` adds 1 to `x`. The type of the variable can be left out, as for any variable: `p := %x` is a `^Int` as well.

### Taking an address

`%` is written before an expression, and gives a pointer to it, of the type that the expression has. The expression must be something that has an address, which is a place where a value lives:

- a variable of a procedure, such as `%x`,
- a field of a variable, such as `%person.age` and `%line.end.y`, and a field of a field of a variable,
- an element of an array or of an array slice, and a field of one, such as `%a[1]` and `%people[1].age`,
- what a pointer points to, and a field of it, such as `%p^` and `%p.age`. `%p^` is `p`, because following a pointer and then taking the address gives back the address that it was.

A parameter, the iterator of a for statement and a global variable have no address: the first two cannot be assigned to, so nothing would be changed through a pointer to them, and a global variable is a global of the module and not in its memory. A value that is computed has none either: a number, a string literal, a call, the result of an operator, and a struct literal. An array and an array slice cannot be pointed to, so `%a` is an error for an array `a`, though `%a[1]` is fine. All are errors at compile time.

`%` binds tighter than every operator, but looser than what follows a name, so `%person.age` is the address of the field `person.age`, and `%a[1]` the address of the element `a[1]`, and not the field or element of an address. It can be repeated, but `%%x` is an error, because `%x` is a value that is computed and has no address.

The pointer points to the variable and not to a copy of its value, so the pointer sees every change of the variable, and the variable every change through the pointer:

```
see_changes_of_variable :: proc() Int ->
    x := 1
    p := %x
    x = 7
    before := p^
    x += 3
    return before * 100 + p^
```

`see_changes_of_variable` returns 710. Because a pointer points to the variable, a variable whose address is taken is a variable like any other in the language: it is defined, assigned to and used as before. Taking its address says where it lives, which matters to the target, see [WebAssembly](#webassembly).

### Following a pointer

`^` is written after an expression whose value is a pointer, and gives what the pointer points to. It is written after, so that a chain of pointers reads from left to right: `p^` and `pp^^`. Following a pointer is an expression, and it can also be assigned to, with `=` and with the operators `+=`, `-=`, `*=` and `/=` for an `Int`, like a variable can:

```
swap :: proc(a: ^Int, b: ^Int) ->
    temp := a^
    a^ = b^
    b^ = temp

swap_through_pointers :: proc() Int ->
    x := 3
    y := 7
    swap(%x, %y)
    return x * 10 + y
```

`swap_through_pointers` returns 73. A procedure gets a copy of a pointer, like of any argument, but the copy points to the same variable, so this is how a procedure changes the variables of its caller. A pointer to a struct object gives the struct object with `p^`, and its fields with `p.age`, see below. The whole struct object is assigned with `p^ = Person { age = 5, name = "Zed" }`, and `copy := p^` copies it, after which a change of `copy` does not change the struct object that `p` points to. A pointer to a pointer is followed twice to get to the value, `pp^^`, and once to get to the pointer, so `pp^ = target` makes the pointer that `pp` points to point somewhere else.

The value that is assigned through a pointer must be of the type that the pointer points to, so `p^ = true` is an error for a `^Int`, and `^` can follow a pointer only: `x^` is an error for an `Int`. A call cannot be followed by anything, as for the fields and elements of a call, so a pointer that a procedure returns is stored in a variable before it is followed.

### Fields of a pointer

A field of a pointer to a struct is written with a dot, like the field of a struct object, and means the field of the struct object that the pointer points to: `p.age` is the same as `p^.age`. This is the recommended way to write it, because it reads the same for a pointer as for a struct object, so a procedure that takes a pointer to a struct and one that takes the struct object read alike, and a change from one to the other changes the type and not the uses. The field can be read and assigned to like the field of a variable, with `=` and with the operators for an `Int`, such as `p.age += 1`:

```
birthday :: proc(p: ^Person) ->
    p.age += 1

struct_as_pointer_parameter :: proc() Int ->
    person := Person { age = 40, name = "Eve" }
    birthday(%person)
    birthday(%person)
    return person.age
```

`struct_as_pointer_parameter` returns 42. Assigning to a field of a pointer changes what the pointer points to, and not the pointer, so the pointer can be a parameter, though a field of a parameter that is a struct object cannot be assigned to, because that would change the parameter. The dots can follow each other, also through several pointers, because the pointer is followed wherever one is met: for a field `next: ^Node` of `Node`, `first.next.next.value` is the field `value` of the node that the `next` of the node that the `next` of `first` points to points to. `p^.age` stays valid, and is the way to write the pointer being followed when that is wanted to be seen, and `pp^.age` is how a field of a pointer to a pointer to a struct is read: a pointer is followed once by a dot, so `pp.age` is an error. A field is not followed by `^` implicitly when it is the last thing that is written, so `node.next` is the pointer `next` and `node.next^` the node it points to.

A pointer that is `nil` is followed by a field as it is by `^`, which traps, see [Null and lifetime](#null-and-lifetime). A field of a pointer can have its address taken, such as `%p.age`, which is the address of the field in what the pointer points to.

### Null and lifetime

> **Lessons learnt from C** In C, following a null pointer is undefined behavior: it may crash, or read whatever is at address zero, and a compiler may assume it never happens and remove the checks around it. Pointers can also be added to and subtracted from, so a pointer can be moved outside of the thing that it pointed to without any mistake being visible. In Mialu, following `nil` traps, and there is no pointer arithmetic: a pointer points to one value, and going through many of them is done with an array or a slice, which are checked.

`nil` is the null pointer, a pointer that points to nothing, like `NULL` in C. It is a [literal word](#literal-words), and a value of every pointer type: it is a `^Int` where a `^Int` is wanted, a `^Person` where that is, and so on, so it can be the value of a variable that has a pointer type, an argument, a returned value, a field of a struct literal and an element of an array literal. A pointer that is defined with a type and without a value, as in `p : ^Int`, is `nil` as well, and so is a field of a struct whose literal has empty braces, `Node {}`. A field that is a pointer is written out in any other literal, as `next = nil`.

```
read_or_default :: proc(p: ^Int, default: Int) Int ->
    if p == nil ->
        return default
    return p^

nil_as_argument :: proc() Int ->
    x := 7
    return read_or_default(%x, 0) * 10 + read_or_default(nil, 5)
```

`nil_as_argument` returns 75. Because `nil` points to nothing in particular, it does not say the type of a variable: `p := nil` is an error, and has to be written `p : ^Int = nil`. For the same reason `nil^` and `%nil` are errors, and `nil` cannot be used where something else than a pointer is wanted, such as `x : Int = nil`.

Pointers are compared with `==` and `not=`, which are defined for two pointers of the same type, or for a pointer and `nil`. Two pointers are equal when they hold the same address, which is when they point to the very same variable or element: the pointers to two variables are different also when the variables have the same value. A pointer has no order, so `<` and the others are errors, like for a `Bool`. The comparison is how code finds out that a pointer can be followed, and how a chain of structs is gone through until it ends:

```
Node :: struct ->
    value: Int
    next: ^Node

sum_list :: proc(first: ^Node) Int ->
    total := 0
    current := first
    for ->
        if current == nil ->
            break
        total += current.value
        current = current.next
    return total
```

Following a `nil` pointer is an error at run time: the program traps, see [WebAssembly](#webassembly). A pointer is not checked in any other way. A pointer to a variable, or to an element of an array, is good for as long as the procedure that has the variable or the array runs, because they are gone when it returns. So a procedure can return a pointer to something that outlives it, such as a variable of its caller that it was given a pointer to, as `choose` does below, but a pointer to its own variable points to nothing after it returns. The target cannot tell a pointer to something that is gone: following it reads and writes the memory where the variable was, which another procedure may be using by then.

```
choose :: proc(a: ^Int, b: ^Int, first: Bool) ^Int ->
    if first ->
        return a
    return b
```

### Pointers in structs and arrays

A field of a struct can be a pointer. It can point to a struct of the same kind, which a field of that struct type cannot do, since a struct would contain itself: a pointer is only an address, so the struct has a fixed size.

```
Node :: struct ->
    value: Int
    next: ^Node

follow_pointers_in_structs :: proc() Int ->
    last := Node { value = 3, next = nil }
    middle := Node { value = 2, next = %last }
    first := Node { value = 1, next = %middle }
    return first.next.value * 10 + first.next.next.value
```

`follow_pointers_in_structs` returns 23. A field that is a pointer is `nil` in a struct literal with empty braces, and is written as `next = nil` in any other. The elements of an [array](#arrays) can be pointers, and so can the elements of an array slice, so a procedure can take `targets: []^Int`, and an array of pointers is made with `[const]^Int { %a, %b }`. An element is followed with `pointers[0]^`, and a for statement over them gives each pointer, as in `for p in pointers ->` and then `p^`.

## Procedures

Procedures are blocks of code that perform a specific task. They can be defined by placing the keyword `proc` after an identifier, followed by a list of parameters and an optional return type:

```
procedure_name :: proc(param1: Type1, param2: Type2) ReturnType ->
    // procedure body
```

A procedure parameter list can be empty, and the return type can be omitted if the procedure does not return a value. A procedure can also return several values, see [Multiple return values](#multiple-return-values). Example of a procedure with no parameters and no return type:

```
procedure_name :: proc() ->
    // procedure body
```

A procedure body consists of a series of statements, which are variable definitions, assignments, [if statements](#if-statements), `return` statements and [calls](#calling-procedures) for now. The procedure body is indented to indicate its scope.

### Calling procedures

A procedure is called by writing its name and, between parentheses, a value for each of its parameters, separated by commas. The parentheses are written also when there are no arguments:

```
forty :: proc() Int ->
    return 40

add :: proc(a: Int, b: Int) Int ->
    return a + b

forty_two :: proc() Int ->
    return add(forty(), 2)
```

Here `forty_two` returns 42. When a procedure is called, its arguments are evaluated first, from left to right, and the procedure runs with its parameters set to them. The call has the value that the procedure returns.

A call is an expression, so it can be the value of a variable or of a `return`, a condition, an operand of an operator, or an argument of another call. It binds tighter than any operator, so `double(3) + 1` means `(double(3)) + 1`, which is 7, and `not is_even(3)` is `not (is_even(3))`. Arguments are expressions as well, so `add(1 + 2, double(4))` is valid.

The call must match the procedure:

- the name must be that of a constant that is a procedure, or that of a variable, a parameter or a global variable that holds a [pointer to a procedure](#pointers-to-procedures), so a call of a name that is something else, such as an `Int` or a constant string, is an error,
- there must be as many arguments as the procedure has parameters,
- each argument must have the type of the parameter that it is given to, because nothing is converted: an `Int` is not a `Bool`, and `add(1, true)` is an error.

The procedure is looked up in the module scope, so it can be defined above or below the procedure that calls it, and a procedure can call itself, which makes it recursive:

```
factorial :: proc(n: Int) Int ->
    if n <= 1 ->
        return 1
    return n * factorial(n - 1)
```

Mialu does not check that a recursion ends, and every call takes space of the stack of the target, so a recursion that never ends, or that is too deep, makes the program fail at run time. For the WebAssembly target the call from JavaScript throws a `RangeError`.

A procedure that has no return type returns nothing, so its call has no value and cannot be used as one: `x := log(1)` and `return log(1)` are errors when `log` has no return type. A call can also be a statement of its own, on a line by itself, and then the value is thrown away when there is one:

```
ignores_result :: proc(n: Int) Int ->
    double(n)
    return n
```

A call that is a statement is the way to call a procedure that returns nothing.

### Multiple return values

A procedure can return several values. Its return types are written between parentheses and separated by commas, and a `return` statement gives a value for each of them, separated by commas, in the same order:

```
divide :: proc(a: Int, b: Int) (Int, Int) ->
    return a / b, a - a / b * b

describe :: proc(n: Int) (Bool, Str, Int) ->
    if n < 0 ->
        return false, "negative", 0 - n
    return true, "not negative", n
```

The values can be of different types, and any type that a procedure can return, a `Str` and a struct too. A single return type is written without parentheses, so `(Int)` is an error. Every `return` must give as many values as the procedure has return types, and each value must be of its type, because nothing is converted, so `return 1` in `divide` is an error. The values are evaluated from left to right, before the deferred statements of the blocks that the `return` leaves run, see [Defer](#defer).

The values of a call are taken by defining a variable for each of them, with the names separated by commas before `:=`. The first name gets the first value, the second name the second one, and so on, and each variable has the type of its value:

```
use_divide :: proc() Int ->
    quotient, remainder := divide(31, 4)
    return quotient * 10 + remainder
```

Here `use_divide` returns 73. The call is evaluated first, and then the variables are defined, so the names are not in scope in the arguments, and they follow the rules for the names of the variables, see [Scope resolution](#scope-resolution). There must be as many names as the procedure returns values, and a definition has at most 8 names, so `quotient := divide(31, 4)` and `a, b, c := divide(31, 4)` are errors. A value that is not wanted takes the name `_`, as in `quotient, _ := divide(31, 4)`. That does not define a variable, so `_` cannot be used as a name, and any number of values of a call can be left out in this way. A call that is a statement throws all of its values away, as for a procedure that returns one value.

The type of the variables is always deduced, so `quotient, remainder : Int = divide(31, 4)` is not written. The definition is the only place that takes several values, so a call that returns several values is not an operand, an argument or a value of a `return`, which each take one value: `add(divide(31, 4))` and `return divide(31, 4)` are errors, and the values have to be named and given on. Several values are not assigned to existing variables with a single `=` either, and a variable is assigned to with a statement of its own.

A pointer to a procedure that returns several values has them in parentheses in its type too, `^proc(Int, Int) (Int, Int)`, and it is called like any procedure, with the values taken as above, see [Pointers to procedures](#pointers-to-procedures). Two pointers to procedures are of the same type when they return the same types in the same order. A foreign procedure of the C library returns one value only.

> **Lessons learnt from C** In C, a function returns one value, so a function that has two results returns one of them through a pointer that the caller passes in, or makes a struct for the two of them, which has a name that nothing else uses. The pointer can be left unset, and then the result is lost without the compiler noticing. In Mialu each result is a value of the procedure, and it is taken with a name of its own where it is called.

### Pointers to procedures

A procedure is not a value that can be stored, but a pointer to it is. The type of a pointer to a procedure is written with a caret, and then `proc`, the types of the parameters between parentheses, and the return type, like the head of a procedure without the names of its parameters: `^proc(Int, Int) Int` is a pointer to a procedure that takes two `Int` values and returns an `Int`, `^proc() Int` to one that takes nothing, and `^proc(^Int)` to one that returns nothing. A pointer to a procedure that returns several values has its return types in parentheses, `^proc(Int, Int) (Int, Int)`, as the head of a procedure does. The caret is always written, because what a variable holds is the pointer, so `proc(Int) Int` alone is not a type.

> **Lessons learnt from C** In C, the type of a pointer to a function is written around the name of the variable, as in `int (*f)(int, int)`, which is hard to read and is the reason that people hide it behind a `typedef`. In Mialu the type is written from left to right like any other, `^proc(Int, Int) Int`, and can be used directly in a parameter, a field or an array.

The name of a procedure that is not called is a pointer to it. A variable that holds one is called by writing its name and the arguments, like the name of a procedure:

```
add :: proc(a: Int, b: Int) Int ->
    return a + b

create_pointer_to_procedure :: proc() Int ->
    proc_p : ^proc(Int, Int) Int = add
    return proc_p(5, 15)

create_pointer_to_procedure2 :: proc() Int ->
    proc_p := add
    return proc_p(5, 15)
```

Both return 20. The type of `proc_p` is written in the first one and deduced from `add` in the second. What makes the type is the types of the parameters and the return type, and not the names of the parameters or of the procedure, so every procedure that takes two `Int` values and returns an `Int` is a `^proc(Int, Int) Int`, and a variable of that type can point to another of them later with `=`. A pointer to a procedure that takes other types, or returns another type, or returns nothing, is another type, and is not accepted where this one is wanted.

A pointer to a procedure can be a variable, a parameter, a field of a [struct](#structs) and an element of an [array](#arrays) or an array slice, as any other type can. Passing one to another procedure is how a procedure is told what to do for each element, or how to combine two values:

```
double :: proc(n: Int) Int ->
    return n * 2

apply_twice :: proc(f: ^proc(Int) Int, n: Int) Int ->
    return f(f(n))

use_callback :: proc() Int ->
    return apply_twice(double, 5)
```

`use_callback` returns 20. A pointer to a procedure that is in an array or a struct is stored in a variable before it is called, as in `f := operations[1]` and then `f(6, 3)`, because a call is written with a name. The elements of an array of them are made with `[const]^proc(Int, Int) Int { add, subtract }`.

A pointer to a procedure that has no value is `nil`, like any other [pointer](#null-and-lifetime): it is the default value, so `f : ^proc(Int) Int` is `nil`, and `nil` can be given to a variable, a field, an element or a parameter of such a type, and compared with `==` and `not=`. Two pointers to procedures are equal when they point to the same procedure. Calling `nil` traps. There is no way to tell that from a bad call at compile time, which is why it is good to compare a pointer with `nil` before it is called when it can be.

A pointer to a procedure does not point to a value, so it is not followed with `^`: `f^` is an error, and the address of a procedure, `%add`, is an error as well. A pointer to a variable that holds a pointer to a procedure is written with one more caret, `^^proc(Int) Int`, and it is made with `%` from the variable, as for any variable. Following it with `^` gives the pointer to the procedure, which is how a procedure can change which procedure a variable of its caller points to.

A few things cannot be done with a pointer to a procedure, which a later version of the language may allow:

- A procedure cannot return one, and it cannot be the return type of a procedure type, because a return type is a name with carets before it. A procedure can take one, and store it where a pointer can be stored.
- The parameters of a type of a pointer to a procedure are not pointers to procedures themselves, so `^proc(^proc(Int) Int) Int` cannot be written.
- A global variable cannot hold one, because its initial value is a constant expression, and no constant expression is a pointer to a procedure.
- Only the name of a variable, a parameter or a global variable is called, and not an element, a field or the result of following a pointer, which are stored in a variable first.


## Built-in procedures

A built-in procedure is a procedure that the language has, and that is called like any other, with its arguments between parentheses, see [Calling procedures](#calling-procedures). It is not written in the program, and it is not a keyword: its name is an [identifier](#identifiers), so a program can define a procedure of the same name, such as `length_of`, which is then called instead of the built-in one, and the built-in one is called only where the program has nothing of that name. A built-in procedure is not a value, so it cannot be assigned to a variable or have its address taken, and a call of it has the arguments that it says and no others. The procedures that the language has are:

| Procedure | Takes | Gives |
|-----------|-------|-------|
| [`length_of`](#length_of) | an array, an array slice or a `Str` | the number of its elements or characters, an `Int` |
| [`int_to_byte`](#int_to_byte) | an `Int` | a `Byte` of it, and stops the program when it is not from 0 to 255 |
| [`int_to_i32`](#int_to_i32) | an `Int` or an `I64` | an `I32` of it, and stops the program when it is not from -2147483648 to 2147483647 |
| [`int_to_str`](#int_to_str) | an integer that is known when the program is compiled | its decimal text, a string that is a constant |
| [`str_to_c_str`](#str_to_c_str) | a `Str` | a `C_Str`, a copy of it that ends with a zero |
| [`try_str_to_int`](#try_str_to_int) | a `Str` | two values: the `Int` that it is, and whether it is one |
| [`utf8_decode`](#utf8_decode) | an array or an array slice of `Byte` | two values: the `Str` of the bytes, and whether they are valid UTF-8 |

### length_of

`length_of` gives the number of elements of an array or an array slice, or the number of characters of a string, as an `Int`. It takes one argument, and it is called like any procedure:

```
length_of_array :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    return length_of(a)

average :: proc(elements: []Int) Int ->
    if length_of(elements) == 0 ->
        return 0
    return sum(elements) / length_of(elements)

fill_with_length :: proc() Int ->
    a := [4]Int { .. = 0 }
    for i in 0..<length_of(a) ->
        a[i] = i + 1
    return sum(a)
```

`length_of_array` returns 5. The length of an array is part of the program, so for an array `length_of` is a number that is known when the program is compiled, also for an array whose length is `const`. The length of a slice is not known until the program runs, so for a slice it reads the length that the slice holds, which for `a[low:high]` is `high - low`. The length of an array of nothing, `[const]Int { }`, is 0. The length of a string is the number of its code points, so `length_of("äiti")` is 4, and it is worked out when the program runs by counting the bytes that are not the continuation of another, which takes as long as the string is long, see [Strings](#strings). Anything else as the argument, such as an `Int`, is an error.

`length_of` is not a keyword but a name that a program can define itself: a procedure with that name is called instead of the built-in one. The [members of an enum](#static-reflection) are an array, so `length_of` gives their number as well.

### int_to_byte

`int_to_byte` makes a `Byte` of an `Int`. An `Int` is never a `Byte` without it, whatever its value, because its value is known only when the program runs, see [Bytes](#bytes). It takes one `Int`, and gives a `Byte` that has the same number, and when the number is not from 0 to 255 there is no `Byte` that has it, so the program is stopped, as it is for an index that is outside of an array:

```
to_byte :: proc(n: Int) Byte ->
    return int_to_byte(n)
```

`to_byte(200)` is the `Byte` 200, and `to_byte(256)` and `to_byte(0 - 1)` stop the program. A number that is known when the program is compiled needs no conversion: an untyped integer from 0 to 255 is a `Byte` where one is wanted. The other direction, from a `Byte` to an `Int`, never loses anything, so it is a [cast](#casts), `cast(Int)b`.

### int_to_i32

`int_to_i32` makes an `I32` of an `Int` or an `I64`, which is not a cast because they may not fit in 32 bits, see [32-bit integers](#32-bit-integers) and [64-bit integers](#64-bit-integers). It takes one `Int` or one `I64`, and gives an `I32` that has the same number, and when the number is not from -2147483648 to 2147483647 there is no `I32` that has it, so the program is stopped, as it is by `int_to_byte`:

```
to_i32 :: proc(n: Int) I32 ->
    return int_to_i32(n)
```

`to_i32(123456)` and `to_i32(0 - 5)` are the `I32` values 123456 and -5, and `to_i32(2147483648)` and `to_i32(0 - 2147483649)` stop the program. A number that is known when the program is compiled needs no conversion: an untyped integer that fits in 32 bits is an `I32` where one is wanted. The other direction, from an `I32` to an `Int`, never loses anything, so it is a [cast](#casts), `cast(Int)n`.

### int_to_str

`int_to_str` makes the decimal text of an integer, a string. It is the only built-in procedure that is a [constant](#constants): its argument must be known when the program is compiled, so a literal, a constant or a constant expression, whose type is untyped, `Int`, `I32` or `I64`, and the text is made then. The result is one string, like a string literal, so nothing is made when the program runs, and it can be joined with `++`, compared, measured, given to a constant, or given to a `C_Str`, as any other constant string. A negative number has a minus sign before its digits, and there are no other characters: no plus sign, no leading zeros and no separators.

```
MAJOR :: 1
MINOR :: 20

VERSION :: int_to_str(MAJOR) ++ '.' ++ int_to_str(MINOR)
```

`VERSION` is the string `"1.20"`, and `int_to_str(0 - 7)` is `"-7"`. A `Byte`, a `Rune`, a truth value, a float or a string is not an integer, so `int_to_str(B)` for a `Byte` constant `B` is an error, and so is an argument that is known only when the program runs, such as a variable or a parameter, because the text is made when the program is compiled. The other way round, making a number of text, is `try_str_to_int`, which works when the program runs.

### str_to_c_str

A `Str` value is never converted to a `C_Str` implicitly. A string literal, and a constant that is an untyped string, is not a `Str` yet: it is a `C_Str` where one is wanted, which is the same as `str_to_c_str` of it, see [Untyped constants](#untyped-constants). `str_to_c_str` makes a `C_Str` of a `Str` explicitly, and the `C_Str` it gives can be kept in a variable, passed on or returned. Example:

```
package greetings

import foreign libc

foreign libc ->
    puts :: proc(content: C_Str) -> ...

greet :: proc() ->
    text : C_Str = "Hello, world!"
    puts(text)

main :: proc() Int ->
    puts("Hello, world!")
    puts(str_to_c_str("The same, written out."))
    return 0
```

`str_to_c_str`, and an untyped string that is a `C_Str`, copies the bytes of the string and a zero after them to temporary storage, and gives the address of the copy. Temporary storage is a stack of strings that belongs to the program: a copy is made on top of it, and all the copies that a procedure made are given back when that procedure returns. So the `C_Str` that `str_to_c_str` gives is good until the procedure that called it returns, and it must not be returned from that procedure or kept anywhere that outlives it. Temporary storage has a fixed size, and a program that makes more copies than fit in it is stopped.

See [Compilation targets](#compilation-targets) for which targets have it, and what they allow as the argument.

### try_str_to_int

`try_str_to_int` makes an `Int` of a `Str`, when the string is the text of one. It takes one `Str`, or an untyped string such as a literal, and it gives two values, which is why the call is written as the definition of two names, see [Multiple return values](#multiple-return-values): the first is the number, an `Int`, and the second is a `Bool` that says whether the string was a number at all.

```
parse_port :: proc(text: Str) Int ->
    port, is_ok := try_str_to_int(text)
    if not is_ok or port > 65535 ->
        return 0 - 1
    return port

is_number :: proc(text: Str) Bool ->
    _, is_ok := try_str_to_int(text)
    return is_ok
```

`parse_port("8080")` returns 8080, and `parse_port("80a")` and `parse_port("")` return -1. A string is a number when it is an optional `-`, and then one or more of the digits 0 to 9, and nothing else. So `"42"`, `"-42"` and `"0042"` are numbers, and `""`, `"-"`, `"+5"`, `" 5"`, `"5 "`, `"1_000"`, `"0x10"`, `"--5"` and `"3.5"` are not: there is no plus sign, no space, no separator, no other base and no fraction. The number must fit in an `Int`, so `"9223372036854775807"` and `"-9223372036854775808"`, the largest and the smallest, are numbers, and `"9223372036854775808"` and `"-9223372036854775809"` are not, because a number is never wrapped around.

When the string is not a number, the first value is 0 and the second is `false`, so a program that does not look at the second value cannot tell `"0"` from `"zero"`. The first value can be left out with the name `_`, as above. A call that is given one name, as in `n := try_str_to_int(text)`, is an error, since it gives two values, and so is a call that is given another number of names.

The string is converted when the program runs, also when it is a literal, so the result is not a constant, and cannot be the length of an array or the pattern of a [match statement](#match-statements). A `C_Str` is not converted, because it has no length that the language knows, see [C strings](#c-strings).

### utf8_decode

`utf8_decode` makes a `Str` of bytes, which is how a program that has bytes, from a file or from the C library, gets text that the string operations work on: the subscript, the range, `length_of` and the for statement of a [string](#strings). It takes one array or array slice of `Byte`, and it gives two values, which is why the call is written as the definition of two names, see [Multiple return values](#multiple-return-values): the first is the `Str`, and the second is a `Bool` that says whether the bytes were valid UTF-8. A `Str` is always valid UTF-8, so a conversion that cannot be sure of that has to say so, and it is the only way to a `Str` that is not a constant:

```
text_of :: proc(bytes: []Byte) Str ->
    text, is_ok := utf8_decode(bytes)
    if not is_ok ->
        return "?"
    return text

is_text :: proc(bytes: []Byte) Bool ->
    _, is_ok := utf8_decode(bytes)
    return is_ok
```

The bytes are valid when they are UTF-8 as [RFC 3629](https://www.rfc-editor.org/rfc/rfc3629) defines it. A byte below 0x80 is a character. Any other character starts with a byte that says how many bytes continue it, and each of them has the bits 10 at the top, so they are from 0x80 to 0xBF. The first byte of a character that has one continuing byte is 0xC2 to 0xDF, of one that has two is 0xE0 to 0xEF, and of one that has three is 0xF0 to 0xF4. Any other first byte, which includes a continuing byte that comes alone, 0xC0 and 0xC1, and 0xF5 and above, is not valid. A character must also be written in as few bytes as it can be, so the byte after 0xE0 is 0xA0 or more and after 0xF0 it is 0x90 or more, a surrogate, which is U+D800 to U+DFFF and is the bytes 0xED 0xA0 0x80 to 0xED 0xBF 0xBF, is not valid, so the byte after 0xED is 0x9F or less, and nothing above U+10FFFF is, so the byte after 0xF4 is 0x8F or less. A character that is cut off by the end of the bytes is not valid either. No bytes at all are valid, and make the empty string.

When the bytes are not valid, the first value is the empty string and the second is `false`, so a program that does not look at the second value gets a string with no characters, and not part of the bytes. The first value can be left out with the name `_`, as above. A call that is given one name, as in `text := utf8_decode(bytes)`, is an error, since it gives two values, and so is a call that is given another number of names. An argument that is not an array or an array slice of `Byte`, such as an array of `Int` or a `Str`, is an error.

The `Str` is a view of the bytes that are given, and not a copy, like a [slice of a string](#strings): nothing is allocated, and the string is good as long as the bytes are, which for an array is until the procedure that has it returns. A string that is returned from a procedure that made it from the array of that procedure points at bytes that are gone. The bytes are not changed by it, and a change to them afterwards is seen by the string, so bytes that are decoded should not be changed while the string is used. The bytes are checked once, when the program runs, which takes as long as there are bytes.

## Scope resolution

Every name that is used refers to something that is declared, and the compiler finds out what before the program is built for a target. This is called scope resolution. There are two scopes:

- The module scope has the name of every constant that is defined in the file, such as the procedures and the strings, and of every global variable.
- The parameters and the variables of a procedure live in a local scope nested inside it. A variable is in scope below its definition only.
- Every block of an if statement, a for statement, a loop or a scope statement is a scope nested inside the one that it is written in. A variable that is defined in a block is in scope below its definition until the block ends, so the blocks inside it can use it, and the statements after the block cannot. The iterator of a for statement is in scope in the block of the statement, but not in its range.

A name is looked up in the local scope of the procedure that uses it first, and then in the module scope. So a procedure can use a constant that is defined outside of it:

```
NAME :: "Bob"

get_name :: proc() Str ->
    return NAME
```

The order of the definitions does not matter, so a procedure can use a constant or a global variable that is defined below it.

> **Lessons learnt from C** In C, a variable in an inner block can have the name of one in an outer block and hide it. A reader, or the author a month later, can take one for the other, and the change of a name or a block can silently make a use refer to another variable. In Mialu, a name has one meaning wherever it can be used, so this cannot happen.

There is no shadowing of names so a name cannot be declared twice in the same scope. A name cannot be declared again in a nested scope either, where the first declaration is visible. A name has one meaning wherever it can be used. The compiler rejects a program when:

- two definitions, constants or global variables, have the same name,
- two parameters of a procedure have the same name,
- a parameter has the name of a constant, including the constant that is the procedure itself, or of a global variable,
- a variable has the name of a constant, of a global variable, of a parameter, or of a variable that is in scope where it is defined, so a block cannot define a variable that the blocks around it have, but two blocks that are not inside each other can each define a variable of the same name,
- a name in an expression is neither a variable that is in scope there, nor a parameter of the procedure, nor a constant, nor a global variable,
- the name in a call is not a constant that is a procedure, which is also the case when it is a variable, a global variable or a parameter,
- an attribute of a global variable is not `static`, or is given twice, or the global variable does not have the attribute `static`,
- a `break` is not inside a loop or a scope, or a `continue` is not inside the block of a loop,
- the label of a `break` or a `continue` is not the label of a statement that it is inside of, the label of a `continue` is not the one of a loop, or a statement has the label of a statement around it,
- a `defer` takes a variable definition, a `return`, a `break` or a `continue`, a deferred statement has a `return` or a `defer` in it, or a `break` or a `continue` in it leaves it, see [Defer](#defer),
- two fields of a struct have the same name,
- the name that an assignment assigns to is not a variable or a global variable, but a parameter or a constant, or the iterator of a for statement. A field that is assigned through a parameter or an iterator is an error as well, unless the parameter is a pointer, but only the target tells whether a name is a pointer, see [WebAssembly](#webassembly).

Assigning through a pointer, as in `p^ = 1` or `p.age = 1`, changes what `p` points to and not `p`, so `p` can be a parameter. A variable whose address is taken, as in `%x` and `%x.field`, is still a variable of the procedure: the analysis only notes that its address is taken, because a target may keep such a variable somewhere else than the others, see [WebAssembly](#webassembly).

Scope resolution is about names only. Whether the value of a name has the right type is checked separately for the target, see below.

## Attributes

An attribute gives a definition a property that its syntax does not say. The attributes of a definition are written as an attribute list after its name, before the `:`. An attribute list begins with `#`, and each attribute in it is a name surrounded by parentheses:

```
counter #(static) := 0
```

A list can have more than one attribute, which are written one after the other, such as `#(first)(second)`, and the `#` is written once. The list is not an expression and it has no value. Spaces may be written between its tokens, but the convention is to write no space inside it and one space before the `#`, as above. A list has at least one attribute, so `#()` is an error, and so is a name that is not between parentheses, such as `#static`.

The attributes are built into the language. Mialu has two so far: `static`, which is an attribute of a variable, see [Global variables](#global-variables), and `partial`, which is an attribute of a match statement and is written after `match`, as in `match #(partial) color ->`, see [Match statements](#match-statements). Any other attribute is an error, and so is an attribute that is given twice in the same list, or one that is not an attribute of what it is given to, such as `static` on a match statement. Attributes are accepted on the definition of a global variable and on a match statement only. A constant and a variable of a procedure take none yet, so they are errors there.

## Foreign procedures

A program can call procedures that are not written in Mialu but in another language, which is JavaScript, the C library or a library of the system. They are called foreign procedures, and the language that they are in is the foreign target. Two things are needed: an import, which says where the procedures come from, and a foreign block, which lists the ones that the program uses, with their parameters and return types:

```
package alert_hello

import foreign js "functions"

foreign js ->
    alert :: proc(msg: Str) -> ...

print_hello :: proc() ->
    alert("Hello, world!")
```

`import foreign js "functions"` is written in the first column, like a definition, below the [package definition](#packages). `foreign` and `import` are [keywords](#keywords). `js` is the foreign target, JavaScript, and the other is `libc`, the C library of the platform, which is imported with `import foreign libc` and needs no name, because there is one C library. `js` and `libc` are not keywords. The text in quotes is the name of the object that the procedures of `js` are properties of. It is a name and not a path, and Mialu does not check that it exists, because that is only known when the program runs. A program imports once for each target, above the first foreign block of that target, and a second `import foreign` of one target is an error, as is a block without the import of its target. A library of the system, which is the third kind, has a name that the program gives it, see [Foreign procedures in the libraries of the system](#foreign-procedures-in-the-libraries-of-the-system).

A foreign block is `foreign js ->` or `foreign libc ->` in the first column, and below it, indented one level, the procedures, one on each line. A program can have any number of blocks. A foreign procedure is written like a procedure, but where its body would be it has `...`, which stands for the code that is somewhere else:

```
foreign js ->
    add :: proc(a: Int, b: Int) Int -> ...
    log :: proc(message: Str) -> ...
```

The parameters and the return type are what the other language receives and has to give back, and the types are the ones that any procedure can have. A foreign procedure is a constant of the module, like a procedure that has a body, so it has a name in the same scope, which no other definition can have, and it is called in the same way as the others, with [arguments](#calling-procedures) that must match its parameters, from the procedure above or below its block. It can return a value, which is used like the value of any call. A foreign procedure is not a value: it can only be called. The `...` can only be written in a foreign block, so `f :: proc() -> ...` anywhere else is an error, and the block lists procedures only, nothing else.

The compiler cannot know what the other language does, so it trusts the declaration. What happens when the code of a foreign procedure does not match it depends on the target.

### Foreign procedures in WebAssembly

The target makes a foreign procedure an imported function of the module. See [WebAssembly](build-target-wasm.md#foreign-procedures). An `I32` arrives in JavaScript as a plain number and is returned as one, where an `I64` and an `Int` are a `BigInt`.

### Foreign procedures in the libraries of the system

A library of the system, such as `Kernel32` of Windows, is imported with a name that the program chooses and the library in quotes, and its procedures are declared in a block that has the name:

```
import foreign kernel32 "system:Kernel32.lib"

foreign kernel32 #(default_calling_convention("system")) ->
    GetCurrentProcessId :: proc() U32 -> ...
    CloseHandle :: proc(handle: Int) I32 -> ...
```

The text in quotes is `system:`, the name of the library and `.lib`, as it is given to a linker, but nothing is linked statically: `User32.lib` is the import library of `User32.dll`, which says what the DLL exports and holds none of its code, and Mialu has no such file to read, since the name is only turned into the name of the DLL. The program carries the names of the DLL and of its procedures, and the loader of Windows finds them when the program starts, as for any program that uses the library. The program imports from the file that has the name and `.dll`, which is `Kernel32.dll` here, and calls the procedures by the name that they have in the library, as it does those of the C library. The name that the import gives, `kernel32`, is not a keyword, and only the blocks use it: a block must come below the import of its name, and a name is given once, so a second `import foreign kernel32` is an error. A program can import any number of libraries, and any number of blocks can name one.

The attribute `#(default_calling_convention("system"))` after the name of the block says how its procedures are called, `"system"` for the convention that the libraries of the system have, and `"c"` for the one of C. It may be left out. The targets have one convention, which is the one of Windows on x64, where they are the same, so it changes nothing in the code of the program: it says what the programmer means. Any other convention is an error.

The types are the ones that the procedure has in the library, as for the C library: a `DWORD` is a `U32`, a `BOOL` is an `I32`, a `HANDLE` or any other address is a `^Byte` or, when it is to be compared with a number such as `INVALID_HANDLE_VALUE`, an `Int`, and a `LPCWSTR` is a `^Byte` that points at UTF-16 text, which the program makes in a buffer that it owns. Only the x64-win32 target imports from a library of the system, and the wasm target does not accept the block, as it does not accept `foreign libc ->`.

### Foreign procedures in the C library

A foreign procedure of `libc` is a function of the C library of the platform, which the program links to and calls by the name of the procedure, as C code does. A parameter or a result that is an `I64` is a `long long`, an `int64_t` or a `size_t` in C, so `_abs64 :: proc(number: I64) I64 -> ...` and `strlen :: proc(text: C_Str) I64 -> ...` are functions of the C library, and an `Int` is the same size, so either is passed the same way, but the declaration says which the function is. A parameter or a result that is an `I32` is an `int` in C, so `abs :: proc(number: I32) I32 -> ...` and `atoi :: proc(text: C_Str) I32 -> ...` are the functions of `stdlib.h`, and an `Int` is a `long long`, which is not the same size, so a function that returns an `int` is declared with an `I32`, since the other half of what it returns is not said. A parameter or a result that is an `F32` is a `float` in C, and one that is an `F64` is a `double`, so `sqrtf :: proc(x: F32) F32 -> ...` and `sqrt :: proc(x: F64) F64 -> ...` are the functions of `math.h`, and `printf :: proc(format: C_Str, value: F64) Int -> ...` takes a `double` for `%f`. C converts a `float` to a `double` when it is one of the variable number of arguments of a procedure like `printf`, and Mialu does not, so an `F32` is made an `F64` with `cast` first. See [x64-win32](build-target-x64-win32.md#foreign-procedures).

## Compilation targets

The compiler is used from the command line, see [mialuc](mialu-cli.md) for its commands. `build --target <target>` writes a program for a target, and the targets are `wasm`, `wat` and `x64-win32`. `run` and `dump-asm` use `x64-win32`, the only target that can run on a machine, and have assembly to show, for now. What each target builds, how, and what it rejects, is in a document of its own.

### WebAssembly

The `wasm` target writes a WebAssembly binary module, and the `wat` target the same module as text. See [Build target: WebAssembly](build-target-wasm.md).

### x64 for Windows

The `x64-win32` target writes an executable file for Windows on x64. See [Build target: x64-win32](build-target-x64-win32.md).

## Possible future features

### Parametric polymorphism

```
sum :: proc(a: $T, b: T) T ->
    return a + b

main :: proc() ->
    x := sum(1, 2)
    y := sum(1.0, 2.0)
    ...
```

### Converting a string to bytes

The recommendation of [Strings](#strings), to go through a `Str` by byte and to measure it in bytes by converting it to a `[]Byte`, needs a conversion that the language does not have yet. Today a program has no way to read the bytes of a `Str`: it can go through the characters with a for statement, take one with a subscript, take a part with a range and count them with `length_of`, and it can hand the bytes to the C library with `str_to_c_str`, but it cannot see a byte, or count them.

The other direction is the built-in procedure [`utf8_decode`](#utf8_decode), which makes a `Str` of bytes and says whether they are valid UTF-8. The conversion that this section describes would give a `[]Byte` that has the address and the length in bytes of the string, which are the two words that a `Str` is already, so there is nothing to compute and nothing to copy. How it is written is not decided: it could be a [cast](#casts), such as `cast([]Byte)text`, or a [built-in procedure](#built-in-procedures). The `[]Byte` that it gives is read only, since a string is immutable.

```
byte_count :: proc(text: Str) Int ->
    bytes := cast([]Byte)text
    return length_of(bytes)

sum_of_bytes :: proc(text: Str) Int ->
    total := 0
    for b in cast([]Byte)text ->
        total += cast(Int)b
    return total
```

`byte_count("äiti")` would be 5, where `length_of("äiti")` is 4.

## Rejected ideas

Below is a list of ideas that were considered for Mialu, but rejected. They are not part of the language, but they exist to document the decisions that were made.

### Interfaces

I had this idea in 2026-11-04 that I could give a procedure an interface parameter. This interface parameter would allow any struct that has the same fields as the interface to be passed to the procedure. The procedure could then access the fields of the struct through the interface parameter, without knowing the exact type of the struct. This would allow for more generic programming and code reuse.

This implementation would have not required any specialization of the procedure for each struct type or some genetic programming template generation because individual fields can just be parameters implicitly. For a given interface, the actual parameters would be determined based on the order of fields defined.

Here's an example of how this would work:

```
Person :: struct ->
    age: Int
    name: Str

Aged :: interface ->
    age: Int

bar :: proc(p: Aged) Int ->
    return p.age

foo :: proc() Int ->
    p := Person {
        age = 30,
        name = "Bob",
    }
    return bar(p)
```

The bar procedure would actually be represented this way:

```
bar :: proc(age: Int) Int ->
    return p.age
```

If you have multiple fields in a struct

```
Restaurant :: struct ->
    name: Str
    location: Str
    rating: Int
```

and the interface also included multiple fields like this

```
Company :: interface ->
    location: Str
    name: Str

get_location :: proc(c: Company) Str ->
    return c.location
```

then the parameters would work this way then:

```
get_location :: proc(location: Str, name: Str) Str ->
    return p.location
```

The reason I rejected this idea is that I couldn't really find good enough use cases for it.
