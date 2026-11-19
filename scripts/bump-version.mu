/// Sets the version of the compiler. The version is written once, in the file VERSION of the root
/// of the project, as MAJOR.MINOR.PATCH, a semantic version such as 0.5.0 without leading zeros,
/// which CMake gives to the compiler and scripts/deploy-release.ps1 releases. This program writes
/// the new version to that file. The version is the argument that is given to the program, and when
/// there is none it is a line that the program asks for and reads from the standard input.
///
/// It is run with the compiler of this project, from the root of the project, because the path of
/// the file is relative to it. What comes after the source file is for the program:
///
///     build\mialuc.exe run scripts/bump-version.mu 0.5.0
///     build\mialuc.exe run scripts/bump-version.mu
///     echo 0.5.0| build\mialuc.exe run scripts/bump-version.mu
///
/// The compiler does not have the new version until it is built again.
//
// MISSING FEATURE: the directory of the source file, so that the file could be found whatever the
// directory is that the program is run in.

package bump_version

import "mu:filesystem"
import "mu:process"
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
    // The version is what follows the name of the program in its command line, the arguments as they
    // were written, so a version with a space is not one.
    command_line := [1024]Byte { .. = 0 }
    command_length := mu.process_command_line(command_line[:])
    version_bytes := command_line[mu.process_arguments_start(command_line[:command_length]):command_length]

    // Without an argument the version is a line of the input.
    line := [64]Byte { .. = 0 }
    if length_of(version_bytes) == 0 ->
        puts("The new version of the compiler, MAJOR.MINOR.PATCH, such as 0.5.0:")
        fflush(nil)
        line_length, error := mu.file_read_line(mu.file_standard_input(), line[:])
        if error not= .NONE ->
            puts("There is no version: give it as an argument, or on a line of the input.")
            return 1
        version_bytes = line[:line_length]

    version, is_text := utf8_decode(version_bytes)
    if not is_text or not is_version(version) ->
        puts("That is not a version of the form MAJOR.MINOR.PATCH, such as 0.5.0, without zeros at the start.")
        return 1

    if not write_line(VERSION_PATH, version_bytes) ->
        puts("The file VERSION cannot be written: run this from the root of the project.")
        return 2
    printf("The version is now %s. Build the compiler again, commit the file VERSION and push the tag.", str_to_c_str(version))
    puts("")
    return 0
