// Arrays and array slices of bytes compared with `==` and `not=`: they are the same when they have
// the same number of bytes and the same bytes, whether both are arrays or both are slices, and they
// have no order. The exit code of the program is the number of wrong results, 0.

package byte_comparisons

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

/// Prints a line with the number, and counts it as wrong when it is not what was expected.
check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

flag :: proc(is_true: Bool) Int ->
    if is_true ->
        return 1
    return 0

/// Slices are compared too, whatever they view.
same :: proc(a: []Byte, b: []Byte) Int ->
    return flag(a == b)

different :: proc(a: []Byte, b: []Byte) Int ->
    return flag(a not= b)

main :: proc() Int ->
    wrong := 0

    first := [const]Byte { 1, 2, 3 }
    second := [const]Byte { 1, 2, 3 }
    third := [const]Byte { 1, 2, 4 }
    longer := [const]Byte { 1, 2, 3, 4 }
    shorter := [const]Byte { 1, 2 }
    nothing := [const]Byte { }
    other_nothing := [const]Byte { }

    // Two arrays.
    wrong += check("same arrays are equal %lld", flag(first == second), 1)
    wrong += check("same arrays are not unequal %lld", flag(first not= second), 0)
    wrong += check("last byte differs, equal %lld", flag(first == third), 0)
    wrong += check("last byte differs, unequal %lld", flag(first not= third), 1)
    wrong += check("longer array, equal %lld", flag(first[:] == longer[:]), 0)
    wrong += check("longer array, unequal %lld", flag(first[:] not= longer[:]), 1)
    wrong += check("shorter array, equal %lld", flag(first[:] == shorter[:]), 0)
    wrong += check("an array is the same as itself %lld", flag(first == first), 1)
    wrong += check("two arrays of nothing are equal %lld", flag(nothing == other_nothing), 1)
    wrong += check("nothing is not something %lld", flag(nothing[:] == first[:]), 0)

    // The first byte, a byte in the middle and a byte above 127, which are compared as numbers
    // from 0 to 255 and not as numbers with a sign.
    high_one := [const]Byte { 200, 2, 3 }
    high_two := [const]Byte { 200, 2, 3 }
    low_one := [const]Byte { 100, 2, 3 }
    wrong += check("first byte differs %lld", flag(first == high_one), 0)
    wrong += check("high bytes are equal %lld", flag(high_one == high_two), 1)
    wrong += check("high and low differ %lld", flag(high_one == low_one), 0)
    middle := [const]Byte { 1, 9, 3 }
    wrong += check("middle byte differs %lld", flag(first == middle), 0)

    // An array and a slice, and two slices, in either order.
    slice := first[0:3]
    wrong += check("array and slice are equal %lld", flag(first[:] == slice), 1)
    wrong += check("slice and array are equal %lld", flag(slice == first[:]), 1)
    wrong += check("array and the slice of another are equal %lld", flag(second[:] == slice), 1)
    wrong += check("slice and different array %lld", flag(slice == third[:]), 0)
    wrong += check("slice and different array, unequal %lld", flag(slice not= third[:]), 1)
    shorter_slice := longer[0:3]
    wrong += check("a slice of a longer array is equal %lld", flag(shorter_slice == first[:]), 1)
    wrong += check("whole longer array is not %lld", flag(longer[:] == first[:]), 0)
    last_slice := longer[1:4]
    wrong += check("another place is not equal %lld", flag(last_slice == first[:]), 0)
    wrong += check("two slices are equal %lld", flag(shorter_slice == slice), 1)
    wrong += check("two slices are different %lld", flag(last_slice == slice), 0)
    empty_slice := first[2:2]
    wrong += check("an empty slice is an array of nothing %lld", flag(empty_slice == nothing[:]), 1)
    wrong += check("two empty slices are equal %lld", flag(empty_slice == longer[0:0]), 1)

    // Slices that are parameters, and an array that is given where a slice is wanted.
    wrong += check("parameters are equal %lld", same(first, second), 1)
    wrong += check("parameters are different %lld", different(first, third), 1)
    wrong += check("parameters of a slice %lld", same(longer[0:3], first), 1)

    // An array literal, a computed result and a comparison as a condition.
    wrong += check("literal is equal %lld", flag(first == [const]Byte { 1, 2, 3 }), 1)
    wrong += check("literal is not equal %lld", flag(first == [const]Byte { 1, 2, 5 }), 0)
    if first == second and first not= third ->
        wrong += check("condition is %lld", 1, 1)
    else ->
        wrong += check("condition is %lld", 0, 1)

    // Bytes that are changed are seen.
    changed := [const]Byte { 1, 2, 3 }
    changed[2] = 4
    wrong += check("a changed byte is not equal %lld", flag(changed == first), 0)
    wrong += check("a changed byte is equal to the one %lld", flag(changed == third), 1)

    // A longer run of bytes, which a loop compares.
    big_one := [const]Byte { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 }
    big_two := [const]Byte { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 }
    big_three := [const]Byte { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 21 }
    wrong += check("20 bytes are equal %lld", flag(big_one == big_two), 1)
    wrong += check("20 bytes that differ at the end %lld", flag(big_one == big_three), 0)
    return wrong
