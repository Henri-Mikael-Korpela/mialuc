// A global variable is defined outside of every procedure with the attribute `static`, written
// `#(static)`. It is initialized once, when the module is instantiated, and keeps its value between
// calls, so every call of count_call counts on.
console.log("First call:", count_call());
console.log("Second call:", count_call());
console.log("Total calls:", get_total_calls());
// An assignment on a global variable works as on a variable.
console.log("Step by 4:", step(4n));
// A variable of a procedure that is set from a global variable is a copy.
console.log("Copy changed, global unchanged:", copy_then_change());
// A global variable has a type like any variable: Bool, Str, Int.
console.log("Ready:", Boolean(is_ready()));
console.log("Label:", Str(get_label()));
// A global variable can be used in a condition, and a procedure can use one defined below it.
console.log("Calls until the limit:", calls_until_limit());
console.log("Defined below the procedure:", use_before_definition());
