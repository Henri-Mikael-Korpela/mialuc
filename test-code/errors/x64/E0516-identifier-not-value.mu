package errors

Point :: struct ->
    x: Int
    y: Int

add :: proc(a: Int, b: Int) Int ->
    return a + b

main :: proc() Int ->
    y := add(Point, 1)
    return 0
