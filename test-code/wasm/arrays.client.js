// An array lives on the stack of the procedure that defines it, and an array slice, written
// []Int, views some of its elements. Every example returns an Int, which comes back as BigInt.
// The length of an array is a number, or `const` to count the elements of the literal.
console.log("Element 0 with a deduced and an explicit length:",
    array_element_access_with_deduced_length(), array_element_access_with_explicit_length());
console.log("Declared type and no elements:", array_with_declared_type(), array_of_nothing());
// An element can be assigned to, also with += and the others.
console.log("Element assignment:", array_element_assignment());
console.log("Compound element assignment:", compound_element_assignment());
console.log("Elements set in a loop, 0 + 1 + 4 + 9 + 16:", fill_with_loop());
console.log("Swap the ends of 1..5, 5 and 1:", swap_ends());
console.log("Index is an expression, for 2:", element_index_is_expression(2));

// A slice a[1:4] has the elements 1 through 3: it includes the low bound and excludes the high one.
console.log("Sum of a[0:3], a[1:4] and a[2:2]:", pass_array_slice_to_proc(), sum_of_middle(),
    sum_of_empty_slice());
console.log("Slice of a slice, a[1:5][1:3]:", slice_of_slice());
console.log("Slice with a type, []Int:", slice_variable_with_type());
console.log("Bounds are expressions, for 0 and 2:", slice_bounds_are_expressions(0, 2));
console.log("Literal as an argument:", array_literal_as_argument());

// A range of elements can be assigned an array or a slice with as many elements.
console.log("Range assignment:", array_element_range_assignment());
console.log("Copy between arrays:", copy_between_arrays());
console.log("Copy within one array, which overlaps:", copy_within_one_array());

// A for statement goes through the elements of an array or a slice.
console.log("Sum with a for statement:", sum_of_array());
console.log("Dot product of 1 2 3 and 5 6 7:", dot_product_of_arrays());
console.log("Break and continue:", breaks_and_continues());
console.log("Literal in a loop:", literal_in_loop());
console.log("Return from the loop:", return_from_nested_block(4), return_from_nested_block(9));

// Every call has its own array, also when a procedure calls itself.
console.log("Arrays in nested calls, for 3:", arrays_in_nested_calls(3));

// An array slice is two numbers: the address of its first element in the memory of the module,
// and the number of elements, so JavaScript can write Int values (BigInt64Array) to the memory and
// pass them to a procedure that takes a slice. This program has no strings, so the start of the
// memory is free.
new BigInt64Array(memory.buffer, 0, 5).set([4n, 8n, 15n, 16n, 23n]);
console.log("Sum of 5 elements that JavaScript wrote at address 0:", sum(0, 5));
console.log("Sum of the last 2, at address 24:", sum(24, 2));
console.log("Count above 10, and twice the sum:", count_above(0, 5, 10), sum_twice(0, 5));

// An index or a bound outside of the array traps, which makes the call throw.
function trapped(call) {
    try {
        return call();
    } catch (error) {
        return `${error.name}: ${error.message}`;
    }
}
console.log("Element 2 and element 3 of three:", element_out_of_bounds(2), trapped(() => element_out_of_bounds(3)));
console.log("Negative index:", trapped(() => element_out_of_bounds(-1)));
console.log("Slice 1:3 and slice 1:4 of three:", slice_out_of_bounds(1, 3), trapped(() => slice_out_of_bounds(1, 4)));
console.log("Low bound above the high one:", trapped(() => slice_out_of_bounds(2, 1)));
console.log("Copy to a range of another length:", trapped(() => copy_with_different_lengths()));

// length_of gives the number of elements of an array or an array slice as an Int. For an array the
// number is known when the program is compiled, and for a slice it is read when the program runs.
console.log("Length of arrays with a deduced, an explicit and no length, 5, 7 and 0:",
    length_of_array(), length_of_array_with_explicit_length(), length_of_empty_array());
console.log("Length of a[1:4] and of a[2:2], 3 and 0:", length_of_slice_of_array(1, 4),
    length_of_slice_of_array(2, 2));
console.log("A loop up to the length, 1 + 2 + 3 + 4:", fill_with_length());
console.log("Length of a slice that JavaScript wrote at address 0, 5 and 2:", length_of_slice(0, 5),
    length_of_slice(24, 2));
console.log("Average of 4 8 15 16 23, and of nothing:", average(0, 5), average(0, 0));
console.log("The last element of the five, and of the last two:", last_element(0, 5),
    last_element(24, 2));

// length_of works on slices of the members, and on any other array or slice too.
console.log("Length of a literal of four, 2 and 4:", length_of_literal());
new BigInt64Array(memory.buffer, 0, 7).set([1n, 2n, 3n, 4n, 5n, 6n, 7n]);
console.log("Length of a slice that JavaScript wrote at address 0, 7:", slice_length(0, 7));
