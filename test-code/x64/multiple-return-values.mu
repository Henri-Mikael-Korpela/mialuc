// Procedures that return several values: their return types are written in parentheses and
// separated by commas, `proc() (Int, Int)`, and `return` gives a value for each of them. The
// values of a call are taken with a name for each, `first, second := call()`, where the name `_`
// takes the place of a value that is not wanted. A pointer to a procedure has the same kind of
// return types, as in `^proc(Int, Int) (Int, Int)`. The exit code of the program is the number of
// wrong results, 0.

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, first: Int, second: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

check :: proc(format: C_Str, first: Int, second: Int, expected_first: Int, expected_second: Int) Int ->
    printf(format, first, second)
    if first == expected_first and second == expected_second ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

say :: proc(text: Str) ->
    puts(str_to_c_str(text))

/// Divide returns both the quotient and the remainder.
divide :: proc(a: Int, b: Int) (Int, Int) ->
    return a / b, a - a / b * b

/// Two strings are four words, which are returned like the words of any other value.
split :: proc() (Str, Str) ->
    return "Mialu", "language"

/// The types of the values do not have to be the same.
describe :: proc(n: Int) (Bool, Str, Int) ->
    if n < 0 ->
        return false, "negative", 0 - n
    return true, "not negative", n

/// The values are worked out in the order that they are written, and they can use each other's
/// inputs.
swap :: proc(a: Int, b: Int) (Int, Int) ->
    return b, a

/// A `Byte` and an `Int` are returned as the words that they are.
byte_and_int :: proc(n: Int) (Byte, Int) ->
    return 200, n * 2

/// The smallest and the largest element of a slice, which is two results that a loop makes.
min_max :: proc(numbers: []Int) (Int, Int) ->
    smallest := numbers[0]
    largest := numbers[0]
    for n in numbers ->
        if n < smallest ->
            smallest = n
        if n > largest ->
            largest = n
    return smallest, largest

/// A pair of consecutive numbers of the Fibonacci sequence, which the next call goes on from.
fibonacci_step :: proc(previous: Int, current: Int) (Int, Int) ->
    return current, previous + current

/// A procedure that returns several values can call another one, and return each of the values
/// that it got.
divide_twice :: proc(a: Int, b: Int) (Int, Int, Int) ->
    quotient, remainder := divide(a, b)
    inner, rest := divide(quotient, b)
    return inner, rest, remainder

Point :: struct ->
    x: Int
    y: Int

/// A struct is a value like any other, whose words follow the words of the values before it.
origin_and_corner :: proc(size: Int) (Point, Point) ->
    return Point { x = 0, y = 0 }, Point { x = size, y = size * 2 }

/// A pointer to a procedure with several return types.
apply :: proc(operation: ^proc(Int, Int) (Int, Int), a: Int, b: Int) (Int, Int) ->
    first, second := operation(a, b)
    return first + 1, second + 1

main :: proc() Int ->
    wrong := 0

    quotient, remainder := divide(31, 4)
    wrong += check("31 divided by 4 is %lld remainder %lld", quotient, remainder, 7, 3)

    first, second := split()
    say(first)
    say(second)

    is_positive, text, size := describe(0 - 5)
    say(text)
    if is_positive ->
        wrong += 1
    wrong += check("%lld is the size, and a negative number is %lld", size, 0, 5, 0)

    a, b := swap(1, 2)
    wrong += check("swapped 1 and 2 to %lld and %lld", a, b, 2, 1)

    small, doubled := byte_and_int(21)
    wrong += check("the byte is %lld and the Int is %lld", cast(Int)small, doubled, 200, 42)

    numbers := [const]Int { 7, 3, 9, 1, 8 }
    low, high := min_max(numbers[0:5])
    wrong += check("the elements are from %lld to %lld", low, high, 1, 9)

    // Only the values that are wanted get a name.
    _, high_only := min_max(numbers[1:3])
    _, rest := divide(100, 7)
    wrong += check("the maximum is %lld and the remainder is %lld", high_only, rest, 9, 2)

    // The values of a call can be dropped altogether.
    divide(5, 2)

    step_previous := 0
    step_current := 1
    for i in 0..<10 ->
        next_previous, next_current := fibonacci_step(step_previous, step_current)
        step_previous = next_previous
        step_current = next_current
    wrong += check("after 10 calls of Fibonacci: %lld and %lld", step_previous, step_current, 55, 89)

    inner, rest_of_inner, last := divide_twice(100, 7)
    wrong += check("100 divided by 7 twice is %lld and %lld", inner, rest_of_inner, 2, 0)
    wrong += check("the first remainder is %lld, and 0 is %lld", last, 0, 2, 0)

    corner_start, corner_end := origin_and_corner(5)
    wrong += check("the corner is %lld, %lld", corner_end.x, corner_end.y, 5, 10)
    wrong += check("the origin is %lld, %lld", corner_start.x, corner_start.y, 0, 0)

    operation : ^proc(Int, Int) (Int, Int) = divide
    through_pointer, remainder_through_pointer := operation(17, 5)
    wrong += check("through a pointer, 17 divided by 5 is %lld remainder %lld", through_pointer, remainder_through_pointer, 3, 2)

    applied, applied_remainder := apply(swap, 1, 2)
    wrong += check("applied to swap, the values are %lld and %lld", applied, applied_remainder, 3, 2)

    return wrong
