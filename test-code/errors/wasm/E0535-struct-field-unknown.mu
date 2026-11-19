package errors

Point :: struct ->
    x: Int
    y: Int

main :: proc() Int ->
    p := Point { z = 1, x = 1, y = 2 }
    return 0
