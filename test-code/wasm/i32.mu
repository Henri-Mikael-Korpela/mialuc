// `I32` is a 32-bit integer with a sign, from -2147483648 to 2147483647. It is a type of its own,
// and nothing is converted to it or from it implicitly. JavaScript passes and gets it as a plain
// number, where an `Int` is a BigInt, which is what it is for: a procedure of JavaScript that takes
// numbers of 32 bits.

package integers_32

import foreign js "functions"

foreign js ->
    average :: proc(a: I32, b: I32) I32 -> ...

LIMIT : I32 : 2147483647

/// The arithmetic of two `I32` values is an `I32`, and wraps around in 32 bits.
wraps_over :: proc() I32 ->
    top : I32 = LIMIT
    return top + 1

wraps_under :: proc() I32 ->
    bottom : I32 = -2147483648
    return bottom - 1

product :: proc() I32 ->
    a : I32 = 46341
    return a * a

/// Division goes toward zero, and the order is the one of the numbers with their signs.
divide :: proc() I32 ->
    minus : I32 = -7
    return minus / 2

is_less :: proc() Bool ->
    a : I32 = -7
    b : I32 = 7
    return a < b

/// A procedure that takes and gives `I32` values, and compound assignment.
add :: proc(a: I32, b: I32) I32 ->
    return a + b

accumulate :: proc() I32 ->
    n : I32 = 10
    n += 5
    n -= 3
    n *= 4
    n /= 6
    return n

/// An `I32` is made an `Int` with a cast, which keeps its sign, and an `Int` is made an `I32` by
/// `int_to_i32`, which stops the program when the number does not fit.
widen :: proc() Int ->
    n : I32 = -2147483648
    return cast(Int)n

narrow :: proc(n: Int) I32 ->
    return int_to_i32(n)

/// The foreign procedure takes two `I32` values and gives one.
call_javascript :: proc() I32 ->
    return average(-10, 20)
