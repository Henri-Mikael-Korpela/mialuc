// A Rune is a 32-bit integer (`i32`), the code point of a character, which JavaScript passes and
// receives as a plain number. `String.fromCodePoint` makes the character of a code point, and
// `codePointAt` the other way round, so the characters of the source come out as they were written.
const show = code => `${String.fromCodePoint(code)} (U+${code.toString(16).toUpperCase()})`;
console.log("Letter, A:", show(ascii_letter()));
console.log("Kanji, 芋:", show(kanji()));
console.log("Greek letter, λ:", show(greek_letter()));
console.log("Emoji of four bytes in UTF-8, 🚀:", show(emoji_rocket()));
console.log("Inferred, é:", show(inferred()));
console.log("Upper case ASCII, 65 and 90 are, 64 and 91 are not, 1 1 0 0:",
    is_upper_case_ascii(65), is_upper_case_ascii(90), is_upper_case_ascii(64), is_upper_case_ascii(91));
console.log("compare('a', 'b'), 1 + 2 + 8 = 11:", compare(97, 98));
console.log("compare('b', 'b'), 2 + 4 + 32 = 38:", compare(98, 98));
console.log("compare('🚀', 'b'), 8 + 16 + 32 = 56:", compare("🚀".codePointAt(0), 98));
console.log("Identity of a character from JavaScript, 芋:", show(identity("芋".codePointAt(0))));
console.log("Global variable, a then 🚀 then z:", show(next_global("🚀".codePointAt(0))),
    show(next_global(122)), show(next_global(0)));
console.log("Fields of a struct, 芋 and 🚀:", show(struct_of_runes()), show(struct_second()));
console.log("Array of runes, one character of hello! is above l, and z is above m, 1 + 10 = 11:", array_of_runes());
console.log("Pointer to a rune, 🚀:", show(pointer_to_rune()));
console.log("Default rune, 0:", default_rune());

done
