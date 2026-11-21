// Dynamic arrays, which grow when elements are appended to them: elements appended one at a time and
// several at once, the number of elements from `length_of`, and slices that a procedure goes through.
// The elements are in the temporary storage, which a procedure gives back when it returns. Each
// procedure prints what it has, and the others are checked against the results that the language gives,
// and the exit code of the program is the number of wrong results, 0.

package dynamic_arrays

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

Point :: struct ->
    x: Int
    y: Int

Color :: enum Int ->
    RED
    GREEN
    BLUE

create_dynamic_array_with_appended_elements_one_at_a_time :: proc() ->
    a := [dynamic]Int
    append(%a, 2)
    append(%a, 4)
    append(%a, 8)

    for x in a ->
        printf("Value: %lld\n", x)

create_dynamic_array_with_multiple_appended_elements_at_once :: proc() ->
    a := [dynamic]Int
    append(%a, 2, 4, 8)

    for x in a ->
        printf("Value: %lld\n", x)

length_of_dynamic_array :: proc() Int ->
    a := [dynamic]Int
    append(%a, 2, 4, 8)
    return length_of(a)

iterate_elements :: proc(values: []Int) ->
    for x in values ->
        printf("Value: %lld\n", x)

slice_dynamic_array :: proc() ->
    a := [dynamic]Int
    append(%a, 2, 4, 8)
    iterate_elements(a[:])
    iterate_elements(a[1:])

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

/// Arrays, slices and dynamic arrays of Int are compared element by element, in any mix.
compare_int_sequences :: proc() Int ->
    a := [const]Int { 1, 2, 3 }
    b := [const]Int { 1, 2, 3 }
    c := [const]Int { 1, 2, 4 }
    d := [dynamic]Int
    append(%d, 1, 2, 3)
    n := 0
    if a == b ->
        n += 1
    if a == c ->
        n += 10
    if a not= c ->
        n += 100
    if a[:] == b[:] ->
        n += 1000
    if a[0:2] == c[0:2] ->
        n += 10000
    if a[0:2] == c[:] ->
        n += 100000
    if d[:] == a[:] ->
        n += 1000000
    if d[:] == c[:] ->
        n += 10000000
    if d[:] not= a[1:] ->
        n += 100000000
    return n

/// Arrays of the other types of elements that are compared: Bool, enums, I32, Byte and empty dynamic arrays.
compare_other_sequences :: proc() Int ->
    x := [const]Bool { true, false }
    y := [const]Bool { true, false }
    z := [const]Bool { true, true }
    p := [const]Color { Color.RED, Color.GREEN }
    q := [const]Color { Color.RED, Color.GREEN }
    r := [const]Color { Color.GREEN, Color.GREEN }
    e := [dynamic]Int
    f := [dynamic]Int
    i := [const]I32 { 1, -2 }
    j := [const]I32 { 1, -2 }
    k := [const]I32 { 1, 2 }
    bs := [const]Byte { 1, 2 }
    cs := [const]Byte { 1, 2 }
    n := 0
    if x == y ->
        n += 1
    if x == z ->
        n += 10
    if p == q ->
        n += 100
    if p == r ->
        n += 1000
    if e == f ->
        n += 10000
    if i == j ->
        n += 100000
    if i == k ->
        n += 1000000
    if bs == cs ->
        n += 10000000
    return n

/// Prints the result of a procedure and says whether it is the one that the language gives.
check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

main :: proc() Int ->
    create_dynamic_array_with_appended_elements_one_at_a_time()
    create_dynamic_array_with_multiple_appended_elements_at_once()
    printf("Length: %lld\n", length_of_dynamic_array())
    slice_dynamic_array()

    wrong := 0
    wrong += check("empty_dynamic_array() is %lld", empty_dynamic_array(), 0)
    wrong += check("grow_many_times() is %lld", grow_many_times(), 500500)
    wrong += check("assign_elements() is %lld", assign_elements(), 49)
    wrong += check("bytes_in_dynamic_array() is %lld", bytes_in_dynamic_array(), 201)
    wrong += check("strings_in_dynamic_array() is %lld", strings_in_dynamic_array(), 259)
    wrong += check("structs_in_dynamic_array() is %lld", structs_in_dynamic_array(), 541)
    wrong += check("enums_in_dynamic_array() is %lld", enums_in_dynamic_array(), 311)
    wrong += check("bools_in_dynamic_array() is %lld", bools_in_dynamic_array(), 2)
    wrong += check("floats_in_dynamic_array() is %lld", floats_in_dynamic_array(), 7)
    wrong += check("two_dynamic_arrays() is %lld", two_dynamic_arrays(), 1978)
    wrong += check("slices_of_dynamic_array() is %lld", slices_of_dynamic_array(), 4878)
    wrong += check("arrays_in_calls() is %lld", arrays_in_calls(), 500063)
    wrong += check("value_from_expressions() is %lld", value_from_expressions(), 371)
    wrong += check("compare_int_sequences() is %lld", compare_int_sequences(), 101011101)
    wrong += check("compare_other_sequences() is %lld", compare_other_sequences(), 10110101)
    return wrong
