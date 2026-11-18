/// Contains version information for the current compiler implementation.
/// The version is also written down in src/version.h, which is the source of truth.
/// The version info in this file has to say the same, and `main` checks that it does,
/// by reading src/version.h and parsing the three numbers out of its
/// `#define MIALU_VERSION_MAJOR`, `MINOR` and `PATCH` lines.
/// 
/// Run this from the root of the project: `mialuc run scripts/check-version.mu`.

package mialuc

import "mu:filesystem"
import foreign kernel32 "system:Kernel32.lib"
import foreign libc

/// `puts` of the C library gives an `int`, which is an `I32`.
foreign libc ->
    puts :: proc(content: C_Str) I32 -> ...

/// What the standard library cannot do yet is read a file, which `mu:filesystem` only opens and
/// closes, so the bytes are read with `ReadFile` of Windows. A `HANDLE` is the size of an `Int`, a
/// `DWORD` is a `U32` and a `BOOL` is an `I32`, and the address of the number of bytes that were
/// read is the address of a `U32`.
//
// MISSING FEATURE: `file_read` in `mu:filesystem`, which would take the place of `ReadFile` here.
foreign kernel32 #(default_calling_convention("system")) ->
    ReadFile :: proc(file: Int, buffer: ^Byte, size: U32, read: ^U32, overlapped: ^Byte) I32 -> ...

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

// The most bytes of the header that are read is 4096, which is more than it has, and the memory for
// the path in UTF-16 that `file_open` makes is 128 bytes, which is twice the bytes of the path and
// two more, and more than the path of the header needs. An array has a length that is written as a
// number, so the numbers are written where they are used.

/// Reads the file at `path` into `buffer`, from its start, as many bytes as it has up to 4096, which
/// the buffer must have, and gives how many, and whether the file was opened and read. The memory
/// that the standard library needs for the path is on the stack of this procedure and is given to
/// it, as it takes none of its own. The file is closed when the procedure returns.
read_file :: proc(path: Str, buffer: []Byte) (Int, Bool) ->
    if length_of(buffer) < 4096 ->
        return 0, false
    scratch := [128]Byte { .. = 0 }
    file, error := mialu.file_open(path, mialu.File_Open_Mode.READ, scratch[0:128])
    if error not= mialu.File_Error.NONE ->
        return 0, false
    defer mialu.file_close(file)

    count : U32 = 0
    if ReadFile(file.handle, %buffer[0], 4096, %count, nil) == 0 ->
        return 0, false
    return cast(Int)count, true

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
/// lines are the bytes between the line feeds, and a last line that has none is not one, as the
/// header ends with one.
parse_header :: proc(version: ^Version) Bool ->
    buffer := [4096]Byte { .. = 0 }
    length, is_read := read_file(HEADER_PATH, buffer[0:4096])
    if not is_read ->
        return false

    start := 0
    for index in 0..<length ->
        if buffer[index] == '\n' ->
            read_line(buffer[0:4096], start, index, version)
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
