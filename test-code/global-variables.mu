total_calls #(static) := 0
ready #(static) : Bool = true
label #(static) := "calls"
limit #(static) : Int = 3

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

copy_then_change :: proc() Int ->
    copy := total_calls
    copy += 100
    return total_calls

calls_until_limit :: proc() Int ->
    for ->
        if total_calls >= limit ->
            return total_calls
        total_calls += 1

use_before_definition :: proc() Int ->
    return later

later #(static) := 40
