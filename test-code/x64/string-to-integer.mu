// The built-in procedure `try_str_to_int`, which makes an `Int` of a string and says whether the
// string was one: an optional `-` and one or more of the digits 0 to 9, and nothing else, that
// fits in an `Int`. For any other string the number is 0 and the second value is false. The exit
// code of the program is the number of wrong results, 0.

package string_to_integer

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int, is_ok: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

/// Prints a line with the number and whether it was a number, and counts it as wrong when it is
/// not what was expected.
check :: proc(format: C_Str, number: Int, is_ok: Bool, expected: Int, expected_ok: Bool) Int ->
    is_ok_number := 0
    if is_ok ->
        is_ok_number = 1
    printf(format, number, is_ok_number)
    if number == expected and is_ok == expected_ok ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

/// Converts a string that is only known when the program runs.
convert :: proc(text: Str) (Int, Bool) ->
    number, is_ok := try_str_to_int(text)
    return number, is_ok

main :: proc() Int ->
    wrong := 0

    number, is_ok := try_str_to_int("0")
    wrong += check("0 is %lld, ok %lld", number, is_ok, 0, true)
    seven, seven_ok := try_str_to_int("7")
    wrong += check("7 is %lld, ok %lld", seven, seven_ok, 7, true)

    first, first_ok := try_str_to_int("42")
    wrong += check("42 is %lld, ok %lld", first, first_ok, 42, true)
    second, second_ok := try_str_to_int("-42")
    wrong += check("-42 is %lld, ok %lld", second, second_ok, -42, true)
    third, third_ok := try_str_to_int("-0")
    wrong += check("-0 is %lld, ok %lld", third, third_ok, 0, true)
    fourth, fourth_ok := try_str_to_int("0042")
    wrong += check("0042 is %lld, ok %lld", fourth, fourth_ok, 42, true)

    // The largest and the smallest Int fit, and one more does not.
    largest, largest_ok := try_str_to_int("9223372036854775807")
    wrong += check("largest is %lld, ok %lld", largest - 9223372036854775806, largest_ok, 1, true)
    smallest, smallest_ok := try_str_to_int("-9223372036854775808")
    smallest_plus := smallest + 9223372036854775807
    wrong += check("smallest is %lld, ok %lld", smallest_plus, smallest_ok, -1, true)
    over, over_ok := try_str_to_int("9223372036854775808")
    wrong += check("over is %lld, ok %lld", over, over_ok, 0, false)
    under, under_ok := try_str_to_int("-9223372036854775809")
    wrong += check("under is %lld, ok %lld", under, under_ok, 0, false)
    long, long_ok := try_str_to_int("99999999999999999999")
    wrong += check("long is %lld, ok %lld", long, long_ok, 0, false)

    // What is not a number.
    nothing, nothing_ok := try_str_to_int("")
    wrong += check("empty is %lld, ok %lld", nothing, nothing_ok, 0, false)
    sign, sign_ok := try_str_to_int("-")
    wrong += check("- is %lld, ok %lld", sign, sign_ok, 0, false)
    plus, plus_ok := try_str_to_int("+5")
    wrong += check("+5 is %lld, ok %lld", plus, plus_ok, 0, false)
    space, space_ok := try_str_to_int(" 5")
    wrong += check("space 5 is %lld, ok %lld", space, space_ok, 0, false)
    trailing, trailing_ok := try_str_to_int("5 ")
    wrong += check("5 space is %lld, ok %lld", trailing, trailing_ok, 0, false)
    letters, letters_ok := try_str_to_int("12a")
    wrong += check("12a is %lld, ok %lld", letters, letters_ok, 0, false)
    word, word_ok := try_str_to_int("abc")
    wrong += check("abc is %lld, ok %lld", word, word_ok, 0, false)
    twice, twice_ok := try_str_to_int("--5")
    wrong += check("--5 is %lld, ok %lld", twice, twice_ok, 0, false)
    below, below_ok := try_str_to_int("/")
    wrong += check("/ is %lld, ok %lld", below, below_ok, 0, false)
    above, above_ok := try_str_to_int(":")
    wrong += check(": is %lld, ok %lld", above, above_ok, 0, false)

    // A string that is known only when the program runs, and one that is a variable.
    name := "123"
    value, value_ok := convert(name)
    wrong += check("variable is %lld, ok %lld", value, value_ok, 123, true)
    rune_text := "12" ++ "34"
    joined, joined_ok := try_str_to_int(rune_text)
    wrong += check("joined is %lld, ok %lld", joined, joined_ok, 1234, true)
    nothing_text : Str
    unset, unset_ok := try_str_to_int(nothing_text)
    wrong += check("a Str without a value is %lld, ok %lld", unset, unset_ok, 0, false)

    // The value can be left out.
    _, only_ok := try_str_to_int("9")
    wrong += check("only ok is %lld, ok %lld", 0, only_ok, 0, true)
    return wrong
