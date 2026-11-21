package errors

main :: proc() Int ->
    a := [const]F64 { 1.5, 2.5 }
    b := [const]F64 { 1.5, 2.5 }
    same := a == b
    return 0
