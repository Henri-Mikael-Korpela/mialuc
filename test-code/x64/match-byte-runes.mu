// A `Byte` is matched with runes of ASCII as well as with numbers, as it is compared with `==`: a
// rune in the range from 0 to 127 is the one byte that it is in UTF-8, so a range of characters is a
// range of bytes. The exit code of the program is the number of wrong results, 0.

package match_byte_runes

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

LETTER_A :: 'a'

/// Prints a line with the number, and counts it as wrong when it is not what was expected.
check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

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

main :: proc() Int ->
    wrong := 0
    bytes := [const]Byte { 97, 45, 98, 95, 90, 57, 32, 33 }
    count := 0
    for b in bytes ->
        if is_name_byte(b) ->
            count += 1
    wrong += check("name bytes are %lld", count, 6)
    wrong += check("a line feed is %lld", kind_of(10), 1)
    wrong += check("a carriage return is %lld", kind_of(13), 1)
    wrong += check("a space is %lld", kind_of(32), 2)
    wrong += check("a is %lld", kind_of(97), 3)
    wrong += check("c is %lld", kind_of(99), 4)
    wrong += check("200 is %lld", kind_of(200), 5)
    wrong += check("A is %lld", kind_of(65), 0)
    return wrong
