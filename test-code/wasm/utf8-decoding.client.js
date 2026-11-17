// utf8_decode makes a string of bytes, and says whether they are valid UTF-8. The string views the
// same bytes, so JavaScript reads it as any other, with Str(...).
console.log("The bytes of aé€🚀z are a string of 5 characters:", Str(decode_mixed()), count_mixed(),
    Boolean(is_mixed_valid()));
console.log("0xFF is not valid, and the string is empty:", Boolean(is_ff_valid()),
    count_after_failure());
console.log("Overlong, a surrogate, above U+10FFFF and cut off are not valid:",
    Boolean(is_overlong_valid()), Boolean(is_surrogate_valid()), Boolean(is_too_large_valid()),
    Boolean(is_cut_off_valid()));
console.log("U+10FFFF and no bytes at all are valid:", Boolean(is_largest_valid()),
    Boolean(is_nothing_valid()));
console.log("A slice of the bytes, é€, and one that cuts a character:", Str(decode_middle()),
    Boolean(is_cut_slice_valid()));
