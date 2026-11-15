// Comparisons of Int values, whose order is signed, and of Bool values with `==` and `not=`, and
// the logical operators `and`, `or` and `not`. A Bool is 0 or 1 in a word, so printf is declared
// with a Bool, which it shows as the number. The exit code of the program is 0.

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, condition: Bool) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

/// Prints a line that has the Bool in it as 1 or 0, where the format has `%lld`.
show :: proc(format: C_Str, condition: Bool) ->
    printf(format, condition)
    puts("")

is_even :: proc(n: Int) Bool ->
    return n / 2 * 2 == n

in_range :: proc(n: Int, low: Int, high: Int) Bool ->
    return n >= low and n <= high

can_enter :: proc(age: Int, has_ticket: Bool, is_banned: Bool) Bool ->
    return age >= 18 and has_ticket and not is_banned

main :: proc() Int ->
    x := 4
    show("4 > 5 is %lld", x > 5)
    show("4 <= 4 is %lld", x <= 4)
    show("0 - 1 < 1 is %lld, because the order is signed", 0 - 1 < 1)
    show("is_even(10) is %lld", is_even(10))
    show("is_even(7) is %lld", is_even(7))
    show("in_range(4, 1, 10) is %lld", in_range(x, 1, 10))
    show("in_range(11, 1, 10) is %lld", in_range(11, 1, 10))
    show("can_enter(20, true, false) is %lld", can_enter(20, true, false))
    show("can_enter(20, true, true) is %lld", can_enter(20, true, true))

    // `not=` of two Bool values holds when exactly one of them is true.
    a := is_even(x)
    b := x > 10
    show("a not= b is %lld", a not= b)
    show("a == b is %lld", a == b)
    show("a or b and not a is %lld", a or b and not a)

    ready : Bool
    show("a Bool without a value is %lld", ready)
    ready = a and not b
    show("after the assignment it is %lld", ready)
    return 0
