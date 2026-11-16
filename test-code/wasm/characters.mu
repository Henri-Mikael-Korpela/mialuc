// Characters: a `Rune` is a Unicode character, which is the code point of the character, a 32-bit
// number from 0 to 0x10FFFF, and a rune literal is one character between single quotes. The source
// is UTF-8, so a character can be a letter of any alphabet, a kanji or an emoji, and it is one rune,
// whatever number of bytes it takes. A rune has an order, the order of the code points, but no
// arithmetic. In WebAssembly it is a 32-bit integer, which JavaScript passes and receives as a
// plain number, the code point, and `String.fromCodePoint` makes the character of it. A character
// that cannot be written as itself, or is hard to see, is an escape sequence: a backslash and what
// stands for the character, such as `'\n'` for a new line and `'\u0123'` for U+0123. A rune is cast
// to an Int, which is its code point, and to 4 bytes, which are its UTF-8 encoding, and a Byte is
// cast to a rune.

package rune_characters

/// A constant that is a rune, with or without its type.
FIRST_LETTER :: 'A'
LAST_LETTER : Rune : 'Z'

/// The initial value of a global variable is a constant, such as a rune literal.
current #(static) : Rune = 'a'

ascii_letter :: proc() Rune ->
    uppercase: Rune = 'A'
    return uppercase

/// A character from another alphabet.
kanji :: proc() Rune ->
    return '芋'

greek_letter :: proc() Rune ->
    return 'λ'

/// A character that takes four bytes in UTF-8 is one rune.
emoji_rocket :: proc() Rune ->
    rocket: Rune = '🚀'
    return rocket

/// A rune literal as the value of a variable without a type makes a Rune.
inferred :: proc() Rune ->
    character := 'é'
    return character

/// Runes are compared by their code points, with all six operators.
is_upper_case_ascii :: proc(character: Rune) Bool ->
    return character >= FIRST_LETTER and character <= LAST_LETTER

compare :: proc(a: Rune, b: Rune) Int ->
    result := 0
    if a < b ->
        result += 1
    if a <= b ->
        result += 2
    if a == b ->
        result += 4
    if a not= b ->
        result += 8
    if a > b ->
        result += 16
    if a >= b ->
        result += 32
    return result

/// A rune is a parameter, a result and a variable like any other value.
identity :: proc(character: Rune) Rune ->
    copy := character
    return copy

next_global :: proc(character: Rune) Rune ->
    previous := current
    current = character
    return previous

Token :: struct ->
    first: Rune
    second: Rune

/// A struct has fields of runes.
struct_of_runes :: proc() Rune ->
    token := Token { first = 'x', second = '🚀' }
    token.first = '芋'
    return token.first

struct_second :: proc() Rune ->
    token := Token { first = 'x', second = '🚀' }
    return token.second

/// An array of runes, and a for statement over it, which counts the characters that are above 'l',
/// and an element that is assigned.
array_of_runes :: proc() Int ->
    word := [const]Rune { 'h', 'e', 'l', 'l', 'o', '!' }
    above := 0
    for character in word ->
        if character > 'l' ->
            above += 1
    word[0] = 'z'
    if word[0] > 'm' ->
        above += 10
    return above

/// A pointer to a rune is followed and assigned through.
pointer_to_rune :: proc() Rune ->
    character := 'a'
    pointer := %character
    pointer^ = '🚀'
    return character

default_rune :: proc() Rune ->
    character : Rune
    return character

/// The escape sequences of the language. The
/// control characters have a letter each.
new_line :: proc() Rune ->
    newline: Rune = '\n'
    return newline

control_characters :: proc(which: Int) Rune ->
    if which == 0 ->
        return '\a'
    if which == 1 ->
        return '\b'
    if which == 2 ->
        return '\e'
    if which == 3 ->
        return '\f'
    if which == 4 ->
        return '\n'
    if which == 5 ->
        return '\r'
    if which == 6 ->
        return '\t'
    return '\v'

/// A backslash and the two quotes are written with a backslash too.
quotes_and_backslash :: proc(which: Int) Rune ->
    if which == 0 ->
        return '\\'
    if which == 1 ->
        return '\''
    return '\"'

/// A character is written with its number in octal, in two digits of hexadecimal, in four or in
/// eight. They are all the same rune, A, whatever the way that it is written.
numbers :: proc() Bool ->
    return '\101' == 'A' and '\x41' == 'A' and '\u0041' == 'A' and '\U00000041' == 'A'

unicode_character :: proc() Rune ->
    unicode_char: Rune = '\u0123'
    return unicode_char

/// Four bytes of a character are eight digits of hexadecimal, and the digits are of either case.
rocket_escaped :: proc() Rune ->
    return '\U0001F680'

digits_of_either_case :: proc() Bool ->
    return '\u00e9' == '\u00E9' and '\u00e9' == 'é'

/// The character with the code point 0 is written with an octal or a hexadecimal number.
zero_characters :: proc() Bool ->
    return '\000' == '\x00' and '\x00' == '\u0000'

/// A rune is cast to an Int, which is its code point.
code_point :: proc(character: Rune) Int ->
    return cast(Int)character

/// A Byte is cast to a Rune, which is the character of the first 256 with its number.
byte_to_rune :: proc(number: Int) Rune ->
    return cast(Rune)int_to_byte(number)

/// A rune is cast to 4 bytes, the UTF-8 encoding of the character with zeros after it, which are
/// returned as one Int, so that JavaScript sees all four: the first byte is the highest.
encoded :: proc(character: Rune) Int ->
    bytes := cast([4]Byte)character
    return cast(Int)bytes[0] * 16777216 + cast(Int)bytes[1] * 65536 + cast(Int)bytes[2] * 256 + cast(Int)bytes[3]

/// The number of bytes of the encoding of a character, which are the ones before the first zero.
encoded_length :: proc(character: Rune) Int ->
    bytes := cast([4]Byte)character
    length := 0
    for byte in bytes ->
        if byte not= 0 ->
            length += 1
    return length

/// The array has a length, and is passed as an array slice.
encoded_array_length :: proc() Int ->
    bytes := cast([4]Byte)'🚀'
    return length_of(bytes)

sum_of_bytes :: proc(bytes: []Byte) Int ->
    sum := 0
    for byte in bytes ->
        sum += cast(Int)byte
    return sum

encoded_sum :: proc(character: Rune) Int ->
    bytes := cast([4]Byte)character
    return sum_of_bytes(bytes)

/// A character is cast to bytes and the first one is cast back: the first byte of an ASCII
/// character is the character itself.
round_trip :: proc(character: Rune) Rune ->
    bytes := cast([4]Byte)character
    return cast(Rune)bytes[0]

/// More than one array of bytes, each in a place of its own.
two_encodings :: proc() Int ->
    first := cast([4]Byte)'é'
    second := cast([4]Byte)'🚀'
    return cast(Int)first[0] * 256 + cast(Int)first[1] + cast(Int)second[3]
