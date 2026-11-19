package errors

main :: proc() Int ->
    :outer ->
        for i in 0..<3 ->
            continue outer
    return 0
