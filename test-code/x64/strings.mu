// `mu.try_bytes_to_str_ascii` of `mu:strings` makes a `Str` of bytes of the ASCII range, and says
// that it could, or gives the empty string and `false` when a byte is not of the range. The exit code
// of the program is the number of wrong results, 0.

package strings

import "mu:strings"

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

flag :: proc(value: Bool) Int ->
    if value ->
        return 1
    return 0

main :: proc() Int ->
    wrong := 0
    word := [4]Byte { 119, 97, 115, 109 }
    text, is_ascii := mu.try_bytes_to_str_ascii(word[:])
    wrong += check("wasm is ASCII: %lld", flag(is_ascii), 1)
    wrong += check("and has %lld characters", length_of(text), 4)
    wrong += check("and is wasm: %lld", flag(text == "wasm"), 1)

    part, is_part := mu.try_bytes_to_str_ascii(word[1:3])
    wrong += check("a part of the bytes is ASCII: %lld", flag(is_part), 1)
    wrong += check("and is as: %lld", flag(part == "as"), 1)

    none, is_none := mu.try_bytes_to_str_ascii(word[0:0])
    wrong += check("no bytes are ASCII: %lld", flag(is_none), 1)
    wrong += check("and are the empty string: %lld", length_of(none), 0)

    edge := [2]Byte { 127, 0 }
    edge_text, is_edge := mu.try_bytes_to_str_ascii(edge[:])
    wrong += check("127 and 0 are ASCII: %lld", flag(is_edge), 1)
    wrong += check("and are %lld characters", length_of(edge_text), 2)

    accent := [3]Byte { 99, 97, 195 }
    accent_text, is_accent := mu.try_bytes_to_str_ascii(accent[:])
    wrong += check("a byte of 195 is not ASCII: %lld", flag(is_accent), 0)
    wrong += check("and the text is empty: %lld", length_of(accent_text), 0)

    limit := [1]Byte { 128 }
    limit_text, is_limit := mu.try_bytes_to_str_ascii(limit[:])
    wrong += check("a byte of 128 is not ASCII: %lld", flag(is_limit), 0)
    return wrong
