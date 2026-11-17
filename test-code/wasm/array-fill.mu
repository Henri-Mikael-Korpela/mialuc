package array_fill

Point :: struct ->
    x: Int
    y: Int

/// An array literal that gives every element the same value: `[5]Int { .. = 0 }`. The length is a
/// number, because `const` is the number of the elements that are written, and the value is worked
/// out once and put in every element.
sum_of_sevens :: proc() Int ->
    sevens := [4]Int { .. = 7 }
    total := 0
    for value in sevens ->
        total += value
    return total

zero_length :: proc() Int ->
    zeros := [5]Int { .. = 0 }
    return length_of(zeros)

/// The elements are the array's own, so one that is changed does not change the others.
changed_one :: proc() Int ->
    own := [3]Int { .. = 10 }
    own[1] = 99
    return own[0] + own[1] + own[2]

/// Bytes, a Bool, an I32 and a string are filled the same way, and so is a struct.
last_byte :: proc() Byte ->
    bytes := [6]Byte { .. = 255 }
    return bytes[5]

last_i32 :: proc() I32 ->
    small := [3]I32 { .. = -5 }
    return small[2]

last_name :: proc() Str ->
    names := [3]Str { .. = "Bob" }
    return names[2]

middle_y :: proc() Int ->
    points := [3]Point { .. = Point { x = 1, y = 2 } }
    return points[1].y

/// The value is an expression, and a large number of elements is a loop.
computed :: proc(base: Int) Int ->
    own := [3]Int { .. = base * 2 + 1 }
    return own[0] + own[2]

large :: proc() Int ->
    many := [1000]Int { .. = 3 }
    total := 0
    for value in many ->
        total += value
    return total

/// An array of no elements has no place for the value.
none :: proc() Int ->
    nothing := [0]Int { .. = 5 }
    return length_of(nothing)
