// Each number is the results of the slices, in the order that they are written in the program.
console.log("a[0:6], a[:6], a[0:] and a[:] all have the sum 21:", whole_array(), "= 21 21 21 21");
console.log("a[2:], a[:2], a[6:] and a[:0] are the sum 18, the sum 3, and two empty ones:",
    start_and_end(), "= 18 03 00 00");
console.log("a[1:1 + 3] and a[1:][:3] both have the sum 9:", chunk(1, 3), chunk_of_a_slice(1, 3));
console.log("a[:0 + 6] and a[0:][:6] both have the sum 21:", chunk(0, 6), chunk_of_a_slice(0, 6));
console.log("a slice of a slice has the lengths 5 and 5, the sums 6, 7 and 5:", slices_of_slices(),
    "= 05 05 06 07 05");
console.log("b[:3] = a[3:] and b[3:] = a[:3] give the sums 15 and 6:", assigned());
console.log("bytes[1:] has the length 3, and its first byte is 2:", bytes_from_one(), "= 3 02");
try {
    past_the_end(4);
} catch (error) {
    console.log("a[4:] of an array of 3 traps:", error.message);
}
