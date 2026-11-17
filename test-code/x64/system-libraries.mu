// `import foreign kernel32 "system:Kernel32.lib"` gives a name to a library of the system, and a
// `foreign kernel32 ->` block declares procedures that the program imports from it by name, as it
// does from the C library. Here `CreateFileW` opens the device `NUL`, which every Windows has, and a
// file that does not exist, and `CloseHandle` closes the first. A `HANDLE` is the size of an `Int`,
// a `DWORD` is a `U32` and a `BOOL` is an `I32`. A library is not linked into the program: the file only
// names the DLL, `Kernel32.dll` and `User32.dll`, and the loader of Windows resolves the procedures in it
// when the program starts. The exit code of the program is the number of
// wrong results, 0.

package system_libraries

import foreign libc
import foreign kernel32 "system:Kernel32.lib"
import foreign user32 "system:User32.lib"

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

foreign kernel32 #(default_calling_convention("system")) ->
    CloseHandle :: proc(handle: Int) I32 -> ...
    CreateFileW :: proc(file_name: ^Byte, desired_access: U32, share_mode: U32, security: ^Byte, disposition: U32, attributes: U32, template: Int) Int -> ...
    GetCurrentProcessId :: proc() U32 -> ...
    MultiByteToWideChar :: proc(code_page: U32, flags: U32, text: C_Str, text_length: I32, wide: ^Byte, wide_length: I32) I32 -> ...

foreign user32 #(default_calling_convention("system")) ->
    IsWindow :: proc(window: Int) I32 -> ...

GENERIC_READ : U32 : 0x80000000
FILE_SHARE_READ : U32 : 1
FILE_SHARE_WRITE : U32 : 2
OPEN_EXISTING : U32 : 3
CP_UTF8 : U32 : 65001
INVALID_HANDLE_VALUE :: -1

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

/// Opens the file for reading with `CreateFileW`, which takes the path as UTF-16, and gives the
/// handle, or INVALID_HANDLE_VALUE. The caller owns the buffer that the UTF-16 is made in.
open_file :: proc(path: Str, wide: ^Byte, wide_length: I32) Int ->
    written := MultiByteToWideChar(CP_UTF8, 0, str_to_c_str(path), -1, wide, wide_length)
    if written == 0 ->
        return INVALID_HANDLE_VALUE
    return CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ + FILE_SHARE_WRITE, nil, OPEN_EXISTING, 0, 0)

main :: proc() Int ->
    wrong := 0
    wide := [64]Byte { .. = 0 }

    wrong += check("the process has an id: %lld", flag(GetCurrentProcessId() > 0), 1)

    written := MultiByteToWideChar(CP_UTF8, 0, str_to_c_str("NUL"), -1, %wide[0], 32)
    wrong += check("NUL is %lld wide characters with its end", cast(Int)written, 4)

    handle := open_file("NUL", %wide[0], 32)
    wrong += check("NUL is opened: %lld", flag(handle not= INVALID_HANDLE_VALUE), 1)
    wrong += check("NUL is closed: %lld", flag(CloseHandle(handle) not= 0), 1)

    missing := open_file("this-file-does-not-exist.txt", %wide[0], 32)
    wrong += check("a missing file is not opened: %lld", flag(missing == INVALID_HANDLE_VALUE), 1)

    // A second library: `user32` is not linked into the program, which only names `User32.dll`, and
    // the loader finds `IsWindow` in it when the program starts. No window has the handle 0.
    wrong += check("IsWindow of 0 is %lld", cast(Int)IsWindow(0), 0)

    return wrong
