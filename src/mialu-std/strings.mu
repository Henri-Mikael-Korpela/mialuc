/// Strings: making a `Str` of bytes. They work on every target, as they only use the language itself.
package mu

/// Makes a `Str` of `bytes`, which is a text of the ASCII range only, and says whether it could: the
/// second value is `false`, and the `Str` is empty, when a byte is 128 or more, which is not a
/// character of the ASCII range.
try_bytes_to_str_ascii :: proc(bytes: []Byte) (Str, Bool) ->
    for value in bytes ->
        if value > 127 ->
            return "", false
    // Every character of the ASCII range is one byte of UTF-8, so these bytes are valid.
    text, is_valid := utf8_decode(bytes)
    return text, is_valid
