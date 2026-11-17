// An I64 is a BigInt for JavaScript, as an Int is, and an I32 is a plain number.
console.log("BIG + 1 wraps to -9223372036854775808, and 5000000000 * 3:", wraps_over(), large());
console.log("-7 / 2 goes toward zero, -3, and -7 < 7:", divide(), Boolean(is_less()));
console.log("10 + 5 - 3 * 4 / 6 is 8:", accumulate());
console.log("An I64 and an Int are cast to each other:", to_int(12345678901n),
    to_i64(12345678901n), from_i32());
console.log("An I64 is made an I32 with int_to_i32, a plain number:", to_i32(123456n));
console.log("JavaScript doubles an I64:", call_javascript());
try {
    to_i32(2147483648n);
} catch (error) {
    console.log("An I64 that does not fit in an I32 traps:", error.message);
}
