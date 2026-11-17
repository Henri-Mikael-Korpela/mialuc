package string_iteration

/// A for statement over a string goes through its characters, which are code points, and the
/// iterator is a `Rune`. "Héllo" has 5 characters, though it takes 6 bytes.
count_characters :: proc() Int ->
    count := 0
    for character in "Héllo" ->
        count += 1
    return count

/// The code points are 97, 233, 8364, 128640 and 122: characters of 1, 2, 3 and 4 bytes.
sum_of_code_points :: proc() Int ->
    total := 0
    for character in "aé€🚀z" ->
        total += cast(Int)character
    return total

/// The iterator is a `Rune`, so it is compared with a rune literal.
count_vowels :: proc() Int ->
    text := "Héllo, wörld"
    vowels := 0
    for character in text ->
        if character == 'é' or character == 'ö' or character == 'o' ->
            vowels += 1
    return vowels

/// `continue` and `break` work as in any for statement.
count_before_comma :: proc() Int ->
    count := 0
    for character in "ab,cd" ->
        if character == ',' ->
            break
        count += 1
    return count

count_without_commas :: proc() Int ->
    count := 0
    for character in "a,b,c" ->
        if character == ',' ->
            continue
        count += 1
    return count

/// An empty string, and one that has no value, have no characters.
count_nothing :: proc() Int ->
    nothing : Str
    count := 0
    for character in nothing ->
        count += 1
    return count
