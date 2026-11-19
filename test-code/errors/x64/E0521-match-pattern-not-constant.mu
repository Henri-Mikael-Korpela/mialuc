package errors

main :: proc() Int ->
    n := 1
    m := 2
    match n ->
        case m ->
            n = 2
        case ->
            n = 3
    return 0
