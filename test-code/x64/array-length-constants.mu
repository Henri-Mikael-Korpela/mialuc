// The length of an array can be a constant, or arithmetic of constants, and not only a number:
// `[SIZE]Int`, `[SIZE * 2]Int` and `[SIZE + OFFSET]Byte` are arrays of 4, 8 and 7 elements when SIZE
// is 4 and OFFSET is 3. The length is known when the program is compiled, so it can be the length of
// the array of a literal that is filled, `[SIZE]Int { .. = 0 }`, and of one that lists its elements.
// The exit code of the program is the number of wrong results, 0.

package array_length_constants

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

SIZE :: 4
DOUBLE :: SIZE * 2
OFFSET : Int : 3
SMALL : I32 : 2
LAST :: SIZE - 1

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

    // The length is a constant, which has the elements that are listed.
    listed := [SIZE]Int { 1, 2, 3, 4 }
    wrong += check("[SIZE]Int has the length %lld", length_of(listed), 4)
    wrong += check("[SIZE]Int has the sum %lld", total(listed[:]), 10)

    // And the array that is filled has as many as the constant says, whatever the arithmetic is.
    filled := [DOUBLE]Int { .. = 5 }
    wrong += check("[DOUBLE]Int has the length %lld", length_of(filled), 8)
    wrong += check("[DOUBLE]Int has the sum %lld", total(filled[:]), 40)
    wrong += check("[SIZE * 2]Int has the length %lld", length_of([SIZE * 2]Int { .. = 0 }), 8)

    // Arithmetic of constants of several kinds, in the type of a variable as well.
    bytes : [SIZE + OFFSET]Byte = [SIZE + OFFSET]Byte { .. = 1 }
    wrong += check("[SIZE + OFFSET]Byte has the length %lld", length_of(bytes), 7)
    wrong += check("[LAST]Int has the length %lld", length_of([LAST]Int { .. = 0 }), 3)
    small := [SMALL]Int { 7, 8 }
    wrong += check("[SMALL]Int, of an I32 constant, has the length %lld", length_of(small), 2)

    // The slice of an array made like that, and an element that was written to.
    filled[LAST] = 100
    wrong += check("the last element of [DOUBLE]Int is %lld", filled[LAST], 100)
    wrong += check("the sum of filled[LAST:] is %lld", total(filled[LAST:]), 120)

    // An array of zero elements.
    nothing := [SIZE - SIZE]Int { }
    wrong += check("[SIZE - SIZE]Int has the length %lld", length_of(nothing), 0)
    return wrong
