// Memory that is made while the program runs: `make` makes the elements of an array slice, of any
// type, which are zero, and `destroy` gives them back. Unlike a dynamic array, the elements outlive
// the procedure that made them, until they are destroyed. The module grows its memory when the
// elements need more of it.

package make_and_destroy

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

/// Memory that is destroyed and made again many times, which reuses the blocks that are given back.
made_again_and_again :: proc() Int ->
    total := 0
    for i in 0..<10000 ->
        block := make([]Int, 100)
        total += block[99] + block[0]
        block[99] = i
        total += block[99]
        destroy(block)
    return total

/// More elements than the memory had when the module started, which grows it.
grows_the_memory :: proc() Int ->
    big := make([]Int, 300000)
    defer destroy(big)
    big[299999] = 5
    return big[299999] + big[0] + length_of(big)

/// A negative count, which traps, like an index that is out of bounds.
negative_count :: proc(count: Int) Int ->
    nothing := make([]Int, count)
    return length_of(nothing)
