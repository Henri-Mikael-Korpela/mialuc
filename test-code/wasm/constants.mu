// Constants: values that the compiler works out, which a program uses by name. A literal, and a
// constant that is defined without a type, is untyped: it has a kind, a number, a truth value or a
// string, but no type until it is used, and then it becomes the type that its place wants, when
// its value fits in it. Where nothing says which type, it has its default type: Int, Bool or Str.

package constant_values

import foreign js "host"

foreign js ->
    log_c_str :: proc(text: C_Str) -> ...

/// An untyped integer. It is an Int or a Byte, wherever it is used.
LIMIT :: 200

/// Constant expressions are worked out when the program is compiled, so they cost nothing when it
/// runs. A constant can use one that is defined below it.
DOUBLE_LIMIT :: LIMIT * 2
KIBIBYTE :: 0x400
MEBIBYTE :: KIBIBYTE * KIBIBYTE

/// Comparisons and logical operators of constants are untyped truth values.
DEBUG :: true
VERBOSE :: DEBUG and LIMIT > 100

/// A typed constant has its type, like a variable, and is not converted.
MASK : Byte : 0x0F
COUNT : Int : 3
NAME : Str : "Mialu"

/// An untyped string is a Str, or a C_Str where one is wanted.
GREETING :: "Hello from a constant!"

/// A global variable starts with a constant expression.
calls #(static) := LIMIT / 2

/// The one constant is an Int here and a Byte there, because both can hold 200.
limit_as_both :: proc() Int ->
    count : Int = LIMIT
    small : Byte = LIMIT
    return count + cast(Int)small

/// Without a type, a variable has the default type of the constant, which is Int.
default_type :: proc() Int ->
    x := DOUBLE_LIMIT
    return x + 1

/// An untyped integer next to an Int becomes an Int, and next to a Byte a Byte.
compare_with_byte :: proc(b: Byte) Bool ->
    return b == LIMIT - 100

/// A constant expression of untyped integers is a Byte as an argument, when it fits.
byte_argument :: proc() Int ->
    return take_byte(LIMIT / 4 + 5)

take_byte :: proc(b: Byte) Int ->
    return cast(Int)b

/// The elements of an array of Byte can be untyped constants, and typed Byte constants.
sum_of_bytes :: proc() Int ->
    bytes := [const]Byte { LIMIT, MASK, 0b11 }
    total := 0
    for b in bytes ->
        total += cast(Int)b
    return total

/// A typed Int and an untyped integer give an Int.
typed_arithmetic :: proc() Int ->
    return COUNT * 2

/// The sizes are exact: 1 MiB is worked out once, here.
mebibytes :: proc(n: Int) Int ->
    return n * MEBIBYTE

/// A condition that is a constant is known when the program is compiled. `not` binds looser than
/// the comparison, so this is `not` of `DEBUG == false`.
verbose :: proc() Bool ->
    return VERBOSE and not DEBUG == false

/// A global variable keeps its value between calls.
count_call :: proc() Int ->
    calls += 1
    return calls

/// A string literal and a string constant are a C_Str where one is wanted, without
/// str_to_c_str.
greet :: proc() ->
    log_c_str(GREETING)
    log_c_str("A literal is a C_Str as well.")
    text : C_Str = "So is the value of a C_Str variable."
    log_c_str(text)

/// A typed Str constant is a Str, like any other.
name :: proc() Str ->
    return NAME
