// A union match puts fields in a struct that exist only for some member of an enum, such as
// `Token_Kind` with IDENTIFIER, INTEGER_LITERAL and OPERATOR_ADD. A token that is an identifier has
// the field `identifier`, one that is an integer literal has `integer_literal`, and the operator
// has neither. The compiler checks that a field is only given or used where the token has that
// kind. For JavaScript a token is the values of its fields one after the other: the kind, which is
// a BigInt (IDENTIFIER is 0n, INTEGER_LITERAL is 1n and OPERATOR_ADD is 2n), the address and the
// length of the text of `identifier`, and the value of `integer_literal`. The fields of the cases
// do not share their place yet, so a token has all of them and the ones of the other cases are
// the defaults.
const [kind, textAddress, textLength, integerValue] = test_tokens();
console.log("The first token is an identifier, so its kind is 0, its text has 1 byte and the value"
    + " of the integer is the default:", kind, textLength, integerValue);

// A field of a case is read inside an if statement that compares the kind with the member of the
// case, because that is where the compiler knows that the token has the field.
console.log("Text of an identifier token:", JSON.stringify(Str(first_token_text())));
console.log("Value of an integer token:", value_of_integer_token());

// A token is given to a procedure as its four values. `value_of_token` reads the field of the case
// that the token is in.
console.log("An integer literal 5, an identifier of 5 bytes and the operator, 99:",
    value_of_token(1, 0, 0, 5), value_of_token(0, 0, 3, 0), value_of_token(2, 0, 0, 0));
console.log("The three tokens made by a procedure, 7 * 100 + 5 * 10 + 99:", pass_token_to_proc());

// Fields of a matched token can be assigned, and a token can be replaced by another one.
console.log("A field assigned in the case, 1 + 41:", assign_field_of_matched_union());
console.log("A token replaced by one of another kind:", change_the_kind_by_assigning_an_object());

// The kind can be compared in the same condition as the field.
console.log("Integer literal 3 and another value, true and false:",
    Boolean(kind_and_field_in_one_condition(1, 0, 0, 3)),
    Boolean(kind_and_field_in_one_condition(1, 0, 0, 4)));

// A kind that is left out is the first member, as for any enum.
console.log("The kind that is left out is IDENTIFIER, 1:", omitted_kind_is_the_first_member());

// A field of a pointer to a token is accessed the same way.
console.log("Through a pointer to a token:", use_pointer_to_token());
