package utf8_decoding

/// The built-in procedure `utf8_decode` makes a `Str` of an array or an array slice of bytes, and
/// says whether the bytes are valid UTF-8. The string is the same bytes, and not a copy. The bytes
/// here are "aé€🚀z".
decode_mixed :: proc() Str ->
    bytes := [const]Byte { 97, 0xC3, 0xA9, 0xE2, 0x82, 0xAC, 0xF0, 0x9F, 0x9A, 0x80, 122 }
    text, is_ok := utf8_decode(bytes)
    return text

/// The characters of the string, which is 5 though the bytes are 11.
count_mixed :: proc() Int ->
    bytes := [const]Byte { 97, 0xC3, 0xA9, 0xE2, 0x82, 0xAC, 0xF0, 0x9F, 0x9A, 0x80, 122 }
    text, is_ok := utf8_decode(bytes)
    return length_of(text)

is_mixed_valid :: proc() Bool ->
    bytes := [const]Byte { 97, 0xC3, 0xA9, 0xE2, 0x82, 0xAC, 0xF0, 0x9F, 0x9A, 0x80, 122 }
    text, is_ok := utf8_decode(bytes)
    return is_ok

/// A byte that no character starts with, 0xFF, is not valid, and then the string is empty.
is_ff_valid :: proc() Bool ->
    bytes := [const]Byte { 97, 0xFF, 98 }
    text, is_ok := utf8_decode(bytes)
    return is_ok

count_after_failure :: proc() Int ->
    bytes := [const]Byte { 97, 0xFF, 98 }
    text, is_ok := utf8_decode(bytes)
    return length_of(text)

/// The characters that could be written in fewer bytes, the surrogates, the ones above U+10FFFF and
/// the ones that are cut off by the end are not valid.
is_overlong_valid :: proc() Bool ->
    bytes := [const]Byte { 0xC0, 0x80 }
    text, is_ok := utf8_decode(bytes)
    return is_ok

is_surrogate_valid :: proc() Bool ->
    bytes := [const]Byte { 0xED, 0xA0, 0x80 }
    text, is_ok := utf8_decode(bytes)
    return is_ok

is_too_large_valid :: proc() Bool ->
    bytes := [const]Byte { 0xF4, 0x90, 0x80, 0x80 }
    text, is_ok := utf8_decode(bytes)
    return is_ok

is_cut_off_valid :: proc() Bool ->
    bytes := [const]Byte { 0xE2, 0x82 }
    text, is_ok := utf8_decode(bytes)
    return is_ok

/// The largest character, U+10FFFF, is valid, and so are no bytes at all.
is_largest_valid :: proc() Bool ->
    bytes := [const]Byte { 0xF4, 0x8F, 0xBF, 0xBF }
    text, is_ok := utf8_decode(bytes)
    return is_ok

is_nothing_valid :: proc() Bool ->
    bytes := [const]Byte { }
    text, is_ok := utf8_decode(bytes)
    return is_ok

/// A slice of the bytes is decoded too: the bytes of "é€", and a slice that cuts a character.
decode_middle :: proc() Str ->
    bytes := [const]Byte { 97, 0xC3, 0xA9, 0xE2, 0x82, 0xAC, 122 }
    text, is_ok := utf8_decode(bytes[1:6])
    return text

is_cut_slice_valid :: proc() Bool ->
    bytes := [const]Byte { 97, 0xC3, 0xA9, 0xE2, 0x82, 0xAC, 122 }
    text, is_ok := utf8_decode(bytes[1:5])
    return is_ok
