# Mialu Programming Language Specification

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

## Operators

Mialu supports the following operators:

| Operator | Description |Precedence|
|----------|-------------|----------|
| `+`      | Addition    |1         |

A binary operator is written between its two operands, such as `a + b`. The operands are integer literals or names.

Operators of the same precedence are left associative, so `a + b + c` means `(a + b) + c`.

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

A procedure body consists of a series of statements, which can include variable declarations, expressions, and control flow statements. The procedure body is indented to indicate its scope.