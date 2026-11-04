for_in_exclusive_range :: proc() Int ->
    x := 1
    for i in 0..<10 ->
        x += 1
    return x

for_in_inclusive_range :: proc() Int ->
    x := 1
    for i in 0..<=10 ->
        x += 1
    return x

sum_exclusive :: proc(first: Int, last: Int) Int ->
    total := 0
    for i in first..<last ->
        total += i
    return total

sum_inclusive :: proc(first: Int, last: Int) Int ->
    total := 0
    for i in first..<=last ->
        total += i
    return total

factorial :: proc(n: Int) Int ->
    product := 1
    for i in 2..<=n ->
        product *= i
    return product

sum_of_variable_bounds :: proc(n: Int) Int ->
    low := n - 2
    high := n + 2
    total := 0
    for i in low..<=high ->
        total += i
    return total

sum_of_expression_bounds :: proc(n: Int) Int ->
    total := 0
    for i in n * 2..<n * 3 ->
        total += i
    return total

sum_of_call_bounds :: proc(n: Int) Int ->
    total := 0
    for i in smallest()..<=largest(n) ->
        total += i
    return total

smallest :: proc() Int ->
    return 1

largest :: proc(n: Int) Int ->
    return n * 2

count_empty_range :: proc(n: Int) Int ->
    count := 0
    for i in n..<n ->
        count += 1
    for i in n..<n - 5 ->
        count += 1
    return count

count_single_number :: proc(n: Int) Int ->
    count := 0
    for i in n..<=n ->
        count += 1
    return count

negative_range :: proc() Int ->
    total := 0
    for i in 0 - 3..<=3 ->
        total += i * i
    return total

end_is_read_once :: proc(n: Int) Int ->
    end := n
    count := 0
    for i in 0..<end ->
        end -= 1
        count += 1
    return count

multiplication_table_sum :: proc(n: Int) Int ->
    total := 0
    for row in 1..<=n ->
        for column in 1..<=n ->
            total += row * column
    return total

triangle_count :: proc(n: Int) Int ->
    count := 0
    for row in 0..<n ->
        for column in 0..<=row ->
            count += 1
    return count

sum_of_even_numbers :: proc(n: Int) Int ->
    total := 0
    for i in 0..<=n ->
        if i / 2 * 2 == i ->
            total += i
    return total

first_multiple :: proc(limit: Int) Int ->
    for i in 1..<=limit ->
        if i * i > limit ->
            return i
    return 0

variable_in_loop_block :: proc(n: Int) Int ->
    total := 0
    for i in 0..<n ->
        square := i * i
        copy := i
        total += square - copy
    return total

loops_one_after_another :: proc(n: Int) Int ->
    total := 0
    for i in 0..<n ->
        total += 1
    for i in 0..<=n ->
        total += 10
    return total

call_in_loop :: proc(n: Int) Int ->
    total := 0
    for i in 1..<=n ->
        total += factorial(i)
    return total

count_up_to :: proc(limit: Int) Int ->
    count := 0
    for ->
        if count >= limit ->
            break
        count += 1
    return count

first_square_above :: proc(limit: Int) Int ->
    n := 0
    for ->
        n += 1
        if n * n > limit ->
            return n

collatz_steps :: proc(start: Int) Int ->
    n := start
    steps := 0
    for ->
        if n == 1 ->
            break
        if n / 2 * 2 == n ->
            n /= 2
        else ->
            n *= 3
            n += 1
        steps += 1
    return steps

sum_of_odd_numbers :: proc(n: Int) Int ->
    total := 0
    for i in 1..<=n ->
        if i / 2 * 2 == i ->
            continue
        total += i
    return total

sum_until_limit :: proc(limit: Int) Int ->
    total := 0
    for i in 1..<100 ->
        if total + i > limit ->
            break
        total += i
    return total

count_before_multiple :: proc(n: Int, factor: Int) Int ->
    count := 0
    for i in 1..<=n ->
        if i / factor * factor == i ->
            continue
        count += 1
    return count

continue_in_inclusive_range :: proc() Int ->
    total := 0
    for i in 9223372036854775800..<=9223372036854775807 ->
        continue
    for i in 1..<=3 ->
        if i == 2 ->
            continue
        total += i
    return total

break_inner_loop_only :: proc(n: Int) Int ->
    pairs := 0
    for row in 0..<n ->
        for column in 0..<n ->
            if column > row ->
                break
            pairs += 1
    return pairs

continue_outer_pass :: proc(n: Int) Int ->
    total := 0
    for row in 0..<n ->
        if row == 1 ->
            continue
        for column in 0..<n ->
            total += 1
    return total

break_in_else_branch :: proc(n: Int) Int ->
    total := 0
    for i in 0..<n ->
        if i < 2 ->
            total += 1
        else if i < 4 ->
            total += 10
        else ->
            break
    return total

find_divisor :: proc(n: Int) Int ->
    for d in 2..<n ->
        if n / d * d == n ->
            return d
    return n

loop_with_continue :: proc(n: Int) Int ->
    i := 0
    total := 0
    for ->
        i += 1
        if i > n ->
            break
        if i / 3 * 3 == i ->
            continue
        total += i
    return total

break_after_nested_loops :: proc() Int ->
    count := 0
    for ->
        for i in 0..<3 ->
            count += 1
        count += 100
        break
    return count
