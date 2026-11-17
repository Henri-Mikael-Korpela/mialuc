// Arrays, which are in the frame of their procedure, array slices, which view some of the elements
// of an array, `length_of`, for statements over the elements, and arrays of bytes, which take one
// byte for each element, as the C library wants a buffer. The results are the ones that the
// language specification gives. The exit code of the program is the number of wrong results, 0.

package array_basics

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...
    // The bytes of an array of `Byte` are a string of C when one of them is zero. The result is
    // an `int` of C, of 32 bits, so it is not declared.
    sprintf :: proc(buffer: ^Byte, format: C_Str, number: Int) -> ...

check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

array_element_access :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    return a[0] + a[a[0] + 1]

array_element_assignment :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    a[0] = 10
    a[1] += 5
    return a[0] + a[1]

sum :: proc(elements: []Int) Int ->
    total := 0
    for x in elements ->
        total += x
    return total

sum_of_middle :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    return sum(a[1:4])

array_element_range_assignment :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    a[0:3] = [const]Int { 10, 20, 30 }
    return sum(a)

/// The copy of a range is right also when the two ranges overlap, in either direction.
overlapping_ranges :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    a[1:5] = a[0:4]
    b := [const]Int { 1, 2, 3, 4, 5 }
    b[0:4] = b[1:5]
    return a[0] * 10000 + a[4] * 1000 + b[0] * 10 + b[4]

average :: proc(elements: []Int) Int ->
    if length_of(elements) == 0 ->
        return 0
    return sum(elements) / length_of(elements)

fill_with_length :: proc() Int ->
    a := [4]Int { .. = 0 }
    for i in 0..<length_of(a) ->
        a[i] = i + 1
    return sum(a)

/// A slice of a slice is relative to the first one, and a slice can be a variable.
slice_of_slice :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5, 6 }
    middle := a[1:5]
    inner : []Int = middle[1:3]
    return inner[0] * 10 + inner[1]

/// Bytes are numbers from 0 to 255, which are compared with each other, and their arithmetic is
/// done on Int values.
sum_of_bytes :: proc(bytes: []Byte) Int ->
    total := 0
    for b in bytes ->
        if b >= 100 ->
            total += cast(Int)b
    return total

main :: proc() Int ->
    wrong := 0
    wrong += check("array_element_access() is %lld", array_element_access(), 4)
    wrong += check("array_element_assignment() is %lld", array_element_assignment(), 17)
    wrong += check("sum_of_middle() is %lld", sum_of_middle(), 9)
    range_sum := array_element_range_assignment()
    wrong += check("array_element_range_assignment() is %lld", range_sum, 69)
    wrong += check("overlapping_ranges() is %lld", overlapping_ranges(), 14025)
    wrong += check("average of 2, 4, 9 is %lld", average([const]Int { 2, 4, 9 }), 5)
    wrong += check("average of nothing is %lld", average([const]Int { }), 0)
    wrong += check("fill_with_length() is %lld", fill_with_length(), 10)
    wrong += check("slice_of_slice() is %lld", slice_of_slice(), 34)

    bytes := [const]Byte { 10, 100, 200, 255 }
    wrong += check("sum_of_bytes is %lld", sum_of_bytes(bytes), 555)
    bytes[0] = int_to_byte(cast(Int)bytes[3] - 5)
    wrong += check("bytes[0] is %lld", cast(Int)bytes[0], 250)

    // An array of bytes is a buffer for the C library: `%bytes[0]` is the address of the first.
    buffer := [64]Byte { .. = 0 }
    sprintf(%buffer[0], "%lld", 2026)
    wrong += check("the first is '2', which is %lld", cast(Int)buffer[0], 50)
    wrong += check("the one after the last is %lld", cast(Int)buffer[4], 0)
    yes := true
    wrong += check("a true Bool cast to Int is %lld", cast(Int)yes, 1)
    return wrong
