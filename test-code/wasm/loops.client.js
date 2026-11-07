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
// A loop without a range is written `for ->`. It runs its block again and again, until a `break`
// or a `return` leaves it.
console.log("Loop with break, count up to 5:", count_up_to(5n), count_up_to(0n));
console.log("Loop with return, first n with n * n above 50:", first_square_above(50n));
console.log("Steps of the Collatz sequence from 6, 7 and 27:", collatz_steps(6n), collatz_steps(7n),
    collatz_steps(27n));
// `continue` skips the rest of the block and goes on with the next number of a for-in loop, and
// `break` leaves the loop. They are allowed in loops with a range as well.
console.log("Continue skips even numbers, sum of odd numbers up to 9:", sum_of_odd_numbers(9n));
console.log("Break stops at the limit, sum of 1, 2, 3 and so on while it stays within 10 and 100:", sum_until_limit(10n),
    sum_until_limit(100n));
console.log("Count numbers up to 10 that are not multiples of 3:", count_before_multiple(10n, 3n));
// The iterator still steps after a `continue`, also at the end of an inclusive range.
console.log("Continue in inclusive ranges:", continue_in_inclusive_range());
// A jump belongs to the innermost loop only.
console.log("Break leaves the inner loop only, for 4:", break_inner_loop_only(4n));
console.log("Continue skips a pass of the outer loop, for 3:", continue_outer_pass(3n));
console.log("Break in an else branch, for 10:", break_in_else_branch(10n));
console.log("Smallest divisor of 15, 17 and 49:", find_divisor(15n), find_divisor(17n), find_divisor(49n));
console.log("Continue in a loop without a range, for 10:", loop_with_continue(10n));
console.log("Break after an inner loop:", break_after_nested_loops());
