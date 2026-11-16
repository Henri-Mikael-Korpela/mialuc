// F64 is a 64-bit floating point number, and the type of a float literal where nothing says, so
// `x := 1.42` is an F64. A float is passed to the C library as the calling convention of Windows
// says, in a register of floats and in the register of integers of its word, so `printf` takes it
// for `%f`. The exit code of the program is the number of results that are not what they should
// be, so 0 means that all of them are.

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, value: F64) Int -> ...
    puts :: proc(content: C_Str) Int -> ...
    sqrt :: proc(x: F64) F64 -> ...
    pow :: proc(base: F64, exponent: F64) F64 -> ...
    floor :: proc(x: F64) F64 -> ...

Pair :: struct ->
    small: F32
    large: F64

/// A global variable of an F64.
precise #(static) := 0.1

/// 0.1 + 0.2 is 0.30000000000000004 in 64 bits, which is not 0.3.
double_constant_expression :: proc() F64 ->
    return 0.1 + 0.2

arithmetic :: proc(a: F64, b: F64) F64 ->
    c := a * b + 1.0
    c -= 0.5
    c /= 2.0
    return c

/// An F32 is made an F64 exactly, and an F64 is rounded to the nearest F32.
widen :: proc(x: F32) F64 ->
    return cast(F64)x

narrow :: proc(x: F64) F32 ->
    return cast(F32)x

compare :: proc(a: F64) Int ->
    result := 0
    if a < 1.5 ->
        result += 1
    if a == 1.5 ->
        result += 2
    if a > 1.5 ->
        result += 4
    return result

pair_sum :: proc() F64 ->
    p := Pair { small = 1.5, large = 2.25 }
    p.large += cast(F64)p.small
    return p.large

array_of_doubles :: proc() F64 ->
    values := [const]F64 { 0.1, 0.2, 0.3 }
    sum := 0.0
    for value in values ->
        sum += value
    values[2] = 1.0
    return sum + values[2]

pointer_to_double :: proc() F64 ->
    d := 0.5
    p := %d
    p^ *= 3.0
    return d

add_to_precise :: proc() F64 ->
    precise += 0.2
    return precise

/// Prints `label`, which has `%f` in it, and the value, and gives 1 when it is not `expected`.
check :: proc(label: C_Str, value: F64, expected: F64) Int ->
    printf(label, value)
    puts(str_to_c_str(""))
    if value == expected ->
        return 0
    return 1

main :: proc() Int ->
    failures := 0
    failures += check(str_to_c_str("literal 1.42: %f"), 1.42, 1.42)
    failures += check(str_to_c_str("0.1 + 0.2 is not 0.3: %f"), double_constant_expression(), 0.30000000000000004)
    failures += check(str_to_c_str("arithmetic(3, 4), 6.25: %f"), arithmetic(3.0, 4.0), 6.25)
    failures += check(str_to_c_str("widen(1.5), 1.5: %f"), widen(1.5), 1.5)
    narrowed := narrow(1.5)
    failures += check(str_to_c_str("narrow(1.5) widened, 1.5: %f"), cast(F64)narrowed, 1.5)
    rounded := widen(narrow(1.42))
    failures += check(str_to_c_str("1.42 through an F32, 1.42 not equal: %f"), rounded, 1.4199999570846558)
    failures += check(str_to_c_str("cast of an Int, 9007199254740992: %f"), cast(F64)9007199254740993, 9007199254740992.0)
    one := compare(1.0)
    failures += check(str_to_c_str("compare(1), 1: %f"), cast(F64)one, 1.0)
    failures += check(str_to_c_str("compare(1.5), 2: %f"), cast(F64)compare(1.5), 2.0)
    failures += check(str_to_c_str("compare(2), 4: %f"), cast(F64)compare(2.0), 4.0)
    failures += check(str_to_c_str("pair_sum, 3.75: %f"), pair_sum(), 3.75)
    failures += check(str_to_c_str("array_of_doubles, 1.6: %f"), array_of_doubles(), 1.6)
    failures += check(str_to_c_str("pointer_to_double, 1.5: %f"), pointer_to_double(), 1.5)
    first := add_to_precise()
    failures += check(str_to_c_str("global variable, 0.3: %f"), first, 0.1 + 0.2)
    failures += check(str_to_c_str("sqrt of the C library, 4: %f"), sqrt(16.0), 4.0)
    failures += check(str_to_c_str("pow of the C library, 8: %f"), pow(2.0, 3.0), 8.0)
    failures += check(str_to_c_str("floor of the C library, -3: %f"), floor(-2.5), -3.0)
    return failures
