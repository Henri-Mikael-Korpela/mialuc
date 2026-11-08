// A struct is a record type: a name for several fields. A struct object is made with the name of
// the struct and the fields that are given a value between braces, in any order, and the fields
// that are left out get their default value.
console.log("Name of a struct object:", Str(create_struct_object()));
console.log("Assigned field:", assign_field_of_struct_object());
console.log("Late initialization, with a default and then a value:", Str(late_initialization_of_struct_object()));
console.log("Defaults, an Int and a Str:", default_values_of_struct_object(),
    JSON.stringify(Str(default_str_of_struct_object())));
console.log("The age is given as 0, so the age is:", explicitly_initialized_struct_object());
console.log("The order of the fields does not matter:", order_of_initializers_is_free());
console.log("Without a trailing comma:", struct_object_without_trailing_comma());
console.log("Initializers are expressions, for 3:", initializers_are_expressions(3));
console.log("Name from a procedure:", Str(first_name()));

// A struct is the values of its fields one after the other, so a procedure that returns one
// returns several values, which JavaScript receives as an array. A Str field is two of them.
const [age, nameAddress, nameLength] = return_struct_object();
console.log("Returned struct object:", age, Str([nameAddress, nameLength]));
// A parameter that is a struct is as many parameters, in the same order.
console.log("Struct object as parameters, Person { age = 41 }:", next_age(41, 0, 0));
console.log("Returned from a procedure with a changed copy:", birthday(1, 0, 0));

// A struct object is a value: assigning or passing it copies it.
console.log("Fields can be assigned with operators (10 + 5, * 2, - 4, / 2):", assign_field_with_operators());
console.log("A copy has its own fields, 1 and 99:", struct_object_is_a_value());
console.log("Assigning a whole struct object:", assign_whole_struct_object());
console.log("Passing to a procedure:", pass_struct_object_to_proc());
console.log("The procedure changes its own copy, 6 and 5:", proc_changes_its_own_copy());
console.log("Name of the object a procedure returns:", Str(use_returned_struct_object()));

// A struct can have fields that are structs, defined by name or inside the struct.
console.log("Struct in a struct, 3 * 3 + 4 * 4:", struct_in_struct());
console.log("Nested struct with defaults:", default_of_nested_struct());
console.log("Nested fields assigned:", assign_nested_fields());
console.log("Name of a struct in a struct:", Str(create_nested_struct_object()));
console.log("Bool, Int, Str and struct fields:", mixed_field_types());
console.log("In a loop and in branches:", struct_in_loop(), struct_in_branches(true), struct_in_branches(false));

// An array can have struct objects as its elements, and so can an array slice.
console.log("Age of the first of three:", create_array_of_struct_objects());
console.log("Sum of all ages and of the last two ages:", array_of_struct_objects());
console.log("Fields of elements assigned:", assign_fields_of_elements());
console.log("Elements assigned and swapped:", assign_element_that_is_a_struct());
console.log("Names of elements:", Str(name_of_element(0)), Str(name_of_element(1)));
console.log("Go through struct objects, skipping one:", iterate_struct_objects());
console.log("Copy a range of struct objects:", copy_range_of_struct_objects());
console.log("Struct in a struct in an array:", nested_struct_in_array_element());

// An index outside of the array traps, also when it is followed by a field.
function trapped(call) {
    try {
        return call();
    } catch (error) {
        return `${error.name}: ${error.message}`;
    }
}
console.log("Field of element 0 and of element 1 of one:", element_out_of_bounds_field(0),
    trapped(() => element_out_of_bounds_field(1)));
