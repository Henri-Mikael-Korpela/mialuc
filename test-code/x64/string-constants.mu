// A constant that is a string is in the read-only data of the program, like a string literal, and
// str_to_c_str makes a C_Str of it the same way. The exit code of the program is 0.

import foreign libc

foreign libc ->
    puts :: proc(content: C_Str) Int -> ...

greeting :: "Hello from a constant!"
farewell :: "Goodbye from a constant!"

say :: proc(text: C_Str) ->
    puts(text)

main :: proc() Int ->
    puts(str_to_c_str(greeting))
    say(str_to_c_str(farewell))
    return 0
