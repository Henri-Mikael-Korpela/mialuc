// A rune that is known when the program is compiled is cast to an integer type in a constant. An
// Int is a BigInt in JavaScript.
console.log("A space is 32, a is 97, é is 233:", space(), letter(), latin());
console.log("芋 is 33419, the rocket is 128640:", wide(), emoji());
console.log("A is 65 as a U32, Z is 90 as an I64:", small(), long());
console.log("A space and one is 33, and the number of a constant rune is 65:", next(),
    first_number());
console.log("An array of the length of a cast has 8:", array_length());
console.log("A match on 32, 65 and 7 is 1, 2 and 0:", class_of(32n), class_of(65n), class_of(7n));
