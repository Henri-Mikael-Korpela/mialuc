// A slice of a string is a string, so JavaScript gets it as an address and a length, and Str(...)
// reads the bytes. It views the bytes of the string that it is a slice of.
console.log("text[0:5] and text[7:12] of Hello, World!:", Str(hello()), Str(world()));
console.log("The bounds are characters, so äiti[1:4] and äiti[0:2] are:", Str(without_first()),
    Str(with_first()));
console.log("An empty slice has 0 characters:", empty());
console.log("A slice of a slice, and the Rune at index 2 of World, which is r, 114:", Str(inner()),
    third_character_of_world());
console.log("The whole string is a slice too:", Str(past_the_end(5n)));
try {
    past_the_end(6n);
} catch (error) {
    console.log("A bound that is past the end traps:", error.message);
}
