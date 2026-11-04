# Mialu for VS Code

Language support for Mialu, for now only syntax highlighting of the reserved words (`proc`, `return`, `if`, `else`, `for`, `in`, `break`, `const`, `continue`, `struct`, `true`, `false`, `and`, `or` and `not`), the operators (`+`, `-`, `*`, `/`, `+=`, `-=`, `*=`, `/=`, `==`, `not=`, `<`, `<=`, `>`, `>=`, `..<`, `..<=`, `->`, `:` and `=`), the brackets and braces of arrays and struct objects (`[]` and `{}`, as in `[const]Int { 1, 2 }`, `a[1:4]` and `Person { age = 30 }`, where the `:` is the colon of the two bounds of a slice), the dot of a field access such as `p.age`, labels such as `@outer` and the label in `break outer` or `continue outer`, attribute lists such as `#(static)`, whose `#`, parentheses and names have scopes of their own (`punctuation.definition.attribute.mialu` and `entity.other.attribute-name.mialu`), so that a theme can color them, strings and the names in calls such as `add(1, 2)`. The
extension is enabled for files that end in `.mu`.

The extension is declarative: it is a language definition and a TextMate grammar, so there is
nothing to build and nothing to install with a package manager.

## Install

VS Code only loads extensions from its extensions folder, so link this folder into it:

```
powershell -File scripts/install_vscode_extension.ps1
```

Then run "Developer: Reload Window" in VS Code. The link points at this folder, so later changes
to the grammar only need another reload. To remove the link:

```
powershell -File scripts/install_vscode_extension.ps1 -Uninstall
```

## Reserved words

The reserved words are listed in `syntaxes/mialu.tmLanguage.json`. They must be the same as the
keywords that the tokenizer recognizes (`TOKEN_TYPE_KEYWORD_*` in `src/tokenizer.h`), so update both
when a keyword is added. Today they are `proc`, `return`, `if`, `else`, `for`, `in`, `break`, `const`, `continue`, `struct`, `true`, `false`, `and`, `or` and `not`. `struct` has the scope `storage.type.struct.mialu`, and `const` is a modifier of the length of an array type, so it has its own scope, `storage.modifier.const.mialu`. The operator `not=` is not the keyword `not`, so the pattern of the keyword must not match the start of it.
