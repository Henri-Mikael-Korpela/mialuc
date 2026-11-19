package errors

main :: proc() Int ->
    n := 1
    match n ->
        case 1 ->
            n = 2
        case 1 ->
            n = 3
        case ->
            n = 4
    return 0
