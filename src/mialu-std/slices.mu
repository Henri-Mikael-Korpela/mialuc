/// Slices: searching the elements of an array slice, of any type that `==` compares. They work on
/// every target, as they only use the language itself.
package mu

/// Gives where the first element of `elements` that is `target` is, or -1 when there is none.
find_first_index_of :: proc(elements: []$T, target: T) Int ->
    for index in 0..<length_of(elements) ->
        if elements[index] == target ->
            return index
    return -1
