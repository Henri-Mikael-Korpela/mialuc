package errors

main :: proc() Int ->
    for i in 0..<3 ->
        break missing
    return 0
