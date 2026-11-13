// Defer: `defer` and a statement after it on the same line. The statement does not run where the
// `defer` is, but when the block that the `defer` is in is left: at the end of the block, and at a
// `break`, a `continue` or a `return` that leaves it. The deferred statements of a block run in
// the reverse order of their `defer`s. Each example notes what it does as digits of `trace`.

/// The steps that the examples take, one digit for each, in the order that they are taken.
trace #(static) := 0

/// Notes the step `digit`, after the steps before it.
note :: proc(digit: Int) ->
    trace = trace * 10 + digit

/// Returns the steps that were noted since the last call, and starts over.
take_trace :: proc() Int ->
    steps := trace
    trace = 0
    return steps

/// The deferred statement runs at the end of the procedure, after the statements below it.
defer_runs_at_the_end :: proc() ->
    defer note(3)
    note(1)
    note(2)

/// The deferred statements run the last one first, like things are put away in the reverse order
/// of taking them out.
defers_run_in_reverse :: proc() ->
    defer note(1)
    defer note(2)
    defer note(3)
    note(4)

/// A `defer` belongs to the block that it is in, which is a scope here, and not to the procedure.
defer_belongs_to_its_block :: proc() ->
    defer note(5)
    ->
        defer note(2)
        note(1)
    note(3)
    note(4)

/// `defer ->` defers a scope, which runs its block of statements as one.
defer_a_block :: proc() ->
    defer ->
        note(3)
        note(4)
    note(1)
    note(2)

/// The deferred statement reads the variables when it runs, so it sees the last value of `x`.
defer_reads_variables_when_it_runs :: proc() ->
    x := 1
    defer note(x)
    x = 7

/// A `defer` that is not reached does nothing, so an early `return` runs only the first one.
defer_only_when_reached :: proc(early: Bool) Int ->
    defer note(1)
    if early ->
        return 0
    defer note(2)
    note(3)
    return 0

/// The value of a `return` is computed before the deferred statements run, so the change of `x`
/// comes too late for it.
return_value_comes_first :: proc() Int ->
    x := 1
    defer x += 10
    return x

/// The deferred change of what a pointer points to is seen by the caller, after the call.
increment_after_return :: proc(counter: ^Int) Int ->
    defer counter^ += 1
    return counter^

change_through_pointer_after_return :: proc() Int ->
    counter := 5
    before := increment_after_return(%counter)
    return before * 10 + counter

/// The block of a loop is left after every pass, so its deferred statement runs on every pass.
defer_in_loop :: proc(n: Int) ->
    for i in 1..<=n ->
        defer note(i)
        note(9)

/// A `continue` leaves the block of the pass, so it runs the deferred statement of the pass.
defer_with_continue :: proc(n: Int) ->
    for i in 1..<=n ->
        defer note(i)
        if i == 2 ->
            continue
        note(9)

/// A `break` runs the deferred statement of the pass it leaves, and not the one of the procedure,
/// which runs when the procedure ends.
defer_with_break :: proc() ->
    defer note(9)
    for i in 1..<=5 ->
        defer note(i)
        if i == 3 ->
            break
    note(8)

/// A loop without a range is left by `break` the same way.
defer_in_loop_without_range :: proc() ->
    count := 0
    for ->
        count += 1
        defer note(count)
        if count == 3 ->
            break

/// A `return` leaves every block that it is in, so it runs all of their deferred statements, the
/// innermost first.
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

/// A `break` with a label leaves several scopes at once, and runs the deferred statements of each.
break_by_label_runs_defers :: proc() ->
    @outer ->
        defer note(3)
        ->
            defer note(2)
            note(1)
            break outer
    note(4)

/// The block of a branch is left at its end, before the statement after the if statement.
defer_in_branch :: proc(n: Int) ->
    if n > 0 ->
        defer note(2)
        note(1)
    else ->
        defer note(4)
        note(3)
    note(5)

/// An if statement can be deferred with its `else` branches. The `defer` is on the `if` line only.
defer_if_statement :: proc(n: Int) ->
    defer if n > 5 ->
        note(2)
    else if n > 0 ->
        note(3)
    else ->
        note(4)
    note(1)

/// A for statement can be deferred too.
defer_for_statement :: proc() ->
    defer for i in 1..<=3 ->
        note(i)
    note(9)

/// A deferred scope can have a label, and a `break` inside it leaves it. A jump in a deferred
/// statement stays inside it, because the block around it is being left already.
break_inside_deferred_scope :: proc(n: Int) ->
    defer @cleanup ->
        note(1)
        if n > 0 ->
            break cleanup
        note(2)
    note(3)

/// The deferred statement can use an array of the procedure, which still exists when it runs.
defer_with_array :: proc() Int ->
    values := [const]Int { 1, 2, 3 }
    defer values[0] = 100
    defer note(values[2])
    values[2] = 5
    return values[0]

/// What is set up is undone right next to it, and every way out of the procedure undoes it, which
/// is what `defer` is for. `depth` is how many calls are in progress.
depth #(static) := 0
depth_max #(static) := 0

enter :: proc() ->
    depth += 1
    if depth > depth_max ->
        depth_max = depth

leave :: proc() ->
    depth -= 1

/// Sums the numbers from 1 to `n`, calling itself for the rest of them, and stops early at `limit`.
sum_with_depth :: proc(n: Int, limit: Int) Int ->
    enter()
    defer leave()
    if n == 0 ->
        return 0
    if n > limit ->
        return sum_with_depth(limit, limit)
    return n + sum_with_depth(n - 1, limit)

get_depth :: proc() Int ->
    return depth

get_depth_max :: proc() Int ->
    return depth_max
