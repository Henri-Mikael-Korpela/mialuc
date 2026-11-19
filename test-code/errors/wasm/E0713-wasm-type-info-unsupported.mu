package errors

Point :: struct ->
    x: Int
    y: Int

info #(static) := type_info_of(Point).members

main :: proc() Int ->
    return 0
