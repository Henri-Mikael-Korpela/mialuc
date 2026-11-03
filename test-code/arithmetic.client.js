// Arithmetic on Int, which come back as BigInt.
console.log("Addition:", add_foo());
console.log("Addition assignment:", add_assign_foo());
console.log("Subtraction:", subtract_foo());
console.log("Multiplication:", multiply_foo());
console.log("Multiplication assignment:", multiply_assign_foo());
console.log("Division:", divide_foo());
console.log("Division assignment:", divide_assign_foo());
// Multiplication binds tighter than addition, so this is 2 + (3 * 4).
console.log("Precedence:", precedence_foo());
// Division binds tighter than subtraction, so this is 20 - (12 / 4).
console.log("Precedence of division:", precedence_division_foo());
