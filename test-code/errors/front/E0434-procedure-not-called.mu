package errors

add :: proc(a: $T, b: T) T ->
    return a + b

main :: proc() Int ->
    adder := add
    return 0
