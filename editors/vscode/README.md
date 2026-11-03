# Mialu for VS Code

Language support for Mialu, for now only syntax highlighting of the reserved words, the operators (`+`, `-`, `*`, `/`, `+=`, `*=`, `/=`, `:` and `=`) and strings. The
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
when a keyword is added. Today they are `proc` and `return`.
