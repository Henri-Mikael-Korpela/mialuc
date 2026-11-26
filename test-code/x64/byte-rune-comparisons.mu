// A `Byte` compared with a rune with `==` and `not=`: the rune is known when the program is compiled
// and is in the ASCII range, from 0 to 127, which is one byte in UTF-8, so the two are the same when
// the byte has the number of the character. That is the way to look for a character in bytes that
// are read, such as the new line `'\n'`, which is a rune that is written as an escape sequence. The
// exit code of the program is the number of wrong results, 0.

package byte_rune_comparisons

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

LETTER :: 'k'
LINE_FEED : Byte : 10

/// Prints a line with the number, and counts it as wrong when it is not what was expected.
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

is_new_line :: proc(b: Byte) Bool ->
    return b == '\n'

/// A rune is given to a `Byte` parameter as the byte that it is.
is_byte :: proc(b: Byte, wanted: Byte) Bool ->
    return b == wanted

/// A polymorphic procedure whose type parameter is a `Byte` by the other argument.
find_first :: proc(elements: []$T, target: T) Int ->
    for index in 0..<length_of(elements) ->
        if elements[index] == target ->
            return index
    return -1

main :: proc() Int ->
    wrong := 0

    // Characters, and the escape sequences, in either order.
    bytes := [const]Byte { 65, 10, 9, 13, 92, 39, 0, 127, 107 }
    wrong += check("A is %lld", flag(bytes[0] == 'A'), 1)
    wrong += check("A not= B is %lld", flag(bytes[0] not= 'B'), 1)
    wrong += check("the new line is %lld", flag(bytes[1] == '\n'), 1)
    wrong += check("the tab is %lld", flag(bytes[2] == '\t'), 1)
    wrong += check("the carriage return is %lld", flag(bytes[3] == '\r'), 1)
    wrong += check("the backslash is %lld", flag(bytes[4] == '\\'), 1)
    wrong += check("the single quote is %lld", flag(bytes[5] == '\''), 1)
    wrong += check("the zero is %lld", flag(bytes[6] == '\000'), 1)
    wrong += check("the last of the range is %lld", flag(bytes[7] == '\x7F'), 1)
    wrong += check("a hexadecimal escape is %lld", flag(bytes[0] == '\x41'), 1)
    wrong += check("a unicode escape is %lld", flag(bytes[0] == 'A'), 1)
    wrong += check("the rune first is %lld", flag('\n' == bytes[1]), 1)
    wrong += check("the rune first, not= is %lld", flag('\n' not= bytes[1]), 0)

    // Characters that are not the byte.
    wrong += check("a new line is not a tab %lld", flag(bytes[1] == '\t'), 0)
    wrong += check("a new line not= a tab %lld", flag(bytes[1] not= '\t'), 1)
    wrong += check("zero is not a new line %lld", flag(bytes[6] == '\n'), 0)
    wrong += check("a rune above the byte is not it %lld", flag(bytes[0] == 'a'), 0)

    // A byte above 127 is not the same as any rune that is in the range.
    high : Byte = 200
    wrong += check("200 is not a new line %lld", flag(high == '\n'), 0)
    wrong += check("200 not= a new line %lld", flag(high not= '\n'), 1)

    // A constant rune, a parameter and a condition.
    wrong += check("a constant is %lld", flag(bytes[8] == LETTER), 1)
    wrong += check("a parameter is %lld", flag(is_new_line(10)), 1)
    wrong += check("a parameter that is not is %lld", flag(is_new_line(11)), 0)
    count := 0
    for b in bytes ->
        if b == '\n' or b == '\r' ->
            count += 1
    wrong += check("line ends are %lld", count, 2)
    if bytes[1] == '\n' and bytes[1] not= '\r' ->
        wrong += check("condition is %lld", 1, 1)
    else ->
        wrong += check("condition is %lld", 0, 1)

    // A rune is given to a Byte parameter, which is the byte that it is, also as a constant and an
    // escape sequence.
    wrong += check("a rune is a Byte parameter %lld", flag(is_byte(bytes[0], 'A')), 1)
    wrong += check("a rune that is not it %lld", flag(is_byte(bytes[0], 'B')), 0)
    wrong += check("an escape sequence is a Byte parameter %lld", flag(is_byte(bytes[1], '\n')), 1)
    wrong += check("a constant rune is a Byte parameter %lld", flag(is_byte(bytes[8], LETTER)), 1)
    wrong += check("the last of the range is a Byte parameter %lld", flag(is_byte(bytes[7], '')), 1)
    wrong += check("the first of a polymorphic search is at %lld", find_first(bytes[:], '\n'), 1)
    wrong += check("a rune that is not there is at %lld", find_first(bytes[:], 'z'), -1)

    // Two constants, which the compiler compares.
    wrong += check("constants are %lld", flag(LINE_FEED == '\n'), 1)
    return wrong
