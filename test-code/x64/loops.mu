// For statements over exclusive and inclusive ranges, infinite loops, scopes, and `break` and
// `continue`, also with labels. The results are the ones that the language specification gives for
// the same procedures. The exit code of the program is the number of results that are wrong, 0.

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

sum_to_nine :: proc() Int ->
    total := 0
    for i in 1..<10 ->
        total += i
    return total

sum_to_ten :: proc() Int ->
    total := 0
    for i in 1..<=10 ->
        total += i
    return total

count_pairs :: proc(n: Int) Int ->
    count := 0
    for row in 0..<n ->
        for column in 0..<=row ->
            if row not= column ->
                count += 1
    return count

count_up_to :: proc(limit: Int) Int ->
    count := 0
    for ->
        if count >= limit ->
            break
        count += 1
    return count

sum_of_odd_numbers :: proc(n: Int) Int ->
    total := 0
    for i in 1..<=n ->
        if i / 2 * 2 == i ->
            continue
        total += i
    return total

add_eleven :: proc(x: Int) Int ->
    total := x
    ->
        extra := 10
        total += extra
        total += 1
    return total

break_early_with_label :: proc() Int ->
    x := 1
    ->
        @my_block ->
            if x > 10 ->
                x += 1
            else ->
                break my_block
            x += 2
        x += 5
    return x

classify :: proc(n: Int) Int ->
    x := 0
    @check if n > 5 ->
        x += 1
        if n > 10 ->
            break check
        x += 10
    else if n > 2 ->
        x += 100
    else ->
        x += 1000
    x += 10000
    return x

count_triangle :: proc(n: Int) Int ->
    total := 0
    @rows for row in 0..<n ->
        for column in 0..<n ->
            if column > row ->
                continue rows
            total += 1
    return total

/// A match in a loop: `continue` belongs to the loop around the match.
sum_of_multiples_of_three :: proc(limit: Int) Int ->
    total := 0
    for i in 0..<limit ->
        match i - i / 3 * 3 ->
            case 0 ->
                total += i
            case ->
                continue
    return total

/// The end of the range is read once, so changing it in the block changes nothing.
end_read_once :: proc() Int ->
    end := 3
    passes := 0
    for i in 0..<end ->
        end += 1
        passes += 1
    return passes

/// An inclusive range that ends at the largest Int ends instead of wrapping around.
largest_inclusive :: proc() Int ->
    largest := 0x7FFF_FFFF_FFFF_FFFF
    passes := 0
    for i in largest - 2..<=largest ->
        passes += 1
    return passes

main :: proc() Int ->
    wrong := 0
    wrong += check("sum_to_nine() is %lld", sum_to_nine(), 45)
    wrong += check("sum_to_ten() is %lld", sum_to_ten(), 55)
    wrong += check("count_pairs(4) is %lld", count_pairs(4), 6)
    wrong += check("count_up_to(5) is %lld", count_up_to(5), 5)
    wrong += check("sum_of_odd_numbers(10) is %lld", sum_of_odd_numbers(10), 25)
    wrong += check("add_eleven(4) is %lld", add_eleven(4), 15)
    wrong += check("break_early_with_label() is %lld", break_early_with_label(), 6)
    wrong += check("classify(11) is %lld", classify(11), 10001)
    wrong += check("classify(7) is %lld", classify(7), 10011)
    wrong += check("classify(1) is %lld", classify(1), 11000)
    wrong += check("count_triangle(3) is %lld", count_triangle(3), 6)
    wrong += check("sum_of_multiples_of_three(10) is %lld", sum_of_multiples_of_three(10), 18)
    wrong += check("end_read_once() is %lld", end_read_once(), 3)
    wrong += check("largest_inclusive() is %lld", largest_inclusive(), 3)
    return wrong
