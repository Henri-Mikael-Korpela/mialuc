// A for statement with only a condition, `for count > 0 ->`, goes on while the condition is true: it
// is tested before each pass, so the block may not run at all, and `break` and `continue` work as in
// any loop. The exit code of the program is the number of wrong results, 0.

package while_loops

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

/// Prints a line with the number, and counts it as wrong when it is not what was expected.
check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

/// Counts down from `start`, so the number of passes is `start`, and none when it is not above 0.
passes :: proc(start: Int) Int ->
    count := start
    done := 0
    for count > 0 ->
        count -= 1
        done += 1
    return done

/// The sum of the numbers below `limit` that are not multiples of 3, with `continue`.
sum_skipping :: proc(limit: Int) Int ->
    number := 0
    sum := 0
    for number < limit ->
        number += 1
        if number == number / 3 * 3 ->
            continue
        sum += number
    return sum

/// `break` leaves a loop whose condition is still true.
first_above :: proc(bound: Int) Int ->
    number := 1
    for number > 0 ->
        if number * number > bound ->
            break
        number += 1
    return number

/// A condition of two comparisons, and a loop in a loop.
pairs :: proc(rows: Int, columns: Int) Int ->
    total := 0
    row := 0
    for row < rows and total < 1000 ->
        column := 0
        for column < columns ->
            total += 1
            column += 1
        row += 1
    return total

/// A condition that is false from the start never runs the block, and a return leaves the loop.
find :: proc(limit: Int) Int ->
    number := 0
    for number < limit ->
        if number == 7 ->
            return number
        number += 1
    return -1

main :: proc() Int ->
    wrong := 0
    wrong += check("passes of 5: %lld", passes(5), 5)
    wrong += check("passes of 0: %lld", passes(0), 0)
    wrong += check("passes of -3: %lld", passes(-3), 0)
    wrong += check("sum of 1..10 without multiples of 3: %lld", sum_skipping(10), 37)
    wrong += check("first number whose square is above 50: %lld", first_above(50), 8)
    wrong += check("pairs of 3 by 4: %lld", pairs(3, 4), 12)
    wrong += check("pairs stop at 1000 after whole rows: %lld", pairs(100, 100), 1000)
    wrong += check("find 7 below 20: %lld", find(20), 7)
    wrong += check("find 7 below 5: %lld", find(5), -1)
    return wrong
