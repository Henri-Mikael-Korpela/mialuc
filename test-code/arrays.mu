array_element_access_with_deduced_length :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    return a[0]

array_element_access_with_explicit_length :: proc() Int ->
    a := [5]Int { 1, 2, 3, 4, 5 }
    return a[0]

array_element_assignment :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    a[0] = 10
    return a[0]

sum :: proc(elements: []Int) Int ->
    total := 0
    for x in elements ->
        total += x
    return total

array_element_range_assignment :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    a[0:3] = [const]Int { 10, 20, 30 }
    return sum(a)

pass_array_slice_to_proc :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    return sum(a[0:3])

array_with_declared_type :: proc() Int ->
    a : [3]Int = [const]Int { 7, 8, 9 }
    return a[2]

array_of_nothing :: proc() Int ->
    a := [const]Int { }
    return sum(a)

array_literal_as_argument :: proc() Int ->
    return sum([const]Int { 5, 6, 7 })

sum_of_array :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    total := 0
    for x in a ->
        total += x
    return total

sum_of_middle :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    return sum(a[1:4])

sum_of_empty_slice :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    return sum(a[2:2])

slice_of_slice :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    middle := a[1:5]
    return sum(middle[1:3])

slice_variable_with_type :: proc() Int ->
    a := [const]Int { 4, 5, 6 }
    all : []Int = a
    return all[0] + all[1] + all[2]

slice_bounds_are_expressions :: proc(low: Int, high: Int) Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    return sum(a[low + 1:high * 2])

element_index_is_expression :: proc(n: Int) Int ->
    a := [const]Int { 10, 20, 30, 40, 50 }
    return a[n * 2 - 1] + a[a[0] / 10]

compound_element_assignment :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    a[0] += 10
    a[1] -= 1
    a[2] *= 7
    a[3] /= 2
    return a[0] * 10000 + a[1] * 1000 + a[2] * 100 + a[3] * 10 + a[4]

fill_with_loop :: proc() Int ->
    a := [const]Int { 0, 0, 0, 0, 0 }
    for i in 0..<5 ->
        a[i] = i * i
    return sum(a)

swap_ends :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    first := a[0]
    a[0] = a[4]
    a[4] = first
    return a[0] * 10 + a[4]

copy_between_arrays :: proc() Int ->
    a := [const]Int { 1, 2, 3 }
    b := [const]Int { 0, 0, 0, 0 }
    b[1:4] = a
    return b[0] * 1000 + b[1] * 100 + b[2] * 10 + b[3]

copy_within_one_array :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    a[0:4] = a[1:5]
    return a[0] + a[1] * 10 + a[2] * 100 + a[3] * 1000 + a[4] * 10000

dot_product :: proc(left: []Int, right: []Int) Int ->
    total := 0
    index := 0
    for x in left ->
        total += x * right[index]
        index += 1
    return total

dot_product_of_arrays :: proc() Int ->
    a := [const]Int { 1, 2, 3 }
    b := [const]Int { 4, 5, 6, 7 }
    return dot_product(a, b[1:4])

count_above :: proc(elements: []Int, limit: Int) Int ->
    count := 0
    for x in elements ->
        if x > limit ->
            count += 1
    return count

sum_twice :: proc(elements: []Int) Int ->
    return sum(elements) + sum(elements)

sum_until_negative :: proc(elements: []Int) Int ->
    total := 0
    for x in elements ->
        if x < 0 ->
            break
        total += x
    return total

sum_of_odd_elements :: proc(elements: []Int) Int ->
    total := 0
    for x in elements ->
        if x / 2 * 2 == x ->
            continue
        total += x
    return total

breaks_and_continues :: proc() Int ->
    a := [const]Int { 1, 0 - 1, 2, 3, 0 - 4, 5 }
    b := [const]Int { 10, 20 }
    total := 0
    @outer for x in a ->
        for y in b ->
            if y == 20 and x == 3 ->
                break outer
            if x < 0 ->
                continue outer
            total += x * y
    return total

literal_in_loop :: proc() Int ->
    total := 0
    for i in 0..<3 ->
        a := [const]Int { i, i + 1 }
        total += a[0] * a[1]
    return total

arrays_in_nested_calls :: proc(depth: Int) Int ->
    a := [const]Int { depth, depth * 2 }
    if depth == 0 ->
        return a[0] + a[1]
    inner := arrays_in_nested_calls(depth - 1)
    return inner + a[0] + a[1]

return_from_nested_block :: proc(limit: Int) Int ->
    a := [const]Int { 3, 6, 9 }
    for x in a ->
        if x > limit ->
            return x
    return 0

element_out_of_bounds :: proc(index: Int) Int ->
    a := [const]Int { 1, 2, 3 }
    return a[index]

slice_out_of_bounds :: proc(low: Int, high: Int) Int ->
    a := [const]Int { 1, 2, 3 }
    return sum(a[low:high])

copy_with_different_lengths :: proc() Int ->
    a := [const]Int { 1, 2, 3 }
    a[0:2] = a[0:3]
    return a[0]

length_of_array :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    return length_of(a)

length_of_array_with_explicit_length :: proc() Int ->
    a := [7]Int { 1, 2, 3, 4, 5, 6, 7 }
    return length_of(a)

length_of_empty_array :: proc() Int ->
    a := [const]Int { }
    return length_of(a)

length_of_slice :: proc(elements: []Int) Int ->
    return length_of(elements)

length_of_slice_of_array :: proc(low: Int, high: Int) Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    return length_of(a[low:high])

average :: proc(elements: []Int) Int ->
    if length_of(elements) == 0 ->
        return 0
    return sum(elements) / length_of(elements)

fill_with_length :: proc() Int ->
    a := [const]Int { 0, 0, 0, 0 }
    for i in 0..<length_of(a) ->
        a[i] = i + 1
    return sum(a)

last_element :: proc(elements: []Int) Int ->
    return elements[length_of(elements) - 1]
