/// The version of the compiler, which is written once in the file VERSION of the root of the project:
/// MAJOR.MINOR.PATCH, a semantic version such as 0.5.0, without zeros at the start of a number.
package mu

/// Whether `part` is a number of a version: one or more digits, and no zero at the start unless the
/// number is only that, as semantic versioning says.
is_version_part :: proc(part: Str) Bool ->
    if length_of(part) == 0 ->
        return false
    for digit in part ->
        if digit < '0' or digit > '9' ->
            return false
    if length_of(part) > 1 ->
        if part[:1] == "0" ->
            return false
    return true

/// Whether `text` is a version of the form MAJOR.MINOR.PATCH: three numbers, with a dot between
/// each two. The characters of the text are gone through, and a `.` ends a number.
is_version :: proc(text: Str) Bool ->
    dots := 0
    start := 0
    index := 0
    for character in text ->
        if character == '.' ->
            if not is_version_part(text[start:index]) ->
                return false
            dots += 1
            start = index + 1
        index += 1
    return dots == 2 and is_version_part(text[start:])
