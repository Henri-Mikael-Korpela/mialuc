package errors

nothing :: proc() ->
    y := 1

main :: proc() Int ->
    x := nothing()
    return 0
