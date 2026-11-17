// `I32` is a 32-bit integer with a sign, from -2147483648 to 2147483647. It is a type of its own: it
// is not converted implicitly, so an `Int` is not an `I32`, and the arithmetic of two of them is
// an `I32` that wraps around in 32 bits, where an `Int` wraps in 64. A literal that fits is an
// `I32` where one is wanted. The procedures of the C library that take and give an `int` are the
// ones that it is for. The exit code of the program is the number of wrong results, 0.

package integers_32

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...
    abs :: proc(number: I32) I32 -> ...
    atoi :: proc(text: C_Str) I32 -> ...

LIMIT : I32 : 2147483647
SMALLEST : I32 : -2147483648
STEP :: 1000

Counter :: struct ->
    count: I32
    step: I32

global_counter #(static) : I32 = 7

/// Prints a line with the number, and counts it as wrong when it is not what was expected.
check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

add :: proc(a: I32, b: I32) I32 ->
    return a + b

/// The number of an `I32`, which is what `check` takes.
as_int :: proc(number: I32) Int ->
    return cast(Int)number

/// A `Bool` as 1 or 0.
flag :: proc(is_true: Bool) Int ->
    if is_true ->
        return 1
    return 0

main :: proc() Int ->
    wrong := 0

    // Literals and constants.
    a : I32 = 40
    b := add(a, 2)
    wrong += check("40 + 2 is %lld", cast(Int)b, 42)
    wrong += check("LIMIT is %lld", cast(Int)LIMIT, 2147483647)
    wrong += check("SMALLEST is %lld", cast(Int)SMALLEST, -2147483648)
    c : I32 = STEP
    wrong += check("STEP is %lld", cast(Int)c, 1000)
    wrong += check("global is %lld", cast(Int)global_counter, 7)

    // Arithmetic wraps around in 32 bits, and a result is kept as the number with its sign.
    top : I32 = LIMIT
    over := top + 1
    wrong += check("LIMIT + 1 is %lld", cast(Int)over, -2147483648)
    under : I32 = SMALLEST
    below := under - 1
    wrong += check("SMALLEST - 1 is %lld", cast(Int)below, 2147483647)
    big : I32 = 65536
    square := big * big
    wrong += check("65536 * 65536 is %lld", cast(Int)square, 0)
    product : I32 = 46341
    product = product * product
    wrong += check("46341 * 46341 wraps to %lld", cast(Int)product, -2147479015)

    // Division goes toward zero, and the order is the one of the numbers with their signs.
    seven : I32 = 7
    minus : I32 = -7
    wrong += check("7 / 2 is %lld", as_int(seven / 2), 3)
    wrong += check("-7 / 2 is %lld", as_int(minus / 2), -3)
    wrong += check("-7 + 7 is %lld", as_int(minus + seven), 0)
    is_less := minus < seven
    is_more := minus > seven
    wrong += check("-7 < 7 is %lld", flag(is_less), 1)
    wrong += check("-7 > 7 is %lld", flag(is_more), 0)
    wrong += check("-7 == -7 is %lld", flag(minus == -7), 1)
    wrong += check("7 not= 8 is %lld", flag(seven not= 8), 1)
    wrong += check("LIMIT >= 1 is %lld", flag(top >= 1), 1)
    wrong += check("SMALLEST <= 1 is %lld", flag(under <= 1), 1)

    // Compound assignment, also of a field and an element.
    n : I32 = 10
    n += 5
    n -= 3
    n *= 4
    n /= 6
    wrong += check("compound is %lld", cast(Int)n, 8)
    counter := Counter { count = 1, step = 2 }
    counter.count += counter.step
    counter.count *= 10
    wrong += check("field is %lld", cast(Int)counter.count, 30)
    values := [const]I32 { 5, 6, 7 }
    values[1] += 10
    wrong += check("element is %lld", cast(Int)values[1], 16)
    sum : I32 = 0
    for value in values ->
        sum += value
    wrong += check("sum is %lld", cast(Int)sum, 28)
    pointer := %n
    pointer^ += 1
    wrong += check("through a pointer is %lld", cast(Int)n, 9)

    // Casts: an I32 is made an Int, a float and an I32 of a Byte or a Rune, and an Int needs a
    // check, which int_to_i32 makes.
    wide := cast(Int)under
    wrong += check("cast to Int is %lld", wide, -2147483648)
    byte_value : Byte = 200
    wrong += check("cast of a Byte is %lld", as_int(cast(I32)byte_value), 200)
    wrong += check("cast of a Rune is %lld", as_int(cast(I32)'A'), 65)
    narrow := int_to_i32(123456)
    wrong += check("int_to_i32 is %lld", cast(Int)narrow, 123456)
    negative := int_to_i32(0 - 5)
    wrong += check("int_to_i32 of a negative is %lld", cast(Int)negative, -5)
    edge := int_to_i32(2147483647)
    wrong += check("int_to_i32 of the largest is %lld", cast(Int)edge, 2147483647)
    low_edge := int_to_i32(0 - 2147483648)
    wrong += check("int_to_i32 of the smallest is %lld", cast(Int)low_edge, -2147483648)
    real := cast(F64)seven / 2.0
    scaled_real := real * 10.0
    scaled := cast(Int)scaled_real
    wrong += check("float of an I32 is %lld", scaled, 35)

    // The C library: an `int` goes in and comes out as an I32, also when it is negative.
    wrong += check("abs of -5 is %lld", cast(Int)abs(-5), 5)
    wrong += check("abs of a variable is %lld", cast(Int)abs(minus), 7)
    wrong += check("atoi of -12 is %lld", cast(Int)atoi("-12"), -12)
    wrong += check("atoi sign is %lld", as_int(atoi("-12") + 20), 8)
    return wrong
