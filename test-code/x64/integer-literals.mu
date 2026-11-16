// Integer literals in decimal, binary after `0b` and hexadecimal after `0x`, with underscores as
// digit separators between two digits. Each is printed in decimal, so that the output shows that
// every form is the same Int. The exit code of the program is the value of `0x2A`, which is 42.

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

/// Prints a line that has the number in it, where the format has `%lld`.
show :: proc(format: C_Str, number: Int) Int ->
    printf(format, number)
    puts(str_to_c_str(""))
    return number

main :: proc() Int ->
    show(str_to_c_str("0b100 is %lld"), 0b100)
    show(str_to_c_str("42 is %lld"), 42)
    show(str_to_c_str("0xFF is %lld"), 0xFF)
    show(str_to_c_str("0xcafe is %lld"), 0xcafe)
    show(str_to_c_str("0b1111_0000 is %lld"), 0b1111_0000)
    show(str_to_c_str("1_000_001 is %lld"), 1_000_001)
    show(str_to_c_str("0xFF_FF is %lld"), 0xFF_FF)

    // A literal above 32 bits is loaded whole, as every Int is 64 bits.
    largest := 0x7FFF_FFFF_FFFF_FFFF
    show(str_to_c_str("0x7FFF_FFFF_FFFF_FFFF is %lld"), largest)

    // A signed integer literal has a minus sign directly before its digits, in any form.
    show(str_to_c_str("-5 is %lld"), -5)
    show(str_to_c_str("-112 is %lld"), -112)
    show(str_to_c_str("-0x10 is %lld"), -0x10)
    show(str_to_c_str("-0b101 is %lld"), -0b101)
    show(str_to_c_str("-9_223_372_036_854_775_807 is %lld"), -9_223_372_036_854_775_807)

    // Where an operand is expected a minus sign starts a literal, and between two operands it is
    // the subtraction, whether or not there is a space after it.
    base := 10
    show(str_to_c_str("3 * -4 is %lld"), 3 * -4)
    show(str_to_c_str("base - -2 is %lld"), base - -2)
    show(str_to_c_str("base -5 + base-1 is %lld"), base -5 + base-1)

    exit_code : Int = 0x2A
    return exit_code
