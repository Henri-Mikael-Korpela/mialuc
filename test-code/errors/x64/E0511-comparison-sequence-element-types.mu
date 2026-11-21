package errors

main :: proc() Int ->
    x := [const]Int { 1, 2 }
    y := [const]Bool { true, false }
    same := x == y
    return 0
