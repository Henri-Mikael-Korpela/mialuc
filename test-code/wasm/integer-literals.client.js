// Integer literals are decimal, binary after 0b or hexadecimal after 0x, and an underscore can be
// between two digits. They are all an Int, which JavaScript receives as a BigInt.
console.log("Binary 0b100, 4:", integer_binary_literal());
console.log("Decimal 42, 42:", integer_decimal_literal());
console.log("Hexadecimal 0xFF, 255:", integer_hexadecimal_literal());
console.log("Hexadecimal 0xcafe, 51966:", integer_hexadecimal_literal_lowercase());
console.log("Binary 0b1111_0000, 240:", integer_binary_literal_with_digit_separator());
console.log("Decimal 1_000_001, 1000001:", integer_decimal_literal_with_digit_separator());
console.log("Hexadecimal 0xFF_FF, 65535:", integer_hexadecimal_literal_with_digit_separator());
console.log("Irregular separators 1_2_3_4, 1234:", irregular_digit_separators());
console.log("Largest Int in decimal and hexadecimal, 9223372036854775807 twice:",
    largest_decimal(), largest_hexadecimal());
console.log("0, 0b0 and 0x0 are equal, 1:", zeros_are_equal());
console.log("0b1010 + 10 + 0xA, 30:", mixed_arithmetic());
console.log("0b1111_1111, 255 and 0xFF are equal, 1:", same_number_in_every_form());
console.log("Byte from 0xFF, 255:", hexadecimal_byte());
console.log("Sum of [0x3]Int { 0x1, 0b10, 3 }, 6:", array_with_hexadecimal_length());
console.log("Lowest four bits of 0x1234 and 255, 4 and 15:",
    lowest_four_bits(0x1234n), lowest_four_bits(255n));
console.log("-0x10, -16:", negative_hexadecimal());
console.log("-112, -112:", negative_decimal());
console.log("-0b101, -5:", negative_binary());
console.log("-9_223_372_036_854_775_807, -9223372036854775807:", lowest_literal());
console.log("Signed literals in expressions, -5 + -12 + -3 + -6, -26:", signed_in_expressions());
console.log("n -5 + n-1 for 10, 14:", subtraction_is_not_a_literal(10n));
console.log("-5 < -2 and -112 < 0 and -1 == 0 - 1, 1:", signed_comparison());
