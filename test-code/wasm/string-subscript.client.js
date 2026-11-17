// A string is subscripted by the character, and a Rune is a plain number for JavaScript, its code
// point, so 'ä' is 228. An index that is outside of the string traps, as it does for an array.
console.log("The first and the second character of äiti, 228 and 105:", first_character(),
    second_character());
console.log("Characters of 1, 2, 3 and 4 bytes:",
    [0n, 1n, 2n, 3n, 4n].map((index) => character_at(index)));
console.log("aé€🚀z has 5 characters, though it has 11 bytes:", character_count());
console.log("A string without a value has no characters:", Boolean(is_empty()));
try {
    character_at(5n);
} catch (error) {
    console.log("An index that is not below the number of characters traps:", error.message);
}
