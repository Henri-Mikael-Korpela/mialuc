import foreign libc

foreign libc ->
    printf :: proc(content: C_Str) -> ...

main :: proc() Int ->
    printf("Hello, world!")
    return 0
