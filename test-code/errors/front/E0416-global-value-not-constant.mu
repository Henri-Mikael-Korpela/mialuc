package errors

base :: proc() Int ->
    return 1

counter #(static) := base()

main :: proc() Int ->
    return 0
