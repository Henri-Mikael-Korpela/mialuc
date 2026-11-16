// A procedure can return several values. Its return types are written in parentheses and
// separated by commas, and `return` gives a value for each of them, in the same order. The values
// of a call are taken with a name for each, `quotient, remainder := divide(31, 4)`, and a value that
// is not wanted takes the name `_`. In WebAssembly the values are the results of the function, so
// JavaScript receives them as an array.

/// Divide returns both the quotient and the remainder.
divide :: proc(a: Int, b: Int) (Int, Int) ->
    return a / b, a - a / b * b

/// Two strings are four numbers, which the client reads with Str.
split :: proc() (Str, Str) ->
    return "Mialu", "language"

/// The types of the values do not have to be the same.
describe :: proc(n: Int) (Bool, Str, Int) ->
    if n < 0 ->
        return false, "negative", 0 - n
    return true, "not negative", n

swap :: proc(a: Int, b: Int) (Int, Int) ->
    return b, a

/// A Byte comes back as a number, and an Int as a BigInt.
byte_and_int :: proc(n: Int) (Byte, Int) ->
    return 200, n * 2

/// The smallest and the largest element of a slice.
min_max :: proc(numbers: []Int) (Int, Int) ->
    smallest := numbers[0]
    largest := numbers[0]
    for n in numbers ->
        if n < smallest ->
            smallest = n
        if n > largest ->
            largest = n
    return smallest, largest

smallest_and_largest :: proc() (Int, Int) ->
    numbers := [const]Int { 7, 3, 9, 1, 8 }
    smallest, largest := min_max(numbers[0:5])
    return smallest, largest

fibonacci_step :: proc(previous: Int, current: Int) (Int, Int) ->
    return current, previous + current

/// A procedure can use the values of another and return values of its own.
fibonacci :: proc(steps: Int) (Int, Int) ->
    previous := 0
    current := 1
    for i in 0..<steps ->
        next_previous, next_current := fibonacci_step(previous, current)
        previous = next_previous
        current = next_current
    return previous, current

/// The values that are not wanted are left out with `_`.
quotient_only :: proc(a: Int, b: Int) Int ->
    quotient, _ := divide(a, b)
    return quotient

remainder_only :: proc(a: Int, b: Int) Int ->
    _, remainder := divide(a, b)
    return remainder

/// A call can be a statement, and then all of its values are dropped.
drop_values :: proc() Int ->
    divide(7, 2)
    swap(1, 2)
    return 1

/// A procedure that returns several values can call another one and return what it got.
divide_twice :: proc(a: Int, b: Int) (Int, Int, Int) ->
    quotient, remainder := divide(a, b)
    inner, rest := divide(quotient, b)
    return inner, rest, remainder

Point :: struct ->
    x: Int
    y: Int

/// A struct is a value like any other.
origin_and_corner :: proc(size: Int) (Point, Point) ->
    return Point { x = 0, y = 0 }, Point { x = size, y = size * 2 }

corner_sum :: proc(size: Int) Int ->
    origin, corner := origin_and_corner(size)
    return origin.x + origin.y + corner.x + corner.y

/// A pointer to a procedure has return types in parentheses too.
apply :: proc(operation: ^proc(Int, Int) (Int, Int), a: Int, b: Int) (Int, Int) ->
    first, second := operation(a, b)
    return first + 1, second + 1

apply_divide :: proc() (Int, Int) ->
    first, second := apply(divide, 17, 5)
    return first, second

through_pointer :: proc() Int ->
    operation : ^proc(Int, Int) (Int, Int) = swap
    first, second := operation(3, 4)
    return first * 10 + second
