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

add_assign_foo :: proc() Int ->
    x := 4
    x += 5
    return x

divide_assign_foo :: proc() Int ->
    x := 20
    x /= 4
    return x

multiply_assign_foo :: proc() Int ->
    x := 6
    x *= 7
    return x

subtract_assign_foo :: proc() Int ->
    x := 9
    x -= 4
    return x
