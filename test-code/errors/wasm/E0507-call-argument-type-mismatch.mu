package errors

add :: proc(a: Int, b: Int) Int ->
    return a + b

main :: proc() Int ->
    x := add(1, "b")
    return 0
