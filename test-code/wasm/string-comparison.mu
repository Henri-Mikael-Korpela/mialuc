package string_comparison

/// Two strings are the same when they have the same bytes, so they are compared with `==` and
/// `not=`, and they have no order.
is_same_command :: proc() Bool ->
    command := "print"
    return command == "print"

is_other_command :: proc() Bool ->
    command := "print"
    return command not= "run"

/// A string with a different length is not the same, nor is one with a different byte.
is_longer_the_same :: proc() Bool ->
    command := "print"
    return command == "printed"

is_other_byte_the_same :: proc() Bool ->
    command := "print"
    return command == "prinT"

/// The empty string is the same as another empty string only.
is_empty_the_same :: proc() Bool ->
    nothing : Str
    return nothing == ""

is_nothing_the_same_as_text :: proc() Bool ->
    nothing : Str
    return nothing == "text"

/// Strings that are compared are also a parameter, a result and a field.
is_hello :: proc(text: Str) Bool ->
    return text == "hello"

greet :: proc() Str ->
    return "hello"

Person :: struct ->
    name: Str

count_alices :: proc() Int ->
    people := [const]Person { Person { name = "Alice" }, Person { name = "Bob" }, Person { name = "Alice" } }
    count := 0
    for person in people ->
        if person.name == "Alice" ->
            count += 1
    return count

is_greeting_hello :: proc() Bool ->
    return is_hello(greet())
