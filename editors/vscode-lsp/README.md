# Mialu Language Server for VS Code

A language server for Mialu, and the extension that connects VS Code to it. It is an extension of its own next to the [language extension](../vscode/README.md), which it needs for the language `mialu`.

For now it has one feature, the size of a struct: above the definition of each struct there is a line that says how much memory a value of it takes and how it is aligned.

```
Size: 24 bytes | Alignment: 8 bytes
Person :: struct ->
    age: Int
    initial: Byte
    is_member: Bool
```

The server is `server.js`. It speaks the [Language Server Protocol](https://microsoft.github.io/language-server-protocol/) over the standard input and output, so another editor with a client for it can use it too: it answers `textDocument/codeLens`, and keeps the text of the open documents from `textDocument/didOpen`, `didChange` (the whole text) and `didClose`.

The server does not know the layout of the structs itself, the compiler does: it runs [`mialuc dump-struct-sizes`](../../docs/mialu-cli.md#print-the-sizes-of-the-structs) on the text of the document, which is what is in the editor and not only what is saved. The sizes are those of the `x64-win32` target, where every field is made of words of 8 bytes and there is no padding, so a struct is always aligned to 8 bytes. While the text does not compile, such as in the middle of typing a line, the lenses of the last text that did are kept.

## Requirements

- mialuc, built with the version that has `dump-struct-sizes`: `build/mialuc.exe` in the folder of the workspace when it is there, which is where the project of the compiler builds it, otherwise `mialuc` on the `PATH`, or the setting `mialu.lsp.compilerPath`.
- Nothing else: the extension and the server are plain JavaScript that run on the Node.js of VS Code, so nothing is built and no package is installed. To try the server alone, [Node.js](https://nodejs.org/) 18 or newer runs `node --test editors/vscode-lsp/server.test.js`.

## Install

VS Code only loads extensions from its extensions folder, so link this folder into it with the script that links the other extensions too:

```
powershell -File scripts/install_vscode_extension.ps1
```

Then run "Developer: Reload Window" in VS Code.

## Settings

- `mialu.lsp.structSizes`: show the sizes above the structs. On by default.
- `mialu.lsp.compilerPath`: the path of mialuc, see above.
