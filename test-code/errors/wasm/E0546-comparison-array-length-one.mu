package errors

main :: proc() Int ->
    x := [const]Int { 1 }
    y := [const]Int { 1, 2, 3 }
    same := x == y
    return 0
