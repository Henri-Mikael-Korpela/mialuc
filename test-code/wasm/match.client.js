// A match statement runs the block of the first case whose pattern its subject matches, and the
// default case, `case ->`, when none does. The subject is an Int, a Byte, a Str or an enum.
console.log("An Int, a Str and ranges:", Str(match_with_integers()), "/",
    Str(match_with_strings()), "/", Str(match_with_ranges()));

// A match of an enum has a case for every member, or it is `#(partial)`. For JavaScript a member
// is its number, counted from 0, as a BigInt, so RED is 0n, GREEN is 1n and BLUE is 2n.
console.log("Every member of Color:", Str(match_with_enum(0n)), Str(match_with_enum(1n)),
    Str(match_with_enum(2n)));
console.log("A partial match, BLUE has no case:", Str(match_partially_with_enum(0n)),
    Str(match_partially_with_enum(2n)));
console.log("Cases without a block share the next block, so only ALPHA is true:",
    Boolean(is_alpha(0n)), Boolean(is_alpha(1n)), Boolean(is_alpha(2n)), Boolean(is_alpha(3n)));
console.log("Members written with their enum, 1, 2 and 3:", match_with_qualified_members(0n),
    match_with_qualified_members(1n), match_with_qualified_members(2n));

// Patterns are constants: literals, named constants and arithmetic of them, and ranges.
const numbers = [-1, 0, 1, 9, 10, 99, 100, 101, 200, 201, -5];
console.log("Classified:", numbers.map((n) => `${n} is ${Str(classify(n))}`).join(", "));
console.log("Days 6, 7 and 25 are free, 5 is not:", Boolean(is_weekend_or_holiday(6)),
    Boolean(is_weekend_or_holiday(7)), Boolean(is_weekend_or_holiday(25)),
    Boolean(is_weekend_or_holiday(5)));

// "go" is 1, "stop" and "halt" share 2, "gone" is 3, "" is 0 and "g" matches no string, so -1.
console.log("Commands, 1 + 20 + 200 + 3000 + 0 - 100000:", parse_commands());
console.log("Bytes: zero 0, digit 1, letters 2, 255 is 3, space 4, so 432210:", byte_kinds());

console.log("No case runs for 3, so the result stays 1, and 1 and 2 give 10 and 20:",
    no_case_runs(3), no_case_runs(1), no_case_runs(2));
console.log("The subject is computed once, so the call count is 1 and the result 100:",
    subject_is_computed_once());
console.log("Multiples of 3 up to 9, as continue skips the rest of the other passes, 18:",
    sum_until_stop());
console.log("Nested matches, 10, 11, 20 and 20:", nested_match(0n, 0), nested_match(0n, 5),
    nested_match(1n, 0), nested_match(2n, 0));
console.log("A defer in a case runs at its end, 214, and the default case 34:", defer_in_case(1),
    defer_in_case(2));
console.log("A deferred match runs last, 92 for GREEN:", deferred_match(1n));
