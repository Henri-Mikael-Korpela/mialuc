// `defer` runs a statement when the block that it is in is left. Each example notes its steps as
// the digits of a number, which take_trace returns, so the digits show the order of the steps.
defer_runs_at_the_end();
console.log("Runs at the end of the procedure, 123:", take_trace());
defers_run_in_reverse();
// The last `defer` runs first.
console.log("Runs in reverse order, 4321:", take_trace());
defer_belongs_to_its_block();
console.log("Runs at the end of its own block, 12345:", take_trace());
defer_a_block();
console.log("Defers a whole block, 1234:", take_trace());
// The deferred statement reads the variables when it runs, not when the `defer` is reached.
defer_reads_variables_when_it_runs();
console.log("Reads the last value of the variable, 7:", take_trace());
// Only a `defer` that was reached runs.
defer_only_when_reached(true);
console.log("Leaves early, 1:", take_trace());
defer_only_when_reached(false);
console.log("Reaches both, 321:", take_trace());
// `return` computes its value, then the deferred statements run, and then it returns the value.
console.log("Returned value is computed first, 1:", return_value_comes_first());
console.log("Deferred change through a pointer, 56:", change_through_pointer_after_return());
// The block of a loop is left after every pass, by its end, by `continue` and by `break`.
defer_in_loop(3n);
console.log("Every pass of a loop, 919293:", take_trace());
defer_with_continue(3n);
console.log("Continue runs it, 91293:", take_trace());
defer_with_break();
console.log("Break runs it, 12389:", take_trace());
defer_in_loop_without_range();
console.log("Loop without a range, 123:", take_trace());
// A `return` runs the deferred statements of every block that it leaves, the innermost first.
console.log("Return from inside a loop, 100:", return_runs_every_defer(3n),
    "and the steps, 3321:", take_trace());
console.log("Return at the end, 0:", return_runs_every_defer(0n),
    "and the steps, 241:", take_trace());
break_by_label_runs_defers();
console.log("Break with a label leaves two scopes, 1234:", take_trace());
defer_in_branch(1n);
console.log("Defer in the if branch, 125:", take_trace());
defer_in_branch(0n);
console.log("Defer in the else branch, 345:", take_trace());
// An if statement, a for statement and a scope can be deferred as a whole.
defer_if_statement(7n);
const first = take_trace();
defer_if_statement(3n);
const second = take_trace();
defer_if_statement(0n);
console.log("Deferred if statement for 7, 3 and 0, 12 13 14:", first, second, take_trace());
defer_for_statement();
console.log("Deferred for statement, 9123:", take_trace());
break_inside_deferred_scope(1n);
const leftEarly = take_trace();
break_inside_deferred_scope(0n);
console.log("Break inside a deferred scope, for 1 and 0, 31 312:", leftEarly, take_trace());
console.log("Deferred assignment to an array, 1:", defer_with_array(),
    "and the element that it noted, 5:", take_trace());
// What is set up is undone by the `defer` next to it, whichever `return` leaves the procedure.
console.log("Sum to 4, 10:", sum_with_depth(4n, 10n), "deepest, 5:", get_depth_max(),
    "depth after it, 0:", get_depth());
console.log("Sum to 20 that stops at 3, 6:", sum_with_depth(20n, 3n), "depth after it, 0:",
    get_depth());
