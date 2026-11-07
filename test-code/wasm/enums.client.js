// An enum is a type with a fixed set of named members, such as `Color :: enum Int ->` with RED, GREEN
// and BLUE below it, where `Int` is the type of the numbers of the members. A member is written with
// the name of its enum, `Color.RED`, or with just a dot, `.RED`, where the enum is known from the
// place it is in. For JavaScript a member is the number of its place in the enum, counted from 0,
// as a BigInt, so RED is 0n, GREEN is 1n and BLUE is 2n.
console.log("A variable, a constant and a member found by the type of the result, 0, 0 and 1:",
    create_enum_variable(), return_enum_constant_outside_the_current_proc(),
    auto_deduce_enum_by_return_type());
console.log("A constant of an enum is a function too, DEFAULT_COLOR and DEFAULT_LIGHT, 0 and 1:",
    DEFAULT_COLOR(), DEFAULT_LIGHT());
console.log("Members are numbered from zero, 1 + 10:", members_are_numbered_from_zero());
console.log("The default value is the first member:", Boolean(default_value_is_the_first_member()));

// Members are compared with == and not=, and have no order. The enum of `.RED` is the one of what it
// is compared with, so it can be on either side.
console.log("RED is not BLUE, so false:", Boolean(comparison_between_enum_values()));
console.log("RED not= GREEN:", Boolean(not_equal_between_enum_values()));
console.log("A literal on either side of a comparison, 1 + 10 + 100:",
    literal_on_either_side_of_a_comparison());
console.log("Two enums can have members of one name, 1 + 10 + 100:",
    same_member_names_in_two_enums());

// A variable can be given a member with `=`, with or without the name of the enum.
console.log("Written type, assigned with a dot and assigned with the name, 2, 1 and 2:",
    variable_with_written_type(), assign_enum_variable(),
    assign_enum_variable_with_qualified_member());

// The enum is known from the parameter, the result, the field and the element that it is given to.
console.log("Weekend, 1 + 100 and no 10:", weekend_and_weekdays());
console.log("The day after Sunday, MONDAY is 0:", enum_as_argument_and_result());
console.log("Three days after Friday, MONDAY is 0:", three_days_after_friday());
console.log("A constant of another enum, 1:", enum_constant_of_another_enum());
console.log("Constants in comparisons and arguments:", Boolean(enum_constant_in_a_comparison()),
    Boolean(enum_constant_as_argument()));

// The members of an enum in the fields of a struct. A field that is left out is the first member.
console.log("Fields left out, 5 + 10 + 100:", enum_fields_are_set_by_default());
console.log("Assign a field, BLUE is 2:", assign_enum_field());
console.log("Compare fields with members:", Boolean(compare_enum_field_with_literal()));
console.log("In a struct in a struct, YELLOW is 1:", enum_in_a_nested_struct_literal());
console.log("In a literal without a name, GREEN is 1:", enum_in_a_struct_literal_without_a_name());
console.log("The enum field of a struct from the first example, RED:", enum_field_in_struct(),
    enum_field_type_auto_detected_in_struct());

// A pointer to an enum, and arrays and slices of enums.
console.log("Through a pointer, BLUE and GREEN, 2 and 1:", pointer_to_an_enum(),
    assign_through_a_pointer_to_an_enum());
console.log("Read through a pointer:", Boolean(read_through_a_pointer_to_an_enum()));
console.log("The greens of RED GREEN BLUE GREEN, 2:", array_of_enums());
console.log("The greens of five and of the first two, 3 * 10 + 1:", slice_of_enums());
console.log("An element is assigned, BLUE is 2:", assign_enum_element());
console.log("An element is read, SUNDAY is 6:", index_array_of_enums());

// An enum is passed to and returned from a pointer to a procedure like any other value.
console.log("Rotate GREEN, BLUE is 2:", pointer_to_a_procedure_with_an_enum());
console.log("Rotate RED twice, BLUE is 2:", callback_with_an_enum());
console.log("Rotate from RED until BLUE, 2 steps:", enum_in_a_loop_condition());
