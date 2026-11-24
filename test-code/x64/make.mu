// Memory that is made while the program runs: `make` makes the elements of an array slice, of any
// type, which are zero, and `destroy` gives them back. Unlike a dynamic array, the elements outlive
// the procedure that made them, until they are destroyed. Each procedure is checked against the
// result that the language gives, and the exit code of the program is the number of wrong results,
// 0.

package make_and_destroy

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

Point :: struct ->
    x: Int
    y: Int

/// The elements of bytes that `make` makes are zero, and are assigned and read like the elements of an array.
bytes_are_zero_and_assigned :: proc() Int ->
    bytes := make([]Byte, 16)
    defer destroy(bytes)
    zeros := 0
    for b in bytes ->
        if b == 0 ->
            zeros += 1
    bytes[3] = 200
    bytes[15] = 7
    return zeros * 1000 + cast(Int)bytes[3] + cast(Int)bytes[15] + length_of(bytes) * 100000

/// Elements of other types: integers, floats and structs, whose fields are zero too.
other_elements :: proc() Int ->
    numbers := make([]Int, 5)
    for i in 0..<5 ->
        numbers[i] = i * i
    points := make([]Point, 3)
    points[1].x = 40
    points[2].y = 2
    floats := make([]F64, 2)
    floats[1] = 2.5
    doubled := floats[1] * 2.0 + floats[0]
    total := numbers[4] + points[1].x + points[2].y + points[0].x + cast(Int)doubled
    destroy(numbers)
    destroy(points)
    destroy(floats)
    return total

/// Elements that are made with a count of none, which is an empty slice.
empty :: proc() Int ->
    nothing := make([]Int, 0)
    count := length_of(nothing)
    destroy(nothing)
    return count

fill_squares :: proc(values: []Int) ->
    for i in 0..<length_of(values) ->
        values[i] = i * i

sum :: proc(values: []Int) Int ->
    total := 0
    for value in values ->
        total += value
    return total

/// The elements are passed to other procedures as a slice, which change them and read them.
passed_to_procedures :: proc() Int ->
    values := make([]Int, 10)
    defer destroy(values)
    fill_squares(values)
    return sum(values) + sum(values[1:3])

/// The elements outlive the procedure that made them, which a dynamic array cannot, so a procedure
/// can return them, and a struct can keep them.
squares :: proc(count: Int) []Int ->
    result := make([]Int, count)
    for i in 0..<count ->
        result[i] = i * i
    return result

Table :: struct ->
    values: []Int
    name: Str

outlive_the_procedure :: proc() Int ->
    table := Table { values = squares(10), name = "squares" }
    defer destroy(table.values)
    return sum(table.values) + sum(table.values[1:3]) + length_of(table.name) * 1000

/// Memory that is destroyed and made again many times, which does not run out.
made_again_and_again :: proc() Int ->
    total := 0
    for i in 0..<10000 ->
        block := make([]Int, 100)
        block[99] = i
        total += block[99] + block[0]
        destroy(block)
    return total

/// A slice of the elements views them, like a slice of an array.
slices_of_made :: proc() Int ->
    text := make([]Byte, 4)
    defer destroy(text)
    text[0] = 77
    text[1] = 105
    text[2] = 97
    text[3] = 108
    word, is_valid := utf8_decode(text[0:3])
    if not is_valid ->
        return -1
    return length_of(word)

/// Prints the result of a procedure and says whether it is the one that the language gives.
check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

main :: proc() Int ->
    wrong := 0
    wrong += check("bytes_are_zero_and_assigned() is %lld", bytes_are_zero_and_assigned(), 1616207)
    wrong += check("other_elements() is %lld", other_elements(), 63)
    wrong += check("empty() is %lld", empty(), 0)
    wrong += check("passed_to_procedures() is %lld", passed_to_procedures(), 290)
    wrong += check("outlive_the_procedure() is %lld", outlive_the_procedure(), 7290)
    wrong += check("made_again_and_again() is %lld", made_again_and_again(), 49995000)
    wrong += check("slices_of_made() is %lld", slices_of_made(), 3)
    return wrong
