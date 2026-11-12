// Variables of the types Int and C_Str. A variable takes the type of its value, or has its type
// written, and without a value it has the default value of its type. Its value is kept in the
// frame of its procedure, where a debugger finds it too. The exit code of the program is the value
// of total at the end of main, which is 42.

import foreign libc

foreign libc ->
    puts :: proc(content: C_Str) Int -> ...

greeting :: "Hello from a variable!"

/// A variable can be given a parameter, and passed on as an argument.
echo :: proc(text: C_Str, number: Int) Int ->
    copy := text
    puts(copy)
    result : Int = number
    return result

main :: proc() Int ->
    message := str_to_c_str(greeting)
    puts(message)

    count : Int
    count = echo(str_to_c_str("Counting"), 41)

    total := count
    total = echo(message, 42)
    return total
