foo :: proc() Int ->
    x := 4
    return x

foo2 :: proc() Int ->
    x : Int = 5
    return x

assignment :: proc() Int ->
    x := 4
    x = 5
    return x

assignment_twice :: proc() Int ->
    x := 1
    x = 2
    x = 3
    return x

assignment_uses_old_value :: proc() Int ->
    x := 4
    x = x * 2 + 1
    return x

assignment_from_other_variable :: proc() Int ->
    a := 10
    b := 20
    a = b
    b = 0
    return a

swap :: proc() Int ->
    a := 3
    b := 7
    temp := a
    a = b
    b = temp
    return a * 10 + b

assignment_of_bool :: proc() Bool ->
    flag := false
    flag = 3 > 2
    return flag

assignment_of_string :: proc() Str ->
    text := "before"
    text = "after"
    return text

assignment_to_parameter_copy :: proc(n: Int) Int ->
    result := n
    result = result + 1
    return result

assignment_in_condition :: proc(n: Int) Int ->
    x := 0
    if n > 0 ->
        x = 1
    else ->
        x = 0 - 1
    return x

assignment_in_loop :: proc() Int ->
    x := 1
    for i in 0..<5 ->
        x = x * 2
    return x

assignment_after_declared_type :: proc() Int ->
    x : Int
    x = 9
    return x
