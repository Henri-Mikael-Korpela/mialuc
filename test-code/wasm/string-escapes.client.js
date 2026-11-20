// A string is its address and length, and `Str` reads the text of them, so what is shown is the text
// that the escape sequences stand for. `JSON.stringify` writes the control characters back as
// escape sequences, so that they can be seen.
console.log("Letters:", JSON.stringify(Str(spaces())));
console.log("Controls, 7 8 27 12 11:", [...Str(controls())].map(c => c.codePointAt(0)).join(" "));
console.log("Quotes:", JSON.stringify(Str(quotes())));
console.log("Numbers, AAAA A:", Str(numbers()));
console.log("Non ASCII, é 日本 🚀:", Str(non_ascii()));
console.log("Joined:", JSON.stringify(Str(joined())));
console.log("Not a constant:", JSON.stringify(Str(described())));
console.log("Characters, 3:", character_count());
console.log("Second character, é U+E9:", "U+" + second_character().toString(16).toUpperCase());
