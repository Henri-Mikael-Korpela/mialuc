// Deferred statements, which run when their block is left: at its end, and at every `break`,
// `continue` and `return` that leaves it, the innermost block first and in each block the last
// `defer` first. The results are the ones that the language specification gives. The exit code of
// the program is the number of results that are wrong, 0.

package deferred_cleanup

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

/// Prints a line with the number, and counts it as wrong when it is not what was expected.
check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

/// Each pass of the loop runs its deferred statement, which adds a digit to the trace.
trace_of_loop :: proc(n: Int) Int ->
    trace := 0
    for i in 0..<n ->
        defer trace = trace * 10 + 3
        if i == 1 ->
            continue
        trace = trace * 10 + 1
    return trace

/// A `break` runs the deferred statements of the block of the loop that it leaves, and the scope
/// around the loop goes on, and runs its own at its end: 1, then 5, then 2.
trace_of_break :: proc() Int ->
    trace := 0
    ->
        defer trace = trace * 10 + 2
        for ->
            defer trace = trace * 10 + 1
            break
        trace = trace * 10 + 5
    return trace

/// A `return` computes its value first, so a deferred statement cannot change it.
return_value_comes_first :: proc() Int ->
    x := 1
    defer x += 10
    return x

/// The deferred statements of a block run in the reverse order of their `defer`s, and a deferred
/// scope and a deferred if statement are written whole.
reverse_order :: proc(n: Int) Int ->
    trace := 0
    ->
        defer trace = trace * 10 + 1
        defer ->
            trace = trace * 10 + 2
            trace = trace * 10 + 3
        defer if n > 5 ->
            trace = trace * 10 + 4
        else ->
            trace = trace * 10 + 5
        trace = trace * 10 + 6
    return trace

/// A deferred statement reads the variables when it runs, not where the `defer` is.
last_value :: proc() Int ->
    trace := 0
    ->
        x := 1
        defer trace = x
        x = 7
    return trace

/// A `return` runs the deferred statements of every block that it is in, out to the body of the
/// procedure, which print "loop", "scope" and "body" in that order.
return_runs_every_defer :: proc(n: Int) Int ->
    defer puts("    body")
    ->
        defer puts("    scope")
        for i in 0..<n ->
            defer puts("    loop")
            if i == 1 ->
                return 100
    puts("    not reached when n > 1")
    return 0

main :: proc() Int ->
    wrong := 0
    puts("return_runs_every_defer(3) prints:")
    wrong += check("and returns %lld", return_runs_every_defer(3), 100)
    wrong += check("trace_of_loop(3) is %lld", trace_of_loop(3), 13313)
    wrong += check("trace_of_break() is %lld", trace_of_break(), 152)
    wrong += check("return_value_comes_first() is %lld", return_value_comes_first(), 1)
    wrong += check("reverse_order(9) is %lld", reverse_order(9), 64231)
    wrong += check("reverse_order(1) is %lld", reverse_order(1), 65231)
    wrong += check("last_value() is %lld", last_value(), 7)
    return wrong
