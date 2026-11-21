package errors

main :: proc() Int ->
    a := [const]Int { 2, 3, 5 }
    b := [dynamic]Int
    append(%a, 2, 3, 5)
    return 0
