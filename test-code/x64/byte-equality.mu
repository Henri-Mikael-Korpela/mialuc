package byte_equality

import "mu:slices"

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

/// Two `Byte` values are compared with `==` and `not=`, whether they are variables, elements of an
/// array, parameters or typed constants that a cast of a rune made. The exit code of the program is
/// the number of wrong results, 0.
NEWLINE : Byte : cast(Byte)'\n'
SPACE : Byte : cast(Byte)' '

check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

flag :: proc(is_true: Bool) Int ->
    if is_true ->
        return 1
    return 0

same :: proc(first: Byte, second: Byte) Bool ->
    return first == second

differ :: proc(first: Byte, second: Byte) Bool ->
    return first not= second

main :: proc() Int ->
    wrong := 0
    bytes := [4]Byte { 200, 200, 100, 0 }
    wrong += check("200 is 200: %lld", flag(bytes[0] == bytes[1]), 1)
    wrong += check("200 is not 100: %lld", flag(bytes[0] == bytes[2]), 0)
    wrong += check("200 is different from 100: %lld", flag(bytes[0] not= bytes[2]), 1)
    wrong += check("200 is not different from 200: %lld", flag(bytes[0] not= bytes[1]), 0)
    wrong += check("0 is 0: %lld", flag(bytes[3] == bytes[3]), 1)
    wrong += check("same parameters: %lld", flag(same(bytes[0], bytes[1])), 1)
    wrong += check("different parameters: %lld", flag(differ(bytes[0], bytes[2])), 1)
    line := [5]Byte { 72, 105, 32, 10, 33 }
    wrong += check("the first space is at %lld", mu.find_first_index_of(line[:], SPACE), 2)
    wrong += check("the first new line is at %lld", mu.find_first_index_of(line[:], NEWLINE), 3)
    wrong += check("a byte that is not there is at %lld", mu.find_first_index_of(line[:], 0), -1)
    wrong += check("a constant is a byte: %lld", flag(line[3] == NEWLINE), 1)
    return wrong
