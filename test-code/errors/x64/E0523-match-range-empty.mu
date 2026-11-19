package errors

main :: proc() Int ->
    n := 1
    match n ->
        case 5..<5 ->
            n = 2
        case ->
            n = 3
    return 0
