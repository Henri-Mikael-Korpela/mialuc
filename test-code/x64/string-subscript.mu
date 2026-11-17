// A string is bytes, but it is subscripted by the character, which is a code point and not a byte:
// `s[0]` of "äiti" is the `Rune` 'ä', which is two bytes, and `s[1]` is 'i'. A subscript has to walk
// over the string from its start, one character after the other, so it takes as long as the index
// is large. `length_of` of a string is the number of its characters too, and not its bytes. The
// exit code of the program is the number of wrong results, 0.

package string_subscript

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

TITLE : Str : "Älä"

/// Prints a line with the number, and counts it as wrong when it is not what was expected.
check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

code_point :: proc(text: Str, index: Int) Int ->
    return cast(Int)text[index]

main :: proc() Int ->
    wrong := 0

    // One byte for each character.
    plain := "abc"
    wrong += check("a is %lld", cast(Int)plain[0], 97)
    wrong += check("c is %lld", cast(Int)plain[2], 99)
    wrong += check("length of abc is %lld", length_of(plain), 3)

    // Two bytes for the first character: ä is U+00E4.
    s := "äiti"
    wrong += check("first is %lld", cast(Int)s[0], 228)
    wrong += check("second is %lld", cast(Int)s[1], 105)
    wrong += check("last is %lld", cast(Int)s[3], 105)
    wrong += check("characters are %lld", length_of(s), 4)
    wrong += check("a Rune is a Rune: %lld", number_of_equal(s[1], 'i'), 1)

    // Characters of 1, 2, 3 and 4 bytes.
    mixed := "aé€🚀z"
    wrong += check("a is %lld", cast(Int)mixed[0], 97)
    wrong += check("é is %lld", cast(Int)mixed[1], 233)
    wrong += check("€ is %lld", cast(Int)mixed[2], 8364)
    wrong += check("rocket is %lld", cast(Int)mixed[3], 128640)
    wrong += check("z is %lld", cast(Int)mixed[4], 122)
    wrong += check("characters of mixed are %lld", length_of(mixed), 5)

    // A computed index, a parameter and a typed constant.
    index := 1
    wrong += check("computed is %lld", cast(Int)mixed[index + 1], 8364)
    wrong += check("parameter is %lld", code_point("héllo", 1), 233)
    wrong += check("constant is %lld", cast(Int)TITLE[0], 196)
    wrong += check("constant length is %lld", length_of(TITLE), 3)

    // The empty string has no characters.
    nothing : Str
    wrong += check("nothing has %lld", length_of(nothing), 0)
    wrong += check("empty has %lld", length_of(""), 0)

    // A loop that reads every character.
    total := 0
    for i in 0..<length_of(mixed) ->
        total += 1
    wrong += check("visited %lld", total, 5)
    return wrong

number_of_equal :: proc(a: Rune, b: Rune) Int ->
    if a == b ->
        return 1
    return 0
