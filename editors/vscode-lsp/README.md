# Mialu Language Server for VS Code

A language server for Mialu, and the extension that connects VS Code to it. It is an extension of its own next to the [language extension](../vscode/README.md), which it needs for the language `mialu`.

It has three features. The first is the size of a struct: above the definition of each struct there is a line that says how much memory a value of it takes and how it is aligned.

```
Size: 24 bytes | Alignment: 8 bytes
Person :: struct ->
    age: Int
    initial: Byte
    is_member: Bool
```

The second is the type of a name: when the pointer rests on the name of a constant, a global variable, a parameter or a variable, at its definition or where it is used, a hover shows its type, such as `LIMIT: untyped Int`, `n: Int` or `max: proc(a: Int, b: Int) Int`. A constant that is a string that `++` joined when the program was compiled, such as `TITLE :: GREETING ++ ' ' ++ "world"`, shows the text it was joined to below its type, as `"Hello world"`, at its definition and where it is used. A constant that is a number, a truth value or a rune shows the value that the compiler worked out below its type, so that the length of `[BUFFER_SIZE]Byte` can be read from the constant: `BUFFER_SIZE: untyped Int` and `4096 (0x1000)`, where a number of 256 or more is also written in hexadecimal. It is what a constant that is worked out from others, such as `DOUBLE :: BUFFER_SIZE * 2`, is, at its definition and where it is used. The documentation comment of an item, the lines of `///` right above its definition, is its description: it is shown below the type, where the item is defined and where it is used, also for an item of an imported file such as `mu.file_open`. It is for a procedure, a constant, a global variable, a struct, an enum, a field and a member of an enum, and a struct and an enum, which are types, say `struct` or `enum Int` as their type. A blank line, or a line of `//` that is not documentation, ends the comment. The pointer can also rest on a member of an enum, where it is defined or used as `Color.RED` or `.RED`, which shows the enum as its type, such as `RED: Color`. The same goes for the fields of a struct, where they are defined, given a value in a struct literal, or read, such as `age: Int` for `person.age`. An array that is made with `[const]Int { 1, 2, 3 }` shows the length that the compiler counted, `a: [3]Int`, because the length is known when the program is compiled. A slice, a parameter `items: []Int` or a variable that is given a range such as `a[1:3]`, shows `[]Int` with no length, since that is known only when the program runs. The iterator of a for statement has the type of the elements of the array or slice that it goes over, such as `person: Person` in `for person in people`, and the hover adds what it is an element of, as highlighted code: `element of` and then `people: []Person`. The pointer can rest on a call of a built-in procedure too, such as `length_of`, which shows its signature, `length_of: proc(elements: [N]T | []T | Str) Int`, where a parameter that takes more than one type has them between `|`. The names that are given its values have the types that it gives: in `text, is_text := utf8_decode(bytes)` the name `text` shows `Str` and `is_text` shows `Bool`. A name that stores the result of a cast, such as `b := cast(Byte)a`, has the type that the cast names. The type is the one that the definition gives, otherwise the one of its value, where the compiler can tell it. A name whose type it cannot tell shows nothing. [`mialuc dump-types`](../../docs/mialu-cli.md#print-the-types-of-the-names) works it out.

The third is the outline of a file, which VS Code shows in the Outline view, in the breadcrumbs and for "Go to Symbol in Editor": the procedures, structs, enums, constants and global variables, with the fields of a struct and the members of an enum below it. [`mialuc dump-outline`](../../docs/mialu-cli.md#print-the-outline-of-a-file) works it out.

The fourth is diagnostics: a moment after a document is opened or changed, the server checks it with the compiler, and the error is marked in the code with a red squiggle and listed in the Problems tab, and it goes away when the text is fixed. The compiler stops at the first error, so there is one at a time. It is checked on the text in the editor, not the saved file.

The fifth is the value of a constant, shown inline after the `::` of its definition, or the second `:` of a typed one, as a hint of the editor: `VERSION_STRING :: ⇒ "0.3.0" int_to_str(VERSION_MAJOR) ++ '.' ++ ...` for a constant that the compiler worked out from others, and `SIZE :: ⇒ 4096 (0x1000) 64 * 64` for a number, which is also written in hexadecimal from 256 up. A constant that is written as its value, such as `LIMIT :: 100`, has no hint, since it would say the same. The server answers `textDocument/inlayHint` from the same `mialuc dump-types` as the hover, and the setting `mialu.lsp.constantValues` turns the hints off.

The server is `server.js`. It speaks the [Language Server Protocol](https://microsoft.github.io/language-server-protocol/) over the standard input and output, so another editor with a client for it can use it too: it answers `textDocument/codeLens`, `textDocument/hover` and `textDocument/documentSymbol`, publishes `textDocument/publishDiagnostics`, and keeps the text of the open documents from `textDocument/didOpen`, `didChange` (the whole text) and `didClose`.

The server does not know the layout of the structs itself, the compiler does: it runs [`mialuc dump-struct-sizes`](../../docs/mialu-cli.md#print-the-sizes-of-the-structs) on the text of the document, which is what is in the editor and not only what is saved. The sizes are those of the `x64-win32` target, where every field is made of words of 8 bytes and there is no padding, so a struct is always aligned to 8 bytes. While the text does not compile, such as in the middle of typing a line, the lenses of the last text that did are kept.

## Requirements

- mialuc, built with the version that has `dump-struct-sizes`, `dump-types` and `dump-outline`: `build/mialuc.exe` in the folder of the workspace when it is there, which is where the project of the compiler builds it, otherwise `mialuc` on the `PATH`, or the setting `mialu.lsp.compilerPath`.
- Nothing else: the extension and the server are plain JavaScript that run on the Node.js of VS Code, so nothing is built and no package is installed. To try the server alone, [Node.js](https://nodejs.org/) 18 or newer runs `node --test editors/vscode-lsp/server.test.js`.

## Install

VS Code only loads extensions from its extensions folder, so link this folder into it with the script that links the other extensions too:

```
powershell -File scripts/install_vscode_extension.ps1
```

Then run "Developer: Reload Window" in VS Code.

## Settings

- `mialu.lsp.structSizes`: show the sizes above the structs. On by default.
- `mialu.lsp.typeHover`: show the type of a name when the pointer rests on it. On by default.
- `mialu.lsp.compilerPath`: the path of mialuc, see above.
