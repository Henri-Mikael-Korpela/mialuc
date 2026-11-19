/// Sets the version of the compiler. The version is written once, in the file VERSION of the root
/// of the project, as MAJOR.MINOR.PATCH, a semantic version such as 0.5.0 without leading zeros,
/// which CMake gives to the compiler and scripts/deploy-release.ps1 releases. This program reads the
/// new version from a line of the standard input and writes it to that file.
///
/// A program of Mialu has no arguments, so the version is read, and it is run with the compiler
/// of this project from the root of the project, because the path of the file is relative to it:
///
///     build\mialuc.exe run scripts/bump-version.mu
///     echo 0.5.0| build\mialuc.exe run scripts/bump-version.mu
///
/// The compiler does not have the new version until it is built again.
//
// MISSING FEATURE: the arguments of a program, and the directory of the source file, so that the
// version could be an argument, `bump-version.mu 0.5.0`, and the file could be found whatever the
// directory is that the program is run in.

package bump_version

import "mu:filesystem"
import foreign libc

foreign libc ->
    fflush :: proc(stream: ^Byte) I32 -> ...
    printf :: proc(format: C_Str, text: C_Str) I32 -> ...
    puts   :: proc(content: C_Str) I32 -> ...

/// The path of the file of the version, relative to the root of the project.
VERSION_PATH :: "VERSION"

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

/// Writes `bytes` and then a line feed to the file at `path`, which is made when it is not there and
/// emptied when it is. Gives whether all of it was written.
write_line :: proc(path: Str, bytes: []Byte) Bool ->
    wide_path := [128]Byte { .. = 0 }
    file, open_error := mu.file_open(path, .WRITE, wide_path[:])
    if open_error not= .NONE ->
        return false
    defer mu.file_close(file)

    count, error := mu.file_write(file, bytes)
    if error not= .NONE or count not= length_of(bytes) ->
        return false
    line_feed := [1]Byte { 10 }
    written, line_feed_error := mu.file_write(file, line_feed[:])
    return line_feed_error == .NONE and written == 1

main :: proc() Int ->
    puts("The new version of the compiler, MAJOR.MINOR.PATCH, such as 0.5.0:")
    fflush(nil)

    line := [64]Byte { .. = 0 }
    length, error := mu.file_read_line(mu.file_standard_input(), line[:])
    version, is_text := utf8_decode(line[:length])
    if error not= .NONE or not is_text or not is_version(version) ->
        puts("That is not a version of the form MAJOR.MINOR.PATCH, such as 0.5.0, without zeros at the start.")
        return 1

    if not write_line(VERSION_PATH, line[:length]) ->
        puts("The file VERSION cannot be written: run this from the root of the project.")
        return 2
    printf("The version is now %s. Build the compiler again, commit the file VERSION and push the tag.", str_to_c_str(version))
    puts("")
    return 0
