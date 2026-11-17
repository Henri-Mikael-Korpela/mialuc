package while_loops

/// A for statement with only a condition goes on while the condition is true. It is tested before
/// each pass, so the block may not run at all.
passes :: proc(start: Int) Int ->
    count := start
    done := 0
    for count > 0 ->
        count -= 1
        done += 1
    return done

/// `continue` goes back to the test, and `break` leaves a loop whose condition is still true.
sum_skipping :: proc(limit: Int) Int ->
    number := 0
    sum := 0
    for number < limit ->
        number += 1
        if number == number / 3 * 3 ->
            continue
        sum += number
    return sum

first_above :: proc(bound: Int) Int ->
    number := 1
    for number > 0 ->
        if number * number > bound ->
            break
        number += 1
    return number

/// A loop in a loop, and a condition of two comparisons.
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

find :: proc(limit: Int) Int ->
    number := 0
    for number < limit ->
        if number == 7 ->
            return number
        number += 1
    return -1
