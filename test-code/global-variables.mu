// Global variables have static storage, so they keep their value between calls. They can be
// used in every procedure of the module, also above where they are defined.

/// The type of a global variable can be deduced, as for a variable.
total_calls #(static) := 0
/// Or written, which is the same.
ready #(static) : Bool = true
label #(static) := "calls"
limit #(static) : Int = 3

/// Changes a global variable, so the change outlasts the call.
count_call :: proc() Int ->
    total_calls += 1
    return total_calls

get_total_calls :: proc() Int ->
    return total_calls

step :: proc(by: Int) Int ->
    total_calls += by
    total_calls *= 2
    total_calls -= 1
    total_calls /= 2
    return total_calls

is_ready :: proc() Bool ->
    return ready

get_label :: proc() Str ->
    return label

/// Changing a copy of a global variable does not change the global variable.
copy_then_change :: proc() Int ->
    copy := total_calls
    copy += 100
    return total_calls

/// A loop without a range goes on until it returns.
calls_until_limit :: proc() Int ->
    for ->
        if total_calls >= limit ->
            return total_calls
        total_calls += 1

/// A global variable can be used above the line that defines it.
use_before_definition :: proc() Int ->
    return later

later #(static) := 40
