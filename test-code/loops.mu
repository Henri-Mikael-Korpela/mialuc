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
