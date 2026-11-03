add_foo :: proc() Int ->
    x := 4 + 5
    return x

subtract_foo :: proc() Int ->
    x := 5 - 1
    return x

multiply_foo :: proc() Int ->
    x := 6 * 7
    return x

divide_foo :: proc() Int ->
    x := 20 / 4
    return x

precedence_foo :: proc() Int ->
    x := 2 + 3 * 4
    return x

precedence_division_foo :: proc() Int ->
    x := 20 - 12 / 4
    return x
