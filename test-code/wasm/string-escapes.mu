// A string literal can have the escape sequences of a rune literal: a backslash and one of a b e
// f n r t v \ ' or ", three octal digits, x and two hexadecimal digits, u and four, or U and eight.
// Each stands for one character, which is part of the string as its UTF-8 bytes.

package string_escapes

/// The characters that have a letter: tab, new line and carriage return.
spaces :: "one\ttwo\nthree\rfour"

/// The control characters bell, backspace, escape, form feed and vertical tab.
controls :: "\a\b\e\f\v"

/// The characters that end a string or an escape sequence are written with an escape sequence.
quotes :: "say \"hi\", a backslash \\ and a quote \'"

/// The same character in every form: `A` is 65, which is 101 in octal and 41 in hexadecimal.
numbers :: "\101\x41\u0041\U00000041 A"

/// Characters that take more than one byte in UTF-8, written by their code points.
non_ascii :: "\u00e9 \u65e5\u672c \U0001F680"

/// Escape sequences are joined like the other text of a string.
joined :: "a\n" ++ "b\t" ++ 'c'

/// A string that is not a constant has them too.
described :: proc() Str ->
    return "tab:\t, quote:\""

/// The number of characters of a string, which is not the number of its bytes.
character_count :: proc() Int ->
    return length_of("\u00e9\U0001F680\x41")

/// The character at an index of a string that has an escape sequence.
second_character :: proc() Rune ->
    text := "\x41\u00e9"
    return text[1]
