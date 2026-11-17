// `I64` is a signed 64-bit integer, the size of an `Int`, and a type of its own: nothing is
// converted to it or from it implicitly. JavaScript passes and gets it as a BigInt, like an `Int`,
// and a foreign procedure that says `I64` says what it takes and gives.

package integers_64

import foreign js "functions"

foreign js ->
    double :: proc(n: I64) I64 -> ...

BIG : I64 : 9223372036854775807

/// The arithmetic of two `I64` values is an `I64`, and wraps around in 64 bits.
wraps_over :: proc() I64 ->
    top : I64 = BIG
    return top + 1

/// A number that does not fit in 32 bits.
large :: proc() I64 ->
    a : I64 = 5000000000
    return a * 3

divide :: proc() I64 ->
    minus : I64 = -7
    return minus / 2

is_less :: proc() Bool ->
    a : I64 = -7
    b : I64 = 7
    return a < b

accumulate :: proc() I64 ->
    n : I64 = 10
    n += 5
    n -= 3
    n *= 4
    n /= 6
    return n

/// An `I64` and an `Int` are each cast to the other, an `I32` is cast to an `I64`, and an `I64` is
/// made an `I32` by `int_to_i32`, which stops the program when the number does not fit.
to_int :: proc(n: I64) Int ->
    return cast(Int)n

to_i64 :: proc(n: Int) I64 ->
    return cast(I64)n

from_i32 :: proc() I64 ->
    n : I32 = -2147483648
    return cast(I64)n

to_i32 :: proc(n: I64) I32 ->
    return int_to_i32(n)

/// The foreign procedure takes an `I64` and gives one, as BigInts.
call_javascript :: proc() I64 ->
    return double(21)
