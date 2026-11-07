import foreign libc

foreign libc ->
    printf :: proc(content: C_Str) -> ...

main :: proc() Int ->
    printf(str_to_c_str("Hello, world!"))
    return 0
