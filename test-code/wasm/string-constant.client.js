// A constant that is a string is a procedure without parameters.
// A Str parameter takes the two numbers, so `...` passes a string on.
console.log("Greeting:", Str(greeting()));
console.log("Echo:", Str(echo(...greeting())));
console.log("Second:", Str(second(1, ...greeting(), ...empty())));
// `++` joins strings and runes when the program is compiled.
console.log("Excited:", Str(excited()));
console.log("Framed:", Str(framed()));
console.log("Framed again:", Str(framed_again()));
