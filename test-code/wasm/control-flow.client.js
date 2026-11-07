// A scope is a block of its own, written `->` alone on a line. A `break` in it leaves it, and the
// statement after the scope runs next. The `break` of an if statement leaves the scope around it,
// because the block of an if is not a scope to leave.
console.log("Break in a scope, skips x += 2:", break_early());
// A scope can have a label, which is written `@name`, with no space after the `@`. A `break` names
// the label without the `@`, to leave that scope.
console.log("Break with a label:", break_early_with_label());
console.log("A scope without a break runs once:", scope_without_break());
// A variable of a scope is gone after it, so two scopes can define one name.
console.log("Same variable name in two scopes:", scopes_with_same_variable_name());
// A `break` without a label leaves the innermost scope only.
console.log("Break leaves the innermost scope, for 0 and 1:", break_leaves_innermost_scope(0n),
    break_leaves_innermost_scope(1n));
// A label names a scope further out, so a `break` can leave several scopes at once.
console.log("Break the outer scope, for 0 and 1:", break_outer_scope_by_label(0n),
    break_outer_scope_by_label(1n));
console.log("Break the inner scope, for 0 and 1:", break_inner_scope_by_label(0n),
    break_inner_scope_by_label(1n));
console.log("Scopes one after another, for 0 and 1:", label_by_the_innermost_name_only(0n),
    label_by_the_innermost_name_only(1n));
// A `break` without a label in a loop leaves the scope that is inside the loop, and not the loop.
console.log("Break leaves the scope, not the loop, for 3 and 7:", break_from_loop_leaves_scope_only(3n),
    break_from_loop_leaves_scope_only(7n));
console.log("Break a labeled scope in a loop, for 3 and 7:", break_scope_label_inside_loop(3n),
    break_scope_label_inside_loop(7n));
// A labeled scope around loops can be left from inside them, which leaves all of the loops.
console.log("Break out of nested loops, for 0, 1, 3 and 7:", break_loop_by_label(0n),
    break_loop_by_label(1n), break_loop_by_label(3n), break_loop_by_label(7n));
// A `continue` goes on with the innermost loop, also when scopes are in between.
console.log("Continue in a scope in a loop, for 3 and 7:", continue_in_scope_in_loop(3n),
    continue_in_scope_in_loop(7n));
console.log("Continue and break in an endless loop, for 1, 3 and 7:",
    continue_after_scope_in_endless_loop(1n), continue_after_scope_in_endless_loop(3n),
    continue_after_scope_in_endless_loop(7n));
// A scope can be in the block of an if statement.
console.log("Scopes in the branches of an if, for 0, 1 and 7:", scope_in_branch_of_if(0n),
    scope_in_branch_of_if(1n), scope_in_branch_of_if(7n));
// A label can also be given to an if statement, `@name if condition ->`, and a `break` with the
// label leaves the whole statement, with its `else if` and `else` branches. The label is on the `if`
// line only. A `break` without a label passes a labeled if statement, as it passes any other.
console.log("Break a labeled if, for 11, 7, 3 and 0:", break_labeled_if(11n), break_labeled_if(7n),
    break_labeled_if(3n), break_labeled_if(0n));
console.log("Break a labeled if from a loop in it, for 0, 2 and 5:", break_labeled_if_from_loop(0n),
    break_labeled_if_from_loop(2n), break_labeled_if_from_loop(5n));
console.log("Labeled ifs inside each other, for (0, 0), (1, 0), (1, 1) and (6, 1):",
    labeled_ifs_inside_each_other(0n, 0n), labeled_ifs_inside_each_other(1n, 0n),
    labeled_ifs_inside_each_other(1n, 1n), labeled_ifs_inside_each_other(6n, 1n));
console.log("Plain break passes a labeled if, for 0, 1 and 5:", plain_break_passes_labeled_if(0n),
    plain_break_passes_labeled_if(1n), plain_break_passes_labeled_if(5n));
// A for statement or a loop without a range can have a label too, `@name for ...` and
// `@name for ->`. A `break` leaves the labeled loop, also from an inner loop, and a `continue`
// goes on with its next pass. Only a loop can be named by a `continue`.
console.log("Continue the outer loop from the inner one, for 0, 1 and 3:", continue_labeled_for(0n),
    continue_labeled_for(1n), continue_labeled_for(3n));
console.log("Break the outer loop from the inner one, for 0, 2 and 3:", break_labeled_for(0n),
    break_labeled_for(2n), break_labeled_for(3n));
console.log("Break a labeled loop without a range, for 10, 100:", break_labeled_endless_loop(10n),
    break_labeled_endless_loop(100n));
console.log("Continue a labeled loop without a range, for 0, 4 and 7:",
    continue_labeled_endless_loop(0n), continue_labeled_endless_loop(4n),
    continue_labeled_endless_loop(7n));
