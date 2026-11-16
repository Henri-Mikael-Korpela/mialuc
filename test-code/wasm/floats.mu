// Floats: `F32` is a 32-bit floating point number and `F64` a 64-bit one, written as a float
// literal such as `1.42`. A float literal is untyped, like an integer literal, and is an F32 or an
// F64 where one is wanted, and an F64 where nothing says. Nothing is converted between Int, F32 and
// F64 without `cast`. An operation of floats follows IEEE 754: it never traps, a result that is
// too large is infinity, and a comparison with not a number is false, except `not=`.

package float_math

/// A constant that is a float, which the compiler works out.
HALF :: 0.5

/// A float with a type, whose value is rounded to the nearest F32.
TENTH : F32 : 0.1

/// The initial value of a global variable is a constant, such as a float literal.
total #(static) : F32 = 0.25

/// A float literal as the value of a variable with the type F32 is an F32.
literal_is_f32 :: proc() F32 ->
    my_float : F32 = 1.42
    return my_float

/// A float literal as the value of a variable without a type makes an F64.
literal_is_f64 :: proc() F64 ->
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
    a : F32 = 1.5
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

/// A result that is too large is infinity, which is larger than every number, and infinity times
/// zero is not a number.
infinity :: proc() Int ->
    x : F32 = 1.0
    for i in 0..<100 ->
        x *= 10.0
    result := 0
    if x > 1000.0 ->
        result += 1
    zero : F32 = 0.0
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
    sum : F32 = 0.0
    for value in values ->
        sum += value
    values[0] = 10.0
    values[1] += 0.5
    return sum + values[0] + values[1]

/// A pointer to a float is followed and assigned through.
pointer_to_float :: proc() F32 ->
    f : F32 = 3.5
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

/// An F64 has 53 bits of precision, so the sum of two constants is not rounded to an F32: 0.1 + 0.2
/// is 0.30000000000000004, which is not 0.3, as in every language that has doubles.
double_constant_expression :: proc() F64 ->
    return 0.1 + 0.2

double_arithmetic :: proc(a: F64, b: F64) F64 ->
    c := a * b + 1.0
    c -= 0.5
    c /= 2.0
    return c

/// An F32 is made an F64 exactly, and an F64 is rounded to the nearest F32.
float_to_double :: proc(x: F32) F64 ->
    return cast(F64)x

double_to_float :: proc(x: F64) F32 ->
    return cast(F32)x

int_to_double :: proc(n: Int) F64 ->
    return cast(F64)n

double_to_int :: proc(x: F64) Int ->
    return cast(Int)x

/// An F64 is compared with an F64, and an untyped float is an F64 there.
double_compare :: proc(a: F64) Int ->
    result := 0
    if a < 1.5 ->
        result += 1
    if a == 1.5 ->
        result += 2
    if a > 1.5 ->
        result += 4
    return result

/// A constant of the type F64 is an F64, so it is not mixed with an F32.
SMALL : F64 : 0.000_000_1

small_constant :: proc() F64 ->
    return SMALL * 2.0

Pair :: struct ->
    small: F32
    large: F64

/// The fields of a struct can be of both types.
pair_sum :: proc() F64 ->
    p := Pair { small = 1.5, large = 2.25 }
    p.large += cast(F64)p.small
    return p.large

double_array :: proc() F64 ->
    values := [const]F64 { 0.1, 0.2, 0.3 }
    sum := 0.0
    for value in values ->
        sum += value
    values[2] = 1.0
    return sum + values[2]

double_pointer :: proc() F64 ->
    d := 0.5
    p := %d
    p^ *= 3.0
    return d

/// A global variable of an F64.
precise #(static) := 0.1

add_to_precise :: proc() F64 ->
    precise += 0.2
    return precise

double_pointer_to_procedure :: proc() F64 ->
    f := double_arithmetic
    return f(3.0, 4.0)

double_not_a_number :: proc() Int ->
    zero := 0.0
    nan := zero / zero
    result := 0
    if nan == nan ->
        result += 1
    if nan not= nan ->
        result += 2
    return result
