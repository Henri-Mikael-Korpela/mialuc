// A pointer is the address of a value. Its type is the type of the value with a caret before it, so
// `^Int` is a pointer to an Int. `%x` takes the address of a variable, and `p^` follows the pointer
// `p`, to read the value it points to or to assign a new one. Every example returns what it
// changed, which comes back as a BigInt for an Int.
console.log("Pointer with a deduced and a written type, 4 + 1 twice:",
    pointer_with_implicit_type(), pointer_with_explicit_type());

// Following a pointer reads and writes the variable itself, with `=` and with the operators.
console.log("Read through a pointer, 20 + 1:", read_through_pointer());
console.log("Assign through a pointer:", assign_through_pointer());
console.log("The pointer sees later changes of the variable, 7 and 10:", see_changes_of_variable());
console.log("Operators through a pointer, ((10 + 5) * 2 - 4) / 2:", operators_through_pointer());
console.log("A Bool and a Str:", Boolean(pointer_to_bool()), Str(pointer_to_str()));

// A pointer can be passed to a procedure, which then changes the variable of its caller. The
// procedure gets a copy of the pointer, but the copy points to the same variable.
console.log("Increment twice through a parameter, 10 + 2:", pointer_as_parameter());
console.log("Swap 3 and 7:", swap_through_pointers());
console.log("Set a flag:", Boolean(flag_through_pointer()));
console.log("Pass a pointer on to another procedure, twice:", pointer_passed_on());
console.log("Follow a pointer in a condition, for 500:", condition_through_pointer());
console.log("Recursion that adds to one variable, 1 + 2 + ... + 10:", recursion_with_pointer());
console.log("A variable in a block gets a new place per pass, 2 + 5 + 10:",
    pointer_to_variable_in_block());

// A pointer can point to a pointer, with a caret more: ^^Int. Following it twice reaches the Int.
console.log("Pointer to a pointer, 5 + 1:", pointer_to_pointer());
console.log("Make a pointer point elsewhere, a and b:", change_where_pointer_points());
// A procedure can return a pointer, as long as what it points to still exists. A pointer to a
// variable of the procedure that returns it points to nothing after it returns.
console.log("Return one of two pointers, x and y:", return_pointer());

// A pointer to a struct reaches its fields with `p^.age`, and the whole struct with `p^`.
console.log("Read a field, 30:", read_field_through_pointer());
console.log("Assign a field, 31 + 1:", assign_field_through_pointer());
console.log("A Str field:", Str(str_field_through_pointer()));
console.log("Assign a whole struct object:", assign_whole_struct_through_pointer());
console.log("A copy of the struct object, 30 and 99:", read_whole_struct_through_pointer());
console.log("Struct object as a pointer parameter, 40 + 2:", struct_as_pointer_parameter());
console.log("The pointer sees later changes of a field, 8 + 1:", see_changes_of_struct());

// The address of a field, a field of a field, an element and a field of an element can be taken.
console.log("Pointer to a field, 30 + 12:", pointer_to_field());
console.log("Pointer to a nested field, 1 + 2 + 3 + 40:", pointer_to_nested_field());
console.log("Pointer to a nested struct object:", pointer_to_nested_struct());
console.log("Pointer to a field through a pointer:", pointer_to_field_through_pointer());
console.log("Pointer to a pointer to a struct object:", pointer_to_pointer_to_struct());
console.log("The address of what a pointer points to is the pointer:", address_of_dereference());
console.log("Pointer to an element:", pointer_to_element());
console.log("Pointer to an element of a slice, which is one of the array:",
    pointer_to_slice_element());
console.log("Pointer to a field of an element:", pointer_to_field_of_element());

// A pointer can be returned from a procedure that gets a slice, to point to one of its elements.
console.log("Double the largest of 3 9 4:", double_the_largest());

// An array can have pointers as its elements, and so can a slice, written [const]^Int and []^Int.
console.log("Sum of what three pointers point to, 1 + 20 + 300:", sum_of_pointed_to());
console.log("Change what each of two pointers point to, 1 * 10 + 5 and 2 * 10:",
    change_all_pointed_to());
console.log("A slice of pointers, 1 + 10 and 2 + 10:", slice_of_pointers());

// A struct can have a pointer to a struct of the same kind, which makes a chain.
console.log("Follow two pointers in structs, 2 and 3:", follow_pointers_in_structs());
console.log("Assign through a pointer in a struct, 30 + 1:", assign_through_pointers_in_structs());

// A pointer that was not given a value is null, and following it traps, which makes the call from
// JavaScript throw a WebAssembly.RuntimeError.
function trapped(call) {
    try {
        return call();
    } catch (error) {
        return `${error.name}: ${error.message}`;
    }
}
console.log("Read a null pointer:", trapped(() => read_null_pointer()));
console.log("Assign through a null pointer:", trapped(() => assign_null_pointer()));
console.log("Follow a null pointer in a struct:", trapped(() => follow_null_pointer_in_struct()));

// A pointer is a 32-bit address in the memory of the module, so JavaScript passes it as a number,
// and can write the value it points to. An Int takes 8 bytes. Address 0 is null, so the Ints
// start at 8.
const ints = new BigInt64Array(memory.buffer);
ints[1] = 41n;
increment(8);
console.log("JavaScript wrote 41 at address 8 and called increment(8):", ints[1]);
ints[2] = 5n;
swap(8, 16);
console.log("swap(8, 16) swaps the Ints at two addresses, 5 and 42:", ints[1], ints[2]);
