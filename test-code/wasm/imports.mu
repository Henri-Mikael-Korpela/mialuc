// `import "mu:math"` brings the definitions of a file of the standard library of Mialu into the
// program: `mu` is the standard library, the colon separates the vendor from the path under it, and
// the path is the file without `.mu`. The definitions are used with the name of their package
// before them, `mu.clamp`, which is how they are told from the program's own: this program has
// a `min` of its own too. The functions of the imported file are not exported.

package imports

import "mu:math"
import "mu:slices"
import "mu:strings"

/// The program's own, which has the name of one of the library. It is not the library's.
min :: proc(a: Int, b: Int) Int ->
    return a * 100 + b

clamped :: proc(number: Int) Int ->
    return mu.clamp(number, 0, 10)

largest :: proc(a: Int, b: Int) Int ->
    return mu.max(a, b)

smallest :: proc(a: Int, b: Int) Int ->
    return mu.min(a, b)

own_smallest :: proc(a: Int, b: Int) Int ->
    return min(a, b)

/// Where the first 3 is in the numbers 7, 3, 9 and 3, which is one specialization of the library's
/// polymorphic `find_first_index_of`, and where the first 5 is, which is not there.
first_three :: proc() Int ->
    numbers := [4]Int { 7, 3, 9, 3 }
    return mu.find_first_index_of(numbers[:], 3)

first_five :: proc() Int ->
    numbers := [4]Int { 7, 3, 9, 3 }
    return mu.find_first_index_of(numbers[:], 5)

/// The number of characters of the bytes 119, 97, 115 and 109, which are ASCII text, `wasm`, and
/// of the bytes 99, 97 and 195, which are not, so that there is no text, -1.
ascii_length :: proc() Int ->
    word := [4]Byte { 119, 97, 115, 109 }
    text, is_ascii := mu.try_bytes_to_str_ascii(word[:])
    if not is_ascii ->
        return -1
    return length_of(text)

not_ascii_length :: proc() Int ->
    accent := [3]Byte { 99, 97, 195 }
    text, is_ascii := mu.try_bytes_to_str_ascii(accent[:])
    if not is_ascii ->
        return -1
    return length_of(text)
