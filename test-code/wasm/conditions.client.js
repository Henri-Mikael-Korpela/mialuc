// An if statement runs its block when the condition holds. Every example returns an Int, which
// comes back as BigInt.
console.log("If:", foo_with_if());
console.log("If and else:", foo_with_if_else());
console.log("If, else if and else:", foo_with_if_else_if());
console.log("Nested if:", foo_with_nested_if());
// The parameter decides which branch runs.
console.log("Sign of 5, -5 and 0:", sign(5n), sign(-5n), sign(0n));
console.log("Compare 3 with 3, 20 with 5, 5 with 20 and 6 with 5:",
    compare_foo(3n, 3n), compare_foo(20n, 5n), compare_foo(5n, 20n), compare_foo(6n, 5n));
// A variable defined in a block cannot be used after it, and the blocks that are next to each
// other can each define a variable with the same name.
console.log("Variables in blocks, for 7, 5 and 2:", foo_with_block_variables(7n),
    foo_with_block_variables(5n), foo_with_block_variables(2n));
