// A Str is two numbers: the address of its bytes in memory, and their count.
// A procedure that returns one gives them as an array.
const name = get_name();
console.log("Address and length:", name);

// Str(...) reads the bytes, and makes a JavaScript string of them.
console.log("Name:", Str(name));
