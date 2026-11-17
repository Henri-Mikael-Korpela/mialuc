// `U32` and `U64` are unsigned integers of 32 and 64 bits, and types of their own: nothing is
// converted to them or from them implicitly. Their arithmetic wraps around in their bits, and a
// division and a comparison are the unsigned ones. JavaScript passes and gets a `U32` as a number
// and a `U64` as a BigInt, as WebAssembly has no unsigned values: a number above 2147483647 or
// 9223372036854775807 arrives as the negative one that has the same bits, and `>>> 0` and
// `BigInt.asUintN(64, n)` bring it back.

package unsigned_integers

import foreign js "functions"

foreign js ->
    halve :: proc(n: U32) U32 -> ...

HIGHEST : U32 : 4294967295
BIG : U64 : 9223372036854775807

/// The arithmetic of two `U32` values is a `U32`, and wraps around in 32 bits.
wraps_over :: proc() U32 ->
    top : U32 = HIGHEST
    return top + 1

wraps_under :: proc() U32 ->
    zero : U32 = 0
    return zero - 1

product :: proc() U32 ->
    top : U32 = HIGHEST
    return top * 2

/// The division and the order are unsigned: 3000000000 is above 5, and half of it is 1500000000.
divide :: proc() U32 ->
    large : U32 = 3000000000
    return large / 2

is_greater :: proc() Bool ->
    large : U32 = 3000000000
    small : U32 = 5
    return large > small

accumulate :: proc() U32 ->
    n : U32 = 10
    n += 5
    n -= 3
    n *= 4
    n /= 6
    return n

/// The same in 64 bits: twice the largest number of an `Int` is above it, and half of that is it.
twice :: proc() U64 ->
    top : U64 = BIG
    return top + top

half_of_twice :: proc() U64 ->
    top : U64 = BIG
    both := top + top
    return both / 2

is_twice_greater :: proc() Bool ->
    top : U64 = BIG
    both := top + top
    return both > top

below_zero :: proc() U64 ->
    zero : U64 = 0
    return zero - 1

/// A negative `Int` is the `U64` that has its bits, a `Byte` and a `Rune` fit in a `U32`, and a
/// `U32` is made an `Int` with a cast.
negative_to_u64 :: proc(n: Int) U64 ->
    return cast(U64)n

byte_to_u32 :: proc() U32 ->
    b : Byte = 200
    return cast(U32)b

rune_to_u32 :: proc() U32 ->
    return cast(U32)'A'

u32_to_int :: proc(n: U32) Int ->
    return cast(Int)n

/// A `U32` is made a float with a cast, which is exact.
u32_to_float :: proc(n: U32) F64 ->
    return cast(F64)n

Packet :: struct ->
    size: U32
    offset: U64

packet_size :: proc() U32 ->
    packet := Packet { size = 3000000000, offset = 9000000000 }
    packet.size += 1
    return packet.size

packet_offset :: proc() U64 ->
    packet := Packet { size = 3000000000, offset = 9000000000 }
    packet.offset /= 3
    return packet.offset

/// The foreign procedure takes a `U32` and gives one, as numbers.
call_javascript :: proc() U32 ->
    return halve(3000000000)
