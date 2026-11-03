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
// The keywords and, or and not combine Bool values. not binds tighter than and, and and tighter
// than or, and a comparison binds tighter than all three. There are no parentheses yet.
console.log("Not, for true and false:", Boolean(logical_not(true)), Boolean(logical_not(false)));
console.log("And, for the four combinations:", Boolean(logical_and(false, false)),
    Boolean(logical_and(false, true)), Boolean(logical_and(true, false)),
    Boolean(logical_and(true, true)));
console.log("Or, for the four combinations:", Boolean(logical_or(false, false)),
    Boolean(logical_or(false, true)), Boolean(logical_or(true, false)),
    Boolean(logical_or(true, true)));
console.log("Not not, for true and false:", Boolean(not_not(true)), Boolean(not_not(false)));
console.log("Exclusive or with keywords, for the four combinations:",
    Boolean(exclusive_or_keywords(false, false)), Boolean(exclusive_or_keywords(false, true)),
    Boolean(exclusive_or_keywords(true, false)), Boolean(exclusive_or_keywords(true, true)));
console.log("In range 1 to 10 with and, for 5, 0 and 11:", Boolean(in_range_keywords(5n, 1n, 10n)),
    Boolean(in_range_keywords(0n, 1n, 10n)), Boolean(in_range_keywords(11n, 1n, 10n)));
console.log("Out of range 1 to 10 with or, for 5, 0 and 11:", Boolean(out_of_range(5n, 1n, 10n)),
    Boolean(out_of_range(0n, 1n, 10n)), Boolean(out_of_range(11n, 1n, 10n)));
console.log("Not a == b, for 3 and 3, 3 and 4:", Boolean(not_binds_looser_than_comparison(3n, 3n)),
    Boolean(not_binds_looser_than_comparison(3n, 4n)));
// a or b and c is a or (b and c), so it holds for true, false, false.
console.log("a or b and c, for true false false, false true false and false true true:",
    Boolean(and_binds_tighter_than_or(true, false, false)),
    Boolean(and_binds_tighter_than_or(false, true, false)),
    Boolean(and_binds_tighter_than_or(false, true, true)));
console.log("Leap year with keywords, for 2024, 1900, 2000 and 2023:",
    Boolean(is_leap_year_keywords(2024n)), Boolean(is_leap_year_keywords(1900n)),
    Boolean(is_leap_year_keywords(2000n)), Boolean(is_leap_year_keywords(2023n)));
console.log("Fizz or buzz, for 3, 5, 15 and 7:", Boolean(fizz_or_buzz(3n)),
    Boolean(fizz_or_buzz(5n)), Boolean(fizz_or_buzz(15n)), Boolean(fizz_or_buzz(7n)));
console.log("Fizz only, for 3, 5, 15 and 7:", Boolean(fizz_buzz_only(3n)),
    Boolean(fizz_buzz_only(5n)), Boolean(fizz_buzz_only(15n)), Boolean(fizz_buzz_only(7n)));
// 1 is an adult with a ticket, 2 a child with a ticket, 0 is not let in.
console.log("Can enter, for (20, ticket), (10, ticket), (20, no ticket), (20, ticket, banned):",
    can_enter(20n, true, false), can_enter(10n, true, false), can_enter(20n, false, false),
    can_enter(20n, true, true));
