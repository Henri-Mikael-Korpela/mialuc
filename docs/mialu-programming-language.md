# Mialu Programming Language Specification

This document describes the Mialu programming language, its syntax, semantics and compilation targets.

Mialu is a procedural programming language with straight-foward and familiar syntax, built with simplicity and performance in mind. It is influenced by multiple languages including, but not limited to, C, C++, Go, Jai (currently unreleased), Odin and Python, fully embracing the fact that it comes after many of them, so it has learned from their mistakes and successes.

## Indentation

Indentation is made of spaces only. Tabs are not allowed in indentation. Each level of indentation is exactly 4 spaces, so the number of leading spaces on a line must be a multiple of 4. Blank lines, including lines of only spaces, are ignored.

## Identifiers

Identifiers can contain alphanumeric characters and underscores. Case matters for identifiers in Mialu are case-sensitive. An identifier cannot begin with a digit.

## Constants

Constants are immutable values. They can be defined by placing two subsequent colons after an identifier:

```
CONSTANT_NAME :: value
```

Constants are commonly used to define procedures:

```
sum :: proc(a: Int, b: Int) Int ->
    return a + b
```

A constant can also be a string, which is written on its own line:

```
greeting :: "Hello, world"
```

## Data types

Every value has a type, which is written as an identifier. Mialu has the following types:

| Type  | Description                                                  |
|-------|--------------------------------------------------------------|
| `Int` | A signed 64-bit integer. Written as a decimal number, such as `42`. |
| `Str` | A string, which is text encoded in UTF-8. Written in quotes, such as `"Bob"`. |

### Strings

A string literal is the text between a pair of double quotes, on one line:

```
"Hello, world"
```

A `Str` is encoded in UTF-8: its bytes are always a valid UTF-8 encoding of text, as defined by [RFC 3629](https://www.rfc-editor.org/rfc/rfc3629). A string can hold any Unicode character, such as `"héllo"` or `"日本語"`, and each character takes one to four bytes.

A string is length-based, not null-terminated. It is its bytes together with the number of them, so the length is known without looking for an end, and a string can hold a zero byte. The length counts bytes, not characters: `"héllo"` has a length of 6, because `é` takes two bytes. The empty string `""` has a length of zero. A string is immutable, and does not own its bytes: a string literal is a view of bytes that are part of the program, so returning one from a procedure is safe.

The compiler rejects a string literal that is not valid UTF-8. A string cannot contain a double quote or a line break, because they would end it, and cannot contain a backslash. There are no escape sequences.

## Operators

Mialu supports the following operators. A higher precedence binds tighter:

| Operator | Description | Precedence | Example |
|----------|-------------|------------|---------|
| `==`     | Equal to. A comparison, see [If statements](#if-statements). | 1 | `x == 3` |
| `!=`     | Not equal to. A comparison. | 1 | `x != 3` |
| `<`      | Less than. A comparison. | 1 | `x < 3` |
| `<=`     | Less than or equal to. A comparison. | 1 | `x <= 3` |
| `>`      | Greater than. A comparison. | 1 | `x > 3` |
| `>=`     | Greater than or equal to. A comparison. | 1 | `x >= 3` |
| `+`      | Addition. | 2 | `x := 1 + 2` makes `x` 3 |
| `-`      | Subtraction. | 2 | `x := 5 - 1` makes `x` 4 |
| `*`      | Multiplication. | 3 | `x := 2 + 3 * 4` makes `x` 14 |
| `/`      | Division. The result is truncated toward zero. | 3 | `x := 20 - 12 / 4` makes `x` 17 |
| `+=`     | Addition assignment. It adds the value to a variable that is defined already. | 0 | `x += 2` adds 2 to `x` |
| `-=`     | Subtraction assignment. It subtracts the value from a variable. | 0 | `x -= 2` makes `x` 2 less |
| `*=`     | Multiplication assignment. It multiplies a variable by the value. | 0 | `x *= 3` triples `x` |
| `/=`     | Division assignment. It divides a variable by the value, truncating toward zero. | 0 | `x /= 2` halves `x` |
| `:`      | Type separator. It separates a name from its type. | 0 | `x : Int = 5` |
| `::`     | Defines a constant. It is two subsequent `:`. | 0 | `NAME :: "Bob"` |
| `:=`     | Defines a variable, deducing its type from the value. It is `:` followed by `=`, with the type left out. | 0 | `x := 4` |
| `=`      | Sets the initial value of a variable whose type is written. | 0 | `x : Int = 5` |

A binary operator is written between its two operands, such as `a + b`. The operands are integer literals, string literals or names. Addition, subtraction, multiplication and division are defined for `Int` only, and so are the comparisons.

Operators of the same precedence are left associative, so `a + b + c` means `(a + b) + c`, and `a - b + c` means `(a - b) + c`. `+` and `-` have the same precedence, so `5 - 1 + 2` is 6 and not 2, and so do `*` and `/`, so `8 / 2 * 3` is 12 and not 1. A comparison has the lowest precedence of them, so `a + 1 > b * 2` means `(a + 1) > (b * 2)`. A comparison is not an expression that has a value, it is only the condition of an if statement, so it cannot be an operand, a returned value or the value of a variable, and `a < b < c` is not a comparison. An operator of a higher precedence binds tighter, so `2 + 3 * 4` means `2 + (3 * 4)`, which is 14 and not 20, and `20 - 12 / 4` means `20 - (12 / 4)`, which is 17 and not 2.

The operators of precedence 0 define names or assign to them, and are not part of an expression. Each one is written after the name that it defines or assigns to, and takes everything up to the end of the line as its right-hand side, so `x := a + b` means `x := (a + b)`, and `x += a * b` means `x += (a * b)`. Spaces may be written between the characters of `::` and `:=`, because they are separate tokens. An assignment operator such as `+=`, `-=`, `*=` or `/=` is one token, so no space may be written between its characters.

See [Variables](#variables) for variable definitions and [Constants](#constants) for constants.

## Variables

A variable is a name for a value that is local to a procedure. It is defined by a name followed by `:=` and its initial value, which is an expression:

```
x := 4
```

The type of the variable is deduced from the initial value. It is implicit. `x` above is of type `Int` because literal `4` is.

The type can also be written explicitly, between the `:` and the `=`:

```
x : Int = 5
```

A variable is defined once, with its initial value, and every variable has one. It can be used in the statements below its definition, but not above it and not in its own initial value. The variables of a procedure are not visible outside of it, so two procedures can have a variable of the same name.

## Assignment

An assignment changes the value of a variable that is defined already. It is a statement that has the name of the variable, an assignment operator and a value, which is an expression. `+=` is the addition assignment: it adds the value to the variable and stores the sum in the variable, so `x += 2` leaves `x` 2 greater than it was:

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

`foo` returns 5. Dividing by zero is an error at run time, see [WebAssembly](#webassembly). The value is evaluated first, with the value that the variable has before the assignment, so `x += x` doubles `x`, `x += 2 * 3` adds 6, `x -= 1 + 1` subtracts 2, `x *= 1 + 2` multiplies `x` by 3, and `x /= 1 + 1` divides `x` by 2. The name must be a variable of the procedure, defined above the assignment: a parameter or a constant cannot be assigned to. The variable and the value must be an `Int`, because addition, subtraction, multiplication and division are defined for `Int` only.

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

A procedure body consists of a series of statements, which are variable definitions, assignments, [if statements](#if-statements) and `return` statements for now. The procedure body is indented to indicate its scope.

## If statements

An if statement runs a block of statements only when a condition holds. It is written like in C, but the condition has no parentheses. The `->` after the condition starts the block, which is the lines that are indented one level deeper than the `if`, so an indented block takes the place of the curly braces:

```
foo :: proc() Int ->
    x := 4
    if x > 5 ->
        return 1
    return 2
```

Here `foo` returns 2, because `x > 5` does not hold, so the block of the `if` is skipped and the statements after it run.

A condition is a comparison: two expressions, which are `Int` values, with one of the comparison operators `==`, `!=`, `<`, `<=`, `>` and `>=` between them. The expressions can use everything that an expression can, so `if x + 1 > y * 2 ->` is a condition. There are no values for true and false yet, so a condition cannot be a name or a number on its own, as in C, and the comparison cannot be stored in a variable.

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

## Scope resolution

Every name that is used refers to something that is declared, and the compiler finds out what before the program is built for a target. This is called scope resolution. There are two scopes:

- The module scope has the name of every constant that is defined in the file, such as the procedures and the strings.
- The parameters and the variables of a procedure live in a local scope nested inside it. A variable is in scope below its definition only.
- Every block of an if statement is a scope nested inside the one that it is written in. A variable that is defined in a block is in scope below its definition until the block ends, so the blocks inside it can use it, and the statements after the block cannot.

A name is looked up in the local scope of the procedure that uses it first, and then in the module scope. So a procedure can use a constant that is defined outside of it:

```
NAME :: "Bob"

get_name :: proc() Str ->
    return NAME
```

The order of the definitions does not matter, so a procedure can use a constant that is defined below it.

There is no shadowing of names so a name cannot be declared twice in the same scope. A name cannot be declared again in a nested scope either, where the first declaration is visible. A name has one meaning wherever it can be used. The compiler rejects a program when:

- two constants have the same name,
- two parameters of a procedure have the same name,
- a parameter has the name of a constant, including the constant that is the procedure itself,
- a variable has the name of a constant, of a parameter, or of a variable that is in scope where it is defined, so a block cannot define a variable that the blocks around it have, but two blocks that are not inside each other can each define a variable of the same name,
- a name in an expression is neither a variable that is in scope there, nor a parameter of the procedure, nor a constant,
- the name that an assignment assigns to is not a variable, but a parameter or a constant.

Scope resolution is about names only. Whether the value of a name has the right type is checked separately for the target, see below.

## Compilation targets

The compiler has two modes: `run` prints the tokens and the syntax tree of a program, after which it resolves the names, and `build --target <target> <output file> <source file>` writes a program for a target.

### WebAssembly

The `wasm` target writes a WebAssembly binary module. Every constant definition becomes a function that the module exports under the name of the constant. A constant that is a string becomes a function without parameters that returns the string, and a procedure that uses the constant by its name calls that function. `Int` and `Str` are the only types that the target supports.

`Int` is a 64-bit integer (`i64`), so JavaScript passes and receives it as a `BigInt`, and addition, subtraction and multiplication wrap around on overflow. Division truncates toward zero, so `(0 - 7) / 2` is -3. It traps, which makes the call from JavaScript throw a `WebAssembly.RuntimeError`, when the divisor is zero, or when the smallest `Int` is divided by -1, because the result does not fit.

`Str` is two 32-bit integers (`i32`): the address of its first byte in memory, and its length in bytes. The bytes are UTF-8, so a caller decodes them as that, as `TextDecoder` does by default. So a parameter of type `Str` is two WebAssembly parameters, one after the other, and a result of type `Str` is two WebAssembly results, which JavaScript receives as an array `[address, length]`. The bytes of all the string literals of the program are copied to the start of the memory of the module when it is instantiated, and the module exports the memory as `memory`, so that a caller can read a string:

```js
const [address, length] = instance.exports.get_name();
new TextDecoder().decode(new Uint8Array(instance.exports.memory.buffer, address, length));
```

The variables of a procedure are locals of its function, after the parameters, and a `Str` variable is two locals, like a `Str` parameter. The variables of the blocks of if statements are locals of the function too, each one a local of its own, after the ones above it, also when two variables have the same name in different blocks. The definition of a variable stores the initial value in them, and an assignment such as `x += 2` pushes the value of the variable, then the value, adds them and stores the result in the local of the variable. The assignment `x /= y` traps in the same cases as the division `x / y`.

A module has a memory only when the program has a string literal. A procedure cannot be called `memory`, whether the program has a string literal or not, because that is the name of the export.

The target rejects a program that the parser accepts when:

- a type is not `Int` or `Str`,
- a name in an expression, or the initial value of a variable, is a procedure, which is not a value that the target can use yet,
- a definition is called `memory`,
- a procedure with no return type has a `return` statement,
- the value that a procedure returns is not of its return type,
- the initial value of a variable is not of the type that is written for the variable,
- an operand of an arithmetic operator or of a comparison is not an `Int`,
- the variable or the value of an assignment such as `x += 2`, `x -= 2`, `x *= 2` or `x /= 2` is not an `Int`.

The target is built from the result of scope resolution, so it also rejects every program that scope resolution rejects.

An if statement becomes the structured instructions of WebAssembly: the two operands of the condition, the signed comparison of `i64` values, which leaves 1 or 0, then `if`, the block, `else` and the next branch, up to `end`. An `else if` is written as an `else` with another `if` in it, so the statement ends with an `end` for each of its conditions. When the last statement of a procedure that returns a value is not a `return`, for example an if statement that returns in all of its branches, the body ends with the `unreachable` instruction, because WebAssembly cannot tell that the end of it cannot be reached. A body that gets there anyway traps.

The `wat` target writes the same module in the WebAssembly text format, so that it can be read. It rejects the same programs as the `wasm` target.

There is a playground in `tests/wasm` that compiles Mialu in the browser, shows the parsed code as a tree and the module as text, and runs JavaScript against it. See the README.
