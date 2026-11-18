// The length of an array can be a constant, or arithmetic of constants, and not only a number:
// `[SIZE]Int`, `[SIZE * 2]Int` and `[SIZE + OFFSET]Byte` are arrays of 4, 8 and 7 elements when SIZE
// is 4 and OFFSET is 3. The length is known when the program is compiled, so it can be the length of
// the array of a literal that is filled, `[SIZE]Int { .. = 0 }`, and of one that lists its elements.

package array_length_constants

SIZE :: 4
DOUBLE :: SIZE * 2
OFFSET : Int : 3
SMALL : I32 : 2
LAST :: SIZE - 1

/// The sum of the elements, which says which of them a slice has.
total :: proc(items: []Int) Int ->
    sum := 0
    for item in items ->
        sum += item
    return sum

/// The array that lists its elements: its length and its sum.
listed :: proc() Int ->
    a := [SIZE]Int { 1, 2, 3, 4 }
    return length_of(a) * 100 + total(a[:])

/// The array that is filled, with arithmetic as its length: the length and the sum.
filled :: proc() Int ->
    a := [DOUBLE]Int { .. = 5 }
    return length_of(a) * 100 + total(a[:])

/// Arithmetic of constants of several kinds, in the type of a variable as well: the length.
sum_of_two :: proc() Int ->
    a : [SIZE + OFFSET]Byte = [SIZE + OFFSET]Byte { .. = 1 }
    return length_of(a)

/// A constant of an I32, and one that is the length less one, and one that is zero.
other_kinds :: proc() Int ->
    small := [SMALL]Int { 7, 8 }
    nothing := [SIZE - SIZE]Int { }
    return length_of(small) * 100 + length_of([LAST]Int { .. = 0 }) * 10 + length_of(nothing)

/// An element that is written, and the slice that starts there.
last_element :: proc() Int ->
    a := [DOUBLE]Int { .. = 5 }
    a[LAST] = 100
    return a[LAST] * 1000 + total(a[LAST:])
