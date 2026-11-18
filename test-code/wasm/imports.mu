// `import "mu:math"` brings the definitions of a file of the standard library of Mialu into the
// program: `mu` is the standard library, the colon separates the vendor from the path under it, and
// the path is the file without `.mu`. The definitions are used with the name of their package
// before them, `mialu.clamp`, which is how they are told from the program's own: this program has
// a `min` of its own too. The functions of the imported file are not exported.

package imports

import "mu:math"

/// The program's own, which has the name of one of the library. It is not the library's.
min :: proc(a: Int, b: Int) Int ->
    return a * 100 + b

clamped :: proc(number: Int) Int ->
    return mialu.clamp(number, 0, 10)

largest :: proc(a: Int, b: Int) Int ->
    return mialu.max(a, b)

smallest :: proc(a: Int, b: Int) Int ->
    return mialu.min(a, b)

own_smallest :: proc(a: Int, b: Int) Int ->
    return min(a, b)
