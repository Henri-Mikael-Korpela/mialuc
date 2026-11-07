// A Byte is an 8-bit value from 0 to 255. In WebAssembly it is a 32-bit integer that is passed and
// returned as a JavaScript number, and not as a BigInt, which an Int is. An array of Byte takes one
// byte of memory for each element, so a Uint8Array of the memory of the module is the array itself.
console.log("Byte variable with a literal, 200:", byte_variable());
console.log("Byte made of an Int, 0 255:", byte_from_int(0n), byte_from_int(255n));
console.log("Default byte, 0:", default_byte());
console.log("Comparisons, 1 + 10 + 100 for 200 not= 200 and 100 >= 200 being false:", compare_bytes());
console.log("Next byte of 41 and 254, a number for a Byte:", next_byte(41), next_byte(254));
console.log("A literal as a result and as an argument, 7 and 42:", always_seven(), use_literal_argument());

console.log("Sum of the bytes 72 105 33 10, 220:", sum_of_array());
console.log("Element assignment, 1 255 2 as 10000 + 2550 + 2:", array_element_assignment());
console.log("Sum of a[1:4] of 10 20 30 40 50, 90:", sum_of_slice());
console.log("Fill a[1:3] with 7, 1 + 7 + 7 + 4:", fill_with_seven());

// JavaScript writes bytes to the memory of the module, where a slice of Byte starts at an address.
new Uint8Array(memory.buffer, 0, 6).set([5, 250, 100, 255, 0, 101]);
console.log("Bytes above 100 of 5 250 100 255 0 101, 3:", count_above(0, 6, 100));
console.log("Bytes above 254 of the last 3, 1:", count_above(3, 3, 254));

console.log("Struct with Byte fields, 10 + 250 + 30 + 5:", brightness());
console.log("Pixels in an array, 100 + 2 + 9:", pixels_in_array());
console.log("Pointer to a Byte, 99:", pointer_to_byte());
console.log("Pointer to an element, 5 60 7 as 50000 + 6000 + 7:", pointer_to_element());
console.log("Global Byte, 4 then 5:", raise_level(), raise_level());

function trapped(call) {
    try {
        return call();
    } catch (error) {
        return `${error.name}: ${error.message}`;
    }
}
console.log("int_to_byte of 256 and of -1 trap:", trapped(() => too_large()), trapped(() => negative()));
