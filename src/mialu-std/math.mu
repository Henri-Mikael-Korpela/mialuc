/// Numbers: the smallest and the largest of two, and a number held between two others. They work
/// on every target, as they only use the language itself.
package mialu

/// Gives the smaller of `a` and `b`.
min :: proc(a: Int, b: Int) Int ->
    if a < b ->
        return a
    return b

/// Gives the larger of `a` and `b`.
max :: proc(a: Int, b: Int) Int ->
    if a > b ->
        return a
    return b

/// Gives `number`, or `lowest` when it is below it, or `highest` when it is above it. `lowest` is
/// not above `highest`.
clamp :: proc(number: Int, lowest: Int, highest: Int) Int ->
    return min(max(number, lowest), highest)
