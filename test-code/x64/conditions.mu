// If statements with `else if` and `else`, which run the block of the first condition that holds,
// and match statements, which compare one value with the patterns of their cases: numbers,
// constants and ranges, with cases that share a block and a default case. The exit code of the
// program is 0 when every check holds.

package condition_checks

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

LIMIT :: 100

/// Prints a line that has the number in it, where the format has `%lld`.
show :: proc(format: C_Str, number: Int) ->
    printf(format, number)
    puts("")

/// Returns 1 for a positive number, -1 for a negative one and 0 for zero.
sign :: proc(n: Int) Int ->
    if n > 0 ->
        return 1
    else if n < 0 ->
        return -1
    return 0

/// Returns the larger of two numbers, which a variable is given in either branch.
max :: proc(a: Int, b: Int) Int ->
    result := a
    if b > a ->
        result = b
    return result

/// Names the size of a number with the cases of a match, as a code that is printed.
size_code :: proc(n: Int) Int ->
    match n ->
        case 0 ->
            return 0
        case 1..<10 ->
            return 1
        case 10..<=99 ->
            return 2
        case LIMIT ->
            return 3
        case ->
            return 4

/// Cases without a block run the block of the next case that has one.
is_weekend :: proc(day: Int) Bool ->
    match day ->
        case 6 ->
        case 7 ->
            return true
        case ->
            return false

/// A match without a default case runs nothing for a value that no case matches.
bonus :: proc(level: Int) Int ->
    points := 10
    match level ->
        case 1 ->
            points += 5
        case 2 ->
            points += 20
    return points

/// If statements inside each other, and a condition that is a Bool variable.
classify :: proc(n: Int) Int ->
    is_big := n > 1000
    if is_big ->
        if n > 1000000 ->
            return 3
        else ->
            return 2
    else if n / 2 * 2 == n and n not= 0 ->
        return 1
    else ->
        return 0

main :: proc() Int ->
    failures := 0
    show("sign(42) is %lld", sign(42))
    show("sign(-5) is %lld", sign(-5))
    show("sign(0) is %lld", sign(0))
    show("max(3, 9) is %lld", max(3, 9))
    show("size_code(7) is %lld", size_code(7))
    show("size_code(100) is %lld", size_code(LIMIT))
    show("size_code(5000) is %lld", size_code(5000))
    show("bonus(2) is %lld", bonus(2))
    show("bonus(3) is %lld", bonus(3))
    show("classify(5000000) is %lld", classify(5000000))
    show("classify(8) is %lld", classify(8))

    if sign(-5) not= -1 ->
        failures += 1
    if max(3, 9) not= 9 or max(9, 3) not= 9 ->
        failures += 1
    if size_code(0) not= 0 or size_code(42) not= 2 or size_code(101) not= 4 ->
        failures += 1
    if is_weekend(6) == false or is_weekend(7) == false or is_weekend(3) ->
        failures += 1
    if bonus(1) not= 15 or bonus(9) not= 10 ->
        failures += 1
    if classify(2000) not= 2 or classify(7) not= 0 or classify(0) not= 0 ->
        failures += 1
    show("failures: %lld", failures)
    return failures
