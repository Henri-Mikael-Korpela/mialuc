package int_to_str

/// `int_to_str` makes the decimal text of an integer that is known when the program is compiled, so
/// the result is a constant string, which can be joined with `++`, compared and measured.
MAJOR :: 1
MINOR :: 20
PATCH : I32 : 300
LEAST :: -9223372036854775807 - 1

VERSION :: int_to_str(MAJOR) ++ '.' ++ int_to_str(MINOR) ++ '.' ++ int_to_str(PATCH)

is_version :: proc(text: Str) Bool ->
    return text == VERSION

/// The number of runes of the version, 1.20.300, which is 8.
version_length :: proc() Int ->
    return length_of(VERSION)

/// The text of the smallest number, which has a minus sign and 19 digits.
least_length :: proc() Int ->
    return length_of(int_to_str(LEAST))

is_minus_seven :: proc(text: Str) Bool ->
    return text == int_to_str(-7)
