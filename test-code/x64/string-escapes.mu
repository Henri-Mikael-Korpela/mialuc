// A string literal can have the escape sequences of a rune literal, and each stands for one
// character, which is part of the string as its UTF-8 bytes. The results are the ones that the
// language specification gives. The exit code of the program is the number of wrong results, 0.

package string_escapes

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

/// The strings that have a letter escape, where the length is the number of characters.
letters :: proc() Int ->
    text := "a\tb\nc\rd"
    return length_of(text) * 1000 + cast(Int)text[1] * 10 + cast(Int)text[3]

/// The same character written in every form is the same character.
numbers :: proc() Int ->
    text := "\101\x41\u0041\U00000041"
    wrong := 0
    for character in text ->
        if character not= 'A' ->
            wrong += 1
    return length_of(text) * 10 + wrong

/// Characters of more than one byte, by their code points.
non_ascii :: proc() Int ->
    text := "\u00e9\u65e5\U0001F680"
    total := 0
    if text[0] == 'é' ->
        total += 1
    if text[1] == '日' ->
        total += 10
    if text[2] == '🚀' ->
        total += 100
    return total

/// A quote and a backslash in a string, which would end it or start an escape sequence.
quotes :: proc() Int ->
    text := "\"\\\'"
    return length_of(text) * 1000 + cast(Int)text[0] + cast(Int)text[1] + cast(Int)text[2]

/// Escape sequences in text that is joined when the program is compiled.
joined :: proc() Int ->
    text := "a\n" ++ "b\t" ++ 'c'
    return length_of(text) * 100 + cast(Int)text[1]

main :: proc() Int ->
    wrong := 0
    wrong += check("letters() is %lld", letters(), 7100)
    wrong += check("numbers() is %lld", numbers(), 40)
    wrong += check("non_ascii() is %lld", non_ascii(), 111)
    wrong += check("quotes() is %lld", quotes(), 3000 + 34 + 92 + 39)
    wrong += check("joined() is %lld", joined(), 510)
    puts("a\tb\u00e9\U0001F680 and a \"quote\"")
    return wrong
