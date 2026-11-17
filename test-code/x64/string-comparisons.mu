// Strings compared with `==` and `not=`: two strings are the same when they have the same bytes.
// The operands are string literals, constants, `Str` variables, parameters, results, fields and
// elements, and the comparison is also the condition of an if statement and part of `and` and
// `or`. The exit code of the program is the number of wrong results, 0.

package string_comparisons

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

NAME : Str : "Mialu"
SAME_NAME :: "Mialu"

Person :: struct ->
    name: Str
    age: Int

/// Prints a line with the number, and counts it as wrong when it is not what was expected.
check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

/// A `Bool` is 1 or 0.
number_of :: proc(is_true: Bool) Int ->
    if is_true ->
        return 1
    return 0

is_hello :: proc(text: Str) Bool ->
    return text == "hello"

greeting :: proc(is_formal: Bool) Str ->
    if is_formal ->
        return "Good day"
    return "Hi"

main :: proc() Int ->
    wrong := 0

    // Literals and constants, which the compiler compares.
    wrong += check("literal == literal is %lld", number_of("go" == "go"), 1)
    wrong += check("literal not= literal is %lld", number_of("go" not= "gone"), 1)
    wrong += check("constant == literal is %lld", number_of(SAME_NAME == "Mialu"), 1)

    // Variables, which are compared when the program runs.
    word := greeting(false)
    same := greeting(false)
    other := greeting(true)
    wrong += check("variable == same is %lld", number_of(word == same), 1)
    wrong += check("variable == other is %lld", number_of(word == other), 0)
    wrong += check("variable not= other is %lld", number_of(word not= other), 1)
    wrong += check("variable not= same is %lld", number_of(word not= same), 0)
    wrong += check("variable == literal is %lld", number_of(word == "Hi"), 1)
    wrong += check("literal == variable is %lld", number_of("Hi" == word), 1)
    wrong += check("variable not= literal is %lld", number_of(word not= "Hi"), 0)

    // Different lengths, and the same length with a different byte at each place.
    wrong += check("prefix is %lld", number_of(word == "H"), 0)
    wrong += check("longer is %lld", number_of(word == "Hii"), 0)
    wrong += check("first byte is %lld", number_of(word == "Ki"), 0)
    wrong += check("last byte is %lld", number_of(word == "Hj"), 0)
    wrong += check("different case is %lld", number_of(word == "hi"), 0)

    // The empty string is the same as another one only.
    nothing : Str
    wrong += check("nothing == empty is %lld", number_of(nothing == ""), 1)
    wrong += check("nothing == word is %lld", number_of(nothing == word), 0)
    wrong += check("word not= nothing is %lld", number_of(word not= nothing), 1)

    // A typed constant, a parameter, a result, a field and an element.
    wrong += check("typed constant is %lld", number_of(NAME == "Mialu"), 1)
    wrong += check("parameter is %lld", number_of(is_hello("hello")), 1)
    wrong += check("parameter other is %lld", number_of(is_hello("help")), 0)
    wrong += check("result is %lld", number_of(greeting(true) == "Good day"), 1)
    person := Person { name = "Eve", age = 40 }
    wrong += check("field is %lld", number_of(person.name == "Eve"), 1)
    wrong += check("field other is %lld", number_of(person.name == "Adam"), 0)
    names := [const]Str { "Bob", "Alice", "Charlie" }
    wrong += check("element is %lld", number_of(names[1] == "Alice"), 1)
    wrong += check("elements are %lld", number_of(names[0] == names[2]), 0)

    // As a condition, with `and` and `or`, and in a loop, which compares many times.
    if word == "Hi" and word not= other ->
        wrong += check("condition is %lld", 1, 1)
    else ->
        wrong += check("condition is %lld", 0, 1)
    wrong += check("or is %lld", number_of(word == "no" or word == "Hi"), 1)
    count := 0
    for name in names ->
        if name not= "Alice" ->
            count += 1
    wrong += check("names that are not Alice are %lld", count, 2)

    // The same bytes in two places are the same strings.
    joined : Str = "Mi" ++ "alu"
    wrong += check("joined is %lld", number_of(joined == NAME), 1)

    return wrong
