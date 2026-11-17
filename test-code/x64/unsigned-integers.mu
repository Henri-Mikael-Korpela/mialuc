// `U32` and `U64` are unsigned integers of 32 and 64 bits, and types of their own: nothing is
// converted to them or from them implicitly. Their arithmetic wraps around in their bits, a
// division and a comparison are the unsigned ones, a literal is one where one is wanted, and a
// constant can have the type. They are the `unsigned int`, `DWORD` and `size_t` of the C library,
// when a procedure should say so. The exit code of the program is the number of wrong results, 0.

package unsigned_integers

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...
    _rotl :: proc(number: U32, shift: I32) U32 -> ...
    strlen :: proc(text: C_Str) U64 -> ...

HIGHEST : U32 : 4294967295
BIG : U64 : 9223372036854775807
STEP :: 1000

Packet :: struct ->
    size: U32
    offset: U64

/// Prints a line with the number, and counts it as wrong when it is not what was expected.
check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

as_int :: proc(number: U32) Int ->
    return cast(Int)number

as_int_64 :: proc(number: U64) Int ->
    return cast(Int)number

flag :: proc(is_true: Bool) Int ->
    if is_true ->
        return 1
    return 0

add :: proc(a: U32, b: U32) U32 ->
    return a + b

main :: proc() Int ->
    wrong := 0

    // Literals and constants.
    a : U32 = 40
    wrong += check("40 + 2 is %lld", as_int(add(a, 2)), 42)
    wrong += check("HIGHEST is %lld", as_int(HIGHEST), 4294967295)
    wrong += check("BIG is %lld", as_int_64(BIG), 9223372036854775807)
    step : U32 = STEP
    wrong += check("STEP is %lld", as_int(step), 1000)

    // The arithmetic wraps around in 32 bits, and the number is never negative.
    highest : U32 = HIGHEST
    zero : U32 = 0
    wrong += check("HIGHEST + 1 wraps to %lld", as_int(highest + 1), 0)
    wrong += check("0 - 1 wraps to %lld", as_int(zero - 1), 4294967295)
    wrong += check("HIGHEST * 2 wraps to %lld", as_int(highest * 2), 4294967294)

    // The order and the division are unsigned: 3000000000 is above 5, and half of it is 1500000000.
    large : U32 = 3000000000
    small : U32 = 5
    wrong += check("3000000000 > 5 is %lld", flag(large > small), 1)
    wrong += check("5 < 3000000000 is %lld", flag(small < large), 1)
    wrong += check("3000000000 / 2 is %lld", as_int(large / 2), 1500000000)
    wrong += check("3000000000 >= HIGHEST is %lld", flag(large >= highest), 0)

    // The same in 64 bits: twice the largest number of an Int is still a number, which is above it.
    top : U64 = BIG
    twice := top + top
    wrong += check("BIG + BIG > BIG is %lld", flag(twice > top), 1)
    wrong += check("(BIG + BIG) / 2 is %lld", as_int_64(twice / 2), 9223372036854775807)
    zero_64 : U64 = 0
    wrong += check("0 - 1 > BIG is %lld", flag(zero_64 - 1 > top), 1)
    wrong += check("0 - 1 <= BIG is %lld", flag(zero_64 - 1 <= top), 0)

    // The operators of assignment.
    n : U32 = 10
    n += 5
    n -= 3
    n *= 4
    n /= 6
    wrong += check("10 + 5 - 3 * 4 / 6 is %lld", as_int(n), 8)
    m : U64 = 10
    m -= 11
    wrong += check("10 - 11 > BIG is %lld", flag(m > top), 1)
    m /= 2
    wrong += check("(10 - 11) / 2 is %lld", as_int_64(m), 9223372036854775807)

    // Casts: a negative number is the number that has its bits, a Byte and a Rune always fit in a
    // U32, and a U32 is an Int or a float.
    minus : Int = -1
    wrong += check("cast(U64)-1 is %lld", as_int_64(cast(U64)minus), -1)
    wrong += check("cast(U64)-1 > BIG is %lld", flag(cast(U64)minus > top), 1)
    byte_value : Byte = 200
    wrong += check("cast(U32) of a Byte is %lld", as_int(cast(U32)byte_value), 200)
    wrong += check("cast(U32) of a Rune is %lld", as_int(cast(U32)'A'), 65)
    wrong += check("cast(Int) of a U32 is %lld", cast(Int)large, 3000000000)
    as_float := cast(F64)large
    wrong += check("cast(F64) of a U32 is %lld", cast(Int)as_float, 3000000000)

    // Fields of a struct, and procedures of the C library that take and give them.
    packet := Packet { size = 3000000000, offset = 9000000000 }
    packet.size += 1
    packet.offset /= 3
    wrong += check("a packet has the size %lld", as_int(packet.size), 3000000001)
    wrong += check("a packet has the offset %lld", as_int_64(packet.offset), 3000000000)
    wrong += check("1 rotated by 31 bits is %lld", as_int(_rotl(1, 31)), 2147483648)
    wrong += check("2147483648 rotated by 1 bit is %lld", as_int(_rotl(2147483648, 1)), 1)
    wrong += check("strlen is %lld", as_int_64(strlen("hello")), 5)

    return wrong
