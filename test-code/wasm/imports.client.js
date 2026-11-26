// The library's min and the program's own are two functions: only the program's is exported.
console.log("clamped(50), clamped(-5) and clamped(7) are 10, 0 and 7:", clamped(50), clamped(-5),
    clamped(7));
console.log("largest(3, 9) is 9, and the library's smallest(3, 9) is 3:", largest(3, 9),
    smallest(3, 9));
console.log("The program's own min(3, 9) is 309:", own_smallest(3, 9));
console.log("The exports are the program's, and not the library's:", typeof min, typeof clamp);
console.log("The library's find_first_index_of finds the first 3 at 1, and no 5 at -1:", first_three(),
    first_five());
console.log("The library's try_bytes_to_str_ascii gives 4 characters for wasm, and -1 for a byte of 195:",
    ascii_length(), not_ascii_length());
