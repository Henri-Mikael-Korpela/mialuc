// Str values, which are the address of their bytes and their number: string literals and constants,
// Str variables, parameters, results and fields, arrays of Str, `str_to_c_str` of a Str that is
// worked out when the program runs, and match statements of Str and Byte values. The exit code
// of the program is the number of wrong results, 0.

package string_values

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

NAME : Str : "Mialu"

check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

/// Prints a Str, which the C library takes as a copy that ends with a zero.
say :: proc(text: Str) ->
    puts(str_to_c_str(text))

command_number :: proc(command: Str) Int ->
    match command ->
        case "go" ->
            return 1
        case "stop" ->
            return 2
        case "" ->
            return 3
        case ->
            return 0

/// A Str is returned like any value of two words, to the place that the caller made.
greeting :: proc(is_formal: Bool) Str ->
    if is_formal ->
        return "Good day"
    return "Hi"

Person :: struct ->
    name: Str
    age: Int

/// A match of a Byte, with a range.
is_digit :: proc(b: Byte) Bool ->
    match b ->
        case 48..<=57 ->
            return true
        case ->
            return false

main :: proc() Int ->
    wrong := 0
    say("Hello from a Str!")
    say(NAME)
    message := greeting(true)
    say(message)
    message = greeting(false)
    say(message)

    names := [const]Str { "Bob", "Alice", "Charlie" }
    for name in names ->
        say(name)
    middle := names[1:3]
    say(middle[1])

    person := Person { name = "Eve", age = 40 }
    say(person.name)
    pointer := %person
    pointer.name = "Zed"
    say(person.name)

    wrong += check("command_number(go) is %lld", command_number("go"), 1)
    wrong += check("command_number(stop) is %lld", command_number("stop"), 2)
    wrong += check("command_number(gone) is %lld", command_number("gone"), 0)
    wrong += check("command_number of nothing is %lld", command_number(""), 3)
    nothing : Str
    wrong += check("a Str without a value matches %lld", command_number(nothing), 3)

    digits := 0
    bytes := [const]Byte { 49, 50, 65, 57, 32 }
    for b in bytes ->
        if is_digit(b) ->
            digits += 1
    wrong += check("the digits are %lld", digits, 3)
    return wrong
