package variadic_printing

import foreign libc

foreign libc ->
    sprintf :: proc(buffer: C_Str, format: C_Str, args: ..#(c_varargs) Any) -> ...

main :: proc() Int ->
    x := 5
    buffer := [64]Byte {}
    printf(str_to_c_str("X: "))
    return 0
