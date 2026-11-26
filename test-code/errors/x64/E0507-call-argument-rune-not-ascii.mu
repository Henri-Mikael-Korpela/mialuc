package errors

is_byte :: proc(b: Byte, wanted: Byte) Bool ->
    return b == wanted

main :: proc() Int ->
    found := is_byte(65, 'é')
    return 0
