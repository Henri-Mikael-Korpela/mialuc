// `==` and `not=` compare two strings by their bytes: they are the same when they have the same
// length and the same bytes. Strings have no order, so `<` is an error.
console.log("print == print, and print not= run:", Boolean(is_same_command()),
    Boolean(is_other_command()));
console.log("A longer string, and one with a different byte, are not the same:",
    Boolean(is_longer_the_same()), Boolean(is_other_byte_the_same()));
console.log("Nothing is the same as the empty string, and not as text:",
    Boolean(is_empty_the_same()), Boolean(is_nothing_the_same_as_text()));
console.log("A result is compared as a parameter:", Boolean(is_greeting_hello()));
console.log("The people called Alice, which is 2:", count_alices());
