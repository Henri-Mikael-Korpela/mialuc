package errors

Pair :: struct($T: type_id) ->
    first: T
    second: T

main :: proc() Int ->
    pair : Pair(Int, Bool)
    return 0
