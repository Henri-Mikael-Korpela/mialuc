// Floats: `F32` is a 32-bit floating point number, written as a float literal such as `1.42`, and
// an operation of floats follows IEEE 754. The C library of the platform has no way to print an F32
// here, because `printf` takes a double, so each result is multiplied by 1000 and printed as the
// Int that a cast makes of it, see doubles.mu for printing an F64. Procedures of the C library that take and give a `float` are
// declared with F32, as `sqrtf` is. The exit code of the program is the number of results that are
// not what they should be, so 0 means that all of them are.

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...
    sqrtf :: proc(x: F32) F32 -> ...
    powf :: proc(base: F32, exponent: F32) F32 -> ...

HALF :: 0.5

/// The initial value of a global variable is a constant, such as a float literal.
total #(static) : F32 = 0.25

Point :: struct ->
    x: F32
    y: F32

scale :: proc(x: F32, factor: F32) F32 ->
    return x * factor + 1.0

arithmetic :: proc() F32 ->
    a : F32 = 1.5
    b : F32 = 2.25
    a += b
    a *= 2.0
    a -= 0.75
    a /= 4.0
    return a + b - HALF

compare :: proc(a: F32, b: F32) Int ->
    result := 0
    if a < b ->
        result += 1
    if a <= b ->
        result += 2
    if a == b ->
        result += 4
    if a not= b ->
        result += 8
    if a > 0.5 ->
        result += 16
    if a >= b ->
        result += 32
    if a > b ->
        result += 64
    return result

/// Dividing zero by zero gives not a number, which is not equal to anything, itself included.
not_a_number :: proc() Int ->
    zero : F32 = 0.0
    nan := zero / zero
    result := 0
    if nan == nan ->
        result += 1
    if nan not= nan ->
        result += 2
    if nan < 1.0 ->
        result += 4
    if nan >= 1.0 ->
        result += 8
    return result

add_to_total :: proc(amount: F32) F32 ->
    total += amount
    return total

struct_fields :: proc() F32 ->
    p := Point { x = 1.25, y = 2.5 }
    p.x += 1.0
    p.y *= 2.0
    return p.x + p.y

array_of_floats :: proc() F32 ->
    values := [const]F32 { 1.5, 2.5, 3.0 }
    sum : F32 = 0.0
    for value in values ->
        sum += value
    values[0] = 10.0
    values[1] += 0.5
    return sum + values[0] + values[1]

pointer_to_float :: proc() F32 ->
    f : F32 = 3.5
    p := %f
    p^ += 1.0
    p^ = p^ * 2.0
    return f

apply :: proc(operation: ^proc(F32, F32) F32, a: F32, b: F32) F32 ->
    return operation(a, b)

min_max :: proc(a: F32, b: F32) (F32, F32) ->
    if a < b ->
        return a, b
    return b, a

/// Prints `label` and the value times 1000 as an Int, and gives 1 when it is not `expected`.
check :: proc(label: C_Str, value: F32, expected: Int) Int ->
    thousandths := value * 1000.0
    number := cast(Int)thousandths
    printf(label, number)
    puts(str_to_c_str(""))
    if number == expected ->
        return 0
    return 1

check_int :: proc(label: C_Str, number: Int, expected: Int) Int ->
    printf(label, number)
    puts(str_to_c_str(""))
    if number == expected ->
        return 0
    return 1

main :: proc() Int ->
    failures := 0
    failures += check(str_to_c_str("literal 1.42, 1420: %lld"), 1.42, 1420)
    failures += check(str_to_c_str("literal -0.5, -500: %lld"), -0.5, -500)
    failures += check(str_to_c_str("scale(3, 4), 13000: %lld"), scale(3.0, 4.0), 13000)
    failures += check(str_to_c_str("arithmetic, 3437: %lld"), arithmetic(), 3437)
    failures += check(str_to_c_str("1.5 + 2.25 * 2.0, 6000: %lld"), 1.5 + 2.25 * 2.0, 6000)
    failures += check_int(str_to_c_str("compare(1, 2), 27: %lld"), compare(1.0, 2.0), 27)
    failures += check_int(str_to_c_str("compare(2, 2), 54: %lld"), compare(2.0, 2.0), 54)
    failures += check_int(str_to_c_str("compare(3, 1), 120: %lld"), compare(3.0, 1.0), 120)
    failures += check_int(str_to_c_str("not a number, 2: %lld"), not_a_number(), 2)
    first := add_to_total(1.5)
    failures += check(str_to_c_str("global variable, 1750: %lld"), first, 1750)
    failures += check(str_to_c_str("global variable again, 3250: %lld"), add_to_total(1.5), 3250)
    failures += check(str_to_c_str("cast of an Int, 3000: %lld"), cast(F32)3, 3000)
    failures += check_int(str_to_c_str("cast of an F32, -3: %lld"), cast(Int)-3.75, -3)
    failures += check(str_to_c_str("struct fields, 7250: %lld"), struct_fields(), 7250)
    failures += check(str_to_c_str("array of floats, 20000: %lld"), array_of_floats(), 20000)
    failures += check(str_to_c_str("pointer to a float, 9000: %lld"), pointer_to_float(), 9000)
    applied := apply(scale, 3.0, 4.0)
    failures += check(str_to_c_str("pointer to a procedure, 13000: %lld"), applied, 13000)
    low, high := min_max(9.5, 2.25)
    failures += check(str_to_c_str("two floats returned, 7250: %lld"), high - low, 7250)
    failures += check(str_to_c_str("sqrtf of the C library, 4000: %lld"), sqrtf(16.0), 4000)
    failures += check(str_to_c_str("powf of the C library, 8000: %lld"), powf(2.0, 3.0), 8000)
    return failures
