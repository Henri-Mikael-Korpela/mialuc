package errors

main :: proc() Int ->
    a := [3]Int { 1, 2, 3 }
    x := a["i"]
    return 0
