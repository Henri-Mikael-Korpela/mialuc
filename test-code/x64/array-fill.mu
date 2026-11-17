// An array literal that gives every element the same value, `[5]Int { .. = 0 }`: the length is a
// number, because `const` is the number of the elements that are written, and the value is worked
// out once and put in every element. The exit code of the program is the number of wrong results, 0.

package array_fill

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

Point :: struct ->
    x: Int
    y: Int

/// Prints a line with the number, and counts it as wrong when it is not what was expected.
check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

total_of :: proc(values: []Int) Int ->
    total := 0
    for value in values ->
        total += value
    return total

/// A counter that says how many times the value of a fill was worked out.
calls #(static) : Int = 0

next_value :: proc() Int ->
    calls += 1
    return 7

main :: proc() Int ->
    wrong := 0

    // Zero, and another number, in an array of `Int`.
    zeros := [5]Int { .. = 0 }
    wrong += check("zeros have %lld elements", length_of(zeros), 5)
    wrong += check("zeros add up to %lld", total_of(zeros), 0)
    sevens := [4]Int { .. = 7 }
    wrong += check("sevens add up to %lld", total_of(sevens), 28)
    wrong += check("the first is %lld", sevens[0], 7)
    wrong += check("the last is %lld", sevens[3], 7)

    // Other types of elements.
    bytes := [6]Byte { .. = 255 }
    wrong += check("a byte is %lld", cast(Int)bytes[5], 255)
    wrong += check("bytes are %lld elements", length_of(bytes), 6)
    flags := [3]Bool { .. = true }
    wrong += check("a flag is %lld", cast(Int)flags[2], 1)
    small := [3]I32 { .. = -5 }
    wrong += check("an I32 is %lld", cast(Int)small[2], -5)
    runes := [3]Rune { .. = 'x' }
    wrong += check("a rune is %lld", cast(Int)runes[1], 120)
    names := [3]Str { .. = "Bob" }
    wrong += check("a string has %lld characters", length_of(names[2]), 3)
    ratios := [3]F64 { .. = 1.5 }
    doubled := ratios[2] * 2.0
    wrong += check("a float is %lld", cast(Int)doubled, 3)

    // A struct is copied into every element, with all of its fields.
    points := [3]Point { .. = Point { x = 1, y = 2 } }
    wrong += check("the x of the last point is %lld", points[2].x, 1)
    wrong += check("the y of the middle point is %lld", points[1].y, 2)

    // The value is worked out once, and every element has it.
    counted := [4]Int { .. = next_value() }
    wrong += check("the value was worked out %lld times", calls, 1)
    wrong += check("counted add up to %lld", total_of(counted), 28)

    // The value can be an expression of variables, and the elements are the array's own, so one
    // that is changed does not change the others.
    base := 10
    own := [3]Int { .. = base * 2 + 1 }
    wrong += check("computed is %lld", own[1], 21)
    own[1] = 99
    wrong += check("a changed element is %lld", own[1], 99)
    wrong += check("the others are %lld", own[0] + own[2], 42)

    // No elements, one element, and a large number of them.
    nothing := [0]Int { .. = 5 }
    wrong += check("no elements %lld", length_of(nothing), 0)
    one := [1]Int { .. = 4 }
    wrong += check("one element is %lld", one[0], 4)
    many := [1000]Int { .. = 3 }
    wrong += check("a thousand elements add up to %lld", total_of(many), 3000)
    wrong += check("the last of them is %lld", many[999], 3)
    bytes_many := [500]Byte { .. = 2 }
    wrong += check("five hundred bytes, the last is %lld", cast(Int)bytes_many[499], 2)

    // A slice of the array, a comparison, and the array in a loop that changes it.
    part := zeros[1:4]
    wrong += check("a slice has %lld elements", length_of(part), 3)
    same := [3]Byte { .. = 1 }
    other := [const]Byte { 1, 1, 1 }
    is_same := same == other
    wrong += check("a filled array equals the one written out %lld", cast(Int)is_same, 1)
    for i in 0..<5 ->
        zeros[i] = i
    wrong += check("zeros are filled in by a loop to %lld", total_of(zeros), 10)

    // A comma can come after the value, and the literal can be on several lines.
    spread := [2]Int {
        .. = 8,
    }
    wrong += check("a literal on lines is %lld", total_of(spread), 16)
    return wrong
