// String constants and procedures that take and return strings. Strings are UTF-8, so the
// last constant has characters that take more than one byte.

package string_constants

/// A string constant. The module holds its bytes, and its name is replaced by them.
greeting :: "Hello, world"

/// Returns the string that it is given.
echo :: proc(text: Str) Str ->
    return text

/// Returns the last of its parameters, to show that strings are passed in any position.
second :: proc(count: Int, text: Str, other: Str) Str ->
    return other

/// Returns a string without characters.
empty :: proc() Str ->
    return ""

/// A string constant of characters that are 2, 3 and 4 bytes long in UTF-8.
non_ascii :: "héllo, 日本語, 😀"

/// A rune constant, which `++` can join to a string.
exclamation :: '!'

/// Strings and runes are joined with `++` when the program is compiled, so the module has the
/// joined text and nothing is joined when it runs. A rune can be on either side, and the result is
/// a string, so it can be joined again.
excited :: greeting ++ exclamation ++ " " ++ '🚀'

/// A constant that has the type `Str` can be joined too. Characters of any size are written as
/// their UTF-8 bytes.
framed : Str : '«' ++ excited ++ '»' ++ non_ascii

/// Joins where it is used, as in a constant.
framed_again :: proc() Str ->
    return "<" ++ framed ++ '>'
