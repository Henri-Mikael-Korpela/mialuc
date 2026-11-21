package errors

main :: proc() Int ->
    a := [const]Int { 2, 3, 5 }
    b := [dynamic]Int
    append(%b, 2, 3, 5)
    are_same := a == b
    return 0
