// A for statement runs its block once for each number of a range. The range is written
// first..<end, which leaves out the end, or first..<=end, which includes it. Every example
// returns an Int, which comes back as BigInt.
console.log("Exclusive range 0..<10 runs 10 times:", for_in_exclusive_range() - 1n);
console.log("Inclusive range 0..<=10 runs 11 times:", for_in_inclusive_range() - 1n);
// The start and the end are expressions, so they can be parameters or variables.
console.log("Sum of 1..<5 and 1..<=5:", sum_exclusive(1n, 5n), sum_inclusive(1n, 5n));
console.log("Factorial of 1, 5 and 10:", factorial(1n), factorial(5n), factorial(10n));
console.log("Variables as bounds, for 5 (3 to 7):", sum_of_variable_bounds(5n));
console.log("Expressions as bounds, for 4 (8 to 11):", sum_of_expression_bounds(4n));
console.log("Calls as bounds, for 4 (1 to 8):", sum_of_call_bounds(4n));
// A range with nothing in it runs the block no times, and an inclusive range of one number once.
console.log("Empty ranges run 0 times, for 3 and 0:", count_empty_range(3n), count_empty_range(0n));
console.log("One number runs once:", count_single_number(7n));
console.log("Negative numbers, squares from -3 to 3:", negative_range());
// The end is read once, before the loop, so changing the variable it came from does nothing.
console.log("End is read once, for 10:", end_is_read_once(10n));
// Loops can be nested, and the inner range can use the iterator of the outer one.
console.log("Multiplication table sum, for 3:", multiplication_table_sum(3n));
console.log("Triangle, for 4:", triangle_count(4n));
// A loop block is like any other block: it can have if statements, variables and returns.
console.log("Sum of even numbers up to 10:", sum_of_even_numbers(10n));
console.log("First number with a square above 50:", first_multiple(50n), first_multiple(0n));
console.log("Variable defined in the loop block, for 5:", variable_in_loop_block(5n));
console.log("Loops one after another, for 3:", loops_one_after_another(3n));
console.log("Call in the loop, sum of factorials up to 5:", call_in_loop(5n));
