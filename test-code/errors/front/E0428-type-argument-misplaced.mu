package errors

twice :: proc(value: Int) Int ->
    return value * 2

main :: proc() Int ->
    return twice(^Int)
