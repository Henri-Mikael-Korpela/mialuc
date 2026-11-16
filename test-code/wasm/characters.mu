// Characters: a `Rune` is a Unicode character, which is the code point of the character, a 32-bit
// number from 0 to 0x10FFFF, and a rune literal is one character between single quotes. The source
// is UTF-8, so a character can be a letter of any alphabet, a kanji or an emoji, and it is one rune,
// whatever number of bytes it takes. A rune has an order, the order of the code points, but no
// arithmetic. In WebAssembly it is a 32-bit integer, which JavaScript passes and receives as a
// plain number, the code point, and `String.fromCodePoint` makes the character of it.

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
