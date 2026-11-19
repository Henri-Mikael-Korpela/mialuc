package errors

foreign libc ->
    puts :: proc(content: C_Str) Int -> ...

main :: proc() Int ->
    return 0
