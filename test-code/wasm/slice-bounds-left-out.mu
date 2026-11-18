// The first index of a slice can be left out, which starts at the first element, and so can the end,
// which goes to the last. For `a: [6]Int` these are the same slice: `a[0:6]`, `a[:6]`, `a[0:]` and
// `a[:]`. A chunk of an array slice, `a[offset:offset + length]`, can be written `a[offset:][:length]`
// too, which is the slice from `offset` and then its first `length` elements.

package slice_bounds_left_out

/// The sum of the elements, which says which of them a slice has.
total :: proc(items: []Int) Int ->
    sum := 0
    for item in items ->
        sum += item
    return sum

/// The four ways to write the whole array.
whole_array :: proc() Int ->
    a := [6]Int { 1, 2, 3, 4, 5, 6 }
    return total(a[0:6]) * 1000000 + total(a[:6]) * 10000 + total(a[0:]) * 100 + total(a[:])

/// The start and the end of the array: the sums of a[2:] and of a[:2], and the lengths of a[6:]
/// and of a[:0], which have no elements.
start_and_end :: proc() Int ->
    a := [6]Int { 1, 2, 3, 4, 5, 6 }
    return total(a[2:]) * 1000000 + total(a[:2]) * 10000 + length_of(a[6:]) * 100 + length_of(a[:0])

/// A chunk of the array in the two ways to write it, which have the same elements.
chunk :: proc(offset: Int, length: Int) Int ->
    a := [6]Int { 1, 2, 3, 4, 5, 6 }
    return total(a[offset:offset + length])

chunk_of_a_slice :: proc(offset: Int, length: Int) Int ->
    a := [6]Int { 1, 2, 3, 4, 5, 6 }
    return total(a[offset:][:length])

/// An array slice is sliced the same way: the length of a[1:], of window[:], the sum of window[4:],
/// of window[1:][:2], and of a[:][:][1:][:2].
slices_of_slices :: proc() Int ->
    a := [6]Int { 1, 2, 3, 4, 5, 6 }
    window := a[1:]
    return length_of(window) * 100000000 + length_of(window[:]) * 1000000 + total(window[4:]) * 10000 + total(window[1:][:2]) * 100 + total(a[:][:][1:][:2])

/// A range of elements that is assigned to has the bounds left out too.
assigned :: proc() Int ->
    a := [6]Int { 1, 2, 3, 4, 5, 6 }
    b := [6]Int { .. = 0 }
    b[:3] = a[3:]
    b[3:] = a[:3]
    return total(b[:3]) * 100 + total(b[3:])

/// The elements are not a word each: bytes.
bytes_from_one :: proc() Int ->
    bytes := [4]Byte { 1, 2, 3, 4 }
    return length_of(bytes[1:]) * 100 + cast(Int)bytes[1:][0]

/// A first index past the end traps, as it does with the end written.
past_the_end :: proc(start: Int) Int ->
    a := [3]Int { 1, 2, 3 }
    return length_of(a[start:])
