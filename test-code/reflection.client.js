// `type_info_of(Color)` gathers information about the type `Color` while the program is compiled.
// For an enum it is an `Enum_Type_Info`, a struct object with the `name` of the enum, and its
// `members`, an array of `Enum_Member_Type_Info` objects, which each have the `name` of a member and
// its `value`, the number that JavaScript sees (a BigInt). The array is in the data of the module, so
// it is a constant of the program, and `length_of` gives its length when the program is compiled.
// A procedure that returns a Str gives an array of the address and the length, that `Str` reads.
console.log("Members of Color, Weekday and of Color in a variable, 3, 5 and 3:",
    get_enum_members_count(), members_of_another_enum(), members_in_a_variable());

// The elements of the members are read with an index, and with a for statement.
console.log("The number of the member at index 2 and the name of the one at 1, 2n and GREEN:",
    member_number(2n), Str(member_name(1n)));
console.log("Numbers of Color and of Weekday added, 0 + 1 + 2 and 0 + 1 + 2 + 3 + 4:",
    sum_of_numbers(), sum_of_weekday_numbers());
console.log("The name of the last Weekday member:", Str(name_of_last_member()));

// The name of the enum, read from the information directly and from a global variable.
console.log("Names of the enums:", Str(enum_name()), Str(enum_name_in_a_global()));

// length_of works on slices of the members, and on any other array or slice too.
console.log("Length of Color_Members[1:3], and of a literal of four, 2 and 4:",
    middle_slice_length(), length_of_literal());
new BigInt64Array(memory.buffer, 0, 7).set([1n, 2n, 3n, 4n, 5n, 6n, 7n]);
console.log("Length of a slice that JavaScript wrote at address 0, 7:", slice_length(0, 7));

// An index outside of the members traps, like any other array.
try {
    member_number(3n);
} catch (error) {
    console.log("Index 3 of three members:", `${error.name}: ${error.message}`);
}
