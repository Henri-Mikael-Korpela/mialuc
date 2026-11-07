// A C_Str is the address of bytes that end with a zero, as a C string does. It is for passing text
// to and from code written in C, and a Str is the one to use in Mialu code.

import foreign js "functions"

foreign js ->
    log_c_str :: proc(text: C_Str) -> ...

/// A string literal is a C_Str where one is wanted.
greet :: proc() ->
    log_c_str("Hello, world!")

/// A C_Str is passed on like any other value.
log_twice :: proc(text: C_Str) ->
    log_c_str(text)
    log_c_str(text)

/// A variable can have the type C_Str too.
greet_twice :: proc() ->
    text : C_Str = "Hi!"
    log_twice(text)
