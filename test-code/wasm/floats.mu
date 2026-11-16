// Floats: `F32` is a 32-bit floating point number, written as a float literal such as `1.42`.
// A float literal is untyped, like an integer literal, and is an F32 where one is wanted and where
// nothing says. Nothing is converted between Int and F32 without `cast`. An operation of floats
// follows IEEE 754: it never traps, a result that is too large is infinity, and a comparison with
// not a number is false, except `not=`.

/// A constant that is a float, which the compiler works out.
HALF :: 0.5

/// A float with a type, whose value is rounded to the nearest F32.
TENTH : F32 : 0.1

/// The initial value of a global variable is a constant, such as a float literal.
total #(static) := 0.25

/// A float literal as the value of a variable without a type makes an F32.
literal_is_f32 :: proc() F32 ->
    my_float := 1.42
    return my_float

literal_with_fraction :: proc() F32 ->
    return 42.4445

negative_literal :: proc() F32 ->
    return -0.5

/// Digit separators are allowed on either side of the dot, as in an integer literal.
literal_with_digit_separators :: proc() F32 ->
    return 1_000.000_5

scale :: proc(x: F32, factor: F32) F32 ->
    return x * factor + 1.0

/// The operators of an F32 and a literal: the literal is an F32 as well.
arithmetic :: proc() F32 ->
    a := 1.5
    b : F32 = 2.25
    a += b
    a *= 2.0
    a -= 0.75
    a /= 4.0
    return a + b - HALF

/// Division of floats is not truncated.
division :: proc(a: F32, b: F32) F32 ->
    return a / b

/// A constant expression of floats is worked out when the program is compiled.
constant_expression :: proc() F32 ->
    return 1.5 + 2.25 * 2.0

constant_values :: proc() F32 ->
    return HALF + TENTH

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
    zero := 0.0
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

/// A result that is too large is infinity, which is larger than every number, and infinity times
/// zero is not a number.
infinity :: proc() Int ->
    x := 1.0
    for i in 0..<100 ->
        x *= 10.0
    result := 0
    if x > 1000.0 ->
        result += 1
    zero := 0.0
    if x * zero not= zero ->
        result += 2
    return result

/// A global variable of a float is changed like any other.
add_to_total :: proc(amount: F32) F32 ->
    total += amount
    return total

/// A cast makes an F32 of an Int, which is rounded when it has more than 24 significant bits, and
/// an F32 of a Byte, which is exact.
int_to_float :: proc(n: Int) F32 ->
    return cast(F32)n

byte_to_float :: proc() F32 ->
    b : Byte = 200
    return cast(F32)b

/// A cast makes an Int of an F32 by dropping its fraction, toward zero. A number that is not in the
/// range of an Int, and not a number, stop the program: the call from JavaScript throws.
float_to_int :: proc(x: F32) Int ->
    return cast(Int)x

/// The cast of an F32 to an F32 is the same number, and it can be the operand of an operator.
cast_in_expression :: proc(n: Int) F32 ->
    half := cast(F32)n / 2.0
    return half

Point :: struct ->
    x: F32
    y: F32

/// A struct has fields of floats, and an operator of assignment changes one.
struct_fields :: proc() F32 ->
    p := Point { x = 1.25, y = 2.5 }
    p.x += 1.0
    p.y *= 2.0
    return p.x + p.y

/// An array of floats and a for statement over it.
array_of_floats :: proc() F32 ->
    values := [const]F32 { 1.5, 2.5, 3.0 }
    sum := 0.0
    for value in values ->
        sum += value
    values[0] = 10.0
    values[1] += 0.5
    return sum + values[0] + values[1]

/// A pointer to a float is followed and assigned through.
pointer_to_float :: proc() F32 ->
    f := 3.5
    p := %f
    p^ += 1.0
    p^ = p^ * 2.0
    return f

/// A float is a parameter and a result of a procedure that is called through a pointer.
apply :: proc(operation: ^proc(F32, F32) F32, a: F32, b: F32) F32 ->
    return operation(a, b)

use_pointer_to_procedure :: proc() F32 ->
    return apply(scale, 3.0, 4.0)

/// A procedure that returns two floats.
min_max :: proc(a: F32, b: F32) (F32, F32) ->
    if a < b ->
        return a, b
    return b, a

use_min_max :: proc() F32 ->
    low, high := min_max(9.5, 2.25)
    return high - low
