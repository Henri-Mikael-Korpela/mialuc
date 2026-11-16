// Integer literals: decimal, binary after `0b` and hexadecimal after `0x`. An underscore is a digit
// separator between two digits, which only makes a long number easier to read. Every form is the
// same Int, so they can be mixed in arithmetic, comparisons, array lengths, Bytes and global
// variables.

package integer_literals

integer_binary_literal :: proc() Int ->
    return 0b100

integer_decimal_literal :: proc() Int ->
    return 42

integer_hexadecimal_literal :: proc() Int ->
    return 0xFF

/// The digits of a hexadecimal literal can be lowercase too.
integer_hexadecimal_literal_lowercase :: proc() Int ->
    return 0xcafe

integer_binary_literal_with_digit_separator :: proc() Int ->
    return 0b1111_0000

integer_decimal_literal_with_digit_separator :: proc() Int ->
    return 1_000_001 // One million and one

integer_hexadecimal_literal_with_digit_separator :: proc() Int ->
    return 0xFF_FF

/// A separator can be between any two digits, not only every three or four of them.
irregular_digit_separators :: proc() Int ->
    return 1_2_3_4

/// The largest Int, which is 2 to the power of 63 minus 1, in decimal and in hexadecimal.
largest_decimal :: proc() Int ->
    return 9_223_372_036_854_775_807

largest_hexadecimal :: proc() Int ->
    return 0x7FFF_FFFF_FFFF_FFFF

/// Zero in each form.
zeros_are_equal :: proc() Bool ->
    return 0 == 0b0 and 0 == 0x0

/// The forms are the same Int, so they can be mixed in one expression.
mixed_arithmetic :: proc() Int ->
    return 0b1010 + 10 + 0xA

/// A literal is compared with another of any form.
same_number_in_every_form :: proc() Bool ->
    return 0b1111_1111 == 255 and 255 == 0xFF

/// A literal from 0 to 255 is a Byte where a Byte is wanted, in any form.
hexadecimal_byte :: proc() Int ->
    b : Byte = 0xFF
    return cast(Int)b

/// The length of an array type is an integer literal, so it can be written in any form.
array_with_hexadecimal_length :: proc() Int ->
    numbers := [0x3]Int { 0x1, 0b10, 3 }
    total := 0
    for n in numbers ->
        total += n
    return total

/// The initial value of a global variable is a constant, such as a literal.
mask #(static) := 0x0F

/// Masks the lowest four bits of a number by keeping its remainder of 16, since there are no
/// bitwise operators.
lowest_four_bits :: proc(n: Int) Int ->
    divisor := mask + 1
    return n - n / divisor * divisor

/// A signed integer literal has a minus sign directly before its digits, in any form.
negative_hexadecimal :: proc() Int ->
    return -0x10

negative_decimal :: proc() Int ->
    return -112

negative_binary :: proc() Int ->
    return -0b101

/// The lowest Int that can be written as a literal, which is minus the largest Int.
lowest_literal :: proc() Int ->
    return -9_223_372_036_854_775_807

/// A signed literal is an operand, so it can be the right side of an operator, after another
/// operator, and in a variable, a comparison, an array and an argument.
signed_in_expressions :: proc() Int ->
    a := -5
    b := 3 * -4
    c := a - -2
    numbers := [const]Int { -1, -2, -3 }
    total := 0
    for n in numbers ->
        total += n
    return a + b + c + total

/// A minus sign between two operands is the subtraction, whether or not there is a space after it.
subtraction_is_not_a_literal :: proc(n: Int) Int ->
    return n -5 + n-1

/// The order of signed numbers is the signed order.
signed_comparison :: proc() Bool ->
    return -5 < -2 and -112 < 0 and -1 == 0 - 1
