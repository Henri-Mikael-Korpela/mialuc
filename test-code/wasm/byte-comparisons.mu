package byte_comparisons

/// Arrays and array slices of bytes are compared with `==` and `not=`: they are the same when they
/// have the same number of bytes and the same bytes, whether each is an array or a slice.
same_arrays :: proc() Bool ->
    first := [const]Byte { 1, 2, 3 }
    second := [const]Byte { 1, 2, 3 }
    return first == second

different_arrays :: proc() Bool ->
    first := [const]Byte { 1, 2, 3 }
    second := [const]Byte { 1, 2, 4 }
    return first not= second

/// A different number of bytes is not the same, nor is a different byte.
different_lengths :: proc() Bool ->
    first := [const]Byte { 1, 2, 3 }
    longer := [const]Byte { 1, 2, 3, 4 }
    return first == longer

/// An array and a slice, and two slices, in either order.
array_and_slice :: proc() Bool ->
    first := [const]Byte { 1, 2, 3 }
    longer := [const]Byte { 1, 2, 3, 4 }
    return first[:] == longer[0:3]

two_slices :: proc() Bool ->
    longer := [const]Byte { 1, 2, 3, 4, 2, 3 }
    return longer[1:3] == longer[4:6]

/// Bytes above 127 are compared as numbers from 0 to 255.
high_bytes :: proc() Bool ->
    first := [const]Byte { 200, 255 }
    second := [const]Byte { 200, 255 }
    third := [const]Byte { 100, 255 }
    return first == second and first not= third

/// An array of nothing, and a slice with no bytes, are the same.
nothing :: proc() Bool ->
    empty := [const]Byte { }
    bytes := [const]Byte { 1, 2, 3 }
    return empty[:] == bytes[1:1]

/// A procedure that takes slices compares them, and an array is given where a slice is wanted.
is_same :: proc(a: []Byte, b: []Byte) Bool ->
    return a == b

through_parameters :: proc() Bool ->
    first := [const]Byte { 7, 8 }
    second := [const]Byte { 7, 8 }
    return is_same(first, second)
