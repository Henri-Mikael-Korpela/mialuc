/// Contains version information
/// for the current compiler implementation.
///
/// The version is written down in src/version.h, which is the source of truth. This file has to say
/// the same, and `main` checks that it does, by reading src/version.h and parsing the three
/// numbers out of its `#define MIALU_VERSION_MAJOR`, `MINOR` and `PATCH` lines. Run it from the
/// root of the project: `mialuc run src/version.mu`.
///
/// Not everything that the check should do can be written in Mialu yet. The features that are
/// missing are marked with MISSING FEATURE below, and nothing is implemented past them.
package mialuc

import foreign libc

foreign libc ->
    puts :: proc(content: C_Str) Int -> ...
    fopen :: proc(path: C_Str, mode: C_Str) ^Byte -> ...
    fgetc :: proc(file: ^Byte) Int -> ...
    fclose :: proc(file: ^Byte) Int -> ...

VERSION_MAJOR :: 0
VERSION_MINOR :: 3
VERSION_PATCH :: 0

// MISSING FEATURE: a constant expression that makes text of a number, such as a built-in
// `int_to_str(VERSION_MAJOR)`. `++` joins only strings and runes, so the numbers cannot be spelled
// out as text at compile time, as `MIALU_VERSION_STRING_HELPER(major)` does in C with `#`. The three
// numbers are therefore written a second time as text here. `matches_version_info_in_c` checks
// when the program runs that the text says what the header does, by comparing it with `==`, but
// that is a check and not a definition, and a mismatch is found only by running it. Once there is
// such a feature, these three go away and VERSION_STRING is joined from the numbers.
VERSION_MAJOR_TEXT :: "0"
VERSION_MINOR_TEXT :: "3"
VERSION_PATCH_TEXT :: "0"

VERSION_STRING :: VERSION_MAJOR_TEXT ++ '.' ++ VERSION_MINOR_TEXT ++ '.' ++ VERSION_PATCH_TEXT

/// The path of the header, relative to the root of the project.
//
// MISSING FEATURE: the directory of the source file, or the arguments and the working directory of
// the program. `main` takes no arguments, so the path cannot be found when the program is run from
// somewhere else than the root of the project.
HEADER_PATH :: "src/version.h"

/// The characters of the text that is read, which are the ASCII codes of what the C library gives.
//
// MISSING FEATURE: a constant of a rune that can be compared with an `Int`, or the conversion of a
// rune to an `Int` in a constant expression, so that these are written `'\n'`. Also the escape
// sequences of strings and runes, which a backslash would start, since there is no way to write a
// new line.
LAST_BYTE :: 255
NEW_LINE :: 10
SPACE :: 32
HASH :: 35
DIGIT_0 :: 48
DIGIT_9 :: 57

/// Whether what `fgetc` gave is the end of the file, which is -1 in C, and not a byte.
//
// MISSING FEATURE: the type `int` of C for a foreign procedure, which is the reserved `I32`. `fgetc`
// returns an `int` of 32 bits, and a procedure that returns an `Int` is read as 64 bits, so the -1 of
// the end of the file arrives as 4294967295, the upper half of the register being not set by C. The
// test is made for a number that is not a byte, which is true for both, and is a guess about what
// the upper half holds.
is_end_of_file :: proc(c: Int) Bool ->
    return c < 0 or c > LAST_BYTE

/// What was found in the header. `found` counts the `#define` lines that were read, one for each
/// of the three numbers.
Version :: struct ->
    major: Int
    minor: Int
    patch: Int
    found: Int

/// Reads from the file the bytes of `text`, one each, and gives whether they were all there, and
/// the last byte that was read, which is the first one that was not when they were not.
//
// MISSING FEATURE: a `Str` cannot be subscripted or gone through by a for statement, so the text
// that is expected cannot be given as `"#define MIALU_VERSION_"` and read byte by byte. It is an
// array of the codes of its characters instead, and the caller has to write each of them.
read_text :: proc(file: ^Byte, text: []Int) (Bool, Int) ->
    last := 0
    for expected in text ->
        last = fgetc(file)
        if last not= expected ->
            return false, last
    return true, last

/// Reads the digits of a number, up to the first byte that is not a digit, and gives the number,
/// whether there were digits, and that byte.
read_number :: proc(file: ^Byte) (Int, Bool, Int) ->
    number := 0
    has_digits := false
    for ->
        c := fgetc(file)
        if c < DIGIT_0 or c > DIGIT_9 ->
            return number, has_digits, c
        number = number * 10 + c - DIGIT_0
        has_digits = true

/// Reads up to the end of the line, given the last byte that was read from it. Gives false at the
/// end of the file.
skip_line :: proc(file: ^Byte, last: Int) Bool ->
    c := last
    for ->
        if c == NEW_LINE ->
            return true
        if is_end_of_file(c) ->
            return false
        c = fgetc(file)

/// Reads the rest of a `#define MIALU_VERSION_` line, which is a name and a number, and stores the
/// number in the field of `version` that the name is of. Gives the last byte that was read.
///
/// The name is told by its first letters, which are M and A for MAJOR, M and I for MINOR and P for
/// PATCH, and every other name, such as STRING and STRING_HELPER, is skipped.
//
// MISSING FEATURE: a `Str` made of bytes that are read, such as `str_from_bytes(bytes)`. Strings can
// be compared with `==` now, but the name is read from the file as bytes, and bytes cannot be made
// into a `Str`, nor can a `C_Str` be compared, so the name cannot be compared with "MAJOR", "MINOR"
// and "PATCH". A `match` of a `Str` takes constants only. So the first letters have to stand for
// the names, and a new name that begins with the same letters would be taken for one of them.
read_define :: proc(file: ^Byte, version: ^Version) Int ->
    first := fgetc(file)
    which := 0
    if first == cast(Int)'P' ->
        which = 3
    else if first == cast(Int)'M' ->
        second := fgetc(file)
        if second == cast(Int)'A' ->
            which = 1
        else if second == cast(Int)'I' ->
            which = 2
    if which == 0 ->
        return first

    // The rest of the name, up to the space before the number.
    c := first
    for ->
        if c == SPACE ->
            break
        if c == NEW_LINE or is_end_of_file(c) ->
            return c
        c = fgetc(file)

    number, has_digits, last := read_number(file)
    if not has_digits ->
        return last
    match which ->
        case 1 ->
            version.major = number
        case 2 ->
            version.minor = number
        case 3 ->
            version.patch = number
    version.found += 1
    return last

/// Reads one line of the header, and gives false at the end of the file. A line that starts with
/// `#define MIALU_VERSION_` is read as a definition of the version, and the others are skipped.
read_line :: proc(file: ^Byte, version: ^Version) Bool ->
    first := fgetc(file)
    if is_end_of_file(first) ->
        return false
    last := first
    if first == HASH ->
        // "define MIALU_VERSION_", which comes after the # of the line.
        prefix := [const]Int {
            cast(Int)'d', cast(Int)'e', cast(Int)'f', cast(Int)'i', cast(Int)'n', cast(Int)'e',
            SPACE,
            cast(Int)'M', cast(Int)'I', cast(Int)'A', cast(Int)'L', cast(Int)'U', cast(Int)'_',
            cast(Int)'V', cast(Int)'E', cast(Int)'R', cast(Int)'S', cast(Int)'I', cast(Int)'O',
            cast(Int)'N', cast(Int)'_',
        }
        is_define, after := read_text(file, prefix)
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
    for ->
        if not read_line(file, version) ->
            break
    fclose(file)
    return true

/// The text of a number from 0 to 9, and the empty string, which no number is written as, for any
/// other.
//
// MISSING FEATURE: the text of any number when the program runs, such as a built-in
// `int_to_str(number)`. There is no way to make a `Str` of digits, so only the numbers that have a
// string of their own here can be told, and a version number of 10 or more is never the same as
// its text, though it is right.
digit_text :: proc(number: Int) Str ->
    match number ->
        case 0 ->
            return "0"
        case 1 ->
            return "1"
        case 2 ->
            return "2"
        case 3 ->
            return "3"
        case 4 ->
            return "4"
        case 5 ->
            return "5"
        case 6 ->
            return "6"
        case 7 ->
            return "7"
        case 8 ->
            return "8"
        case 9 ->
            return "9"
        case ->
            return ""

/// Whether the version of this file is the one of the C header, which is when the header has all
/// three numbers, they are the numbers of the constants above, and the text of the numbers, which
/// VERSION_STRING is joined from, is what the numbers of the header are written as.
matches_version_info_in_c :: proc() Bool ->
    version := Version {}
    if not parse_header(%version) ->
        return false
    if version.found not= 3 ->
        return false
    is_same_major_and_minor := version.major == VERSION_MAJOR and version.minor == VERSION_MINOR
    is_same_number := is_same_major_and_minor and version.patch == VERSION_PATCH
    is_same_major_text := digit_text(version.major) == VERSION_MAJOR_TEXT
    is_same_minor_text := digit_text(version.minor) == VERSION_MINOR_TEXT
    is_same_patch_text := digit_text(version.patch) == VERSION_PATCH_TEXT
    is_same_text := is_same_major_text and is_same_minor_text and is_same_patch_text
    return is_same_number and is_same_text

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
