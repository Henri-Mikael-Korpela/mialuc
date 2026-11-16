// An F32 is a 32-bit float (`f32`), which JavaScript passes and receives as a plain number, and not
// as a BigInt, which an Int is. A number that an F32 cannot hold exactly is rounded to the nearest
// one, so 1.42 comes back as 1.4199999570846558. `Math.fround` shows what an F32 holds.
console.log("Literal 1.42 as an F32, 1.4199999570846558:", literal_is_f32(), Math.fround(1.42));
console.log("Literal 42.4445 as an F32, 42.444499969482422:", literal_with_fraction());
console.log("Signed literal, -0.5:", negative_literal());
console.log("Digit separators 1_000.000_5, 1000.0005:", literal_with_digit_separators());
console.log("scale(3, 4) is 3 * 4 + 1, 13:", scale(3, 4));
console.log("Arithmetic and assignments, 3.4375:", arithmetic());
console.log("Division is not truncated, 7 / 2 is 3.5:", division(7, 2));
console.log("Division by zero is infinity, Infinity:", division(1, 0));
console.log("Constant expression 1.5 + 2.25 * 2.0, 6:", constant_expression());
console.log("Constants 0.5 + 0.1, 0.6000000238418579:", constant_values());

console.log("compare(1, 2), 1 + 2 + 8 + 16 = 27:", compare(1, 2));
console.log("compare(2, 2), 2 + 4 + 16 + 32 = 54:", compare(2, 2));
console.log("compare(3, 1), 8 + 16 + 32 + 64 = 120:", compare(3, 1));
console.log("Not a number is not equal to itself, only not= holds, 2:", not_a_number());
console.log("A result that is too large is infinity, 3:", infinity());

console.log("A global variable of a float, 1.75 then 3.25:", add_to_total(1.5), add_to_total(1.5));
console.log("Int to F32, 3 and 16777217 rounded to 16777216:", int_to_float(3n), int_to_float(16777217n));
console.log("Byte to F32, 200:", byte_to_float());
console.log("F32 to Int drops the fraction, -3n and 3n:", float_to_int(-3.75), float_to_int(3.99));
try {
    float_to_int(NaN);
    console.log("Not a number to Int should have trapped");
} catch (error) {
    console.log("Not a number to Int traps:", error instanceof WebAssembly.RuntimeError);
}
try {
    float_to_int(1e30);
    console.log("A number too large for an Int should have trapped");
} catch (error) {
    console.log("A number too large for an Int traps:", error instanceof WebAssembly.RuntimeError);
}
console.log("Cast in an expression, 7 / 2 as floats, 3.5:", cast_in_expression(7n));

console.log("Fields of a struct, 2.25 + 5, 7.25:", struct_fields());
console.log("Array of floats, 7 + 10 + 3, 20:", array_of_floats());
console.log("Pointer to a float, (3.5 + 1) * 2, 9:", pointer_to_float());
console.log("Pointer to a procedure with floats, 13:", use_pointer_to_procedure());
console.log("Two floats returned, 9.5 - 2.25, 7.25:", use_min_max());
console.log("min_max(9.5, 2.25), [2.25, 9.5]:", min_max(9.5, 2.25));

done
