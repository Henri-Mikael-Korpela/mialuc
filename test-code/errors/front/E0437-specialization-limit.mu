package errors

Box :: struct($T: type_id) ->
    item: T

/// Each call is of a copy for a larger type than its own, so the copies never end.
nest :: proc(value: $T) Int ->
    return nest(Box(T) { item = value })

main :: proc() Int ->
    return nest(1)
