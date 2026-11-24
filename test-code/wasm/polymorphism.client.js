// Each procedure returns a number, which is printed with the number that the language gives. The
// copies of the polymorphic procedures are functions of the module too, which `exports` has by their
// names, such as `exports["add(Int)"]`.
console.log("nested_calls, 19:", nested_calls());
console.log("integers_and_floats, 45:", integers_and_floats());
console.log("slices_of_types, 85:", slices_of_types());
console.log("pointers_of_types, 221:", pointers_of_types());
console.log("values_together, 19:", values_together());
console.log("given_types, 7:", given_types());
console.log("structs_of_types, 124:", structs_of_types());
console.log("add(Int) called directly, 5:", exports["add(Int)"](2, 3));
