package errors

main :: proc() Int ->
    for i in "a"..<3 ->
        return i
    return 0
