// The cases of a match on a U32, an I32 and a U64 compare without or with a sign as their type does.
console.log("Codes 2, 3, 5 and 4 give NOT_FOUND, NOT_FOUND, ACCESS_DENIED and OTHER, 1 1 2 3:",
    error_number(2), error_number(3), error_number(5), error_number(4));
console.log("Sizes 0, 100, 4096, 3000000000 and 4000000000 are 0, 1, 2, 2 and 3:",
    size_class(0), size_class(100), size_class(4096), size_class(3000000000 | 0), large_size());
console.log("Signs -1, -50, 0, 100, 101 and -101 are 10, 11, 12, 13, 14 and 14:",
    sign_class(-1), sign_class(-50), sign_class(0), sign_class(100), sign_class(101),
    sign_class(-101));
console.log("The U64 values 0, the largest Int, one above it and 0 - 1 are 0, 1, 2 and 2:",
    wide_class(0n), wide_class(9223372036854775807n), above_top(), wide_class(-1n));
console.log("The I64 values -5000000000, 5000000000 and 5 are 1, 2 and 0:",
    signed_wide_class(-5000000000n), signed_wide_class(5000000000n), signed_wide_class(5n));
