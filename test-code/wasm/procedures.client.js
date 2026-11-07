// A procedure is called by its name with the arguments in parentheses. A call is an expression, so
// it can be used wherever a value can.
console.log("Call without arguments:", forty(), forty_two());
console.log("Call with an argument:", double(21n), quadruple(5n));
console.log("Call with several arguments:", add(2n, 3n), add_three(1n, 2n, 3n), average(7n, 10n));
// The arguments are expressions, and may be calls themselves.
console.log("Arguments that are calls: quadruple(5) =", quadruple(5n));
// A call that returns a Bool is a condition, or an operand of and, or and not.
console.log("Is even, for 4 and 7:", Boolean(is_even(4n)), Boolean(is_even(7n)));
console.log("Is positive even, for 4, -4 and 7:", Boolean(is_positive_even(4n)),
    Boolean(is_positive_even(-4n)), Boolean(is_positive_even(7n)));
console.log("Describe, for 4, 7 and -2:", describe(4n), describe(7n), describe(-2n));
// The result of a call can be stored in a variable.
console.log("Sum of squares of 3 and 4:", sum_of_squares(3n, 4n));
// A procedure may call itself.
console.log("Factorial, for 1, 5 and 10:", factorial(1n), factorial(5n), factorial(10n));
console.log("Fibonacci, for 0 to 10:",
    [0n, 1n, 2n, 3n, 4n, 5n, 6n, 7n, 8n, 9n, 10n].map(fibonacci).join(" "));
// A Str is passed and returned as it is: the address and the length of its bytes.
console.log("Str:", Str(chosen_name(false)), Str(chosen_name(true)));
// A procedure can be used above its definition.
console.log("Called above its definition:", uses_later());
// A call that is a statement throws its value away.
console.log("Call as a statement:", ignores_result(5n));
