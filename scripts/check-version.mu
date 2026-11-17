/// Contains version information
/// for the current compiler implementation.
///
/// The version is written down in src/version.h, which is the source of truth. This file has to say
/// the same, and `main` checks that it does, by reading src/version.h line by line and parsing the
/// three numbers out of its `#define MIALU_VERSION_MAJOR`, `MINOR` and `PATCH` lines. Run it from
/// the root of the project: `mialuc run scripts/check-version.mu`.
///
/// Not everything that the check should do can be written in Mialu yet. The features that are
/// missing are marked with MISSING FEATURE below, and nothing is implemented past them.
package mialuc

import foreign libc

/// The procedures of the C library are declared with the types that C has: `puts`, `fgets` and
/// `fclose` take or give an `int`, which is an `I32`, and `strlen` gives a `size_t`, which is as large
/// as an `Int`. `fgets` and `strlen` take the buffer as a `^Byte`, the address of its first byte.
foreign libc ->
    fclose :: proc(file: ^Byte) I32 -> ...
    fgets :: proc(buffer: ^Byte, size: I32, file: ^Byte) ^Byte -> ...
    fopen :: proc(path: C_Str, mode: C_Str) ^Byte -> ...
    puts :: proc(content: C_Str) I32 -> ...
    strlen :: proc(buffer: ^Byte) Int -> ...

/// The version, written once, as the text of each number. `++` joins strings, so VERSION_STRING is
/// joined from them when the program is compiled, and the numbers are made of the text by
/// `try_str_to_int` when the program runs, see `matches_version_info_in_c`.
//
// MISSING FEATURE: a constant expression that makes a number of text, or text of a number, when the
// program is compiled, such as `str_to_int(VERSION_MAJOR)`. `try_str_to_int` works when the program
// runs, so the numbers are not constants, and cannot be the length of an array or the pattern of a
// `match`, nor can a version that is not valid be an error of the build. C has the numbers, and
// makes the text of them with `#`, so there it is the other way around.
VERSION_MAJOR :: "0"
VERSION_MINOR :: "3"
VERSION_PATCH :: "0"

VERSION_STRING :: VERSION_MAJOR ++ '.' ++ VERSION_MINOR ++ '.' ++ VERSION_PATCH

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

/// Reads the next line of the file into `buffer`, and gives its text without the end of the line,
/// and false at the end of the file. A line that does not fit is cut where the buffer ends, and the
/// rest of it is the next line, which no line of the header that matters is long enough to make.
///
/// Bytes that are not valid UTF-8 are a line with no text, since `utf8_decode` gives the empty
/// string for them.
//
// MISSING FEATURE: the escape sequences of strings and runes, which a backslash would start, so that
// the line feed and the carriage return are written `'\n'` and `'\r'`, and not as their numbers 10
// and 13, which are compared with a `Byte`.
read_line :: proc(file: ^Byte, buffer: []Byte) (Str, Bool) ->
    if fgets(%buffer[0], int_to_i32(length_of(buffer)), file) == nil ->
        return "", false

    length := strlen(%buffer[0])
    for ->
        if length == 0 ->
            break
        last := buffer[length - 1]
        if last not= 10 and last not= 13 ->
            break
        length -= 1
    text, _ := utf8_decode(buffer[0:length])
    return text, true

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
    if line[0:DEFINE_PREFIX_LENGTH] not= DEFINE_PREFIX ->
        return false
    if line[name_end:name_end + 1] not= " " ->
        return false
    number, is_ok := try_str_to_int(line[name_end + 1:length_of(line)])
    if not is_ok ->
        return false
    name := line[DEFINE_PREFIX_LENGTH:name_end]
    if name == "MAJOR" ->
        version.major = number
    else if name == "MINOR" ->
        version.minor = number
    else if name == "PATCH" ->
        version.patch = number
    else ->
        return false
    version.found += 1
    return true

/// Parses the version that src/version.h says. Gives false when the file cannot be read, and
/// otherwise gives the version, whose `found` says whether the three numbers were all there.
parse_header :: proc(version: ^Version) Bool ->
    file := fopen(HEADER_PATH, "rb")
    if file == nil ->
        return false
    defer fclose(file)

    buffer := [128]Byte { .. = 0 }
    for ->
        line, is_read := read_line(file, buffer)
        if not is_read ->
            break
        read_definition(line, version)
    return true

/// Whether a number of the version of this file, which is text, is the number that the header says:
/// the text must be a number, and the same one.
is_same_number :: proc(text: Str, number: Int) Bool ->
    value, is_ok := try_str_to_int(text)
    return is_ok and value == number

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
/// three numbers, the text of each number of this file is the number of the header, and so is
/// VERSION_STRING, which is the three joined with dots.
matches_version_info_in_c :: proc() Bool ->
    version := Version {}
    if not parse_header(%version) ->
        return false
    if version.found not= 3 ->
        return false
    is_same_major := is_same_number(VERSION_MAJOR, version.major)
    is_same_minor := is_same_number(VERSION_MINOR, version.minor)
    is_same_patch := is_same_number(VERSION_PATCH, version.patch)
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
