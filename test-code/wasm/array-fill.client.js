// [4]Int { .. = 7 } gives every element the value 7, and the value is worked out once.
console.log("Four sevens add up to 28, and [5]Int { .. = 0 } has 5 elements:", sum_of_sevens(),
    zero_length());
console.log("The elements are the array's own, 10 + 99 + 10:", changed_one());
console.log("Bytes, an I32, a string and a struct, 255, -5, Bob and 2:", last_byte(), last_i32(),
    Str(last_name()), middle_y());
console.log("The value is an expression, 2 * 3 + 1 twice, and a thousand threes:", computed(3n),
    large());
console.log("An array of no elements:", none());
