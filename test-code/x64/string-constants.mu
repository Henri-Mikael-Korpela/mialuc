// A constant that is a string is in the read-only data of the program, like a string literal, and
// str_to_c_str makes a C_Str of it the same way. `++` joins strings and runes when the program is
// compiled, so a joined string is in the read-only data too. The exit code of the program is 0.

package string_constants

import foreign libc

foreign libc ->
    puts :: proc(content: C_Str) Int -> ...

greeting :: "Hello from a constant!"
farewell :: "Goodbye from a constant!"

// A rune can be joined to a string on either side, and the result can be joined again.
exclamation :: '!'
excited :: greeting ++ exclamation ++ " " ++ '🚀'

// A constant that has the type Str can be joined as well.
framed : Str : '«' ++ excited ++ '»' ++ " " ++ farewell

say :: proc(text: C_Str) ->
    puts(text)

main :: proc() Int ->
    puts(str_to_c_str(greeting))
    say(str_to_c_str(farewell))
    puts(str_to_c_str(excited))
    say(str_to_c_str(framed))
    puts(str_to_c_str("<" ++ framed ++ '>'))
    return 0
