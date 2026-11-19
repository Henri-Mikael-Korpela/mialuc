package errors

Point :: struct ->
    x: Int
    y: Int

main :: proc() Int ->
    p := Point { x = "a", y = 1 }
    return 0
