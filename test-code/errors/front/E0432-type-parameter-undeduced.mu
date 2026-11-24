package errors

zero :: proc($T: type_id) T ->
    nothing : T
    return nothing

main :: proc() Int ->
    return zero()
