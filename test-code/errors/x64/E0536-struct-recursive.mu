package errors

Node :: struct ->
    value: Int
    next: Node

main :: proc() Int ->
    n := Node { value = 1 }
    return 0
