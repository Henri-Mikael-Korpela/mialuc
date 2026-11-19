package errors

pair :: proc() (Int, Int) ->
    return 1, 2

main :: proc() Int ->
    a, b, c := pair()
    return 0
