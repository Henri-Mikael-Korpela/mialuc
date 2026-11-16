// A procedure that returns several values is a function with several results, so JavaScript gets
// them as an array: an Int is a BigInt, a Bool and a Byte are numbers, and a Str is two numbers,
// the address of its bytes and their count, which Str(...) reads.
console.log("Divide 31 by 4, quotient and remainder, 7n and 3n:", divide(31, 4));

const [first_address, first_length, second_address, second_length] = split();
console.log("Two strings, Mialu and language:", Str(first_address, first_length),
    Str(second_address, second_length));

// The values can be of different types. A Bool is 0 or 1, and a Str is two numbers after it.
const [is_not_negative, text_address, text_length, size] = describe(-5);
console.log("Describe -5, 0, negative and 5n:", is_not_negative, Str(text_address, text_length), size);
console.log("Describe 9, 1 and 9n:", describe(9)[0], describe(9)[3]);

console.log("Swap 1 and 2, 2n and 1n:", swap(1, 2));
console.log("A Byte and an Int, 200 and 42n:", byte_and_int(21));

console.log("Smallest and largest of 7 3 9 1 8, 1n and 9n:", smallest_and_largest());
console.log("Fibonacci after 10 steps, 55n and 89n:", fibonacci(10));
console.log("Only the quotient and only the remainder of 100 and 7, 14n and 2n:",
    quotient_only(100, 7), remainder_only(100, 7));
console.log("A call as a statement, 1n:", drop_values());
console.log("100 divided by 7 twice, 2n 0n 2n:", divide_twice(100, 7));
console.log("Two structs as values, 0 + 0 + 5 + 10 as 15n:", corner_sum(5));
console.log("Through a pointer to a procedure, 3n + 1 and 2n + 1:", apply_divide());
console.log("Through a variable that holds a pointer, 43n:", through_pointer());
