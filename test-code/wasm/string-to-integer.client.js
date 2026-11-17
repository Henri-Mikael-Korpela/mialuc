// try_str_to_int gives the number that a string is and whether it was a number. JavaScript calls
// procedures that return one of them, and an Int is a BigInt.
console.log("1234 and -42:", parse_number(), parse_negative());
console.log("abc, the empty string, - and 12a are not numbers:", Boolean(is_word_a_number()),
    Boolean(is_empty_a_number()), Boolean(is_sign_a_number()), Boolean(is_trailing_text_a_number()));
console.log("The largest and the smallest Int:", largest(), smallest());
console.log("One more than either is not a number:", Boolean(is_too_large_a_number()),
    Boolean(is_too_small_a_number()));
console.log("Strings that are variables, 40 and 2:", sum_of_two());
