# Mialu Programming Language Specification

This document describes the Mialu programming language, its syntax, semantics and compilation targets.

Mialu is a procedural programming language with straight-foward and familiar syntax, built with simplicity and performance in mind. It is influenced by multiple languages including, but not limited to, C, C++, Go, Jai (currently unreleased), Odin and Python, fully embracing the fact that it comes after many of them, so it has learned from their mistakes and successes.

## Indentation

Indentation is made of spaces only. Tabs are not allowed in indentation. Each level of indentation is exactly 4 spaces, so the number of leading spaces on a line must be a multiple of 4. Blank lines, including lines of only spaces, are ignored.

## Keywords

Keywords are words that have a meaning of their own in the language, so they are reserved and cannot be used as identifiers. Mialu has the following keywords, in alphabetical order:

```
and
break
continue
else
false
for
if
in
not
or
proc
return
true
```

Keywords are case-sensitive, so `If` is an identifier and `if` is not. A longer identifier that only starts with a keyword, such as `returned` or `cannot`, is an identifier.

## Identifiers

Identifiers can contain alphanumeric characters and underscores. Case matters for identifiers in Mialu are case-sensitive. An identifier cannot begin with a digit. A [keyword](#keywords) cannot be used as an identifier.

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
| `Bool` | A boolean value, which is `true` or `false`. See [Booleans](#booleans). |
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

### Booleans

A `Bool` is either `true` or `false`, which are keywords:

```
ready : Bool = true
```

A comparison such as `x > 5` is an expression of type `Bool`, so it can be the value of a variable, a returned value or the condition of an if statement:

```
is_positive :: proc(n: Int) Bool ->
    return n > 0

is_even :: proc(n: Int) Bool ->
    return n / 2 * 2 == n
```

Unlike C, integer types are not implicitly converted into boolean values. A `Bool` is never converted to or from another type. An `Int` is not a condition, so `if n ->` is an error and has to be written `if n not= 0 ->`, and `true` is not `1`: `x : Int = true` and `b : Bool = 1` are errors.

A `Bool` has no arithmetic. Two `Bool` values can be compared with `==` and `not=`, so `a not= b` holds when exactly one of them is `true`, but they have no order, so `<`, `<=`, `>` and `>=` are defined for `Int` only. Both operands of a comparison must have the same type. The logical operators `and`, `or` and `not` combine `Bool` values, see [Logical operators](#logical-operators).

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

## Operators

Mialu supports the following operators. A higher precedence binds tighter:

| Operator | Description | Precedence | Example |
|----------|-------------|------------|---------|
| `or`     | Logical or, see [Logical operators](#logical-operators). | 1 | `a or b` |
| `and`    | Logical and. | 2 | `a and b` |
| `not`    | Logical not. It is written before its operand, and may be repeated. | 3 | `not a` |
| `==`     | Equal to. A comparison, see [Booleans](#booleans). | 4 | `x == 3` |
| `not=`   | Not equal to. A comparison. `not` and `=` are written together, without a space. | 4 | `x not= 3` |
| `<`      | Less than. A comparison. | 4 | `x < 3` |
| `<=`     | Less than or equal to. A comparison. | 4 | `x <= 3` |
| `>`      | Greater than. A comparison. | 4 | `x > 3` |
| `>=`     | Greater than or equal to. A comparison. | 4 | `x >= 3` |
| `+`      | Addition. | 5 | `x := 1 + 2` makes `x` 3 |
| `-`      | Subtraction. | 5 | `x := 5 - 1` makes `x` 4 |
| `*`      | Multiplication. | 6 | `x := 2 + 3 * 4` makes `x` 14 |
| `/`      | Division. The result is truncated toward zero. | 6 | `x := 20 - 12 / 4` makes `x` 17 |
| `+=`     | Addition assignment. It adds the value to a variable that is defined already. | 0 | `x += 2` adds 2 to `x` |
| `-=`     | Subtraction assignment. It subtracts the value from a variable. | 0 | `x -= 2` makes `x` 2 less |
| `*=`     | Multiplication assignment. It multiplies a variable by the value. | 0 | `x *= 3` triples `x` |
| `/=`     | Division assignment. It divides a variable by the value, truncating toward zero. | 0 | `x /= 2` halves `x` |
| `:`      | Type separator. It separates a name from its type. | 0 | `x : Int = 5` |
| `::`     | Defines a constant. It is two subsequent `:`. | 0 | `NAME :: "Bob"` |
| `:=`     | Defines a variable, deducing its type from the value. It is `:` followed by `=`, with the type left out. | 0 | `x := 4` |
| `=`      | Sets the initial value of a variable whose type is written. | 0 | `x : Int = 5` |

A binary operator is written between its two operands, such as `a + b`. The operands are integer literals, string literals, `true`, `false`, names or [calls](#calling-procedures), or the results of operators of a higher precedence. `not` is a unary operator, written before its operand. Addition, subtraction, multiplication and division are defined for `Int` only. A comparison takes two `Int` values, or two `Bool` values for `==` and `not=`, and its result is a `Bool`.

Operators of the same precedence are left associative, so `a + b + c` means `(a + b) + c`, and `a - b + c` means `(a - b) + c`. `+` and `-` have the same precedence, so `5 - 1 + 2` is 6 and not 2, and so do `*` and `/`, so `8 / 2 * 3` is 12 and not 1. A comparison binds looser than `+` and `-`, and tighter than `not`, `and` and `or`, so `a + 1 > b * 2` means `(a + 1) > (b * 2)`. A comparison is an expression of type `Bool`, but its operands are not comparisons, so it cannot be an operand of another operator, and `a < b < c` and `a == b == c` are errors. An operator of a higher precedence binds tighter, so `2 + 3 * 4` means `2 + (3 * 4)`, which is 14 and not 20, and `20 - 12 / 4` means `20 - (12 / 4)`, which is 17 and not 2.

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

## For statements

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

- the name must be that of a constant that is a procedure, so a call of a variable, of a parameter or of a constant string is an error,
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

## Loops without a range

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

## Break and continue

`break` leaves the loop that it is in, and the statement after the loop runs next. `continue` ends the current pass of the loop and starts the next one: in a loop without a range that is the first statement of the block again, and in a for statement with a range it is the next number of the range, or the end of the loop when there are no more. The iterator is stepped after a `continue` the same way as after a block that runs to its end, so an inclusive range that ends at the largest `Int` also ends. Both are statements of one word on a line of their own:

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

They work in the loops without a range and in the for statements with a range alike, and they can be in the blocks of if statements that are inside the loop. They belong to the innermost loop only, so a `break` in an inner loop does not leave the outer one:

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

A `break` or a `continue` that is not inside a loop is an error, and so is one in a procedure that has no loop, even if the procedure is only called from a loop. They are not expressions, so they have no value and nothing can follow them on the line. The statements that come after them in the same block are never run.

## Scope resolution

Every name that is used refers to something that is declared, and the compiler finds out what before the program is built for a target. This is called scope resolution. There are two scopes:

- The module scope has the name of every constant that is defined in the file, such as the procedures and the strings.
- The parameters and the variables of a procedure live in a local scope nested inside it. A variable is in scope below its definition only.
- Every block of an if statement or a for statement is a scope nested inside the one that it is written in. A variable that is defined in a block is in scope below its definition until the block ends, so the blocks inside it can use it, and the statements after the block cannot. The iterator of a for statement is in scope in the block of the statement, but not in its range.

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
- the name in a call is not a constant that is a procedure, which is also the case when it is a variable or a parameter,
- a `break` or a `continue` is not inside the block of a loop,
- the name that an assignment assigns to is not a variable, but a parameter or a constant, or the iterator of a for statement.

Scope resolution is about names only. Whether the value of a name has the right type is checked separately for the target, see below.

## Compilation targets

The compiler has two modes: `run` prints the tokens and the syntax tree of a program, after which it resolves the names, and `build --target <target> <output file> <source file>` writes a program for a target.

### WebAssembly

The `wasm` target writes a WebAssembly binary module. Every constant definition becomes a function that the module exports under the name of the constant. A constant that is a string becomes a function without parameters that returns the string, and a procedure that uses the constant by its name calls that function. A call of a procedure is the `call` instruction with the function of the procedure, after the instructions of the arguments, which are pushed in order. A `Str` argument or result is two values, as it is for the exports. A call that is a statement is followed by a `drop` for every value that the procedure returns, which is one, or two for a `Str`. `Int`, `Bool` and `Str` are the only types that the target supports.

`Int` is a 64-bit integer (`i64`), so JavaScript passes and receives it as a `BigInt`, and addition, subtraction and multiplication wrap around on overflow. Division truncates toward zero, so `(0 - 7) / 2` is -3. It traps, which makes the call from JavaScript throw a `WebAssembly.RuntimeError`, when the divisor is zero, or when the smallest `Int` is divided by -1, because the result does not fit.

`Bool` is a 32-bit integer (`i32`), which is 0 for `false` and 1 for `true`, as a comparison in WebAssembly leaves it. So JavaScript passes a `Bool` as `true`, `false`, 0 or 1, and receives it as the number 0 or 1, which `Boolean(...)` turns into a boolean. Comparisons of `Int` values are signed `i64` instructions, and comparisons of `Bool` values with `==` and `not=` are `i32.eq` and `i32.ne`. `and`, `or` and `not` are `i32.and`, `i32.or` and `i32.eqz`, which are correct because a `Bool` is always 0 or 1.

`Str` is two 32-bit integers (`i32`): the address of its first byte in memory, and its length in bytes. The bytes are UTF-8, so a caller decodes them as that, as `TextDecoder` does by default. So a parameter of type `Str` is two WebAssembly parameters, one after the other, and a result of type `Str` is two WebAssembly results, which JavaScript receives as an array `[address, length]`. The bytes of all the string literals of the program are copied to the start of the memory of the module when it is instantiated, and the module exports the memory as `memory`, so that a caller can read a string:

```js
const [address, length] = instance.exports.get_name();
new TextDecoder().decode(new Uint8Array(instance.exports.memory.buffer, address, length));
```

The variables of a procedure are locals of its function, after the parameters, and a `Str` variable is two locals, like a `Str` parameter. The variables of the blocks of if statements and for statements are locals of the function too, each one a local of its own, after the ones above it, also when two variables have the same name in different blocks. The definition of a variable stores the initial value in them, and an assignment such as `x += 2` pushes the value of the variable, then the value, adds them and stores the result in the local of the variable. The assignment `x /= y` traps in the same cases as the division `x / y`.

A module has a memory only when the program has a string literal. A procedure cannot be called `memory`, whether the program has a string literal or not, because that is the name of the export.

The target rejects a program that the parser accepts when:

- a type is not `Int`, `Bool` or `Str`,
- a name in an expression, or the initial value of a variable, is a procedure, which is not a value that the target can use yet,
- a definition is called `memory`,
- a procedure with no return type has a `return` statement,
- the value that a procedure returns is not of its return type,
- the initial value of a variable is not of the type that is written for the variable,
- a call has a different number of arguments than the procedure has parameters, or an argument is not of the type of its parameter,
- the call of a procedure without a return type is used as a value, and not as a statement,
- an operand of an arithmetic operator is not an `Int`,
- the operands of a comparison are not both `Int`, or not both `Bool` for `==` and `not=`,
- an operand of `and`, `or` or `not` is not a `Bool`,
- the condition of an if statement is not a `Bool`,
- the first number or the end of a range is not an `Int`,
- the variable or the value of an assignment such as `x += 2`, `x -= 2`, `x *= 2` or `x /= 2` is not an `Int`.

The target is built from the result of scope resolution, so it also rejects every program that scope resolution rejects.

An if statement becomes the structured instructions of WebAssembly: the condition, which leaves 1 or 0 (for a comparison, its two operands and the signed comparison of them), then `if`, the block, `else` and the next branch, up to `end`. An `else if` is written as an `else` with another `if` in it, so the statement ends with an `end` for each of its conditions. When the last statement of a procedure that returns a value is not a `return`, for example an if statement that returns in all of its branches, the body ends with the `unreachable` instruction, because WebAssembly cannot tell that the end of it cannot be reached. A body that gets there anyway traps.

A for statement becomes a `block` that holds a `loop`, and the statements of its block are in a second `block` inside the `loop`. The iterator is a local, and the end of the range is a local right after it, which is where the end is read once. First the start and the end are stored, then the loop begins with a test that leaves the `block` with `br_if` when the iterator is not below the end, or above it for an inclusive range. After the statements of the block, which the second `block` ends, the iterator is increased by one and `br` goes back to the start of the `loop`. A `continue` is a `br` to the end of the second `block`, so the increase is not skipped, and a `break` is a `br` to the end of the first. A loop without a range is the same three instructions without the test and the increase, with a `br` back to the start of the `loop`. The number in a `br` is the number of structured instructions between it and its target, which counts the `if` instructions of the if statements that the jump is in. For an inclusive range the loop leaves the `block` before the increase when the iterator is equal to the end, so an inclusive range that ends at the largest `Int` stops instead of wrapping around.

The `wat` target writes the same module in the WebAssembly text format, so that it can be read. It rejects the same programs as the `wasm` target.

There is a playground in `tests/wasm` that compiles Mialu in the browser, shows the parsed code as a tree and the module as text, and runs JavaScript against it. See the README.
