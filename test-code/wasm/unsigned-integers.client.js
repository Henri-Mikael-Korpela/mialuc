// A U32 is a plain JavaScript number, negative above 2147483647, and a U64 is a BigInt, negative
// above 9223372036854775807, so `>>> 0` and `BigInt.asUintN(64, n)` give the unsigned number.
const unsigned32 = (n) => n >>> 0;
const unsigned64 = (n) => BigInt.asUintN(64, n);
console.log("HIGHEST + 1, 0 - 1 and HIGHEST * 2 wrap around, 0, 4294967295 and 4294967294:",
    unsigned32(wraps_over()), unsigned32(wraps_under()), unsigned32(product()));
console.log("3000000000 / 2 is 1500000000, 3000000000 > 5, and 10 + 5 - 3 * 4 / 6 is 8:",
    unsigned32(divide()), Boolean(is_greater()), unsigned32(accumulate()));
console.log("BIG + BIG is 18446744073709551614, half of it is 9223372036854775807, and it is above BIG:",
    unsigned64(twice()), unsigned64(half_of_twice()), Boolean(is_twice_greater()));
console.log("0 - 1 is 18446744073709551615:", unsigned64(below_zero()));
console.log("-1 is the U64 18446744073709551615:", unsigned64(negative_to_u64(-1n)));
console.log("A Byte, a Rune, and a U32 made an Int, 200, 65 and 3000000000:",
    unsigned32(byte_to_u32()), unsigned32(rune_to_u32()), u32_to_int(3000000000 | 0));
console.log("A U32 made a float is exact, 3000000000:", u32_to_float(3000000000 | 0));
console.log("The fields of a packet, 3000000001 and 3000000000:", unsigned32(packet_size()),
    unsigned64(packet_offset()));
console.log("JavaScript halves a U32, 1500000000:", unsigned32(call_javascript()));
