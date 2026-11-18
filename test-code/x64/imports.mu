// `import "mu:filesystem"` brings the definitions of a file of the standard library of Mialu into
// the program, and they are used with the name of their package before them: `mialu.file_open` is
// `file_open` of the package `mialu`. The program has names of its own that the library has too,
// `File`, `CP_UTF8` and `clamp`, and they are not the library's. The exit code of the program is the
// number of wrong results, 0.

package imports

import "mu:filesystem"
import "mu:math"

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

/// The program's own, with the names that the library has: they cannot be mistaken for each other.
File :: struct ->
    name_length: Int
    is_open: Bool

CP_UTF8 :: 7

clamp :: proc(number: Int) Int ->
    return number + 1000

/// Prints a line with the number, and counts it as wrong when it is not what was expected.
check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

flag :: proc(is_true: Bool) Int ->
    if is_true ->
        return 1
    return 0

main :: proc() Int ->
    wrong := 0
    buffer := [64]Byte { .. = 0 }
    wide := buffer[0:64]

    // The library's file, opened and closed, and the errors that it gives.
    file, error := mialu.file_open("NUL", mialu.File_Open_Mode.READ, wide)
    wrong += check("NUL is opened: %lld", flag(error == mialu.File_Error.NONE), 1)
    wrong += check("NUL is closed: %lld", flag(mialu.file_close(file) == mialu.File_Error.NONE), 1)
    missing, missing_error := mialu.file_open("no-such-directory/none.txt", mialu.File_Open_Mode.READ, wide)
    wrong += check("a missing file is NOT_FOUND: %lld", flag(missing_error == mialu.File_Error.NOT_FOUND), 1)
    wrong += check("a file that is not open is FILE_INVALID: %lld", flag(mialu.file_close(missing) == mialu.File_Error.FILE_INVALID), 1)

    // The program's own File is not the library's, and neither is its CP_UTF8 or its clamp.
    mine := File { name_length = 3, is_open = true }
    theirs := mialu.File { handle = 5 }
    wrong += check("the program's File has %lld", mine.name_length, 3)
    wrong += check("the library's File has %lld", theirs.handle, 5)
    wrong += check("the program's CP_UTF8 is %lld", CP_UTF8, 7)
    wrong += check("the program's clamp gives %lld", clamp(5), 1005)
    wrong += check("the library's clamp gives %lld", mialu.clamp(50, 0, 10), 10)
    wrong += check("the library's min gives %lld", mialu.min(3, 4), 3)
    return wrong
