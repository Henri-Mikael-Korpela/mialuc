// A Byte is compared with a rune that is in the ASCII range with == and not=.
console.log("10 is the new line, 13 is not, and 9 is a tab:", Boolean(is_new_line(10)),
    Boolean(is_new_line(13)), !Boolean(is_not_tab(9)));
console.log("65 is 'A', whichever comes first:", Boolean(is_letter_a(65)), Boolean(is_letter_a(66)));
console.log("The line ends of Hi, a carriage return, a line feed, !, a line feed, are 3:",
    count_line_ends());
console.log("127 is the delete, and 0 is the null:", Boolean(is_delete(127)), Boolean(is_null(0)),
    Boolean(is_null(1)));
console.log("A rune is a Byte parameter, 46 is the dot and 47 is not:", Boolean(is_dot(46)),
    Boolean(is_dot(47)));
console.log("The first dot of a, the dot, b and the dot is at 1:", first_dot());
