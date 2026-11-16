// Global variables, which are in the data of the program and get their initial values before main
// runs, and keep what the procedures assign to them. The results are the ones that the language
// specification gives. The exit code of the program is the number of wrong results, 0.

package global_state

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

LIMIT :: 100

counter #(static) := 0
ready #(static) : Bool = false
small #(static) : Byte = 200
label #(static) := "calls"
trace #(static) := 0
twice #(static) := LIMIT * 2

check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

next :: proc() Int ->
    counter += 1
    return counter

note :: proc(digit: Int) ->
    trace = trace * 10 + digit

/// The deferred statements run on the way out of every block that the return leaves.
return_runs_every_defer :: proc(n: Int) Int ->
    defer note(1)
    ->
        defer note(2)
        for i in 0..<n ->
            defer note(3)
            if i == 1 ->
                return 100
    note(4)
    return 0

main :: proc() Int ->
    wrong := 0
    next()
    next()
    wrong += check("the third call of next() is %lld", next(), 3)
    wrong += check("ready is %lld", one_if(ready), 0)
    ready = true
    wrong += check("ready is %lld after it is set", one_if(ready), 1)
    wrong += check("small is %lld", cast(Int)small, 200)
    wrong += check("twice is %lld", twice, 200)
    puts(str_to_c_str(label))

    return_runs_every_defer(3)
    wrong += check("return_runs_every_defer(3) notes %lld", trace, 3321)
    trace = 0
    return_runs_every_defer(0)
    wrong += check("return_runs_every_defer(0) notes %lld", trace, 241)
    return wrong

one_if :: proc(condition: Bool) Int ->
    if condition ->
        return 1
    return 0
