package errors

main :: proc() Int ->
    :outer for i in 0..<3 ->
        defer ->
            break outer
    return 0
