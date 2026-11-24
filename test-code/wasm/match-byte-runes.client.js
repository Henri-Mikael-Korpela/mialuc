// A Byte is matched with runes of ASCII, and with ranges of them.
console.log("'x', '7', 'Q', '-', '_' are in a name, ' ' and '!' are not:", Boolean(is_name_byte(120)),
    Boolean(is_name_byte(55)), Boolean(is_name_byte(81)), Boolean(is_name_byte(45)),
    Boolean(is_name_byte(95)), Boolean(is_name_byte(32)), Boolean(is_name_byte(33)));
console.log("Bytes of a name in a-b_Z9 !, 6:", count_name_bytes());
console.log("Line feed 1, carriage return 1, space 2, 'a' 3, 'c' 4, 200 is 5, 'A' is 0, so 543211:", kinds());
