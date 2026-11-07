// String constants and procedures that take and return strings. Strings are UTF-8, so the
// last constant has characters that take more than one byte.

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
