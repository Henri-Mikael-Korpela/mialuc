package string_subscript

/// A string is bytes, but it is subscripted by the character, which is a code point and not a byte:
/// `s[0]` of "äiti" is the `Rune` 'ä', which takes two bytes, and `s[1]` is 'i'.
first_character :: proc() Rune ->
    s := "äiti"
    return s[0]

second_character :: proc() Rune ->
    s := "äiti"
    return s[1]

/// A character can take 1, 2, 3 or 4 bytes. The code points are 97, 233, 8364, 128640 and 122.
character_at :: proc(index: Int) Rune ->
    s := "aé€🚀z"
    return s[index]

/// `length_of` of a string is its number of characters, and not its number of bytes.
character_count :: proc() Int ->
    s := "aé€🚀z"
    return length_of(s)

is_empty :: proc() Bool ->
    nothing : Str
    return length_of(nothing) == 0
