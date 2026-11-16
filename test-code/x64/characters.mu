// Characters: a `Rune` is a Unicode character, which is the code point of the character, a 32-bit
// number from 0 to 0x10FFFF in a word, and a rune literal is one character between single quotes.
// The source is UTF-8, so a character can be a letter of any alphabet, a kanji or an emoji, and it
// is one rune, whatever number of bytes it takes. A character that cannot be written as itself, or
// is hard to see, is an escape sequence: a backslash and what stands for the character, such as
// `'\n'` for a new line and `'\u0123'` for U+0123. A rune has an order, the order of the code
// points, but no arithmetic. The output is plain ASCII, so that any console shows it, and the exit code of
// the program is the number of checks that failed, so 0 means that all of them passed.

import foreign libc

foreign libc ->
    puts :: proc(content: C_Str) Int -> ...

/// A constant that is a rune, with or without its type.
FIRST_LETTER :: 'A'
LAST_LETTER : Rune : 'Z'

/// The initial value of a global variable is a constant, such as a rune literal.
current #(static) : Rune = 'a'

Token :: struct ->
    first: Rune
    second: Rune

/// Prints `label` and gives 0 when `holds`, and 1 when it does not, with a line that says so.
check :: proc(label: C_Str, holds: Bool) Int ->
    puts(label)
    if holds ->
        return 0
    puts(str_to_c_str("    FAILED"))
    return 1

is_upper_case_ascii :: proc(character: Rune) Bool ->
    return character >= FIRST_LETTER and character <= LAST_LETTER

identity :: proc(character: Rune) Rune ->
    copy := character
    return copy

next_global :: proc(character: Rune) Rune ->
    previous := current
    current = character
    return previous

main :: proc() Int ->
    failures := 0

    letter : Rune = 'A'
    kanji := '芋'
    rocket : Rune = '🚀'
    greek : Rune = 'λ'
    failures += check(str_to_c_str("letter is 'A'"), letter == 'A')
    failures += check(str_to_c_str("a kanji is above a letter"), kanji > letter)
    failures += check(str_to_c_str("an emoji is above a kanji"), rocket > kanji)
    failures += check(str_to_c_str("a Greek letter is below a kanji"), greek < kanji)
    failures += check(str_to_c_str("a letter is not an emoji"), letter not= rocket)
    failures += check(str_to_c_str("a rune is at least itself"), kanji >= kanji and kanji <= kanji)

    failures += check(str_to_c_str("upper case ASCII"), is_upper_case_ascii('M'))
    failures += check(str_to_c_str("lower case is not upper case"), not is_upper_case_ascii('m'))
    failures += check(str_to_c_str("a rune is a parameter and a result"), identity('🚀') == rocket)

    failures += check(str_to_c_str("global variable starts as a"), next_global('z') == 'a')
    failures += check(str_to_c_str("global variable was assigned"), next_global('a') == 'z')

    token := Token { first = 'x', second = '🚀' }
    token.first = '芋'
    failures += check(str_to_c_str("fields of a struct"), token.first == kanji and token.second == rocket)

    word := [const]Rune { 'h', 'e', 'l', 'l', 'o', '!' }
    above := 0
    for character in word ->
        if character > 'l' ->
            above += 1
    word[0] = 'z'
    failures += check(str_to_c_str("array of runes"), above == 1 and word[0] > 'm')

    character := 'a'
    pointer := %character
    pointer^ = '🚀'
    failures += check(str_to_c_str("pointer to a rune"), character == rocket)

    empty : Rune
    failures += check(str_to_c_str("default rune is zero"), empty < 'A')

    newline : Rune = '\n'
    unicode_char : Rune = '\u0123'
    failures += check(str_to_c_str("a new line is below a space"), newline < ' ')
    failures += check(str_to_c_str("a new line is the code point 10"), newline == '\x0A')
    failures += check(str_to_c_str("a tab is the code point 9"), '\t' == '\011')
    controls := '\a' < '\b' and '\b' < '\t' and '\e' > '\f'
    failures += check(str_to_c_str("bell, backspace, escape and form feed"), controls)
    failures += check(str_to_c_str("carriage return and vertical tab"), '\r' > '\v')
    quotes := '\\' == '\x5C' and '\'' == '\x27' and '\"' == '"'
    failures += check(str_to_c_str("backslash and quotes"), quotes)
    every_way := '\101' == 'A' and '\x41' == 'A' and '\u0041' == 'A' and '\U00000041' == 'A'
    failures += check(str_to_c_str("every way to write A"), every_way)
    failures += check(str_to_c_str("a unicode escape is a letter"), unicode_char == 'ģ')
    failures += check(str_to_c_str("an escaped emoji is the emoji"), '\U0001F680' == rocket)
    failures += check(str_to_c_str("digits of either case"), '\u00e9' == '\u00E9')
    return failures
