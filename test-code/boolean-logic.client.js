// A Bool is a 32-bit integer in WebAssembly, so a procedure that returns one gives 1 for true and 0
// for false, and a Bool parameter takes true or false. Boolean() turns the result into a boolean.
console.log("Constants:", Boolean(always_true()), Boolean(always_false()));
// A comparison is an expression with the type Bool.
console.log("Is positive, for 5, 0 and -5:", Boolean(is_positive(5n)), Boolean(is_positive(0n)),
    Boolean(is_positive(-5n)));
console.log("Is even, for 4, 7 and -6:", Boolean(is_even(4n)), Boolean(is_even(7n)),
    Boolean(is_even(-6n)));
console.log("In range 1 to 10, for 5, 0 and 11:", Boolean(in_range(5n, 1n, 10n)),
    Boolean(in_range(0n, 1n, 10n)), Boolean(in_range(11n, 1n, 10n)));
// Bool values are compared with == and not=, and combined with if statements.
console.log("Negate true and false:", Boolean(negate(true)), Boolean(negate(false)));
console.log("Both, for the four combinations:", Boolean(both(false, false)),
    Boolean(both(false, true)), Boolean(both(true, false)), Boolean(both(true, true)));
console.log("Either, for the four combinations:", Boolean(either(false, false)),
    Boolean(either(false, true)), Boolean(either(true, false)), Boolean(either(true, true)));
console.log("Exclusive or, for the four combinations:", Boolean(exclusive_or(false, false)),
    Boolean(exclusive_or(false, true)), Boolean(exclusive_or(true, false)),
    Boolean(exclusive_or(true, true)));
// A Bool is the condition of an if statement, and an Int is never converted to one.
console.log("Describe true and false:", describe(true), describe(false));
console.log("Same sign, for 3 and 4, 3 and -4, -3 and -4:", Boolean(same_sign(3n, 4n)),
    Boolean(same_sign(3n, -4n)), Boolean(same_sign(-3n, -4n)));
console.log("Grade, for 95, 70 and 20:", grade(95n), grade(70n), grade(20n));
console.log("Leap year, for 2024, 1900, 2000 and 2023:", Boolean(is_leap_year(2024n)),
    Boolean(is_leap_year(1900n)), Boolean(is_leap_year(2000n)), Boolean(is_leap_year(2023n)));
