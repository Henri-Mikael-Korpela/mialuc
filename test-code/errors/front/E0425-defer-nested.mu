package errors

main :: proc() Int ->
    x := 0
    defer ->
        defer x = 1
    return 0
