// Arithmetic on Int, which come back as BigInt.
console.log("Addition:", add_foo());
console.log("Subtraction:", subtract_foo());
console.log("Multiplication:", multiply_foo());
// Multiplication binds tighter than addition, so this is 2 + (3 * 4).
console.log("Precedence:", precedence_foo());
