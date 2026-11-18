/// Contains version information for the current compiler implementation.
/// The version is also written down in src/version.h, which is the source of truth.
/// The version info in this file has to say the same, and `main` checks that it does,
/// by reading src/version.h and parsing the three numbers out of its
/// `#define MIALU_VERSION_MAJOR`, `MINOR` and `PATCH` lines.
/// 
/// Run this from the root of the project: `mialuc run scripts/check-version.mu`.

package mialuc

import "mu:filesystem"
import foreign libc

foreign libc ->
    puts :: proc(content: C_Str) I32 -> ...

/// The version, written once, as the three numbers. `int_to_str` makes the text of a constant number
/// and `++` joins strings, so VERSION_STRING is made of the numbers when the program is compiled.
//
// MISSING FEATURE: a constant expression that makes a number of text, such as
// `str_to_int("0")`, which would let a version that is not valid be an error of the build. The
// text is made of the numbers, as C makes it with `#`, and not the other way around.
VERSION_MAJOR :: 0
VERSION_MINOR :: 3
VERSION_PATCH :: 0

VERSION_STRING :: int_to_str(VERSION_MAJOR) ++ '.' ++ int_to_str(VERSION_MINOR) ++ '.' ++ int_to_str(VERSION_PATCH)

/// The path of the header, relative to the root of the project.
//
// MISSING FEATURE: the directory of the source file, or the arguments and the working directory of
// the program. `main` takes no arguments, so the path cannot be found when the program is run from
// somewhere else than the root of the project.
HEADER_PATH :: "src/version.h"

/// What a line of the header that defines a number starts with, and the length of that, the length
/// of the name that comes after it, which the three names have, and the space after the name.
DEFINE_PREFIX :: "#define MIALU_VERSION_"
DEFINE_PREFIX_LENGTH :: 22
NAME_LENGTH :: 5

/// What was found in the header. `found` counts the `#define` lines that were read, one for each
/// of the three numbers.
Version :: struct ->
    major: Int
    minor: Int
    patch: Int
    found: Int

/// The most bytes of the header that are read, which is more than it has, and the bytes of the memory
/// for the path in UTF-16 that `file_open` makes, which is twice the bytes of the path and two more,
/// and more than the path of the header needs. They are the lengths of the arrays that are made
/// for them.
BUFFER_SIZE :: 4096
PATH_BUFFER_SIZE :: 128

/// Reads a line of the header, `#define MIALU_VERSION_MAJOR 0`, which is the prefix, a name of
/// five letters, a space and a number, and stores the number in the field of `version` that the
/// name is of. Gives whether the line was one. A line that is anything else is left alone, so the
/// names that are longer, such as STRING and STRING_HELPER, are skipped, and so are a name that
/// goes on after the five letters, such as MAJORS, which has no space after them, and a number
/// that is not one.
read_definition :: proc(line: Str, version: ^Version) Bool ->
    name_end := DEFINE_PREFIX_LENGTH + NAME_LENGTH
    if length_of(line) <= name_end + 1 ->
        return false
    if line[:DEFINE_PREFIX_LENGTH] not= DEFINE_PREFIX ->
        return false
    if line[name_end:][:1] not= " " ->
        return false
    number, is_ok := try_str_to_int(line[name_end + 1:])
    if not is_ok ->
        return false
    match line[DEFINE_PREFIX_LENGTH:name_end] ->
        case "MAJOR" ->
            version.major = number
        case "MINOR" ->
            version.minor = number
        case "PATCH" ->
            version.patch = number
        case ->
            return false
    version.found += 1
    return true

/// Reads the line that is the bytes from `start` to `end` of `buffer`, without the carriage return
/// that a header that was written on Windows has before its line feed, which is compared with the
/// byte as the rune that it is. Bytes that are not valid UTF-8 are a line with no text, since
/// `utf8_decode` gives the empty string for them.
read_line :: proc(buffer: []Byte, start: Int, end: Int, version: ^Version) ->
    line_end := end
    if line_end > start and buffer[line_end - 1] == '\r' ->
        line_end -= 1
    line, _ := utf8_decode(buffer[start:line_end])
    read_definition(line, version)

/// Parses the version that src/version.h says. Gives false when the file cannot be read, and
/// otherwise gives the version, whose `found` says whether the three numbers were all there. The
/// whole header is read at once, with the memory for it and for its path that is on the stack of
/// this procedure and is given to the standard library, which takes none of its own. The lines are
/// the bytes between the line feeds, and a last line that has none is not one, as the header ends
/// with one.
parse_header :: proc(version: ^Version) Bool ->
    path_buffer := [PATH_BUFFER_SIZE]Byte { .. = 0 }
    buffer := [BUFFER_SIZE]Byte { .. = 0 }
    length, error := mu.file_read_all(HEADER_PATH, path_buffer[:], buffer[:])
    if error not= .NONE ->
        return false

    start := 0
    for index in 0..<length ->
        if buffer[index] == '\n' ->
            read_line(buffer[:], start, index, version)
            start = index + 1
    return true

/// Whether `text` is a version written as MAJOR.MINOR.PATCH, of the three numbers that are given.
/// The characters of the text are gone through, and a `.` ends a number, which is the text from the
/// one before it. A text that has a number too many, such as `1.2.3.4`, or too few, or that has a
/// part that is not a number, such as `1.x.3`, is not one.
is_version_string :: proc(text: Str, major: Int, minor: Int, patch: Int) Bool ->
    numbers := [3]Int { .. = 0 }
    parts := 0
    start := 0
    index := 0
    for character in text ->
        if character == '.' ->
            if parts > 1 ->
                return false
            number, is_ok := try_str_to_int(text[start:index])
            if not is_ok ->
                return false
            numbers[parts] = number
            parts += 1
            start = index + 1
        index += 1
    number, is_ok := try_str_to_int(text[start:index])
    if parts not= 2 or not is_ok ->
        return false
    numbers[2] = number
    is_same_major_and_minor := numbers[0] == major and numbers[1] == minor
    return is_same_major_and_minor and numbers[2] == patch

/// Whether the version of this file is the one of the C header, which is when the header has all
/// three numbers, each number of this file is the number of the header, and so is VERSION_STRING,
/// which is the three joined with dots.
matches_version_info_in_c :: proc() Bool ->
    version := Version {}
    if not parse_header(%version) ->
        return false
    if version.found not= 3 ->
        return false
    is_same_major := VERSION_MAJOR == version.major
    is_same_minor := VERSION_MINOR == version.minor
    is_same_patch := VERSION_PATCH == version.patch
    is_same_text := is_version_string(VERSION_STRING, version.major, version.minor, version.patch)
    return is_same_major and is_same_minor and is_same_patch and is_same_text

// MISSING FEATURE: a check that is made when the program is compiled, as the C header is checked by
// the C compiler, so that a version that does not match is an error of the build and not of a
// program that somebody runs.

main :: proc() Int ->
    is_ok := matches_version_info_in_c()
    if is_ok ->
        puts("OK")
    else ->
        puts("FAIL")
    return 0
