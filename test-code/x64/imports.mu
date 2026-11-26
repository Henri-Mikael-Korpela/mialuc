// `import "mu:filesystem"` brings the definitions of a file of the standard library of Mialu into
// the program, and they are used with the name of their package before them: `mu.file_open` is
// `file_open` of the package `mu`. The program has names of its own that the library has too,
// `File`, `CP_UTF8` and `clamp`, and they are not the library's. The exit code of the program is the
// number of wrong results, 0.

package imports

import "mu:filesystem"
import "mu:math"
import "mu:net"

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
    file, error := mu.file_open("NUL", mu.File_Open_Mode.READ, wide)
    wrong += check("NUL is opened: %lld", flag(error == mu.File_Error.NONE), 1)
    wrong += check("NUL is closed: %lld", flag(mu.file_close(file) == mu.File_Error.NONE), 1)
    missing, missing_error := mu.file_open("no-such-directory/none.txt", mu.File_Open_Mode.READ, wide)
    wrong += check("a missing file is NOT_FOUND: %lld", flag(missing_error == mu.File_Error.NOT_FOUND), 1)
    wrong += check("a file that is not open is FILE_INVALID: %lld", flag(mu.file_close(missing) == mu.File_Error.FILE_INVALID), 1)

    // Reading: NUL has no bytes, so the first read of it is the end of the input, and reading a file
    // that is not open is an error. The whole of a file that is not there is not read.
    reader, reader_error := mu.file_open("NUL", .READ, wide)
    data := [16]Byte { .. = 0 }
    count, read_error := mu.file_read(reader, data[:])
    wrong += check("NUL gives %lld bytes", count, 0)
    wrong += check("and no error: %lld", flag(read_error == mu.File_Error.NONE), 1)
    wrong += check("NUL is closed again: %lld", flag(mu.file_close(reader) == mu.File_Error.NONE), 1)
    after_close, after_error := mu.file_read(reader, data[:])
    wrong += check("a read of a closed file is FILE_INVALID, %lld", flag(after_error == mu.File_Error.FILE_INVALID), 1)
    // A line of NUL: it has none, so there is no first byte of one.
    nul_reader, nul_reader_error := mu.file_open("NUL", .READ, wide)
    line_length, line_error := mu.file_read_line(nul_reader, data[:])
    wrong += check("a line of NUL is %lld bytes", line_length, 0)
    wrong += check("and the end of the file: %lld", flag(line_error == mu.File_Error.END_OF_FILE), 1)
    wrong += check("NUL is closed once more: %lld", flag(mu.file_close(nul_reader) == mu.File_Error.NONE), 1)
    all_count, all_error := mu.file_read_all("no-such-directory/none.txt", wide, data[:])
    wrong += check("the whole of a missing file is NOT_FOUND: %lld", flag(all_error == mu.File_Error.NOT_FOUND), 1)
    nul_count, nul_error := mu.file_read_all("NUL", wide, data[:])
    wrong += check("the whole of NUL has %lld bytes", nul_count, 0)

    // The entry of a directory by its name: a directory of Windows is a directory, a file in it is not
    // one and has bytes, a name that is not there is NOT_FOUND, and a pattern is not a name.
    directory_entry, directory_error := mu.find_entry_first("C:/Windows", "System32")
    wrong += check("System32 is found: %lld", flag(directory_error == mu.File_Error.NONE), 1)
    wrong += check("and it is a directory: %lld", flag(directory_entry.is_directory), 1)
    file_entry, file_error := mu.find_entry_first("C:/Windows", "explorer.exe")
    wrong += check("explorer.exe is found: %lld", flag(file_error == mu.File_Error.NONE), 1)
    wrong += check("and it is not a directory: %lld", flag(file_entry.is_directory), 0)
    wrong += check("and it has bytes: %lld", flag(file_entry.size_bytes > 0), 1)
    missing_entry, missing_entry_error := mu.find_entry_first("C:/Windows", "no-such-entry.txt")
    wrong += check("a missing entry is NOT_FOUND: %lld", flag(missing_entry_error == mu.File_Error.NOT_FOUND), 1)
    star_entry, star_error := mu.find_entry_first("C:/Windows", "*.exe")
    wrong += check("a * is PATH_INVALID: %lld", flag(star_error == mu.File_Error.PATH_INVALID), 1)
    question_entry, question_error := mu.find_entry_first("C:/Windows", "explorer.ex?")
    wrong += check("a ? is PATH_INVALID: %lld", flag(question_error == mu.File_Error.PATH_INVALID), 1)
    wildcard_entry, wildcard_error := mu.find_entry_first("C:/Win*", "explorer.exe")
    wrong += check("a * in the directory is PATH_INVALID: %lld", flag(wildcard_error == mu.File_Error.PATH_INVALID), 1)

    // Sockets: an address that is not 4 bytes and a port that is not one are ADDRESS_INVALID, a port
    // that is listened on is closed after, and a port that is taken cannot be listened on again.
    local := [4]Byte { 127, 0, 0, 1 }
    short := [3]Byte { 127, 0, 1 }
    bad_socket, bad_address_error := mu.net_listen(short[:], 52731, 4)
    wrong += check("an address of 3 bytes is ADDRESS_INVALID: %lld", flag(bad_address_error == mu.Net_Error.ADDRESS_INVALID), 1)
    zero_socket, zero_error := mu.net_listen(local[:], 0, 4)
    wrong += check("port 0 is ADDRESS_INVALID: %lld", flag(zero_error == mu.Net_Error.ADDRESS_INVALID), 1)
    listener, listen_error := mu.net_listen(local[:], 52731, 4)
    wrong += check("a port is listened on: %lld", flag(listen_error == mu.Net_Error.NONE), 1)
    taken, taken_error := mu.net_listen(local[:], 52731, 4)
    wrong += check("a port that is taken is ADDRESS_IN_USE: %lld", flag(taken_error == mu.Net_Error.ADDRESS_IN_USE), 1)
    wrong += check("the listener is closed: %lld", flag(mu.net_close(listener) == mu.Net_Error.NONE), 1)
    wrong += check("a socket that is not open is SOCKET_INVALID: %lld", flag(mu.net_close(taken) == mu.Net_Error.SOCKET_INVALID), 1)

    // The program's own File is not the library's, and neither is its CP_UTF8 or its clamp.
    mine := File { name_length = 3, is_open = true }
    theirs := mu.File { handle = 5 }
    wrong += check("the program's File has %lld", mine.name_length, 3)
    wrong += check("the library's File has %lld", theirs.handle, 5)
    wrong += check("the program's CP_UTF8 is %lld", CP_UTF8, 7)
    wrong += check("the program's clamp gives %lld", clamp(5), 1005)
    wrong += check("the library's clamp gives %lld", mu.clamp(50, 0, 10), 10)
    wrong += check("the library's min gives %lld", mu.min(3, 4), 3)
    return wrong
