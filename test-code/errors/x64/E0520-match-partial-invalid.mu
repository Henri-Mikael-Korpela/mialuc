package errors

main :: proc() Int ->
    n := 1
    match #(partial) n ->
        case 1 ->
            n = 2
    return 0
