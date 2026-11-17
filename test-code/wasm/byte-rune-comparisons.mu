package byte_rune_comparisons

/// A `Byte` is compared with `==` and `not=` to a rune that is known when the program is compiled and
/// is in the ASCII range, from 0 to 127, which is one byte in UTF-8. So a character is looked for in
/// bytes, such as the new line `'\n'`, which is a rune that is written as an escape sequence.
is_new_line :: proc(b: Byte) Bool ->
    return b == '\n'

is_not_tab :: proc(b: Byte) Bool ->
    return b not= '\t'

/// The rune can come first.
is_letter_a :: proc(b: Byte) Bool ->
    return 'A' == b

/// Bytes that were read, and the line ends that are among them.
count_line_ends :: proc() Int ->
    bytes := [const]Byte { 72, 105, 13, 10, 33, 10 }
    count := 0
    for b in bytes ->
        if b == '\n' or b == '\r' ->
            count += 1
    return count

/// The last of the range, and the escapes that write a number.
is_delete :: proc(b: Byte) Bool ->
    return b == '\x7F'

is_null :: proc(b: Byte) Bool ->
    return b == '\000'
