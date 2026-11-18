// The first index of a slice can be left out, which starts at the first element, and so can the end,
// which goes to the last. For `a: [6]Int` these are the same slice: `a[0:6]`, `a[:6]`, `a[0:]` and
// `a[:]`. A chunk of an array slice, `a[offset:offset + length]`, can be written `a[offset:][:length]`
// too, which is the slice from `offset` and then its first `length` elements. The exit code of the
// program is the number of wrong results, 0.

package slice_bounds_left_out

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

/// The sum of the elements, which says which of them a slice has.
total :: proc(items: []Int) Int ->
    sum := 0
    for item in items ->
        sum += item
    return sum

main :: proc() Int ->
    wrong := 0
    a := [6]Int { 1, 2, 3, 4, 5, 6 }

    // The four ways to write the whole array.
    wrong += check("a[0:6] has the sum %lld", total(a[0:6]), 21)
    wrong += check("a[:6] has the sum %lld", total(a[:6]), 21)
    wrong += check("a[0:] has the sum %lld", total(a[0:]), 21)
    wrong += check("a[:] has the sum %lld", total(a[:]), 21)

    // The start or the end of the array.
    wrong += check("a[2:] has the sum %lld", total(a[2:]), 18)
    wrong += check("a[:2] has the sum %lld", total(a[:2]), 3)
    wrong += check("a[6:] has the length %lld", length_of(a[6:]), 0)
    wrong += check("a[:0] has the length %lld", length_of(a[:0]), 0)

    // A chunk of the array, in the two ways to write it.
    offset := 1
    length := 3
    wrong += check("a[offset:offset + length] has the sum %lld", total(a[offset:offset + length]), 9)
    wrong += check("a[offset:][:length] has the sum %lld", total(a[offset:][:length]), 9)
    wrong += check("a[offset:][:length] has the length %lld", length_of(a[offset:][:length]), 3)

    // An array slice is sliced the same way, and its length is where the end is.
    window := a[1:]
    wrong += check("the slice a[1:] has the length %lld", length_of(window), 5)
    wrong += check("window[:] has the length %lld", length_of(window[:]), 5)
    wrong += check("window[4:] has the sum %lld", total(window[4:]), 6)
    wrong += check("window[1:][:2] has the sum %lld", total(window[1:][:2]), 7)
    wrong += check("a[:][:][1:][:2] has the sum %lld", total(a[:][:][1:][:2]), 5)

    // A range of elements that is assigned to has them left out too.
    b := [6]Int { .. = 0 }
    b[:3] = a[3:]
    b[3:] = a[:3]
    wrong += check("b[:3] is made of a[3:], %lld", total(b[:3]), 15)
    wrong += check("b[3:] is made of a[:3], %lld", total(b[3:]), 6)

    // Elements of other types, which are not a word each.
    bytes := [4]Byte { 1, 2, 3, 4 }
    wrong += check("the bytes from 1 are %lld", length_of(bytes[1:]), 3)
    wrong += check("the first byte of bytes[1:] is %lld", cast(Int)bytes[1:][0], 2)
    return wrong
