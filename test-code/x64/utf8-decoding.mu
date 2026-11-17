// The built-in procedure `utf8_decode`, which makes a `Str` of an array or an array slice of bytes,
// and says whether the bytes are valid UTF-8. The `Str` is the same bytes and not a copy, and when
// the bytes are not valid it is empty and the second value is false. Valid is what RFC 3629 says:
// no first byte that could only make a character that has a shorter form, no surrogate, nothing
// above U+10FFFF, and no character that is cut off. The exit code of the program is the number of
// wrong results, 0.

package utf8_decoding

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

/// Prints a line with the number, and counts it as wrong when it is not what was expected.
check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

number_of :: proc(is_true: Bool) Int ->
    if is_true ->
        return 1
    return 0

/// Decodes a slice of bytes, and gives 1 when they are valid UTF-8 and 0 when they are not.
is_valid :: proc(bytes: []Byte) Int ->
    text, is_ok := utf8_decode(bytes)
    return number_of(is_ok)

main :: proc() Int ->
    wrong := 0

    // Valid: 1, 2, 3 and 4 bytes, the empty array, and the first and the last of each length.
    ascii := [const]Byte { 72, 105 }
    text, is_ok := utf8_decode(ascii)
    wrong += check("ascii is ok %lld", number_of(is_ok), 1)
    wrong += check("ascii is Hi %lld", number_of(text == "Hi"), 1)
    wrong += check("ascii has %lld characters", length_of(text), 2)

    mixed := [const]Byte { 97, 0xC3, 0xA9, 0xE2, 0x82, 0xAC, 0xF0, 0x9F, 0x9A, 0x80, 122 }
    decoded, mixed_ok := utf8_decode(mixed)
    wrong += check("mixed is ok %lld", number_of(mixed_ok), 1)
    wrong += check("mixed is aé€🚀z %lld", number_of(decoded == "aé€🚀z"), 1)
    wrong += check("mixed has %lld characters", length_of(decoded), 5)
    wrong += check("the rocket is %lld", cast(Int)decoded[3], 128640)

    nothing := [const]Byte { }
    empty, empty_ok := utf8_decode(nothing)
    wrong += check("empty is ok %lld", number_of(empty_ok), 1)
    wrong += check("empty has %lld characters", length_of(empty), 0)

    wrong += check("U+0080 is %lld", is_valid([const]Byte { 0xC2, 0x80 }), 1)
    wrong += check("U+07FF is %lld", is_valid([const]Byte { 0xDF, 0xBF }), 1)
    wrong += check("U+0800 is %lld", is_valid([const]Byte { 0xE0, 0xA0, 0x80 }), 1)
    wrong += check("U+D7FF is %lld", is_valid([const]Byte { 0xED, 0x9F, 0xBF }), 1)
    wrong += check("U+E000 is %lld", is_valid([const]Byte { 0xEE, 0x80, 0x80 }), 1)
    wrong += check("U+FFFF is %lld", is_valid([const]Byte { 0xEF, 0xBF, 0xBF }), 1)
    wrong += check("U+10000 is %lld", is_valid([const]Byte { 0xF0, 0x90, 0x80, 0x80 }), 1)
    wrong += check("U+10FFFF is %lld", is_valid([const]Byte { 0xF4, 0x8F, 0xBF, 0xBF }), 1)

    // Not valid: the result is the empty string and false.
    bad := [const]Byte { 97, 0xFF, 98 }
    broken, broken_ok := utf8_decode(bad)
    wrong += check("0xFF is ok %lld", number_of(broken_ok), 0)
    wrong += check("0xFF gives the empty string %lld", length_of(broken), 0)

    wrong += check("a lone continuation is %lld", is_valid([const]Byte { 0x80 }), 0)
    wrong += check("0xC0 is %lld", is_valid([const]Byte { 0xC0, 0x80 }), 0)
    wrong += check("0xC1 is %lld", is_valid([const]Byte { 0xC1, 0xBF }), 0)
    wrong += check("overlong 3 bytes is %lld", is_valid([const]Byte { 0xE0, 0x9F, 0xBF }), 0)
    wrong += check("overlong 4 bytes is %lld", is_valid([const]Byte { 0xF0, 0x8F, 0xBF, 0xBF }), 0)
    wrong += check("surrogate is %lld", is_valid([const]Byte { 0xED, 0xA0, 0x80 }), 0)
    wrong += check("above U+10FFFF is %lld", is_valid([const]Byte { 0xF4, 0x90, 0x80, 0x80 }), 0)
    wrong += check("0xF5 is %lld", is_valid([const]Byte { 0xF5, 0x80, 0x80, 0x80 }), 0)
    wrong += check("cut off 2 is %lld", is_valid([const]Byte { 0xC3 }), 0)
    wrong += check("cut off 3 is %lld", is_valid([const]Byte { 0xE2, 0x82 }), 0)
    wrong += check("cut off 4 is %lld", is_valid([const]Byte { 0xF0, 0x9F, 0x9A }), 0)
    wrong += check("no continuation is %lld", is_valid([const]Byte { 0xC3, 0x41 }), 0)
    wrong += check("second continuation is %lld", is_valid([const]Byte { 0xE2, 0x82, 0x41 }), 0)
    wrong += check("a good one then a bad one is %lld", is_valid([const]Byte { 0xC3, 0xA9, 0xC3 }), 0)

    // A slice of an array is decoded as well, and the string views the bytes.
    middle, middle_ok := utf8_decode(mixed[1:6])
    wrong += check("a slice is ok %lld", number_of(middle_ok), 1)
    wrong += check("a slice is é€ %lld", number_of(middle == "é€"), 1)
    cut, cut_ok := utf8_decode(mixed[1:5])
    wrong += check("a slice that cuts a character is %lld", number_of(cut_ok), 0)
    return wrong
