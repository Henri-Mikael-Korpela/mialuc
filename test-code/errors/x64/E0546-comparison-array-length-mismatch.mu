package errors

SIZE :: 2 + 1

main :: proc() Int ->
    x := [const]Int { 1, 2 }
    y := [SIZE * 2]Int { .. = 0 }
    same := x == y
    return 0
