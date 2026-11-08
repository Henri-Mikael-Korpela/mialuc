// Procedures that take a C_Str and an Int, calls of the C library, and temporary strings: the copy
// that str_to_c_str makes lives until the procedure that called it returns. The exit code of the
// program is the result of main, which is 7.

import foreign libc

foreign libc ->
    puts :: proc(content: C_Str) Int -> ...

say :: proc(text: C_Str, number: Int) Int ->
    puts(text)
    puts(str_to_c_str("temporary string in say"))
    return number

main :: proc() Int ->
    puts(str_to_c_str("string literal"))
    puts(str_to_c_str("temporary string in main"))
    return say(str_to_c_str("argument"), 7)
