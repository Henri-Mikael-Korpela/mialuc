// A comment starts with two slashes and goes on to the end of the line. The compiler turns it into
// a token, and then leaves it out when it parses, so a comment never changes the program.
//
// A comment can be on a line of its own, and it can be at any indentation, because a line with
// only a comment is not part of the structure of the code.

/// A documentation comment starts with three slashes and must be on lines of its own.
/// It can go on over many lines, and each of them starts with three slashes.
///
/// A blank line, or a line of anything else, ends it, as does a line that has no `///`.
/// Returns the number of the answer.
answer :: proc() Int ->
    // A comment inside a block.
    x := 40 // A comment can follow code on the same line.
        // This one is indented more than the block, and that is fine.
    // So is this one, which is not at the indentation of the lines around it.
    x += 2
    return x

// A comment does not hide the next line, and a `//` in a string is not a comment.
url :: "https://example.com"

/// Returns a string that has two slashes in it.
slashes :: proc() Str ->
    return "// not a comment"

// A comment between the lines of an array literal is fine too.
sum_of_array :: proc() Int ->
    numbers := [const]Int {
        // The first number.
        1,
        2, // The second one.
        // The last one.
        3,
    }
    total := 0
    for n in numbers ->
        total += n
    return total

/// Returns 1 when the condition is true. A comment can also be between the branches.
choose :: proc(condition: Bool) Int ->
    if condition ->
        return 1
    // This is the other branch.
    else ->
        return 2
