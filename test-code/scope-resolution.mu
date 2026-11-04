NAME :: "Bob"

counter #(static) := 0

get_name :: proc() Str ->
    return NAME

get_current_counter :: proc() Int ->
    counter += 1
    return counter
