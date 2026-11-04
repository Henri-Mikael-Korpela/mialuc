break_early :: proc() Int ->
    x := 1
    ->
        if x < 10 ->
            x += 1
        else ->
            break
        x += 2
    return x

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

scope_without_break :: proc() Int ->
    x := 1
    ->
        x += 10
    return x

scopes_with_same_variable_name :: proc() Int ->
    total := 0
    ->
        part := 3
        total += part
    ->
        part := 4
        total += part
    return total

break_leaves_innermost_scope :: proc(n: Int) Int ->
    x := 0
    ->
        x += 1
        ->
            if n > 0 ->
                break
            x += 10
        x += 100
    return x

break_outer_scope_by_label :: proc(n: Int) Int ->
    x := 0
    @outer ->
        x += 1
        @inner ->
            x += 10
            if n > 0 ->
                break outer
            x += 100
        x += 1000
    x += 10000
    return x

break_inner_scope_by_label :: proc(n: Int) Int ->
    x := 0
    @outer ->
        x += 1
        @inner ->
            x += 10
            if n > 0 ->
                break inner
            x += 100
        x += 1000
    x += 10000
    return x

label_by_the_innermost_name_only :: proc(n: Int) Int ->
    x := 0
    @first ->
        x += 1
        if n > 0 ->
            break first
        x += 10
    @second ->
        x += 100
        if n > 0 ->
            break second
        x += 1000
    return x

break_from_loop_leaves_scope_only :: proc(n: Int) Int ->
    total := 0
    for i in 0..<n ->
        ->
            if i == 2 ->
                break
            total += 1
        total += 10
    return total

break_scope_label_inside_loop :: proc(n: Int) Int ->
    total := 0
    for i in 0..<n ->
        @skip ->
            if i / 2 * 2 == i ->
                break skip
            total += 1
        total += 10
    return total

break_loop_by_label :: proc(n: Int) Int ->
    total := 0
    @search ->
        for i in 0..<n ->
            for j in 0..<n ->
                total += 1
            if total > 6 ->
                break search
        total += 1000
    return total

continue_in_scope_in_loop :: proc(n: Int) Int ->
    total := 0
    for i in 0..<n ->
        ->
            if i / 2 * 2 == i ->
                continue
            total += 1
        total += 10
    return total

continue_after_scope_in_endless_loop :: proc(n: Int) Int ->
    count := 0
    total := 0
    for ->
        count += 1
        @check ->
            if count > n ->
                break
            if count / 2 * 2 == count ->
                break check
            total += count
            continue
        if count > n ->
            break
    return total

scope_in_branch_of_if :: proc(n: Int) Int ->
    x := 0
    if n > 0 ->
        @positive ->
            x += 1
            if n > 5 ->
                break positive
            x += 10
    else ->
        ->
            x += 100
            break
    return x
