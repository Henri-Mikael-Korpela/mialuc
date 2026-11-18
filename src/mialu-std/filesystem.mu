/// Access to the filesystem of the system. It works on Windows only for now, and is as thin as the
/// system is: a `File` is the handle that the system gives, with no buffer of its own and no lock,
/// and every procedure takes the memory that it needs from its caller, so nothing here allocates,
/// and nothing is shared between threads unless the caller shares it. A buffer, a cursor or a lock
/// is built on top of a `File` by whoever needs one.
package mialu

import foreign kernel32 "system:Kernel32.lib"

/// The procedures of `Kernel32.dll`, which the program imports by name and the loader of Windows
/// resolves when the program starts. A `HANDLE` is the size of an `Int`, a `DWORD` is a `U32`, a
/// `BOOL` is an `I32`, and `LPCWSTR` is the address of text in UTF-16.
foreign kernel32 #(default_calling_convention("system")) ->
    CloseHandle :: proc(handle: Int) I32 -> ...
    CreateFileW :: proc(file_name: ^Byte, desired_access: U32, share_mode: U32, security_attributes: ^Byte, creation_disposition: U32, flags_and_attributes: U32, template_file: Int) Int -> ...
    GetLastError :: proc() U32 -> ...
    MultiByteToWideChar :: proc(code_page: U32, flags: U32, text: C_Str, text_length: I32, wide_text: ^Byte, wide_length: I32) I32 -> ...

CP_UTF8 : U32 : 65001
MB_ERR_INVALID_CHARS : U32 : 8

GENERIC_READ : U32 : 0x80000000
GENERIC_WRITE : U32 : 0x40000000
FILE_APPEND_DATA : U32 : 4
FILE_SHARE_READ : U32 : 1
FILE_SHARE_WRITE : U32 : 2
CREATE_ALWAYS : U32 : 2
OPEN_EXISTING : U32 : 3
OPEN_ALWAYS : U32 : 4
FILE_ATTRIBUTE_NORMAL : U32 : 0x80

ERROR_FILE_NOT_FOUND : U32 : 2
ERROR_PATH_NOT_FOUND : U32 : 3
ERROR_ACCESS_DENIED : U32 : 5
ERROR_SHARING_VIOLATION : U32 : 32
ERROR_INSUFFICIENT_BUFFER : U32 : 122

/// What the system gives for a file that could not be opened. The handle is -1, which is not a
/// handle that a file has.
INVALID_HANDLE_VALUE :: -1

/// The most bytes that a path needs in the buffer that `file_open` is given, per byte of the path:
/// the UTF-16 of a path takes two bytes for each of its characters, at most one for each byte of
/// its UTF-8, and two more for the zero that ends it.
WIDE_PATH_BYTES_PER_BYTE :: 2

/// An open file: the handle of the system. A file that is not open has the handle -1. It is
/// copied by copying the handle, so a copy is the same file, and the file is closed once.
File :: struct ->
    handle: Int

/// How a file is opened. The others, such as reading and writing at once, come when the procedures
/// that need them do.
File_Open_Mode :: enum Int ->
    /// An existing file is read. Others may read it and write it meanwhile.
    READ
    /// A file is written from its start: it is made when it does not exist, and emptied when it does.
    /// Others may read it meanwhile.
    WRITE
    /// A file is written at its end: it is made when it does not exist. Others may read it
    /// meanwhile.
    APPEND

/// Why a procedure of the filesystem did not do what it was asked. NONE is that it did.
File_Error :: enum Int ->
    NONE
    /// The file, or a directory on the way to it, is not there.
    NOT_FOUND
    /// The system does not let the program do it.
    ACCESS_DENIED
    /// Someone else has the file open in a way that does not go with this.
    IN_USE
    /// The path is not valid UTF-8, or has a character that is not allowed.
    PATH_INVALID
    /// The buffer for the path was too small. See `WIDE_PATH_BYTES_PER_BYTE`.
    BUFFER_TOO_SMALL
    /// The file given is not an open one.
    FILE_INVALID
    /// Anything else that the system says.
    OTHER

/// Gives the error that the code of the system, which `GetLastError` gives, is.
file_error_from :: proc(code: U32) File_Error ->
    match code ->
        case ERROR_FILE_NOT_FOUND ->
        case ERROR_PATH_NOT_FOUND ->
            return .NOT_FOUND
        case ERROR_ACCESS_DENIED ->
            return .ACCESS_DENIED
        case ERROR_SHARING_VIOLATION ->
            return .IN_USE
        case ERROR_INSUFFICIENT_BUFFER ->
            return .BUFFER_TOO_SMALL
        case ->
            return .OTHER

/// What a program may do with a file that is opened in the mode: read it, write it, or add to it.
file_mode_access :: proc(mode: File_Open_Mode) U32 ->
    match mode ->
        case .READ ->
            return GENERIC_READ
        case .WRITE ->
            return GENERIC_WRITE
        case ->
            return FILE_APPEND_DATA

/// Whether a file that is opened in the mode is made when it is not there, and emptied when it is.
file_mode_disposition :: proc(mode: File_Open_Mode) U32 ->
    match mode ->
        case .READ ->
            return OPEN_EXISTING
        case .WRITE ->
            return CREATE_ALWAYS
        case ->
            return OPEN_ALWAYS

/// Opens the file at `path`, a path in UTF-8, in `mode`, and gives it, with `File_Error.NONE`, or
/// a file that is not open with the error. The system takes a path in UTF-16, which is made in
/// `wide_path`, memory that the caller owns and that is not needed after the call, so it can be
/// used again, or be on the stack: it takes `WIDE_PATH_BYTES_PER_BYTE` bytes for each byte of the
/// path, and one more pair for the zero that ends it. The file is closed with `file_close`.
file_open :: proc(path: Str, mode: File_Open_Mode, wide_path: []Byte) (File, File_Error) ->
    closed := File { handle = INVALID_HANDLE_VALUE }
    if length_of(wide_path) < WIDE_PATH_BYTES_PER_BYTE ->
        return closed, .BUFFER_TOO_SMALL

    characters := int_to_i32(length_of(wide_path) / WIDE_PATH_BYTES_PER_BYTE)
    written := MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str_to_c_str(path), -1, %wide_path[0], characters)
    if written == 0 ->
        error := file_error_from(GetLastError())
        if error == .OTHER ->
            error = .PATH_INVALID
        return closed, error

    handle := CreateFileW(%wide_path[0], file_mode_access(mode), FILE_SHARE_READ, nil, file_mode_disposition(mode), FILE_ATTRIBUTE_NORMAL, 0)
    if handle == INVALID_HANDLE_VALUE ->
        return closed, file_error_from(GetLastError())
    return File { handle = handle }, .NONE

/// Closes `file`, which gives its handle back to the system. A file is closed once, and the file
/// that was given is not usable after: `File_Error.FILE_INVALID` is the answer for a file that is
/// not open, such as one that `file_open` could not open.
file_close :: proc(file: File) File_Error ->
    if file.handle == INVALID_HANDLE_VALUE ->
        return .FILE_INVALID
    if CloseHandle(file.handle) == 0 ->
        return file_error_from(GetLastError())
    return .NONE
