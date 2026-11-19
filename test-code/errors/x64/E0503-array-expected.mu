package errors

main :: proc() Int ->
    n := 3
    for c in n ->
        return 1
    return 0
