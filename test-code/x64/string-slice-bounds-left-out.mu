// The bounds of the slice of a string can be left out as well: `s[:3]` starts at the first character,
// `s[3:]` goes to the last, and `s[:]` has all of them. The bounds count characters and not bytes,
// as when they are written, so each of the characters of "añ🚀é" is one, though it is one to four
// bytes. A chunk of a string, `s[offset:offset + length]`, can be written `s[offset:][:length]` too.
// The exit code of the program is the number of wrong results, 0.

package string_slice_bounds_left_out

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

/// The number of characters, which is `length_of` of a `Str`, and 1 when two strings are the same.
same :: proc(a: Str, b: Str) Int ->
    if a == b ->
        return 1
    return 0

main :: proc() Int ->
    wrong := 0
    s := "hello, world"

    // The four ways to write the whole string.
    wrong += check("s[0:12] is the string: %lld", same(s[0:12], s), 1)
    wrong += check("s[:12] is the string: %lld", same(s[:12], s), 1)
    wrong += check("s[0:] is the string: %lld", same(s[0:], s), 1)
    wrong += check("s[:] is the string: %lld", same(s[:], s), 1)

    // The start and the end of the string.
    wrong += check("s[:5] is hello: %lld", same(s[:5], "hello"), 1)
    wrong += check("s[7:] is world: %lld", same(s[7:], "world"), 1)
    wrong += check("s[12:] has no characters: %lld", length_of(s[12:]), 0)
    wrong += check("s[:0] has no characters: %lld", length_of(s[:0]), 0)

    // A chunk of the string, in the two ways to write it.
    offset := 7
    length := 3
    wrong += check("s[offset:offset + length] is wor: %lld", same(s[offset:offset + length], "wor"), 1)
    wrong += check("s[offset:][:length] is wor: %lld", same(s[offset:][:length], "wor"), 1)
    wrong += check("s[:5][1:][:3] is ell: %lld", same(s[:5][1:][:3], "ell"), 1)

    // The bounds count characters, so bytes of a character are never cut.
    t := "añ🚀é!"
    wrong += check("t[:2] is añ: %lld", same(t[:2], "añ"), 1)
    wrong += check("t[2:] is 🚀é!: %lld", same(t[2:], "🚀é!"), 1)
    wrong += check("t[2:][:1] is 🚀: %lld", same(t[2:][:1], "🚀"), 1)
    wrong += check("t[4:] is !: %lld", same(t[4:], "!"), 1)
    wrong += check("t[:] has %lld characters", length_of(t[:]), length_of(t))
    return wrong
