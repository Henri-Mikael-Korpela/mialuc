package errors

Point :: struct ->
    x: Int
    y: Int

main :: proc() Int ->
    p := Point { x = 1, x = 2, y = 3 }
    return 0
