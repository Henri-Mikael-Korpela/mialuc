// Untyped constants become the type that their place wants: LIMIT is an Int and a Byte here.
console.log("LIMIT as an Int and as a Byte, 400:", limit_as_both());
console.log("x := DOUBLE_LIMIT is an Int, 401:", default_type());
console.log("A Byte compared with LIMIT - 100, 1 and 0:", compare_with_byte(100), compare_with_byte(99));
console.log("LIMIT / 4 + 5 given to a Byte, 55:", byte_argument());
console.log("Bytes LIMIT, MASK and 0b11, 218:", sum_of_bytes());
console.log("COUNT * 2 is an Int, 6:", typed_arithmetic());
console.log("3 MiB in bytes, 3145728:", mebibytes(3));
console.log("VERBOSE and not DEBUG == false, 1:", verbose());
console.log("calls starts at LIMIT / 2, 101 and 102:", count_call(), count_call());
console.log("A typed Str constant:", Str(name()));

// Untyped strings are passed to a C_Str parameter without str_to_c_str.
greet();

// Every constant is also a function that returns its value, in its default type when it is untyped.
console.log("The constants, as JavaScript reads them:", LIMIT(), DOUBLE_LIMIT(), MEBIBYTE(), DEBUG(),
    VERBOSE(), MASK(), COUNT(), Str(NAME()), Str(GREETING()));
