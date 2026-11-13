// A C_Str is the address of bytes that end with a zero, as a C string does. It is for passing text
// to and from code written in C, and a Str is the one to use in Mialu code. A Str is made a C_Str
// by the built-in procedure str_to_c_str, which the wasm target takes with a string literal or a
// constant that is a string only. A string literal, and a constant that is a string, is untyped,
// so it is a C_Str without str_to_c_str where a C_Str is wanted.

import foreign js "functions"

foreign js ->
    log_c_str :: proc(text: C_Str) -> ...

/// str_to_c_str makes a C_Str of a string literal.
greet :: proc() ->
    log_c_str(str_to_c_str("Hello, world!"))

farewell :: "Goodbye!"

/// str_to_c_str makes a C_Str of a constant that is a string, like of a literal.
say_farewell :: proc() ->
    log_c_str(str_to_c_str(farewell))

/// A C_Str is passed on like any other value.
log_twice :: proc(text: C_Str) ->
    log_c_str(text)
    log_c_str(text)

/// A variable can have the type C_Str too.
greet_twice :: proc() ->
    text : C_Str = str_to_c_str("Hi!")
    log_twice(text)

/// A string literal and a string constant are a C_Str where one is wanted.
greet_untyped :: proc() ->
    log_c_str("Hello without str_to_c_str!")
    log_c_str(farewell)
