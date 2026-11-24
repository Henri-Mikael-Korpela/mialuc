package errors

add :: proc(a: $T, b: T) T ->
    return a + b

main :: proc() Int ->
    count := 1
    half := 0.5
    total := add(count, half)
    return 0
