// A for statement over a string goes through its characters, which are code points, and the
// iterator is a `Rune`: `for character in "Héllo"` runs 5 times, though the string has 6 bytes. The
// iteration decodes the UTF-8 as it goes, so it takes as long as the string, which a loop over
// the indexes of a subscript does not. `break` and `continue` work as in any for statement. The exit
// code of the program is the number of wrong results, 0.

package string_iteration

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

/// Prints a line with the number, and counts it as wrong when it is not what was expected.
check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

count_characters :: proc(text: Str) Int ->
    count := 0
    for character in text ->
        count += 1
    return count

/// The sum of the code points of a string.
sum_of_code_points :: proc(text: Str) Int ->
    total := 0
    for character in text ->
        total += cast(Int)character
    return total

main :: proc() Int ->
    wrong := 0

    wrong += check("Héllo has %lld characters", count_characters("Héllo"), 5)
    wrong += check("abc has %lld characters", count_characters("abc"), 3)
    wrong += check("empty has %lld characters", count_characters(""), 0)
    nothing : Str
    wrong += check("nothing has %lld characters", count_characters(nothing), 0)
    wrong += check("mixed has %lld characters", count_characters("aé€🚀z"), 5)

    // The code points of 1, 2, 3 and 4 bytes: 97 + 233 + 8364 + 128640 + 122.
    wrong += check("sum is %lld", sum_of_code_points("aé€🚀z"), 137456)
    wrong += check("ASCII sum is %lld", sum_of_code_points("abc"), 294)

    // The characters come in order, and an iterator is a Rune that can be compared.
    order := 0
    for character in "äiti" ->
        order = order * 1000 + cast(Int)character
    wrong += check("order is %lld", order, 228105116105)

    vowels := 0
    for character in "Héllo, wörld" ->
        if character == 'é' or character == 'ö' or character == 'o' ->
            vowels += 1
    wrong += check("vowels are %lld", vowels, 3)

    // `break` and `continue`.
    skipped := 0
    for character in "a,b,c" ->
        if character == ',' ->
            continue
        skipped += 1
    wrong += check("without commas %lld", skipped, 3)
    before_comma := 0
    for character in "ab,cd" ->
        if character == ',' ->
            break
        before_comma += 1
    wrong += check("before the comma %lld", before_comma, 2)

    // A slice, a variable and a nested loop.
    text := "Hello, World!"
    inner := 0
    for character in text[7:12] ->
        inner += 1
    wrong += check("a slice has %lld characters", inner, 5)
    pairs := 0
    for first in "ab" ->
        for second in "xyz" ->
            pairs += 1
    wrong += check("pairs are %lld", pairs, 6)
    return wrong
