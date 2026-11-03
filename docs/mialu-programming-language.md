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

Mialu supports the following operators:

| Operator | Description |Precedence|
|----------|-------------|----------|
| `+`      | Addition    |1         |

A binary operator is written between its two operands, such as `a + b`. The operands are integer literals, string literals or names. Addition is defined for `Int` only.

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

## Compilation targets

The compiler has two modes: `run` prints the tokens and the syntax tree of a program, and `build --target <target> <output file> <source file>` writes a program for a target.

### WebAssembly

The `wasm` target writes a WebAssembly binary module. Every constant definition becomes a function that the module exports under the name of the constant. A constant that is a string becomes a function without parameters that returns the string. `Int` and `Str` are the only types that the target supports.

`Int` is a 64-bit integer (`i64`), so JavaScript passes and receives it as a `BigInt`, and addition wraps around on overflow.

`Str` is two 32-bit integers (`i32`): the address of its first byte in memory, and its length in bytes. The bytes are UTF-8, so a caller decodes them as that, as `TextDecoder` does by default. So a parameter of type `Str` is two WebAssembly parameters, one after the other, and a result of type `Str` is two WebAssembly results, which JavaScript receives as an array `[address, length]`. The bytes of all the string literals of the program are copied to the start of the memory of the module when it is instantiated, and the module exports the memory as `memory`, so that a caller can read a string:

```js
const [address, length] = instance.exports.get_name();
new TextDecoder().decode(new Uint8Array(instance.exports.memory.buffer, address, length));
```

A module has a memory only when the program has a string literal. A procedure cannot be called `memory`, whether the program has a string literal or not, because that is the name of the export.

The target rejects a program that the parser accepts when:

- a type is not `Int` or `Str`,
- a name in an expression is not a parameter of the procedure,
- two parameters, or two definitions, have the same name,
- a definition is called `memory`,
- a procedure with no return type has a `return` statement,
- the value that a procedure returns is not of its return type,
- an operand of an addition is not an `Int`.

The `wat` target writes the same module in the WebAssembly text format, so that it can be read. It rejects the same programs as the `wasm` target.

There is a playground in `tests/wasm` that compiles Mialu in the browser, shows the module as text, and runs JavaScript against it. See the README.
