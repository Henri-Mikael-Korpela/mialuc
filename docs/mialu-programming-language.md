# Mialu Programming Language Specification

## Table of contents

- [Introduction](#introduction)
- [Source code representation](#source-code-representation)
- [Lines, spaces and indentation](#lines-spaces-and-indentation)
- [Comments](#comments)
- [Reserved words](#reserved-words)
- [Identifiers](#identifiers)
- [Primitive data types](#primitive-data-types)
  - [Booleans](#booleans)
  - [Integers](#integers)
  - [Bytes](#bytes)
  - [Strings](#strings)
- [Operators](#operators)
  - [Assignment and compound assignment operators](#assignment-and-compound-assignment-operators)
  - [Comparison operators](#comparison-operators)
  - [Logical operators](#logical-operators)
- [Constants and variables](#constants-and-variables)
  - [Constants](#constants)
  - [Variables](#variables)
  - [Global variables](#global-variables)
- [Control flow structures](#control-flow-structures)
  - [If statements](#if-statements)
  - [For statements](#for-statements)
  - [Loops without a range](#loops-without-a-range)
  - [Scopes](#scopes)
  - [Break and continue](#break-and-continue)
- [Arrays](#arrays)
  - [Array literals](#array-literals)
  - [Elements](#elements)
  - [Array slices](#array-slices)
  - [Length of an array](#length-of-an-array)
- [Custom data types](#custom-data-types)
  - [Enumerations](#enumerations)
  - [Structs](#structs)
  - [Unions](#unions)
- [Reflection](#reflection)
  - [Enumeration type info](#enumeration-type-info)
- [Pointers](#pointers)
  - [Taking an address](#taking-an-address)
  - [Following a pointer](#following-a-pointer)
  - [Fields of a pointer](#fields-of-a-pointer)
  - [Null and lifetime](#null-and-lifetime)
  - [Pointers in structs and arrays](#pointers-in-structs-and-arrays)
- [Procedures](#procedures)
  - [Calling procedures](#calling-procedures)
  - [Pointers to procedures](#pointers-to-procedures)
- [Scope resolution](#scope-resolution)
- [Attributes](#attributes)
- [Foreign procedures](#foreign-procedures)
  - [Foreign procedures in WebAssembly](#foreign-procedures-in-webassembly)
  - [Foreign procedures in the C library](#foreign-procedures-in-the-c-library)
- [Compilation targets](#compilation-targets)
  - [WebAssembly](#webassembly)
  - [x64 for Windows](#x64-for-windows)
- [Possible future features](#possible-future-features)
  - [Parametric polymorphism](#parametric-polymorphism)
- [Rejected ideas](#rejected-ideas)
  - [Interfaces](#interfaces)

## Introduction

This document describes the Mialu programming language, its syntax, semantics and compilation targets.

Mialu is a procedural programming language with straight-foward and familiar syntax, built with simplicity and performance in mind. It is influenced by multiple programming languages including, but not limited to, C, C++, Go, Odin and Python, fully embracing the fact that it comes after many of them, so it has learned from their mistakes and successes.

The fact that there's no mention of functional programming, object-oriented programming or metaprogramming does not mean the author is unfamiliar with or blissfully ignorant of such concepts. The author has made an intention choice to not focus on such paradigms that much in the design of this language. In the future, the overall reasoning will become more evident as the language progresses and new posts documenting certain design decisions emerge.

Where does the name Mialu come from? It comes from the first syllables of my children's names, Miriam, Amira and Lukas. That's my personal touch to such a technical tool.

## Source code representation

Mialu source code is a sequence of Unicode characters, encoded in UTF-8. Source code files should have the `.mu` extension.

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

The compiler does not ignore comments: the tokenizer makes each comment a token (`COMMENT` for `//` and `DOCUMENTATION_COMMENT` for a documentation comment), which holds the text exactly as it is in the source, including the slashes. A documentation comment of many lines is one token, which holds the lines with their line breaks. The parser leaves the comment tokens out, so a comment never changes the syntax tree or the program that is built. The tokens are there for tools, such as formatters and generators of documentation, which need to know where the comments are. `mialuc run` prints them with the other tokens.

The reason for `///` is that documentation should be found by tools and not guessed from the `//` comments around it, so it has a form of its own, which is also the form that editors can color apart. What a documentation comment is attached to is not yet used by the compiler.

## Reserved words

Mialu has reserved words that cannot be used as names for identifiers. Reserved words are case-sensitive, so `If` is an identifier and `if` is not. A longer identifier that only starts with a keyword, such as `returned` or `cannot`, is an identifier.

Reserved words are categorized into used and unused keywords and literal words. Mialu has the following keywords in use, in alphabetical order:

```
and
break
case const continue
else enum
for foreign
if import in
match
not
or
proc
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
cast catch class
defer delete distinct do dynamic
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
safe self switch
super
this throw throws trait
transmute try type_id_of type_of
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
| `Int` | A signed 64-bit integer. Written as a decimal number, such as `42`. See [Integers](#integers). |
| `Str` | A string, which is text encoded in UTF-8. Written in quotes, such as `"Bob"`. |
| `C_Str` | A null-terminated C string, the address of bytes that end with a zero. It is for passing text to and from code written in C, to a parameter of type `char *` or `char const *`. See [C strings](#c-strings). |

The following primitive data types are reserved for possible future use:

| Category | Types |
| -------- | ----- |
| Character | `Rune` |
| Floating point number | `F32 F64` |
| Signed integer | `I8 I16 I32 I64` |
| Unsigned integer | `U8 U16 U32 U64` |

### Booleans

There's only one boolean value type, `Bool`, which is either `true` or `false`, which are [literal words](#literal-words):

```
ready : Bool = true
```

Unlike C, integer types are not implicitly converted into boolean values. A `Bool` is never converted to or from another type. An `Int` is not a condition, so `if n ->` is an error and has to be written `if n not= 0 ->`, and `true` is not `1`: `x : Int = true` and `b : Bool = 1` are errors.

A comparison such as `x > 5` is an expression of type `Bool`, see [Comparison operators](#comparison-operators). The logical operators `and`, `or` and `not` combine `Bool` values, see [Logical operators](#logical-operators).

### Integers

There's one integer type, `Int`, a signed 64-bit integer: it holds the numbers from -9223372036854775808 to 9223372036854775807. An integer literal is a decimal number, such as `42`, and a literal that does not fit in an `Int` is an error. There is no negative literal, so a negative number is written as a subtraction, such as `0 - 4`.

```
count : Int = 42
```

The arithmetic operators `+`, `-`, `*` and `/` take two `Int` values and give an `Int`, see [Operators](#operators), and so do the assignment operators `+=`, `-=`, `*=` and `/=`, see [Assignment](#assignment). Addition, subtraction and multiplication wrap around on overflow, so the largest `Int` plus 1 is the smallest. Division truncates toward zero, so `(0 - 7) / 2` is -3. Dividing by zero, or dividing the smallest `Int` by -1, whose result does not fit, is an error at run time, see [WebAssembly](#webassembly).

An `Int` is not converted implicitly: it is not a `Bool`, so `if n ->` is an error, and an operand of arithmetic that is not an `Int` is an error. The integer types `I8`, `I16`, `I32`, `I64` and `U8`, `U16`, `U32`, `U64` are reserved for possible future use, see [Primitive data types](#primitive-data-types).

### Bytes

A `Byte` is an unsigned 8-bit integer: it holds the numbers from 0 to 255. It can be used to represent individual bytes of memory. The default value for `Byte` is `0`.

The built-in procedures `byte_to_int` and `int_to_byte` allow conversion between bytes and integers.

A `Byte` has no arithmetic: `a + b` for two bytes is an error, and the arithmetic is done on `Int` values, as in `byte_to_int(a) + byte_to_int(b)`.

### Strings

A string literal is the text between a pair of double quotes, on one line:

```
"Hello, world"
```

A `Str` is encoded in UTF-8: its bytes are always a valid UTF-8 encoding of text, as defined by [RFC 3629](https://www.rfc-editor.org/rfc/rfc3629). A string can hold any Unicode character, such as `"héllo"` or `"日本語"`, and each character takes one to four bytes.

A string is length-based, not null-terminated. It is its bytes together with the number of them, so the length is known without looking for an end, and a string can hold a zero byte. The length counts bytes, not characters: `"héllo"` has a length of 6, because `é` takes two bytes. The empty string `""` has a length of zero. A string is immutable, and does not own its bytes: a string literal is a view of bytes that are part of the program, so returning one from a procedure is safe.

The compiler rejects a string literal that is not valid UTF-8. A string cannot contain a double quote or a line break, because they would end it, and cannot contain a backslash. There are no escape sequences.

A `C_Str` (short for "C string") represents a null-terminated string. Unlike `Str`, it doesn't store length, it must determined by a null-terminator, which is a byte with value `0`.

A `C_Str` is for interoperating with existing C code, as in [foreign procedures](#foreign-procedures), the equivalent type in C being `char*` or `char const*` Nothing can be done with a `C_Str` but to keep it in a variable or a parameter, pass it on, return it, and hand it to a foreign procedure.

A `Str` is never converted to a `C_Str` implicitly and there is no `C_Str` literal. The built-in procedure `str_to_c_str` makes a `C_Str` of a `Str`, and the `C_Str` it gives can be kept in a variable, passed on or returned. Example:

```
import foreign libc

foreign libc ->
    puts :: proc(content: C_Str) -> ...

greet :: proc() ->
    text : C_Str = str_to_c_str("Hello, world!")
    puts(text)

main :: proc() Int ->
    puts(str_to_c_str("Hello, world!"))
    return 0
```

`str_to_c_str` copies the bytes of its argument and a zero after them to temporary storage, and gives the address of the copy. Temporary storage is a stack of strings that belongs to the program: a copy is made on top of it, and all the copies that a procedure made are given back when that procedure returns. So the `C_Str` that `str_to_c_str` gives is good until the procedure that called it returns, and it must not be returned from that procedure or kept anywhere that outlives it. Temporary storage has a fixed size, and a program that makes more copies than fit in it is stopped.

See [Compilation targets](#compilation-targets) for which targets have it, and what they allow as the argument.

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
| `*`      | Multiplication. | 6 | `x := 2 + 3 * 4` makes `x` 14 |
| `/`      | Division. The result is truncated toward zero. | 6 | `x := 20 - 12 / 4` makes `x` 17 |
| `%`      | Address of. It is written before its operand, which must have an address, and gives a pointer to it. It binds looser than what follows a name, so `%p.x` is the address of `p.x`. See [Taking an address](#taking-an-address). | 7 | `%x` is a pointer to `x` |
| `[ ]`    | Subscript. The index of an element between brackets. It is written after the name of an array or of an array slice, and binds tighter than every operator. See [Elements](#elements). | 8 | `a[2]` is the element at index 2 of `a` |
| `.`      | Field access. The name of a field after a struct object, which is a name, an element of an array, what a pointer points to, or another field. See [Fields](#fields). | 8 | `p.age` is the field `age` of `p` |
| `[ : ]`  | Slice. The two bounds of an array slice between brackets, separated by a colon. It includes the low bound and excludes the high bound. See [Array slices](#array-slices). | 8 | `a[1:4]` has the elements 1 through 3 |
| `^`      | Dereference. It is written after an expression whose value is a pointer, and gives what the pointer points to. It can be assigned to. See [Following a pointer](#following-a-pointer). | 8 | `p^` is the value that `p` points to |
| `+=`     | Addition assignment. It adds the value to a variable that is defined already. | 0 | `x += 2` adds 2 to `x` |
| `-=`     | Subtraction assignment. It subtracts the value from a variable. | 0 | `x -= 2` makes `x` 2 less |
| `*=`     | Multiplication assignment. It multiplies a variable by the value. | 0 | `x *= 3` triples `x` |
| `/=`     | Division assignment. It divides a variable by the value, truncating toward zero. | 0 | `x /= 2` halves `x` |
| `:`      | Type separator. It separates a name from its type. | 0 | `x : Int = 5` |
| `::`     | Defines a constant. It is two subsequent `:`. | 0 | `NAME :: "Bob"` |
| `:=`     | Defines a variable, deducing its type from the value. It is `:` followed by `=`, with the type left out. | 0 | `x := 4` |
| `=`      | Sets the initial value of a variable whose type is written. It also assigns a new value to a variable, an element or a field, and copies to a range of elements. | 0 | `x : Int = 5`, `x = 6`, `a[0] = 5`, `p.age = 5` |

A binary operator is written between its two operands, such as `a + b`. The operands are integer literals, string literals, `true`, `false`, names, [calls](#calling-procedures) or [array literals](#array-literals), which can have subscripts, or the results of operators of a higher precedence. An operand can have `%` before it, and a name can have `^` after it. `not` is a unary operator, written before its operand. Addition, subtraction, multiplication and division are defined for `Int` only. A comparison takes two `Int` values, or two `Bool` values or two pointers of one type for `==` and `not=`, and its result is a `Bool`.

Operators of the same precedence are left associative, so `a + b + c` means `(a + b) + c`, and `a - b + c` means `(a - b) + c`. `+` and `-` have the same precedence, so `5 - 1 + 2` is 6 and not 2, and so do `*` and `/`, so `8 / 2 * 3` is 12 and not 1. A comparison binds looser than `+` and `-`, and tighter than `not`, `and` and `or`, so `a + 1 > b * 2` means `(a + 1) > (b * 2)`. A comparison is an expression of type `Bool`, but its operands are not comparisons, so it cannot be an operand of another operator, and `a < b < c` and `a == b == c` are errors. An operator of a higher precedence binds tighter, so `2 + 3 * 4` means `2 + (3 * 4)`, which is 14 and not 20, and `20 - 12 / 4` means `20 - (12 / 4)`, which is 17 and not 2.

The operators of precedence 0 define names or assign to them, and are not part of an expression. Each one is written after the name that it defines or assigns to, which can be followed by subscripts, fields and carets, as in `a[0] = 1`, `p.age = 1` and `p^ = 1`, and takes everything up to the end of the line as its right-hand side, so `x := a + b` means `x := (a + b)`, and `x += a * b` means `x += (a * b)`. Spaces may be written between the characters of `::` and `:=`, because they are separate tokens. An assignment operator such as `+=`, `-=`, `*=` or `/=` is one token, so no space may be written between its characters.

See [Variables](#variables) for variable definitions and [Constants](#constants) for constants.

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

`swap` returns 73. An assignment can be in a block of an if or a for statement, and the variable that it assigns to can be defined above the block. A variable that is defined with a type and no value, such as `x : Int`, is given its value with `=` as well. The name must stand for a variable, so `n = 2` is an error for a parameter `n`, and so is assigning to a constant or to the iterator of a for statement. The other assignments work on an `Int` only. `+=` is the addition assignment: it adds the value to the variable and stores the sum in the variable, so `x += 2` leaves `x` 2 greater than it was.

A compound assignment operator applies an arithmetic operator to the variable and the value, and stores the result in the variable, so `x += 2` means `x = x + 2`. The variable and the value must be `Int`.

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

Both operands must have the same type, because nothing is converted. Two [`Int`](#integers) values can be compared with all six operators, and the comparison is signed. `Int` and `Byte` are the only types that have an order, so `<`, `<=`, `>` and `>=` are defined for them only. Two [`Bool`](#booleans) values can be compared with `==` and `not=`, so `a not= b` holds when exactly one of them is `true`. Two pointers can be compared with `==` and `not=` as well, and so can two [pointers to procedures](#pointers-to-procedures), see [Null and lifetime](#null-and-lifetime). Two members of one [enum](#enumerations) can be compared with `==` and `not=` too. Two [`Byte`](#bytes) values can be compared with all six operators, and the comparison is of the numbers from 0 to 255; an integer literal from 0 to 255 is a `Byte` when the other operand is one, so `b >= 65` is allowed, but a `Byte` is never compared with an `Int`. Strings cannot be compared.

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

## Constants and variables

### Constants

A constant is an immutable value with a name, and it is visible in the whole module, also above where it is defined. It is defined by placing two subsequent colons after an identifier:

```
CONSTANT_NAME :: value
```

Constants are commonly used to define procedures:

```
sum :: proc(a: Int, b: Int) Int ->
    return a + b
```

A constant can also be a [struct](#structs) or an [enum](#enumerations), which are types, the [member of an enum](#enumerations), and a string, which is written on its own line:

```
greeting :: "Hello, world"
```

### Variables

A variable is a name for a value that is local to a procedure. It is defined by a name followed by `:=` and its initial value, which is an expression:

```
x := 4
```

The type of the variable is deduced from the initial value. It is implicit. `x` above is of type `Int` because literal `4` is.

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

A variable is defined once, with its initial value, and every variable has one. It can be used in the statements below its definition, but not above it and not in its own initial value. The variables of a procedure are not visible outside of it, so two procedures can have a variable of the same name.

A variable that is defined outside of every procedure is a [global variable](#global-variables).

### Global variables

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

The initial value is a literal: a number, `true` or `false`, or a string, or else a field of [type information](#static-reflection), such as `type_info_of(Color).members`, which is known when the program is compiled and so is a constant like a literal. It is stored when the program starts, before any code runs, so it cannot be an expression, a name or a call. A global variable can be used and assigned to in every procedure of the module, also in the ones that are written above its definition, because it is in the module scope, see [Scope resolution](#scope-resolution). Like a constant, it has a name of its own in that scope, so it cannot have the name of a constant or of another global variable, and a parameter or a variable of a procedure cannot have its name.

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

A block is a scope, see [Scope resolution](#scope-resolution): a variable that is defined in a block can be used in the rest of that block, but not after it.

### For statements

A for statement runs a block of statements once for each number in a range. There is also a [loop without a range](#loops-without-a-range), and a loop can be left early or skipped ahead with [`break` and `continue`](#break-and-continue). After `for` comes the name of the iterator, then the keyword `in`, and then the range. The `->` after the range starts the block, which is indented one level deeper than the `for`, like the block of an `if`:

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

`sum_of_elements` returns 15. A for statement is told to go through a range by the `..<` or `..<=` in it, so one without such an operator must have an array or a slice after `in`, and `for x in 5 ->` is an error. The address and the length of what is gone through are read once, before the loop, but the elements are read as the loop goes on, so a change to an element that has not been reached yet in the block is seen by it. The iterator is a copy of an element, so it cannot be used to change one, and the same rules as for the iterator of a range hold: it cannot be assigned to, it is not in scope in the expression, and `break` and `continue` work as in any loop. An array with no elements runs the block no times. To change the elements, write the index of each:

```
fill :: proc() Int ->
    a := [const]Int { 0, 0, 0, 0, 0 }
    for i in 0..<5 ->
        a[i] = i * i
    return a[4]
```

### Loops without a range

A for statement with no iterator and no range is a loop that has no end of its own. It is written `for ->`, and the block, which is indented one level deeper, runs again and again until a `break` or a `return` leaves it:

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

### Scopes

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

A scope can have a label, which is written before the `->`. A label is the character `@` directly followed by a name, with no space in between, such as `@outer`:

```
find_first :: proc(limit: Int) Int ->
    found := 0
    @search ->
        for i in 1..<=limit ->
            if i * i > 50 ->
                found += i
                break search
    return found
```

The name of a label follows the rules of an [identifier](#identifiers), so a keyword or a literal word cannot be one, and `@ outer` with a space is an error. Labels are a kind of name of their own: they cannot be used as variables, and a variable or a procedure with the same name does not clash with them. A statement cannot have the label of a statement that it is inside of, so a label has one meaning wherever it can be used, but statements that are not inside each other can have the same label.

An if statement, a for statement and a [loop without a range](#loops-without-a-range) can have a label as well. It is written before the `if` or the `for` on the same line, with a space after it:

```
@check if n > 5 ->
    ...
else ->
    ...

@rows for row in 0..<n ->
    ...

@again for ->
    ...
```

The label belongs to the whole statement. For an if statement that is the `if` with all of its `else if` and `else` branches, and only the first line has the label: `@check else ->` is an error. A label does not change what the statement does. It lets a [`break` or a `continue`](#break-and-continue) name it.

### Break and continue

`break` leaves the innermost loop or scope that it is in, and the statement after it runs next. `continue` ends the current pass of the loop and starts the next one: in a loop without a range that is the first statement of the block again, and in a for statement with a range it is the next number of the range, or the end of the loop when there are no more. The iterator is stepped after a `continue` the same way as after a block that runs to its end, so an inclusive range that ends at the largest `Int` also ends. Both are statements of one word on a line of their own:

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

They work in the loops without a range and in the for statements with a range alike, and they can be in the blocks of if statements that are inside the loop. They belong to the innermost loop only, so a `break` in an inner loop does not leave the outer one. The block of an `if` is not a scope to leave, so a `break` in it leaves the loop or scope around the `if`:

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

`break_early()` returns 4. A `break` can name a statement to leave by the name of its label, written after `break` without the `@`. It leaves that scope, loop or if statement, and everything in between, so it can leave several at once:

```
break_early_with_label :: proc() Int ->
    x := 1
    ->
        @my_block ->
            if x > 10 ->
                x += 1
            else ->
                break my_block
            x += 2
        x += 5
    return x
```

`break_early_with_label()` returns 6: `break my_block` leaves the labeled scope, and `x += 5` runs next. A `break` writes the label without the `@`, which is for the line that declares it only, and `break @my_block` is an error. The label must be one of a statement that the `break` is inside of.

A `break` with a label can leave a [labeled if statement](#scopes) from any of its branches, also from a loop inside it, and then the statement after the if statement runs next. A `break` without a label passes an if statement, labeled or not, and leaves the loop or scope around it, because the block of an `if` is not a scope:

```
classify :: proc(n: Int) Int ->
    x := 0
    @check if n > 5 ->
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
    @rows for row in 0..<n ->
        for column in 0..<n ->
            if column > row ->
                continue rows
            total += 1
    return total
```

`count_triangle(3)` returns 6. The label of a `continue` must be the one of a for statement or a loop without a range that it is inside of. A scope or an if statement cannot be continued, so `continue` with the label of one is an error.

A `break` that is not inside a loop or a scope, and a `continue` that is not inside a loop, are errors, and so is one in a procedure that has no loop, even if the procedure is only called from a loop. A `break` or a `continue` with a label that no statement around it has is an error as well. They are not expressions, so they have no value and nothing can follow them on the line, except a label. The statements that come after them in the same block are never run.

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

A variable of an array type is defined with an array literal, and not with another array or a call: `b := a` is an error, because an array is not copied by a definition. The type can be written as well, as long as the literal has the number of elements that it says:

```
a : [3]Int = [const]Int { 7, 8, 9 }
```

An array literal is an expression, so it can also be used without a variable, for example as an argument of a call. Each time that a literal is evaluated it puts its elements in their place again, so a literal in the block of a loop makes a new array in every pass.

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

### Length of an array

The built-in procedure `length_of` gives the number of elements of an array or an array slice, as an `Int`. It takes one argument, and it is called like any procedure:

```
length_of_array :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    return length_of(a)

average :: proc(elements: []Int) Int ->
    if length_of(elements) == 0 ->
        return 0
    return sum(elements) / length_of(elements)

fill_with_length :: proc() Int ->
    a := [const]Int { 0, 0, 0, 0 }
    for i in 0..<length_of(a) ->
        a[i] = i + 1
    return sum(a)
```

`length_of_array` returns 5. The length of an array is part of the program, so for an array `length_of` is a number that is known when the program is compiled, also for an array whose length is `const`. The length of a slice is not known until the program runs, so for a slice it reads the length that the slice holds, which for `a[low:high]` is `high - low`. The length of an array of nothing, `[const]Int { }`, is 0. Anything else as the argument, such as an `Int` or a `Str`, is an error.

`length_of` is not a keyword but a name that a program can define itself: a procedure with that name is called instead of the built-in one. The [members of an enum](#static-reflection) are an array, so `length_of` gives their number as well.

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

The integer types that are reserved for the future, such as `I8` and `U32`, will be the others. 

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

Enum members don't have any order, so comparsing enum values with `<`, `<=`, `>` and `>=` results in an error. An enum is not converted to or from an `Int` or any other type.

The default value for any enum is the first member of the enum.

In the future, the language spec and implementation will address the following issues:

- An enum cannot be the type of a global variable, because its initial value is a literal, and no literal is a member.
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

A procedure parameter list can be empty, and the return type can be omitted if the procedure does not return a value. Example of a procedure with no parameters and no return type:

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

### Pointers to procedures

A procedure is not a value that can be stored, but a pointer to it is. The type of a pointer to a procedure is written with a caret, and then `proc`, the types of the parameters between parentheses, and the return type, like the head of a procedure without the names of its parameters: `^proc(Int, Int) Int` is a pointer to a procedure that takes two `Int` values and returns an `Int`, `^proc() Int` to one that takes nothing, and `^proc(^Int)` to one that returns nothing. The caret is always written, because what a variable holds is the pointer, so `proc(Int) Int` alone is not a type.

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
- A global variable cannot hold one, because its initial value is a literal, and no literal is a pointer to a procedure.
- Only the name of a variable, a parameter or a global variable is called, and not an element, a field or the result of following a pointer, which are stored in a variable first.


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

The attributes are built into the language. The only one that Mialu has so far is `static`, which is an attribute of a variable, see [Global variables](#global-variables). Any other attribute is an error, and so is an attribute that is given twice in the same list. Attributes are accepted on the definition of a global variable only. A constant and a variable of a procedure take none yet, so they are errors there.

## Foreign procedures

A program can call procedures that are not written in Mialu but in another language, which is JavaScript or the C library. They are called foreign procedures, and the language that they are in is the foreign target. Two things are needed: an import, which says where the procedures come from, and a foreign block, which lists the ones that the program uses, with their parameters and return types:

```
import foreign js "functions"

foreign js ->
    alert :: proc(msg: Str) -> ...

print_hello :: proc() ->
    alert("Hello, world!")
```

`import foreign js "functions"` is written in the first column, like a definition. `foreign` and `import` are [keywords](#keywords). `js` is the foreign target, JavaScript, and the other is `libc`, the C library of the platform, which is imported with `import foreign libc` and needs no name, because there is one C library. `js` and `libc` are not keywords. The text in quotes is the name of the object that the procedures of `js` are properties of. It is a name and not a path, and Mialu does not check that it exists, because that is only known when the program runs. A program imports once for each target, above the first foreign block of that target, and a second `import foreign` of one target is an error, as is a block without the import of its target.

A foreign block is `foreign js ->` or `foreign libc ->` in the first column, and below it, indented one level, the procedures, one on each line. A program can have any number of blocks. A foreign procedure is written like a procedure, but where its body would be it has `...`, which stands for the code that is somewhere else:

```
foreign js ->
    add :: proc(a: Int, b: Int) Int -> ...
    log :: proc(message: Str) -> ...
```

The parameters and the return type are what the other language receives and has to give back, and the types are the ones that any procedure can have. A foreign procedure is a constant of the module, like a procedure that has a body, so it has a name in the same scope, which no other definition can have, and it is called in the same way as the others, with [arguments](#calling-procedures) that must match its parameters, from the procedure above or below its block. It can return a value, which is used like the value of any call. A foreign procedure is not a value: it can only be called. The `...` can only be written in a foreign block, so `f :: proc() -> ...` anywhere else is an error, and the block lists procedures only, nothing else.

The compiler cannot know what the other language does, so it trusts the declaration. What happens when the code of a foreign procedure does not match it depends on the target.

### Foreign procedures in WebAssembly

The target makes a foreign procedure an imported function of the module. The name of the object in `import foreign js` is the name of the module of the import, and the name of the procedure is the name of the import. So the program above needs an object of imports like this when it is instantiated:

```js
const imports = {
    functions: {
        alert: (address, length) => {
            const bytes = new Uint8Array(instance.exports.memory.buffer, address, length);
            console.log(new TextDecoder().decode(bytes));
        },
    },
};
const { instance } = await WebAssembly.instantiate(bytes, imports);
```

The function gets the parameters as the target passes them everywhere: an `Int` is a `BigInt`, a `Bool` is the number 0 or 1, a pointer is one number, the address of what it points to in the memory of the module, and a `Str` is two numbers, the address of its bytes in the memory of the module and their count, which is how the function above reads the message. The memory is the one that the module exports as `memory`, which a program has when it has a string or an array type. A function that returns something returns it in the same way, so a procedure that returns an `Int` is a function that returns a `BigInt`, and one that returns a `Str` returns an array of the address and the length. If the object does not have the function, `WebAssembly.instantiate` rejects with a `LinkError` that names it.

### Foreign procedures in the C library

A foreign procedure of `libc` is a function of the C library of the platform, which the program links to and calls by the name of the procedure, as C code does. The types of the parameters and the result are the ones that the function has in C: a `C_Str` for a `char *` or a `char const *`, and an `Int` for an integer of 64 bits. The compiler trusts the declaration. A C function that returns an `int` has 32 bits, and the other 32 bits of an `Int` that is declared as its result are not defined, so such a function is declared without a result, or its result is not used.

```
import foreign libc

foreign libc ->
    printf :: proc(content: C_Str) -> ...

main :: proc() Int ->
    printf("Hello, world!")
    return 0
```

## Compilation targets

The compiler has two modes: `run` without a target prints the tokens and the syntax tree of a program, after which it resolves the names, and `build --target <target> -o <output file> <source file>` writes a program for a target. The output file can also be written without `-o`, directly after the target. `run --target <target> <source file>` builds the program for a target that can run on this machine, runs it, and ends with the exit code of the program. The targets are `wasm`, `wat` and `x64-win32`, and `x64-win32` is the only one that `run` takes.

### WebAssembly

The `wasm` target writes a WebAssembly binary module. It imports its foreign procedures from JavaScript, so a procedure of a `foreign libc` block is an error, and so is a call of `str_to_c_str` whose argument is not a string literal. Every constant definition becomes a function that the module exports under the name of the constant. A constant that is a string becomes a function without parameters that returns the string, and a procedure that uses the constant by its name calls that function. A call of a procedure is the `call` instruction with the function of the procedure, after the instructions of the arguments, which are pushed in order. A `Str` argument or result is two values, as it is for the exports. A call that is a statement is followed by a `drop` for every value that the procedure returns, which is one, or two for a `Str`. `Int`, `Bool`, `Byte` and `Str` are the basic types that the target supports, and then the structs, the pointers to any of them, and the arrays and array slices of any of them.

`Int` is a 64-bit integer (`i64`), so JavaScript passes and receives it as a `BigInt`. The arithmetic is the one of [Integers](#integers), with the signed `i64` instructions. Division traps, which makes the call from JavaScript throw a `WebAssembly.RuntimeError`, when the divisor is zero, or when the smallest `Int` is divided by -1, because the result does not fit.

`Bool` is a 32-bit integer (`i32`), which is 0 for `false` and 1 for `true`, as a comparison in WebAssembly leaves it. So JavaScript passes a `Bool` as `true`, `false`, 0 or 1, and receives it as the number 0 or 1, which `Boolean(...)` turns into a boolean. Comparisons of `Int` values are signed `i64` instructions, and comparisons of `Bool` values with `==` and `not=` are `i32.eq` and `i32.ne`. `and`, `or` and `not` are `i32.and`, `i32.or` and `i32.eqz`, which are correct because a `Bool` is always 0 or 1.

`Str` is two 32-bit integers (`i32`): the address of its first byte in memory, and its length in bytes. The bytes are UTF-8, so a caller decodes them as that, as `TextDecoder` does by default. So a parameter of type `Str` is two WebAssembly parameters, one after the other, and a result of type `Str` is two WebAssembly results, which JavaScript receives as an array `[address, length]`. The bytes of all the string literals of the program are copied to the start of the memory of the module when it is instantiated, and the module exports the memory as `memory`, so that a caller can read a string:

```js
const [address, length] = instance.exports.get_name();
new TextDecoder().decode(new Uint8Array(instance.exports.memory.buffer, address, length));
```

A `C_Str` is one 32-bit integer (`i32`): the address in the memory of the module of bytes that end with a zero. So a parameter of that type is one WebAssembly parameter, and a caller from JavaScript reads the bytes from the address up to the first zero. A zero is written after every string literal in the data section. `str_to_c_str` is only accepted with a string literal as its argument, and it is then only the address of the literal, `i32.const`, without the length and without a copy. Nothing else is a `C_Str`, so a `C_Str` cannot be given a value as an element of an array, a field of a struct or a global variable.

A global variable is a mutable global of the module, or two when it is a `Str`, which are the address and the length of the string, in the order of the definitions in the source. They are not exported. The initial value is the constant expression of the global, which WebAssembly evaluates once when the module is instantiated, which is why it is a literal. The bytes of a string literal that is the value of a global variable are in the data section, as the bytes of the other string literals are. A use of a global variable is a `global.get` for each of its globals, and an assignment such as `x += 2` pushes it with `global.get`, then the value, adds them and stores the result with `global.set`. So a global variable keeps its value from one call of the exported functions to the next, for as long as the instance lives.

A foreign procedure is an imported function, which the module gets from the object of imports when it is instantiated, see [foreign procedures](#foreign-procedures-in-webassembly). The imported functions come before the other functions of the module, because WebAssembly numbers them first, so a foreign procedure has the index of its place among the foreign procedures, and any other procedure has the number of foreign procedures plus its place among the others. That index is what the `call` instruction has, so it is not the place of the constant in the source. The module has an import section, with one function for each foreign procedure, whose type is the signature of the declaration, and the type section lists the types of the imports before the ones of the other functions. A foreign procedure has no export and no code of its own, and a program that has no foreign procedure has no import section.

The variables of a procedure are locals of its function, after the parameters, and a `Str` variable is two locals, like a `Str` parameter. The exception is a variable whose address is taken, which is in the frame, see below, and has no locals. The variables of the blocks of if statements and for statements are locals of the function too, each one a local of its own, after the ones above it, also when two variables have the same name in different blocks. The definition of a variable stores the initial value in them, and an assignment such as `x += 2` pushes the value of the variable, then the value, adds them and stores the result in the local of the variable. The assignment `x /= y` traps in the same cases as the division `x / y`.

An array is stored in the memory of the module, on a stack that grows down from the end of the memory, and not in locals, because an array slice refers to elements by their address. A procedure that has array literals, or variables whose address is taken, makes room for the elements of all of them when it starts, by moving a stack pointer, which is a mutable global of the module that comes after the globals of the program, and moves it back before it returns. The room is the frame of the procedure. Every value of an element takes 8 bytes in memory, also a 32-bit one, so an element takes 8 bytes for each value it is made of, see below for structs. A variable of an array type is a local that holds the address of its first element, and its length is part of the program: the code that needs it, such as a bounds check, has it as a constant. An array literal stores each of its elements at its place with `i64.store` or `i32.store`, and leaves the address of the first one, which is the stack pointer of the procedure plus the offset of the literal. The stack is 1 MiB. A call that would need more traps, by comparing the new stack pointer with the end of the stack. After a trap the stack pointer is not moved back, since nothing runs to do it, so an instance that has trapped has less stack left, and a program should make a new one.

An array slice is two 32-bit integers (`i32`), like a `Str`: the address of its first element and its number of elements, and a parameter of that type is two WebAssembly parameters. A caller from JavaScript writes the elements, which are `BigInt64Array` elements for `Int` values, to the memory that the module exports as `memory`, and passes the address and the length:

```js
new BigInt64Array(memory.buffer, 0, 3).set([10n, 20n, 30n]);
sum(0, 3);
```

An array is made into a slice where one is wanted by writing its length after its address, which is `i32.const` and the length. A subscript traps unless the index is below the length, which is one unsigned 64-bit comparison, so a negative index and one that does not fit in 32 bits trap as well. Then the address of the element is the address of the first plus the index times 8, and the element is loaded with `i64.load` or stored with `i64.store`. A slice with two bounds traps unless the low bound is not above the high bound, and the high bound is not above the length, and its address is moved up by the low bound, and its length is the difference between the bounds. The assignment to a range of elements is a `memory.copy`, after a comparison of the two lengths, so the module needs an engine that supports bulk memory operations, which all current browsers and Node.js do. The code of subscripts, slices and assignments to elements keeps values between instructions in locals after the variables of the procedure, which a procedure that has any of them gets: two 64-bit locals and three 32-bit ones, the last of which holds the address of the frame.

A for statement that goes through the elements of an array or a slice is the same three structured instructions as one with a range, with two 32-bit locals after the iterator: the address of the next element and the address after the last one. Before the loop the end is stored, which is the address plus the length times 8. The loop begins with a test that leaves the `block` with `br_if` when the next address is not below the end. The iterator is loaded from the next address, and the address moves up by 8 before the statements of the block, so what comes after the second `block`, where `continue` goes, is only the `br` back to the start of the `loop`.

A struct is made of the values of its fields, one after the other, in the order of its definition, where a field that is a struct takes its own fields in its place, and nothing else: it has no header and no padding. So a `Person` is three values, the `i64` of the age and the `i32` address and `i32` length of the name. A parameter of a struct type is that many parameters, a variable is that many locals, and a procedure that returns a struct has that many results, which JavaScript receives as an array, in the order of the fields. Reading a field of a variable or a parameter is a `local.get` for each of its values, with no cost for the rest of the struct, and assigning to a field is a `local.set`; a struct literal pushes the values of the fields in the order of the struct, with the default value, which is zero, for every field when its braces are empty. The values of a struct literal are written in the order of the fields of the struct, so a string in a literal can have its address in the data section before one that comes before it in the source, which the address of a string allows, since it is the length of the strings before it in the source. A struct in an array is in memory, with 8 bytes for each of its values, so `Person` takes 24 bytes there. The field of an element is read with a load at the address of the element plus 8 times the number of values before the field, and is assigned with a store there. An assignment of a whole struct to an element is a store of each of its values, with the last on the stack stored first, using a local to hold each value while the address is pushed below it. A default value is `i64.const 0` or `i32.const 0` for each value. A struct is not a function: the definition of one is a constant that has no code and no export.

A `Byte` is one 32-bit integer (`i32`) whose value is from 0 to 255, so a parameter of the type is one WebAssembly parameter and a result is one result, which JavaScript passes and receives as a plain number and not as a `BigInt`. An integer literal that is a `Byte` is `i32.const`. Bytes are compared with the unsigned instructions `i32.lt_u`, `i32.le_u`, `i32.gt_u` and `i32.ge_u`, and with `i32.eq` and `i32.ne`. `byte_to_int` is `i64.extend_i32_u`. `int_to_byte` keeps the number in a local, compares it with 255 as an unsigned 64-bit number with `i64.gt_u`, which also finds a negative number, traps with `unreachable` when it is above, and otherwise is `i32.wrap_i64` of the number. The default value is `i32.const 0`. The elements of an array of `Byte` and what a pointer to a `Byte` points to take one byte of memory, not 8 as the others do: an element is read with `i32.load8_u` and written with `i32.store8`, the address of an element is the address of the array plus the index with no scaling, and a `for` statement moves its address up by 1. So a `Uint8Array` of the memory of the module is the array: `new Uint8Array(memory.buffer, 0, 3).set([1, 2, 3])` and then `sum_of_bytes(0, 3)`. The elements of the array literals in the frame of a procedure, and its variables, are each rounded up to a multiple of 8 bytes, so that what comes after them is aligned. A `Byte` that is a field of a struct is one value of the struct, and in memory it takes the 8 bytes of any value, from which `i32.load8_u` and `i32.store8` use the first.

A pointer is one 32-bit integer (`i32`): the address in the memory of the module of what it points to, so a parameter that is a pointer is one WebAssembly parameter, and a result that is a pointer is one result, which JavaScript passes and receives as a number. `nil` is `i32.const 0`, which is also what a pointer that is not given a value is, and is not the address of anything that a program can point to. Two pointers or a pointer and `nil` are compared with `i32.eq` and `i32.ne`, as two `Bool` values are. What a pointer points to is in memory like an element of an array: 8 bytes for each of its values, aligned, so a `Str` takes 16 bytes and a struct takes 8 for each of its values, and the first four bytes of the 8 hold a 32-bit value. A field that is a pointer is one value of the struct, and so is an element of an array that is a pointer. A caller from JavaScript can point to what it wrote to the memory of the module, as it does for the elements of a slice:

```js
new BigInt64Array(memory.buffer)[1] = 41n;
increment(8);
```

A variable whose address is taken, as in `%x` or `%x.field`, is not in locals, because a local has no address, but in the frame of its procedure, after the elements of its array literals: each variable has a place of its own, also when it is in a block, in the order of the source, and takes 8 bytes for each of its values. The analysis found out which variables these are, see `Semantic_Binding`. The definition of such a variable stores its initial value in its place, with the last value on the stack stored first, using a local to hold each value while the address is pushed below it. A use loads the values from there, an assignment stores to there, and `%x` is the frame plus the offset of the variable, so that the variable and the pointer are the same memory. The address of a field is that plus 8 times the number of values before the field, and the address of an element is the address that a subscript computes, with the same trap for an index outside of the array. The address of a variable is good until its procedure returns and the frame is given back, after which a pointer to it points to the stack of whoever is called next.

`p^` follows a pointer by holding it in a local, trapping if it is 0, and loading the values of what it points to from it, one `i64.load` or `i32.load` for each, with the offset of the field in front of it for `p.age`, which is how a field of a pointer to a struct is read, written with or without the `^`. The pointer that is followed is the one before the last dot that is after a pointer, so `node.next.value` loads the pointer `node.next` first and then the field `value` from what it points to. The trap is an `if` with `unreachable` after a test with `i32.eqz`, which makes the call from JavaScript throw a `WebAssembly.RuntimeError`, and is needed because address 0 is where the strings start, and a null pointer would read them instead of stopping the program. An address that is outside of the memory traps as well, which is what WebAssembly does for any load or store. An assignment through a pointer evaluates the pointer first, checks it and keeps it in a local while the value is evaluated, like the address of an element is kept, and then stores to it. The operators such as `p^ += 1` load from the pointer, apply the operator and store the result to the same address. Nothing else is done for a pointer: it is copied by copying the number, so a procedure gets a pointer to the same memory as its caller.

A pointer to a procedure is one 32-bit integer (`i32`) as well, but it is not an address in the memory: it is the index of the procedure in the table of the module. The table has a slot for every function of the module, the imported ones too, put there by an element segment in the order of the function indexes, from slot 1 on. Slot 0 has no function, so the number 0, which is `nil` and what a pointer that was not given a value is, is the pointer that can be called to no effect but a trap. So the pointer to a procedure is its function index plus one, and the name of a procedure that is not called is `i32.const` of that. A call through a pointer is the arguments, then the pointer, and `call_indirect` with the type of the procedures that the pointer can point to and table 0. WebAssembly checks that the function in the slot has that type, and it traps, which makes the call from JavaScript throw a `WebAssembly.RuntimeError`, when the slot is empty, as it is for `nil`. The type is the type of the function of the first procedure that has the parameters and result of the pointer, and when there is none that comes first, a type of its own that the type section has after the types of the functions. The module has a table and an element segment only when the program uses a pointer to a procedure, which is when it has such a type or uses a procedure by its name without calling it. A pointer to a procedure can be kept in memory like any pointer, so `^^proc(Int) Int` and the elements of an array of them take 8 bytes each. JavaScript has no way to get a pointer to a procedure from the module, but it can pass the number of a slot, which is the function index plus one.


An enum is one `Int` (`i64`), which is the number of the member, counted from 0 in the order of the definition. So a parameter of an enum type is one WebAssembly parameter and a result is one result, which JavaScript passes and receives as a `BigInt`, so `Color.GREEN` is `1n`. A member that is written `Color.GREEN` or `.GREEN` is `i64.const` of its number, which is worked out when the module is built, from the enum that the place of `.GREEN` wants. A constant that is a member is a function without parameters that returns the number, like a constant that is a string, and is exported under its name. Members are compared with `i64.eq` and `i64.ne`, and the default value of an enum type is `i64.const 0`. A field of an enum type is one value of a struct, and the elements of an array of enums and what a pointer to an enum points to take 8 bytes each, like those of any other type. An enum has no code of its own, as a struct has not, so it is not exported and is no function.


A module has a memory only when the program has a string literal, which includes the value of a global variable, an array type, which includes the array literals and the array slices, `type_info_of`, or a pointer: a pointer type, `%` or `^`. The strings are at the start of the memory, then the information of `type_info_of`, and the stack of the arrays is after them.

`type_info_of(Color).members` is an array in the data of the module, after the strings and at an address that is a multiple of 8, and the value of the expression is `i32.const` of that address, as any array is the address of its first element. Its elements are `Enum_Member_Type_Info` structs, which take three slots of 8 bytes each: the address and the length of the name, which are 32-bit numbers in the first two slots, and the number of the member, a 64-bit one, in the third. The bytes of the name of the enum and of the members come after the array. A global variable that is given the members is an `i32` global that has the address, and `type_info_of(Color).name` is the address and the length of the name, as for any `Str`. Every use of `type_info_of` has an array of its own, even for an enum that has one already. `length_of` of an array is the `i64.const` of its length, with the array itself written first, and the length of a slice is the second of its two values, extended to 64 bits, with the address dropped. A procedure cannot be called `memory`, whether the module has one or not, because that is the name of the export.

The target rejects a program that the parser accepts when:

- the type written after `enum` is not `Int`,
- `type_info_of` is asked about a type that is not an enum, or is not followed by `.name` or `.members` right away, or is the initial value of a global variable with anything more than one of them after it,
- `length_of` has other than one argument, or an argument that is not an array or an array slice,
- `byte_to_int` has other than one argument, or an argument that is not a `Byte`, and `int_to_byte` has other than one argument, or an argument that is not an `Int`,
- a type, also the one of a global variable, is not `Int`, `Bool`, `Byte`, `Str`, a struct or an enum that is defined, a pointer to one of them, or an array or an array slice of one of them, or a struct has a field of such a type, or the name of the struct in a struct literal is not one that is defined,
- a struct contains itself, or has a field that is an array or an array slice,
- a field of a struct literal is not a field of the struct, or is given twice, or its value is not of the type of the field, or the literal gives a value to some fields and not to all of them,
- a field is accessed on a value that is not a struct or a pointer to one, or does not exist, or is accessed through something that is not a variable, a parameter, an element of an array or a pointer,
- a field is assigned through a parameter or an iterator that is not a pointer,
- a value that is assigned with `=` is not of the type of the variable, the field, the element or what the pointer points to,
- `^` follows a value that is not a pointer,
- the operand of `%` has no address: it is a value that is computed, a parameter, the iterator of a for statement, a global variable, an array or an array slice, or a variable of an array or array slice type,
- the type of a parameter is an array, or the type of a global variable is an array, an array slice or a pointer, which can be given no literal for its initial value,
- an array literal has another number of elements than the length of its type, or a variable of an array type with a length has another number than that,
- a variable of an array type is not given an array literal,
- an element of an array literal, or the value that is assigned to an element, is not of the type of the elements,
- what is subscripted, sliced or gone through by a for statement, or what is copied to a range of elements, is not an array or an array slice, or an index or a bound is not an `Int`,
- a name in an expression, or the initial value of a variable, is a struct, which is a type and not a value,
- a call is of a variable, a parameter or a global variable that does not hold a pointer to a procedure,
- `^` follows a pointer to a procedure, or `%` is given the name of a procedure, which has no address,
- a type in the parameters or the return type of a pointer to a procedure is not one that the target has,
- the initial value of a global variable is not of the type that is written for the variable,
- a definition is called `memory`,
- a procedure with no return type has a `return` statement,
- the value that a procedure returns is not of its return type,
- the initial value of a variable is not of the type that is written for the variable,
- a call has a different number of arguments than the procedure has parameters, or an argument is not of the type of its parameter,
- the call of a procedure without a return type is used as a value, and not as a statement,
- an operand of an arithmetic operator is not an `Int`,
- the operands of a comparison are not both `Int`, or not both `Bool` or both pointers or both members of one enum (or `nil`) for `==` and `not=`,
- the value of a variable that is defined without a type is `nil` or a member such as `.RED` that is written without its enum, which does not say the type of the variable,
- a member such as `.RED` is where no enum is known, or the enum has no such member, or it is the member of a name that is not an enum,
- the operands of a comparison are members of two enums, or members of an enum are compared with `<`, `<=`, `>` or `>=`,
- an operand of `and`, `or` or `not` is not a `Bool`,
- the condition of an if statement is not a `Bool`,
- the first number or the end of a range is not an `Int`,
- the variable or the value of an assignment such as `x += 2`, `x -= 2`, `x *= 2` or `x /= 2` is not an `Int`.

The target is built from the result of scope resolution, so it also rejects every program that scope resolution rejects.

An if statement becomes the structured instructions of WebAssembly: the condition, which leaves 1 or 0 (for a comparison, its two operands and the signed comparison of them), then `if`, the block, `else` and the next branch, up to `end`. An `else if` is written as an `else` with another `if` in it, so the statement ends with an `end` for each of its conditions. When the last statement of a procedure that returns a value is not a `return`, for example an if statement that returns in all of its branches, the body ends with the `unreachable` instruction, because WebAssembly cannot tell that the end of it cannot be reached. A body that gets there anyway traps.

A for statement with a range becomes a `block` that holds a `loop`, and the statements of its block are in a second `block` inside the `loop`. The iterator is a local, and the end of the range is a local right after it, which is where the end is read once. First the start and the end are stored, then the loop begins with a test that leaves the `block` with `br_if` when the iterator is not below the end, or above it for an inclusive range. After the statements of the block, which the second `block` ends, the iterator is increased by one and `br` goes back to the start of the `loop`. A `continue` is a `br` to the end of the second `block`, so the increase is not skipped, and a `break` is a `br` to the end of the first. A loop without a range is the same three instructions without the test and the increase, with a `br` back to the start of the `loop`. A scope statement becomes a `block`, and a `break` that leaves it is a `br` to the end of it. An if statement with a label is inside a `block` of its own, which a `break` with the label leaves, so the `block` ends after the last `end` of the statement. The number in a `br` is the number of structured instructions between it and its target, which counts the `if` instructions of the if statements that the jump is in, one for each scope or labeled if statement that it is in and has to leave on the way, and three for each loop, because a loop is a `block`, a `loop` and a `block`. A label of a loop or of a scope has no instruction of its own: it only tells which `block` the `br` goes to. For an inclusive range the loop leaves the `block` before the increase when the iterator is equal to the end, so an inclusive range that ends at the largest `Int` stops instead of wrapping around.

The `wat` target writes the same module in the WebAssembly text format, so that it can be read. It rejects the same programs as the `wasm` target.

There is a playground in `tests/wasm` that compiles Mialu in the browser, shows the parsed code as a tree and the module as text, and runs JavaScript against it, also the functions that the program imports with `import foreign js`, which are written in a section of their own. See [Playground](playground.md).


`mialuc --version` prints the version of the compiler, such as `mialuc 0.1.0`. The version is defined in `src/version.h`.


### x64 for Windows

The `x64-win32` target writes an executable file for Windows on x64 (a PE32+ file for the console), which needs nothing but Windows to run:

```
mialuc build --target x64-win32 -o ./printf-test.exe test-code/x64/foreign-functions-libc.mu
mialuc run --target x64-win32 test-code/x64/foreign-functions-libc.mu
```

The program starts at a few instructions of the compiler that call `main`, which has to be `main :: proc() Int ->`, and pass its result to the function `exit` of the C library, so that the output of the program is written out before it ends and its result is the exit code of the process. A foreign procedure of a `foreign libc` block is imported by name from `msvcrt.dll`, which is part of every installation of Windows, and a procedure of a `foreign js` block is an error. A call of a procedure follows the calling convention of Windows on x64, so that the C library can be called: the first four arguments are in `rcx`, `rdx`, `r8` and `r9`, the result is in `rax`, and the caller makes the 32 bytes of space for the callee, with the stack a multiple of 16 bytes.

`Int` is a 64-bit integer, and a `C_Str` is the 64-bit address of its bytes. A string literal is the address of its bytes in the read-only data of the program, which end with a zero. The target does not build much yet, and rejects the rest: procedures of up to four parameters of the types `Int` and `C_Str`, whose statements are calls and `return`, and whose expressions are integer literals, parameters, and calls whose arguments are such expressions or `str_to_c_str`. A string literal is only accepted as the argument of `str_to_c_str`. A `Str` has no value yet, so the argument of `str_to_c_str` must be a string literal.

`str_to_c_str` copies its argument and the zero after it to temporary storage, which is a section of 64 KiB of the program that is zeroed when it is loaded and not in the file. The first 8 bytes of it say how much of the rest is in use. A copy is made at the end of what is in use and takes its size, rounded up to 8 bytes, and a procedure that makes copies saves how much was in use when it started and sets it back when it returns, so its copies are given back. A copy that does not fit stops the program with a breakpoint, and so does a `str_to_c_str` that is returned, which the target rejects, because the copy is gone when the procedure returns.

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
