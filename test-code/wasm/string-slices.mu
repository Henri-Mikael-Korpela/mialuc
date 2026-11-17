package string_slices

/// A slice of a string, `s[low:high]`, is another string: the characters from `low` up to, and not
/// including, `high`. Unlike the subscript, which gives one character, it makes a string, and it
/// does not copy: the string views bytes of the one that it is a slice of.
hello :: proc() Str ->
    text := "Hello, World!"
    return text[0:5]

world :: proc() Str ->
    text := "Hello, World!"
    return text[7:12]

/// The bounds are characters, and not bytes: ä takes two bytes.
without_first :: proc() Str ->
    s := "äiti"
    return s[1:4]

with_first :: proc() Str ->
    s := "äiti"
    return s[0:2]

/// A slice that has no characters is the empty string.
empty :: proc() Int ->
    text := "Hello"
    return length_of(text[2:2])

/// A slice of a slice, and a slice that is subscripted.
inner :: proc() Str ->
    text := "Hello, World!"
    return text[7:13][1:4]

third_character_of_world :: proc() Rune ->
    text := "Hello, World!"
    return text[7:12][2]

/// A bound outside of the string traps, as an index outside of an array does.
past_the_end :: proc(high: Int) Str ->
    text := "Hello"
    return text[0:high]
