// Constants, which the compiler works out. A literal, and a constant without a type, is untyped:
// it becomes the type that its place wants, so a number is an Int, and a string is a C_Str where
// one is wanted, which passes it to the C library without str_to_c_str. The exit code of the
// program is the value of ANSWER, which is 42.

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

KIBIBYTE :: 0x400
MEBIBYTE :: KIBIBYTE * KIBIBYTE
/// A typed constant has its type, like a variable.
COUNT : Int : 3
ANSWER :: COUNT * 10 + 12

GREETING :: "Hello from a constant!"

/// Prints a line that has the number in it, where the format has `%lld`.
show :: proc(format: C_Str, number: Int) Int ->
    printf(format, number)
    puts("")
    return number

main :: proc() Int ->
    puts(GREETING)
    puts("A string literal is a C_Str where one is wanted.")
    show("1 MiB is %lld bytes", MEBIBYTE)
    show("3 MiB is %lld bytes", COUNT * MEBIBYTE)
    size := KIBIBYTE / 2
    show("Half a KiB is %lld bytes", size)
    return ANSWER
