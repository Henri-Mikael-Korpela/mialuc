# Documentation

See [Mialu Programming Language Specification](./docs/mialu-programming-language.md) for the most complete specification Mialu programming language.

See [C Programming Style Guide](./docs/c-programming-style-guide.md) for how to use the C programming language in this project.

See [Programming Mindset](./docs/programming-mindset.md) for how the mentality to approach programming in this project.

# Reacting to Changes

Whenever Mialu programming language implementation is changed, the following changes should be taken into account:

- Grammar in EBNF format and its verification script should be updated.
- Mialu programming language specification should be updated.
- Mialu programming language test cases should be updated.
- Mialu programming language editor support should be updated.
- Wasm building should work with the change: whatever the language gets, the `wasm` target should compile it, and the `wat` target and the formatter in `src/wasm-text.c` should show it. A change that the front end accepts but that Wasm building cannot handle is not finished.
- The playground with Wasm should work with the change: `tests/wasm/playground.html` and `tests/wasm/serve.py` should compile, show and run it, and the examples in `test-code` should work in it.
