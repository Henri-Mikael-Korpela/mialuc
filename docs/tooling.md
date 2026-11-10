# Tooling

Scripts that help to work on the language. They use only the Python standard library, so there is nothing to install besides [Python](https://www.python.org/downloads/) 3. See [Getting Started](getting-started.md) for the versions that have been tested.

## Grammar

[grammar.ebnf](../grammar.ebnf) describes what the compiler accepts, in EBNF that works on characters. To check that a source file matches it:

```
python scripts/verify_grammar.py test-code/wasm/main-function.mu
```

The script uses only the Python standard library, so there is nothing to install. It prints `OK`, or the line and column where the file stops matching and what the grammar expected there. It exits with 0 when every file matches, 1 when a file does not, and 2 when the grammar or the arguments are wrong.

## Language specification

The table of contents of the [language specification](mialu-programming-language.md) is generated from its section titles. After adding or renaming a section, run:

```
python scripts/update_language_spec_table_of_contents.py
```

Add `--check` to only test whether it is up to date. The script uses only the Python standard library.

## Editor support

[editors/vscode](../editors/vscode) is a VS Code extension that highlights the reserved words, the operators, labels, attribute lists such as `#(static)`, strings and calls in `.mu` files. See its [README](../editors/vscode/README.md) for how to install it.

## Versioning

`scripts/bump-version.ps1 <version>` sets the version of the compiler, a semantic version such as `0.2.0`, in [src/version.h](../src/version.h) and in the language specification. It uses PowerShell, so it needs nothing to install on Windows.

```
powershell -File scripts/bump-version.ps1 0.2.0
```

After the version is bumped and committed, `scripts/deploy-release.ps1` tags the commit of main as `v<version>` and pushes it, which starts the release workflow. It refuses to run with uncommitted changes, off main, behind `origin/main`, or when the tag exists, and asks before pushing unless it is given `-Yes`.
