// Characters: a `Rune` is a Unicode character, which is the code point of the character, a 32-bit
// number from 0 to 0x10FFFF in a word, and a rune literal is one character between single quotes.
// The source is UTF-8, so a character can be a letter of any alphabet, a kanji or an emoji, and it
// is one rune, whatever number of bytes it takes. A character that cannot be written as itself, or
// is hard to see, is an escape sequence: a backslash and what stands for the character, such as
// `'\n'` for a new line and `'\u0123'` for U+0123. A rune is cast to an Int, which is its code
// point, and to 4 bytes, which are its UTF-8 encoding, and a Byte is cast to a rune. A rune has an
// order, the order of the code points, but no arithmetic. The output is plain ASCII, so that any console shows it, and the exit code of
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

/// The 4 bytes of the encoding of a character as one Int, with the first byte highest.
encoded :: proc(character: Rune) Int ->
    bytes := cast([4]Byte)character
    return cast(Int)bytes[0] * 16777216 + cast(Int)bytes[1] * 65536 + cast(Int)bytes[2] * 256 + cast(Int)bytes[3]

/// The number of bytes of the encoding, which are the ones before the first zero.
encoded_length :: proc(character: Rune) Int ->
    bytes := cast([4]Byte)character
    length := 0
    for byte in bytes ->
        if byte not= 0 ->
            length += 1
    return length

sum_of_bytes :: proc(bytes: []Byte) Int ->
    sum := 0
    for byte in bytes ->
        sum += cast(Int)byte
    return sum

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

    failures += check(str_to_c_str("code point of A"), cast(Int)'A' == 65)
    failures += check(str_to_c_str("code point of a rocket"), cast(Int)rocket == 128640)
    failures += check(str_to_c_str("a Byte is a Rune"), cast(Rune)int_to_byte(200) == '\u00C8')
    failures += check(str_to_c_str("encoding of A"), encoded('A') == 0x41000000)
    failures += check(str_to_c_str("encoding of e with an acute accent"), encoded('é') == 0xC3A90000)
    failures += check(str_to_c_str("encoding of a kanji"), encoded(kanji) == 0xE88A8B00)
    failures += check(str_to_c_str("encoding of a rocket"), encoded(rocket) == 0xF09F9A80)
    failures += check(str_to_c_str("number of bytes of the encodings"), encoded_length('A') == 1 and encoded_length('é') == 2 and encoded_length(kanji) == 3 and encoded_length(rocket) == 4)
    rocket_bytes := cast([4]Byte)rocket
    failures += check(str_to_c_str("an array of bytes is an array"), length_of(rocket_bytes) == 4 and sum_of_bytes(rocket_bytes) == 681)
    return failures
