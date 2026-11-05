// `x := 4` deduces the type of x, and `x : Int = 5` gives it.
console.log("Deduced type:", foo());
console.log("Given type:", foo2());
// `=` gives a variable that is defined already a new value of its type.
console.log("Assignment:", assignment());
console.log("Last assignment wins, 3:", assignment_twice());
console.log("The old value is used on the right, 9:", assignment_uses_old_value());
console.log("Assigned from another variable, 20:", assignment_from_other_variable());
console.log("Swap of two variables, 73:", swap());
console.log("Assignment of a Bool:", assignment_of_bool());
console.log("Assignment of a Str:", Str(assignment_of_string()));
console.log("Assignment to a copy of a parameter, 42:", assignment_to_parameter_copy(41n));
console.log("Assignment in an if statement, 1 and -1:", assignment_in_condition(5n), assignment_in_condition(-5n));
console.log("Assignment in a loop, 32:", assignment_in_loop());
console.log("Assignment after a declaration with no value, 9:", assignment_after_declared_type());
