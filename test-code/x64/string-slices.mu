// A slice of a string, `s[low:high]`, is another string: the characters from `low` up to, and not
// including, `high`. The bounds count characters, as the subscript does, and not bytes, and the
// string that it makes views the bytes of the one that it is a slice of. A bound that is outside of
// the string stops the program. The exit code of the program is the number of wrong results, 0.

package string_slices

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

/// Whether a slice has the characters that are written, which is also a test of `==`.
is_same :: proc(text: Str, expected: Str) Int ->
    if text == expected ->
        return 1
    return 0

part :: proc(text: Str, low: Int, high: Int) Str ->
    return text[low:high]

main :: proc() Int ->
    wrong := 0

    text := "Hello, World!"
    hello := text[0:5]
    world := text[7:12]
    wrong += check("Hello is %lld", is_same(hello, "Hello"), 1)
    wrong += check("World is %lld", is_same(world, "World"), 1)
    wrong += check("Hello length is %lld", length_of(hello), 5)
    wrong += check("whole is %lld", is_same(text[0:13], text), 1)
    wrong += check("last is %lld", is_same(text[12:13], "!"), 1)

    // An empty slice, at the start, in the middle and at the end.
    wrong += check("empty at start is %lld", length_of(text[0:0]), 0)
    wrong += check("empty in the middle is %lld", length_of(text[6:6]), 0)
    wrong += check("empty at end is %lld", length_of(text[13:13]), 0)
    wrong += check("empty is the empty string %lld", is_same(text[4:4], ""), 1)

    // The bounds are characters, not bytes: ä takes two bytes and € three.
    s := "äiti"
    wrong += check("ä is %lld", is_same(s[0:1], "ä"), 1)
    wrong += check("iti is %lld", is_same(s[1:4], "iti"), 1)
    wrong += check("äi is %lld", is_same(s[0:2], "äi"), 1)
    wrong += check("äi has %lld characters", length_of(s[0:2]), 2)
    mixed := "a€b🚀c"
    wrong += check("€b is %lld", is_same(mixed[1:3], "€b"), 1)
    wrong += check("rocket is %lld", is_same(mixed[3:4], "🚀"), 1)
    wrong += check("rocket code point is %lld", cast(Int)mixed[3:4][0], 128640)

    // A slice of a slice, a computed bound and a parameter.
    inner := text[7:13][1:4]
    wrong += check("slice of a slice is %lld", is_same(inner, "orl"), 1)
    low := 2
    wrong += check("computed is %lld", is_same(text[low:low + 3], "llo"), 1)
    wrong += check("parameter is %lld", is_same(part("héllo", 1, 3), "él"), 1)

    // A slice is a string like any other, subscripted and compared.
    wrong += check("subscript of a slice is %lld", cast(Int)text[7:12][0], 87)
    wrong += check("not= is %lld", is_same(text[0:4], "Hello"), 0)
    return wrong
