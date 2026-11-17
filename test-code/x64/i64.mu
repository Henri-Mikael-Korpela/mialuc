// `I64` is a signed 64-bit integer, the size of an `Int`, and a type of its own: nothing is converted
// to it or from it implicitly, so an `Int` is not an `I64`, and the arithmetic of two of them is an
// `I64` that wraps around in 64 bits. A literal is an `I64` where one is wanted. It is the
// `long long`, `int64_t` and `size_t` of the C library, when a procedure should say so, and it is
// cast to and from an `Int`, an `I32`, a `Byte` and a float. The exit code of the program is the
// number of wrong results, 0.

package integers_64

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...
    _abs64 :: proc(number: I64) I64 -> ...
    _atoi64 :: proc(text: C_Str) I64 -> ...
    strlen :: proc(text: C_Str) I64 -> ...

BIG : I64 : 9223372036854775807
LOWEST : I64 : -9223372036854775807 - 1
STEP :: 1000

Account :: struct ->
    balance: I64
    fee: I64

/// Prints a line with the number, and counts it as wrong when it is not what was expected.
check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

as_int :: proc(number: I64) Int ->
    return cast(Int)number

flag :: proc(is_true: Bool) Int ->
    if is_true ->
        return 1
    return 0

add :: proc(a: I64, b: I64) I64 ->
    return a + b

main :: proc() Int ->
    wrong := 0

    // Literals and constants.
    a : I64 = 40
    b := add(a, 2)
    wrong += check("40 + 2 is %lld", as_int(b), 42)
    wrong += check("BIG is %lld", as_int(BIG), 9223372036854775807)
    lowest := as_int(LOWEST)
    wrong += check("LOWEST is %lld", lowest + 9223372036854775807, -1)
    step : I64 = STEP
    wrong += check("STEP is %lld", as_int(step), 1000)
    wrong += check("a large literal is %lld", as_int(add(5000000000, 1)), 5000000001)

    // Arithmetic wraps around in 64 bits.
    top : I64 = BIG
    over := top + 1
    wrapped := as_int(over)
    wrong += check("BIG + 1 is %lld", wrapped + 9223372036854775807, -1)
    bottom : I64 = LOWEST
    below := bottom - 1
    wrong += check("LOWEST - 1 is %lld", as_int(below), 9223372036854775807)

    // Division goes toward zero, and the order is the one of the numbers with their signs.
    seven : I64 = 7
    minus : I64 = -7
    wrong += check("7 / 2 is %lld", as_int(seven / 2), 3)
    wrong += check("-7 / 2 is %lld", as_int(minus / 2), -3)
    wrong += check("-7 * 3 is %lld", as_int(minus * 3), -21)
    wrong += check("-7 < 7 is %lld", flag(minus < seven), 1)
    wrong += check("-7 > 7 is %lld", flag(minus > seven), 0)
    wrong += check("-7 == -7 is %lld", flag(minus == -7), 1)
    wrong += check("7 not= 8 is %lld", flag(seven not= 8), 1)
    wrong += check("BIG >= 1 is %lld", flag(top >= 1), 1)
    wrong += check("LOWEST <= 1 is %lld", flag(bottom <= 1), 1)

    // Compound assignment, also of a field and an element.
    n : I64 = 10
    n += 5
    n -= 3
    n *= 4
    n /= 6
    wrong += check("compound is %lld", as_int(n), 8)
    account := Account { balance = 100, fee = 3 }
    account.balance -= account.fee
    account.balance *= 2
    wrong += check("field is %lld", as_int(account.balance), 194)
    values := [const]I64 { 5000000000, 6, 7 }
    values[1] += 10
    wrong += check("element is %lld", as_int(values[1]), 16)
    sum : I64 = 0
    for value in values ->
        sum += value
    wrong += check("sum is %lld", as_int(sum), 5000000023)
    pointer := %n
    pointer^ += 1
    wrong += check("through a pointer is %lld", as_int(n), 9)

    // Casts: an I64 and an Int are each cast to the other, an I32 and a Byte are cast to an I64,
    // and an I64 is made an I32 by int_to_i32, which traps when the number does not fit.
    whole : Int = 12345678901
    as_i64 := cast(I64)whole
    wrong += check("Int to I64 is %lld", as_int(as_i64), 12345678901)
    back := cast(Int)as_i64
    wrong += check("I64 to Int is %lld", back, 12345678901)
    small : I32 = -9
    wrong += check("I32 to I64 is %lld", as_int(cast(I64)small), -9)
    byte_value : Byte = 200
    wrong += check("Byte to I64 is %lld", as_int(cast(I64)byte_value), 200)
    fits := int_to_i32(seven)
    wrong += check("I64 to I32 is %lld", cast(Int)fits, 7)
    real := cast(F64)minus / 2.0
    scaled_real := real * 10.0
    wrong += check("float of an I64 is %lld", cast(Int)scaled_real, -35)
    from_float := cast(I64)scaled_real
    wrong += check("I64 of a float is %lld", as_int(from_float), -35)

    // The C library: a `long long` and a `size_t` go in and come out as an I64.
    wrong += check("_abs64 of -5 is %lld", as_int(_abs64(-5)), 5)
    wrong += check("_abs64 of a variable is %lld", as_int(_abs64(minus)), 7)
    wrong += check("_atoi64 of a large number is %lld", as_int(_atoi64("-5000000000")), -5000000000)
    wrong += check("strlen is %lld", as_int(strlen("Hello")), 5)
    return wrong
