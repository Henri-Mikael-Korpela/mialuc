foo_with_if :: proc() Int ->
    x := 4
    if x > 5 ->
        return 1
    return 2

foo_with_if_else :: proc() Int ->
    x := 4
    if x > 5 ->
        return 1
    else ->
        return 2

foo_with_if_else_if :: proc() Int ->
    x := 4
    if x > 5 ->
        return 1
    else if x < 3 ->
        return 2
    else ->
        return 3

foo_with_nested_if :: proc() Int ->
    x := 4
    if x > 5 ->
        return 1
    else ->
        if x < 3 ->
            return 2
        else ->
            return 3

sign :: proc(n: Int) Int ->
    if n > 0 ->
        return 1
    else if n < 0 ->
        return 0 - 1
    else ->
        return 0

compare_foo :: proc(a: Int, b: Int) Int ->
    if a == b ->
        return 0
    else if a not= b ->
        if a >= b + 10 ->
            return 2
        else if a <= b - 10 ->
            return 0 - 2
    return 1

foo_with_block_variables :: proc(n: Int) Int ->
    total := 10
    if n >= 5 ->
        extra := 100
        total += extra
        if n == 7 ->
            bonus := 1000
            total += bonus
    else ->
        extra := 5
        total -= extra
    return total
