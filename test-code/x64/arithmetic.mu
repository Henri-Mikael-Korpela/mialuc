// Arithmetic of Int values, which wraps around on overflow, and division, which truncates toward
// zero. The assignments such as `+=` work out the variable and the value with their operator. A
// procedure can have any number of parameters: the first four words of the arguments are passed
// in registers and the others on the stack. The exit code of the program is 42.

package integer_arithmetic

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

/// Prints a line that has the number in it, where the format has `%lld`.
show :: proc(format: C_Str, number: Int) ->
    printf(format, number)
    puts("")

/// Has more parameters than there are registers for them.
weighted_sum :: proc(a: Int, b: Int, c: Int, d: Int, e: Int, f: Int) Int ->
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f

square :: proc(n: Int) Int ->
    return n * n

main :: proc() Int ->
    x := 7
    y := 3
    show("7 + 3 is %lld", x + y)
    show("7 - 3 is %lld", x - y)
    show("7 * 3 is %lld", x * y)
    show("7 / 3 is %lld", x / y)
    negative := 0 - x
    show("(0 - 7) / 2 is %lld, truncated toward zero", negative / 2)
    show("2 + 3 * 4 is %lld", 2 + 3 * 4)
    show("20 - 12 / 4 is %lld", 20 - 12 / 4)
    show("square(square(3)) + 1 is %lld", square(square(3)) + 1)
    show("weighted_sum(1, 2, 3, 4, 5, 6) is %lld", weighted_sum(1, 2, 3, 4, 5, 6))

    // The largest Int plus one wraps around to the smallest.
    largest := 0x7FFF_FFFF_FFFF_FFFF
    show("the largest Int plus 1 is %lld", largest + y - 2)

    total := 10
    total += 5
    total -= 3
    total *= 7
    total /= 2
    show("((10 + 5 - 3) * 7) / 2 is %lld", total)
    return total
