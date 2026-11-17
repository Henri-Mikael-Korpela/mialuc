/// Contains version information
/// for the current compiler implementation.
///
/// The version is written down in src/version.h, which is the source of truth. This file has to say
/// the same, and `main` checks that it does, by reading src/version.h and parsing the three
/// numbers out of its `#define MIALU_VERSION_MAJOR`, `MINOR` and `PATCH` lines. Run it from the
/// root of the project: `mialuc run scripts/check-version.mu`.
///
/// Not everything that the check should do can be written in Mialu yet. The features that are
/// missing are marked with MISSING FEATURE below, and nothing is implemented past them.
package mialuc

import foreign libc

/// The procedures of the C library are declared with the types that C has: `puts`, `fgetc` and
/// `fclose` give an `int`, which is an `I32`. An `fgetc` that was declared to give an `Int` would be
/// read as 64 bits, and the -1 that it gives at the end of the file would arrive with the upper half
/// of the register not set.
foreign libc ->
    puts :: proc(content: C_Str) I32 -> ...
    fopen :: proc(path: C_Str, mode: C_Str) ^Byte -> ...
    fgetc :: proc(file: ^Byte) I32 -> ...
    fclose :: proc(file: ^Byte) I32 -> ...

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

/// The bytes of the text that is read, which are the ASCII codes of its characters, as `fgetc` gives
/// them: an `I32`, which is -1 at the end of the file, so that is what a byte is here too.
//
// MISSING FEATURE: the escape sequences of strings and runes, which a backslash would start, so that
// the new line is written `'\n'` and not as its number, since there is no way to write one. Also a
// constant that is the number of a rune, such as `NEW_LINE :: cast(I32)'\n'`, which a cast is not,
// so that the other bytes could be constants too, and not the result of `code('#')` where they are
// used.
END_OF_FILE :: -1
NEW_LINE :: 10

/// The code of a character, which is the byte that `fgetc` gives for it, when it is ASCII. This is
/// the only place where a `Rune` is made an `I32`, so that the rest of the program compares bytes
/// with characters without a cast of its own.
code :: proc(character: Rune) I32 ->
    return cast(I32)character

/// Whether what `fgetc` gave is the end of the file.
is_end_of_file :: proc(c: I32) Bool ->
    return c == END_OF_FILE

/// What was found in the header. `found` counts the `#define` lines that were read, one for each
/// of the three numbers.
Version :: struct ->
    major: I32
    minor: I32
    patch: I32
    found: Int

/// Reads from the file one byte for each character of `text`, and gives whether they were all
/// there, and the last byte that was read, which is the first one that was not when they were not.
/// The text is ASCII, so a character and a byte are the same, and the characters of a string are
/// gone through by the for statement.
read_text :: proc(file: ^Byte, text: Str) (Bool, I32) ->
    last : I32 = 0
    for expected in text ->
        last = fgetc(file)
        if last not= code(expected) ->
            return false, last
    return true, last

/// Reads the digits of a number, up to the first byte that is not a digit, and gives the number,
/// whether there were digits, and that byte.
//
// MISSING FEATURE: a `Str` made of bytes that are read, such as `str_from_bytes(bytes)`, which
// would let `try_str_to_int` do this, and with its check for a number that is too large. The digits
// are read from the file as bytes, and bytes cannot be made into a `Str`, nor can a `C_Str` be
// converted, so the number is made of them here, one digit at a time.
read_number :: proc(file: ^Byte) (I32, Bool, I32) ->
    number : I32 = 0
    has_digits := false
    for ->
        c := fgetc(file)
        if c < code('0') or c > code('9') ->
            return number, has_digits, c
        number = number * 10 + c - code('0')
        has_digits = true

/// Reads up to the end of the line, given the last byte that was read from it. Gives false at the
/// end of the file.
skip_line :: proc(file: ^Byte, last: I32) Bool ->
    c := last
    for ->
        if c == NEW_LINE ->
            return true
        if is_end_of_file(c) ->
            return false
        c = fgetc(file)

/// Whether the byte `c` is the character at `index` of `name`, which is false when `name` has no
/// character there. The subscript of a string is by the character, and the name is ASCII.
is_name_byte :: proc(name: Str, index: Int, c: I32) Bool ->
    if index >= length_of(name) ->
        return false
    return code(name[index]) == c

/// Reads the name of a macro, up to the space after it, and gives which of the three names it is:
/// 1 for MAJOR, 2 for MINOR and 3 for PATCH, and 0 for any other, such as STRING and
/// STRING_HELPER. The last byte that was read is given as well. Each byte is compared with the
/// character at its place in all three names, and a name is the one that all of its bytes matched
/// and that has no more.
read_name :: proc(file: ^Byte) (Int, I32) ->
    major := "MAJOR"
    minor := "MINOR"
    patch := "PATCH"
    is_major := true
    is_minor := true
    is_patch := true
    index := 0
    for ->
        c := fgetc(file)
        if c == code(' ') ->
            which := 0
            if is_major and index == length_of(major) ->
                which = 1
            else if is_minor and index == length_of(minor) ->
                which = 2
            else if is_patch and index == length_of(patch) ->
                which = 3
            return which, c
        if c == NEW_LINE or is_end_of_file(c) ->
            return 0, c
        is_major = is_major and is_name_byte(major, index, c)
        is_minor = is_minor and is_name_byte(minor, index, c)
        is_patch = is_patch and is_name_byte(patch, index, c)
        index += 1

/// Reads the rest of a `#define MIALU_VERSION_` line, which is a name and a number, and stores the
/// number in the field of `version` that the name is of. Gives the last byte that was read.
//
// MISSING FEATURE: a `Str` made of bytes that are read, see `read_number`. The name is read from the
// file as bytes too, so it cannot be compared with "MAJOR" and the others with `==`, which is why
// `read_name` compares it byte by byte.
read_define :: proc(file: ^Byte, version: ^Version) I32 ->
    which, last := read_name(file)
    if which == 0 ->
        return last

    number, has_digits, after := read_number(file)
    if not has_digits ->
        return after
    match which ->
        case 1 ->
            version.major = number
        case 2 ->
            version.minor = number
        case 3 ->
            version.patch = number
    version.found += 1
    return after

/// Reads one line of the header, and gives false at the end of the file. A line that starts with
/// `#define MIALU_VERSION_` is read as a definition of the version, and the others are skipped.
read_line :: proc(file: ^Byte, version: ^Version) Bool ->
    first := fgetc(file)
    if is_end_of_file(first) ->
        return false
    last := first
    if first == code('#') ->
        is_define, after := read_text(file, "define MIALU_VERSION_")
        last = after
        if is_define ->
            last = read_define(file, version)
    return skip_line(file, last)

/// Parses the version that src/version.h says. Gives false when the file cannot be read, and
/// otherwise gives the version, whose `found` says whether the three numbers were all there.
parse_header :: proc(version: ^Version) Bool ->
    file := fopen(HEADER_PATH, "rb")
    if file == nil ->
        return false
    defer fclose(file)

    for ->
        if not read_line(file, version) ->
            break

    return true

/// Whether a number of the version of this file, which is text, is the number that the header says:
/// the text must be a number, and the same one. The number of the header is an `I32` and the one of
/// `try_str_to_int` an `Int`, which is not converted, so the `I32` is made an `Int`, which never
/// loses anything.
is_same_number :: proc(text: Str, number: I32) Bool ->
    value, is_ok := try_str_to_int(text)
    return is_ok and value == cast(Int)number

/// Whether the version of this file is the one of the C header, which is when the header has all
/// three numbers, and the text of each number of this file is the number of the header.
matches_version_info_in_c :: proc() Bool ->
    version := Version {}
    if not parse_header(%version) ->
        return false
    if version.found not= 3 ->
        return false
    is_same_major := is_same_number(VERSION_MAJOR, version.major)
    is_same_minor := is_same_number(VERSION_MINOR, version.minor)
    is_same_patch := is_same_number(VERSION_PATCH, version.patch)
    return is_same_major and is_same_minor and is_same_patch

// MISSING FEATURE: comparing VERSION_STRING with the `// "0.3.0"` line of the header. The strings
// can be compared, but the line would have to be read into a `Str`, and a `Str` is made of
// constants only, since an array of bytes cannot be made into one, and an array cannot be made
// without writing every element, so there is no buffer to read a line into, either. There would
// also need to be a way to run the check when the program is compiled, as the C header is checked
// by the C compiler, so that a version that does not match is an error of the build and not of a
// program that somebody runs.

main :: proc() Int ->
    is_ok := matches_version_info_in_c()
    if is_ok ->
        puts("OK")
    else ->
        puts("FAIL")
    return 0
