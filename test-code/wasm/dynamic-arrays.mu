// Dynamic arrays, which grow when elements are appended to them: elements appended one at a time and
// several at once, the number of elements from `length_of`, and slices that a procedure goes through.
// The elements are in the stack of the module, which a procedure gives back when it returns. The
// procedures print what they have through `printf`, a procedure that JavaScript defines.

package dynamic_arrays

import foreign js "functions"

foreign js ->
    printf :: proc(format: Str, number: Int) -> ...

Point :: struct ->
    x: Int
    y: Int

Color :: enum Int ->
    RED
    GREEN
    BLUE

/// Appends 2, 4 and 8 with a call each, and prints them.
create_dynamic_array_with_appended_elements_one_at_a_time :: proc() ->
    a := [dynamic]Int
    append(%a, 2)
    append(%a, 4)
    append(%a, 8)

    for x in a ->
        printf("Value: %", x)

/// Appends 2, 4 and 8 with one call, and prints them.
create_dynamic_array_with_multiple_appended_elements_at_once :: proc() ->
    a := [dynamic]Int
    append(%a, 2, 4, 8)

    for x in a ->
        printf("Value: %", x)

/// The number of elements of a dynamic array, 3.
length_of_dynamic_array :: proc() Int ->
    a := [dynamic]Int
    append(%a, 2, 4, 8)
    return length_of(a)

/// Prints the elements of a slice.
iterate_elements :: proc(values: []Int) ->
    for x in values ->
        printf("Value: %", x)

/// Prints all the elements of a dynamic array, and then all but the first, through slices.
slice_dynamic_array :: proc() ->
    a := [dynamic]Int
    append(%a, 2, 4, 8)
    iterate_elements(a[:])
    iterate_elements(a[1:])

/// Appends more elements than a dynamic array has room for at the start, so that it grows several
/// times, and gives their sum, 4950.
grow_dynamic_array :: proc() Int ->
    a := [dynamic]Int
    for i in 0..<100 ->
        append(%a, i)
    total := 0
    for x in a ->
        total += x
    return total

/// A dynamic array that is made and not appended to has no elements: 0 of them, and a loop over it runs 0 times.
empty_dynamic_array :: proc() Int ->
    a := [dynamic]Int
    count := 0
    for x in a ->
        count += 1
    return length_of(a) * 10 + count

/// A thousand appends, one at a time, make it grow many times: the sum of 0 to 999 and the number of elements.
grow_many_times :: proc() Int ->
    a := [dynamic]Int
    for i in 0..<1000 ->
        append(%a, i)
    total := 0
    for x in a ->
        total += x
    return total + length_of(a)

/// The elements of a dynamic array are assigned like those of an array, also with an operator.
assign_elements :: proc() Int ->
    a := [dynamic]Int
    append(%a, 1, 2, 3)
    a[1] = 40
    a[2] += 5
    return a[0] + a[1] + a[2]

/// Elements that are bytes take a byte each.
bytes_in_dynamic_array :: proc() Int ->
    b := [dynamic]Byte
    append(%b, int_to_byte(65), int_to_byte(66))
    append(%b, int_to_byte(67))
    return cast(Int)b[0] + cast(Int)b[1] + cast(Int)b[2] + length_of(b)

/// Elements that are strings, appended two at a time and then one.
strings_in_dynamic_array :: proc() Int ->
    words := [dynamic]Str
    append(%words, "one", "three")
    append(%words, "seventeen")
    return length_of(words[0]) * 100 + length_of(words[1]) * 10 + length_of(words[2]) - 100

/// Elements that are structs, whose fields are read and assigned through the element.
structs_in_dynamic_array :: proc() Int ->
    points := [dynamic]Point
    append(%points, Point { x = 1, y = 2 }, Point { x = 3, y = 4 })
    append(%points, Point { x = 5, y = 6 })
    points[1].y = 40
    return points[0].x + points[1].y + points[2].x * 100

/// Elements that are members of an enum.
enums_in_dynamic_array :: proc() Int ->
    colors := [dynamic]Color
    append(%colors, Color.RED, Color.BLUE)
    append(%colors, Color.GREEN)
    count := 0
    for color in colors ->
        if color == Color.BLUE ->
            count += 10
        if color == Color.RED ->
            count += 1
    return count + length_of(colors) * 100

/// Elements that are booleans.
bools_in_dynamic_array :: proc() Int ->
    flags := [dynamic]Bool
    append(%flags, true, false, true)
    count := 0
    for flag in flags ->
        if flag ->
            count += 1
    return count

/// Elements that are floats, from literals that are untyped.
floats_in_dynamic_array :: proc() Int ->
    values := [dynamic]F64
    append(%values, 1.5, 2.5)
    append(%values, 3.0)
    total := values[0] + values[1] + values[2]
    return cast(Int)total

/// Two dynamic arrays that grow one after the other in the same procedure do not disturb each other.
two_dynamic_arrays :: proc() Int ->
    a := [dynamic]Int
    b := [dynamic]Int
    for i in 0..<20 ->
        append(%a, i)
        append(%b, i * 2)
    return a[19] * 100 + b[19] + length_of(a) + length_of(b)

sum_of :: proc(values: []Int) Int ->
    total := 0
    for x in values ->
        total += x
    return total

/// A slice of a dynamic array, a slice of that slice, the length of a slice, and a range of elements that is assigned.
slices_of_dynamic_array :: proc() Int ->
    a := [dynamic]Int
    append(%a, 1, 2, 3, 4, 5, 6)
    middle := a[1:5]
    inner := middle[1:3]
    a[0:2] = [const]Int { 10, 20 }
    return sum_of(a[:]) * 100 + sum_of(inner) * 10 + length_of(a[2:]) + length_of(middle)

/// Makes a dynamic array of its own and gives its number of elements, 50.
uses_its_own_array :: proc() Int ->
    inner := [dynamic]Int
    for i in 0..<50 ->
        append(%inner, i)
    return length_of(inner)

/// The elements of a procedure are given back when it returns, so a procedure that makes a dynamic array of its own, called many times, leaves the one of its caller alone.
arrays_in_calls :: proc() Int ->
    a := [dynamic]Int
    append(%a, 7)
    total := 0
    for i in 0..<10 ->
        total += uses_its_own_array()
        append(%a, i)
    return total * 1000 + sum_of(a[:]) + length_of(a)

/// The values that are appended are worked out before they are added, so they can use the elements that there are and the number of them.
value_from_expressions :: proc() Int ->
    a := [dynamic]Int
    append(%a, sum_of(a[:]) + 3)
    append(%a, a[0] * 2, length_of(a) + 10)
    return a[0] * 100 + a[1] * 10 + a[2]
