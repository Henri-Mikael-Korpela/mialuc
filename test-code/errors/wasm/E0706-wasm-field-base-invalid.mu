package errors

Point :: struct ->
    x: Int
    y: Int

main :: proc() Int ->
    x := Point { x = 1, y = 2 }.x
    return 0
