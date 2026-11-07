// Names are resolved in the module: a constant and a global variable can be used by every
// procedure of it.

/// A constant of the module, which every procedure can use.
NAME :: "Bob"

/// A global variable of the module. `static` is required for a global variable.
counter #(static) := 0

/// Uses a constant that is defined above it.
get_name :: proc() Str ->
    return NAME

/// Changes a global variable, so each call returns a bigger number.
get_current_counter :: proc() Int ->
    counter += 1
    return counter
