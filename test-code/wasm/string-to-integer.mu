package string_to_integer

/// The built-in procedure `try_str_to_int` gives two values: the number that a string is, and
/// whether it was one. A number is an optional `-` and one or more of the digits 0 to 9, and
/// nothing else, that fits in an `Int`.
parse_number :: proc() Int ->
    number, is_ok := try_str_to_int("1234")
    return number

parse_negative :: proc() Int ->
    number, is_ok := try_str_to_int("-42")
    return number

/// A string that is not a number gives 0, and false.
is_word_a_number :: proc() Bool ->
    number, is_ok := try_str_to_int("abc")
    return is_ok

is_empty_a_number :: proc() Bool ->
    number, is_ok := try_str_to_int("")
    return is_ok

is_sign_a_number :: proc() Bool ->
    number, is_ok := try_str_to_int("-")
    return is_ok

is_trailing_text_a_number :: proc() Bool ->
    number, is_ok := try_str_to_int("12a")
    return is_ok

/// The largest and the smallest `Int` fit, and one more does not.
largest :: proc() Int ->
    number, is_ok := try_str_to_int("9223372036854775807")
    return number

smallest :: proc() Int ->
    number, is_ok := try_str_to_int("-9223372036854775808")
    return number

is_too_large_a_number :: proc() Bool ->
    number, is_ok := try_str_to_int("9223372036854775808")
    return is_ok

is_too_small_a_number :: proc() Bool ->
    number, is_ok := try_str_to_int("-9223372036854775809")
    return is_ok

/// A string that is a variable is converted the same way.
sum_of_two :: proc() Int ->
    first := "40"
    second := "2"
    a, a_ok := try_str_to_int(first)
    b, b_ok := try_str_to_int(second)
    return a + b
