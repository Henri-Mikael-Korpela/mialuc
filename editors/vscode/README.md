# Mialu for VS Code

Language support for Mialu, for now only syntax highlighting of the keywords (`proc`, `return`, `if`, `else`, `for`, `in`, `break`, `const`, `continue`, `struct`, `foreign`, `import`, `and`, `or` and `not`) and the literal words (`true`, `false` and `nil`), which have a color of their own, the `...` of a foreign procedure, the operators (`+`, `-`, `*`, `/`, `+=`, `-=`, `*=`, `/=`, `==`, `not=`, `<`, `<=`, `>`, `>=`, `..<`, `..<=`, `->`, `:` and `=`), the `%` that takes an address, as in `%x`, and the `^` of a pointer type and of following a pointer, as in `^Int` and `p^` (scopes `keyword.operator.address.mialu` and `keyword.operator.pointer.mialu`), the brackets and braces of arrays and struct objects (`[]` and `{}`, as in `[const]Int { 1, 2 }`, `a[1:4]` and `Person { age = 30 }`, where the `:` is the colon of the two bounds of a slice), the dot of a field access such as `p.age`, labels such as `@outer` and the label in `break outer` or `continue outer`, attribute lists such as `#(static)`, whose `#`, parentheses and names have scopes of their own (`punctuation.definition.attribute.mialu` and `entity.other.attribute-name.mialu`), so that a theme can color them, strings and the names in calls such as `add(1, 2)`. The
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

## Colors

Themes give most of the operator scopes (`keyword.operator.*`) the color of plain text, so the operators would look as if they had no highlighting. `package.json` gives every operator scope of Mialu, and the arrow and the dot of a field access, a color of their own (`#56b6c2`, a teal) with `configurationDefaults`, next to the orange of the literal words. That covers `+`, `-`, `*`, `/`, `+=`, `-=`, `*=`, `/=`, `==`, `not=`, `<`, `<=`, `>`, `>=`, `..<`, `..<=`, `->`, `:`, `::`, `:=`, `=`, `%`, `^`, `.`, `...` and the word operators `and`, `or` and `not`. A new operator scope in the grammar has to be added to that list as well. The colors are defaults that a user can override in `editor.tokenColorCustomizations` in their settings. The colors of the parentheses, brackets and braces do not come from here: VS Code colors them itself, by how deeply they are nested, when bracket pair colorization is on.

## Reserved words

The reserved words are listed in `syntaxes/mialu.tmLanguage.json`. They must be the same as the
keywords and literal words that the tokenizer recognizes (`TOKEN_TYPE_KEYWORD_*` in
`src/tokenizer.h`), so update both when one is added. Today the keywords are `proc`, `return`, `if`, `else`, `for`, `in`, `break`, `const`, `continue`, `struct`, `foreign`, `import`, `and`, `or` and `not`, and the literal words are `true`, `false` and `nil`. `foreign` and `import` have the scope `keyword.control.import.mialu`, and the `...` of a foreign procedure has `keyword.operator.ellipsis.mialu`. `struct` has the scope `storage.type.struct.mialu`, and `const` is a modifier of the length of an array type, so it has its own scope, `storage.modifier.const.mialu`, and the literal words are not keywords: `true` and `false` have the scope `constant.language.boolean.mialu`, and `nil`, the null pointer, `constant.language.nil.mialu`. Most themes color those like keywords, so `package.json` gives both scopes a color of their own (`#d19a66`, an orange) with `configurationDefaults`, which a user can override in `editor.tokenColorCustomizations` in their settings. The operator `not=` is not the keyword `not`, so the pattern of the keyword must not match the start of it.
