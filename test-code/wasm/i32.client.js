// An I32 is a plain JavaScript number, and an Int is a BigInt.
console.log("LIMIT + 1 and SMALLEST - 1 wrap around, -2147483648 and 2147483647:", wraps_over(),
    wraps_under());
console.log("46341 * 46341 wraps to -2147479015:", product());
console.log("-7 / 2 goes toward zero, -3, and -7 < 7:", divide(), Boolean(is_less()));
console.log("40 + 2, and 10 + 5 - 3 * 4 / 6:", add(40, 2), accumulate());
console.log("An I32 is made an Int with a cast, and an Int an I32 with int_to_i32:", widen(),
    narrow(123456n));
console.log("The average of -10 and 20 is made by JavaScript:", call_javascript());
try {
    narrow(2147483648n);
} catch (error) {
    console.log("An Int that does not fit in an I32 traps:", error.message);
}
