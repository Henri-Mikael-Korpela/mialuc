package errors

main :: proc() Int ->
    n := 1
    match n ->
        case "a" ->
            n = 2
        case ->
            n = 3
    return 0
