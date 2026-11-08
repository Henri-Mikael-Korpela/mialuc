// Byte: an 8-bit value from 0 to 255. An integer literal from 0 to 255 is a Byte where a Byte is
// wanted, `byte_to_int` makes an Int of a Byte and `int_to_byte` makes a Byte of an Int. Bytes are
// compared with the six comparison operators, but there is no arithmetic on them: it is done with
// Int values. An array of Byte takes one byte of memory for each element.

/// A Byte is given an integer literal from 0 to 255.
byte_variable :: proc() Int ->
    b : Byte = 200
    return byte_to_int(b)

/// The type is deduced from a call of int_to_byte.
byte_from_int :: proc(n: Int) Int ->
    b := int_to_byte(n)
    return byte_to_int(b)

/// A variable that has a type and no value is 0.
default_byte :: proc() Int ->
    b : Byte
    return byte_to_int(b)

/// Bytes are compared as numbers from 0 to 255, so 200 is above 100.
compare_bytes :: proc() Int ->
    small : Byte = 100
    large : Byte = 200
    result := 0
    if small < large ->
        result += 1
    if large > small ->
        result += 10
    if small == 100 ->
        result += 100
    if 200 not= large ->
        result += 1000
    if small >= large ->
        result += 10000
    return result

/// A Byte can be a parameter and a result.
next_byte :: proc(b: Byte) Byte ->
    return int_to_byte(byte_to_int(b) + 1)

/// A literal is a Byte as a result and as an argument as well.
always_seven :: proc() Byte ->
    return 7

use_literal_argument :: proc() Int ->
    return byte_to_int(next_byte(41))

/// The elements of an array of Byte are one byte each.
sum_of_array :: proc() Int ->
    a := [4]Byte { 72, 105, 33, 10 }
    total := 0
    for b in a ->
        total += byte_to_int(b)
    return total

array_element_assignment :: proc() Int ->
    a := [3]Byte { 1, 2, 3 }
    a[1] = 255
    a[2] = next_byte(a[0])
    return byte_to_int(a[0]) * 10000 + byte_to_int(a[1]) * 10 + byte_to_int(a[2])

/// An array slice of Byte is a view of bytes.
sum_of_bytes :: proc(bytes: []Byte) Int ->
    total := 0
    for b in bytes ->
        total += byte_to_int(b)
    return total

sum_of_slice :: proc() Int ->
    a := [const]Byte { 10, 20, 30, 40, 50 }
    return sum_of_bytes(a[1:4])

/// Writes into a slice of bytes.
fill :: proc(bytes: []Byte, value: Byte) ->
    for i in 0..<length_of(bytes) ->
        bytes[i] = value

fill_with_seven :: proc() Int ->
    a := [const]Byte { 1, 2, 3, 4 }
    fill(a[1:3], 7)
    return sum_of_bytes(a)

/// Takes the bytes that JavaScript wrote to the memory of the module.
count_above :: proc(bytes: []Byte, limit: Byte) Int ->
    count := 0
    for b in bytes ->
        if b > limit ->
            count += 1
    return count

/// A struct can have a field of Byte.
Pixel :: struct ->
    red: Byte
    green: Byte
    blue: Byte
    alpha: Int

brightness :: proc() Int ->
    p := Pixel { red = 10, green = 20, blue = 30, alpha = 5 }
    p.green = 250
    return byte_to_int(p.red) + byte_to_int(p.green) + byte_to_int(p.blue) + p.alpha

pixels_in_array :: proc() Int ->
    a := [2]Pixel { Pixel { red = 1, green = 0, blue = 0, alpha = 0 }, Pixel { red = 2, green = 0, blue = 9, alpha = 0 } }
    a[0].blue = 100
    return byte_to_int(a[0].blue) + byte_to_int(a[1].red) + byte_to_int(a[1].blue)

/// A pointer to a Byte points to a variable or to an element.
pointer_to_byte :: proc() Int ->
    b : Byte = 1
    p := %b
    p^ = 99
    return byte_to_int(b)

pointer_to_element :: proc() Int ->
    a := [3]Byte { 5, 6, 7 }
    p := %a[1]
    p^ = int_to_byte(byte_to_int(p^) * 10)
    return byte_to_int(a[0]) * 10000 + byte_to_int(a[1]) * 100 + byte_to_int(a[2])

/// A global variable can be a Byte.
level #(static) : Byte = 3

raise_level :: proc() Int ->
    level = next_byte(level)
    return byte_to_int(level)

/// Bytes are not Int values: int_to_byte stops the program for a number that is not from 0 to 255.
too_large :: proc() Int ->
    n := 256
    return byte_to_int(int_to_byte(n))

negative :: proc() Int ->
    n := 0 - 1
    return byte_to_int(int_to_byte(n))
