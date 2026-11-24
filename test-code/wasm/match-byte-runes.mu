package match_byte_runes

/// A `Byte` is matched with runes of ASCII as well as with numbers, as it is compared with `==`: a
/// rune in the range from 0 to 127 is the one byte that it is in UTF-8, so a range of characters is
/// a range of bytes.
is_name_byte :: proc(value: Byte) Bool ->
    match value ->
        case '0'..<='9' ->
            return true
        case 'A'..<='Z' ->
            return true
        case 'a'..<='z' ->
            return true
        case '-' ->
            return true
        case '_' ->
            return true
        case ->
            return false

LETTER_A :: 'a'

/// A constant rune is a pattern, an end of a range can be a number, and cases share a block.
kind_of :: proc(value: Byte) Int ->
    match value ->
        case '\n' ->
        case '\r' ->
            return 1
        case ' '..<'0' ->
            return 2
        case LETTER_A ->
            return 3
        case 'b'..<=122 ->
            return 4
        case 128..<=255 ->
            return 5
        case ->
            return 0

/// Counts the bytes of a name that are allowed in it, which are 6 in `a-b_Z9 !`.
count_name_bytes :: proc() Int ->
    bytes := [const]Byte { 97, 45, 98, 95, 90, 57, 32, 33 }
    count := 0
    for b in bytes ->
        if is_name_byte(b) ->
            count += 1
    return count

kinds :: proc() Int ->
    total := kind_of(10) + kind_of(13) * 10 + kind_of(32) * 100 + kind_of(97) * 1000
    total += kind_of(99) * 10000 + kind_of(200) * 100000 + kind_of(65) * 1000000
    return total
