// A pointer to a procedure is written with a caret before the types of the procedure: `^proc(Int) Int`
// is a pointer to a procedure that takes an Int and returns an Int. The name of a procedure is a
// pointer to it, so `f := double` defines a variable `f` with that type, and a variable is called like
// a procedure: `f(21)`. Every example returns what it computed, which comes back as a BigInt for an Int.
console.log("A pointer with a deduced and a written type, 42 and 42:",
    pointer_with_deduced_type(), pointer_with_written_type());
console.log("A procedure without parameters, 42 + 1:", pointer_to_procedure_without_parameters());
console.log("A procedure with two parameters, 50 - 8:", two_parameters());

// A variable can point to another procedure later, as long as it has the same parameter types and
// return type, since those are what makes the type.
console.log("Point elsewhere, 10 * 100 + 15:", make_pointer_point_elsewhere());
console.log("Choose by a condition, 7 * 2 and 7 * 3:", choose_by_condition(true), choose_by_condition(false));
console.log("Choose in each pass of a loop, 1 * 2 + 2 * 3 + 3 * 2:", choose_in_a_loop());
console.log("Procedures with one signature are one type, 6 * 7:", the_same_signature_is_the_same_type());

// A procedure can take a pointer to a procedure, which makes it work with any procedure of that
// signature. A pointer can be passed on, and the call can be an argument of another call.
console.log("Pass a procedure, (10 * 2) * 100 + 10 * 3:", pass_procedure_as_argument());
console.log("Pass a pointer on, 3 * (3 * 2):", pass_procedure_pointer_on());
console.log("Two parameters, (20 + 22) * 100 + 6 * 7:", pass_a_procedure_of_two_parameters());
console.log("Bool and a call in a condition, 1 * 10 + 0:", procedure_returning_bool());
console.log("A Str as parameter and result:", Str(procedure_returning_str()));
console.log("No result, a call statement, 10 + 1 + 1 - 1:", procedure_without_result());
console.log("Calls inside calls, 2 * (3 * 2) + 2 * 3:", call_in_an_expression());
console.log("Arguments that are calls, 2 + (4 + 6):", pointers_in_arguments_of_calls());
console.log("A callback for each element of a slice, (2 + 4 + 6) * 100 + (3 + 6 + 9):",
    procedure_with_a_slice());
console.log("Recursion that passes the pointer on, 3 * (4 + 3 + 2 + 1):", recurse());
console.log("A variable made from a parameter, 8 + 10:", use_variable_defined_from_a_parameter());

// A pointer is compared with `==` and `not=`, and is equal to another when it points to the same
// procedure. `nil` is the null pointer, and so is a pointer that was not given a value.
console.log("nil, not nil and assigned, 1 + 10 + 100:", compare_with_nil());
console.log("A pointer without a value is nil:", Boolean(default_is_nil()));
console.log("Same procedure and another one, 1 + 10:", compare_pointers());

// Calling nil traps, which makes the call from JavaScript throw a WebAssembly.RuntimeError.
function trapped(call) {
    try {
        return call();
    } catch (error) {
        return `${error.name}: ${error.message}`;
    }
}
console.log("Call nil:", trapped(() => call_nil()));
console.log("Call a pointer without a value:", trapped(() => call_default()));
console.log("Pass nil on and call it:", trapped(() => nil_as_argument()));

// Procedure pointers can be the elements of an array or a slice, so that a program can have a table
// of procedures. An element is stored in a variable before it is called.
console.log("Call each of add, subtract and multiply on 6 and 3, 9 + 3 + 18:", array_of_procedures());
console.log("An element chosen by an index, 6 * 7:", index_array_of_procedures());
console.log("Assign to an element, 6 * 7:", assign_to_element_of_array_of_procedures());
console.log("A slice of procedures, 35 * 100 + 25:", slice_of_procedures());

// A field of a struct can be a pointer to a procedure, and it is nil when it is left out.
console.log("A field that is a procedure, 40 + 2:", struct_with_procedure_field());
console.log("Assign a field, 6 * 7:", assign_procedure_field());
console.log("A field that is left out:", Boolean(procedure_field_is_nil_by_default()));
console.log("A struct as an argument, 8 - 2:", struct_as_argument());

// A pointer to a pointer to a procedure, written ^^proc(Int) Int, can change which procedure a
// variable of its caller points to.
console.log("A pointer to a pointer to a procedure, 10 * 100 + 15:", pointer_to_pointer_to_procedure());
console.log("Replace the procedure of the caller, 5 * 3:", replace_through_a_pointer());
console.log("Follow a pointer to a pointer, 8 * 2:", call_through_a_pointer_to_pointer());
