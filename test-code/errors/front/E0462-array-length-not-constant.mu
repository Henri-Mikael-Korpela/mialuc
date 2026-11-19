package errors

main :: proc() Int ->
    n := 3
    a : [n]Int = [n]Int { .. = 0 }
    return 0
