// `import "mu:math"` brings the definitions of a file of the standard library of Mialu into the
// program: `mu` is the standard library, the colon separates the vendor from the path under it, and
// the path is the file without `.mu`. The definitions are used with the name of their package
// before them, `mu.clamp`, which is how they are told from the program's own: this program has
// a `min` of its own too. The functions of the imported file are not exported.

package imports

import "mu:math"
import "mu:slices"

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
