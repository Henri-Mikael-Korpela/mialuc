/// Access to the filesystem of the system. It works on Windows only for now, and is as thin as the
/// system is: a `File` is the handle that the system gives, with no buffer of its own and no lock,
/// and every procedure takes the memory that it needs from its caller, so nothing here allocates,
/// and nothing is shared between threads unless the caller shares it. A buffer, a cursor or a lock
/// is built on top of a `File` by whoever needs one.
package mu

import foreign kernel32 "system:Kernel32.lib"

/// The procedures of `Kernel32.dll`, which the program imports by name and the loader of Windows
/// resolves when the program starts. A `HANDLE` is the size of an `Int`, a `DWORD` is a `U32`, a
/// `BOOL` is an `I32`, and `LPCWSTR` is the address of text in UTF-16.
foreign kernel32 #(default_calling_convention("system")) ->
    CloseHandle         :: proc(handle: Int) I32 -> ...
    FindClose           :: proc(handle: Int) I32 -> ...
    FindFirstFileW      :: proc(file_name: ^Byte, data: ^Byte) Int -> ...
    CreateFileW         :: proc(file_name: ^Byte, desired_access: U32, share_mode: U32, security_attributes: ^Byte, creation_disposition: U32, flags_and_attributes: U32, template_file: Int) Int -> ...
    GetLastError        :: proc() U32 -> ...
    GetStdHandle        :: proc(standard_handle: I32) Int -> ...
    MultiByteToWideChar :: proc(code_page: U32, flags: U32, text: C_Str, text_length: I32, wide_text: ^Byte, wide_length: I32) I32 -> ...
    ReadFile            :: proc(file: Int, buffer: ^Byte, bytes_to_read: I32, bytes_read: ^U32, overlapped: ^Byte) I32 -> ...
    WriteFile           :: proc(file: Int, buffer: ^Byte, bytes_to_write: I32, bytes_written: ^U32, overlapped: ^Byte) I32 -> ...

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
ERROR_INVALID_HANDLE : U32 : 6
ERROR_SHARING_VIOLATION : U32 : 32
ERROR_BROKEN_PIPE : U32 : 109
ERROR_INSUFFICIENT_BUFFER : U32 : 122

/// The standard input is asked for with this number, which is the `DWORD` -10, so `GetStdHandle` takes
/// it as an `I32`, which has the same bits.
STD_INPUT_HANDLE :: -10

/// The most bytes that one read or write asks the system for, which is a `DWORD` that is passed as an `I32`,
/// so it is below 2 GiB. A buffer that is larger is read or written in parts.
READ_BYTES_MAX :: 1073741824

/// What the system gives for a file that could not be opened. The handle is -1, which is not a
/// handle that a file has.
INVALID_HANDLE_VALUE :: -1

/// The most bytes that a path needs in the buffer that `file_open` is given, per byte of the path:
/// the UTF-16 of a path takes two bytes for each of its characters, at most one for each byte of
/// its UTF-8, and two more for the zero that ends it.
WIDE_PATH_BYTES_PER_BYTE :: 2

/// `WIN32_FIND_DATAW` is 592 bytes. What `find_entry_first` reads of it is the attributes, the 4
/// bytes at 0, and the size, the 4 bytes at 28 that are its upper half and the 4 at 32 that are its
/// lower half.
FIND_DATA_BYTES :: 592
FIND_DATA_ATTRIBUTES :: 0
FIND_DATA_SIZE_HIGH :: 28
FIND_DATA_SIZE_LOW :: 32

/// The most bytes that the path of `find_entry_first` has: the directory, a separator and the name.
/// A path that is longer is `File_Error.BUFFER_TOO_SMALL`.
FIND_PATH_BYTES_MAX :: 1024

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
    /// The file given is not an open one, or is one that was closed.
    FILE_INVALID
    /// There is nothing more to read: the file ended before the first byte of a line.
    END_OF_FILE
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
        case ERROR_INVALID_HANDLE ->
            return .FILE_INVALID
        case ERROR_SHARING_VIOLATION ->
            return .IN_USE
        case ERROR_INSUFFICIENT_BUFFER ->
            return .BUFFER_TOO_SMALL
        case ->
            return .OTHER

/// What `find_entry_first` says of an entry of a directory: a file, or a directory when it is one.
/// `size_bytes` is the size of a file, and is not meant for a directory.
Directory_Entry :: struct ->
    is_directory: Bool
    size_bytes: Int

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

/// The file that the program reads its input from, which is the console, or what the input was
/// redirected from: a file or a pipe. It is not opened by the program and is not closed by it. A
/// program that has no input gets a file that is not open, which `file_read` answers.
file_standard_input :: proc() File ->
    handle := GetStdHandle(STD_INPUT_HANDLE)
    if handle == 0 ->
        return File { handle = INVALID_HANDLE_VALUE }
    return File { handle = handle }

/// Reads from `file` into `buffer`, which the caller owns, as many bytes as the system gives at
/// once, which is at most as many as the buffer has, and gives how many with `File_Error.NONE`.
/// Reading goes on from where the last read of the file stopped, and the file has no buffer of its
/// own, so nothing is read past what was asked for. Zero bytes is the end of the file, or the end of
/// the input when the file is a pipe that was closed, or a console that was given the end of the
/// input, which is Ctrl+Z and Enter on Windows. A console gives a line at a time, so one read of it
/// may give fewer bytes than the buffer has even when more are coming, and the buffer is not
/// changed beyond the bytes that were given. A buffer with no bytes asks for none and gets none.
file_read :: proc(file: File, buffer: []Byte) (Int, File_Error) ->
    if file.handle == INVALID_HANDLE_VALUE ->
        return 0, .FILE_INVALID
    if length_of(buffer) == 0 ->
        return 0, .NONE

    bytes_to_read := length_of(buffer)
    if bytes_to_read > READ_BYTES_MAX ->
        bytes_to_read = READ_BYTES_MAX
    bytes_read : U32 = 0
    if ReadFile(file.handle, %buffer[0], int_to_i32(bytes_to_read), %bytes_read, nil) == 0 ->
        code := GetLastError()
        // The other end of a pipe that is closed is the end of the input, as it is for a file.
        if code == ERROR_BROKEN_PIPE ->
            return 0, .NONE
        return 0, file_error_from(code)
    return cast(Int)bytes_read, .NONE

/// Reads the whole file at `path` into `buffer`, from its start, and gives how many bytes it has
/// with `File_Error.NONE`, or the error and the bytes that were read before it. The file is opened,
/// read until the end or until the buffer is full, and closed, so what is left of a file that is
/// larger than the buffer is not read. `wide_path` is the memory that `file_open` needs for the path.
file_read_all :: proc(path: Str, wide_path: []Byte, buffer: []Byte) (Int, File_Error) ->
    file, open_error := file_open(path, .READ, wide_path)
    if open_error not= .NONE ->
        return 0, open_error
    defer file_close(file)

    total := 0
    for total < length_of(buffer) ->
        count, error := file_read(file, buffer[total:])
        if error not= .NONE ->
            return total, error
        if count == 0 ->
            break
        total += count
    return total, .NONE

/// Writes `buffer`, which the caller owns, to `file`, as many bytes as the system takes at once,
/// which is at most as many as the buffer has, and gives how many with `File_Error.NONE`. Writing
/// goes on from where the last write of the file stopped, or at the end of a file that is opened
/// to append, and the file has no buffer of its own, so what the system took is written. A buffer
/// with no bytes writes none.
file_write :: proc(file: File, buffer: []Byte) (Int, File_Error) ->
    if file.handle == INVALID_HANDLE_VALUE ->
        return 0, .FILE_INVALID
    if length_of(buffer) == 0 ->
        return 0, .NONE

    bytes_to_write := length_of(buffer)
    if bytes_to_write > READ_BYTES_MAX ->
        bytes_to_write = READ_BYTES_MAX
    bytes_written : U32 = 0
    if WriteFile(file.handle, %buffer[0], int_to_i32(bytes_to_write), %bytes_written, nil) == 0 ->
        return 0, file_error_from(GetLastError())
    return cast(Int)bytes_written, .NONE

/// Writes the one byte `value` to `file` like `file_write`, and gives `File_Error.NONE` when the
/// system took it. A byte that the system took none of is `File_Error.OTHER`.
file_write_byte :: proc(file: File, value: Byte) File_Error ->
    bytes := [1]Byte { value }
    count, error := file_write(file, bytes[:])
    if error == .NONE and count == 0 ->
        return .OTHER
    return error

/// Writes the whole of `buffer` to the file at `path`, which is made when it is not there and
/// emptied when it is, and gives how many bytes were written with `File_Error.NONE`, or the error
/// and the bytes that were written before it. The file is opened, written until nothing is left, and
/// closed. `wide_path` is the memory that `file_open` needs for the path.
file_write_all :: proc(path: Str, wide_path: []Byte, buffer: []Byte) (Int, File_Error) ->
    file, open_error := file_open(path, .WRITE, wide_path)
    if open_error not= .NONE ->
        return 0, open_error
    defer file_close(file)

    total := 0
    for total < length_of(buffer) ->
        count, error := file_write(file, buffer[total:])
        if error not= .NONE ->
            return total, error
        if count == 0 ->
            break
        total += count
    return total, .NONE

/// Reads a line from `file` into `buffer`, which the caller owns, and gives how many bytes the line
/// has, without its end, with `File_Error.NONE`. The end of a line is a line feed, and a carriage
/// return before it, as on Windows, is not part of the line. A line that is empty has no bytes. A line
/// that does not fit in the buffer is cut where the buffer ends, and the rest of it is the start of
/// the next line. When the file ends before the first byte of a line the answer is
/// `File_Error.END_OF_FILE`, and a last line that has no line feed is a line. The file has no buffer
/// of its own, so the line is read one byte at a time, which takes no byte of the next line, and a
/// console or a pipe gives each line as it comes.
file_read_line :: proc(file: File, buffer: []Byte) (Int, File_Error) ->
    length := 0
    for length < length_of(buffer) ->
        count, error := file_read(file, buffer[length:][:1])
        if error not= .NONE ->
            return length, error
        if count == 0 ->
            if length == 0 ->
                return 0, .END_OF_FILE
            break
        if buffer[length] == '\n' ->
            break
        length += 1

    if length > 0 ->
        if buffer[length - 1] == '\r' ->
            length -= 1
    return length, .NONE

/// The number of bytes that the UTF-8 of a character takes.
utf8_length :: proc(character: Rune) Int ->
    if character >= '𐀀' ->
        return 4
    if character >= 'ࠀ' ->
        return 3
    if character >= '' ->
        return 2
    return 1

/// Puts the UTF-8 of `text` in `buffer` from `at`, and gives where the next byte goes, or -1 when
/// it does not fit. MISSING: reading the bytes of a `Str`, which would make this a copy.
put_utf8 :: proc(buffer: []Byte, at: Int, text: Str) Int ->
    position := at
    for character in text ->
        encoded := cast([4]Byte)character
        count := utf8_length(character)
        if position + count > length_of(buffer) ->
            return -1
        for index in 0..<count ->
            buffer[position] = encoded[index]
            position += 1
    return position

/// Whether `text` has a character that stands for others in the name of a file, `*` or `?`.
has_wildcard :: proc(text: Str) Bool ->
    for character in text ->
        if character == '*' or character == '?' ->
            return true
    return false

/// Reads 4 bytes of `data` at `at`, the lowest first, as a number.
find_data_u32 :: proc(data: []Byte, at: Int) Int ->
    total := 0
    for index in 0..<4 ->
        total = total * 256 + cast(Int)data[at + 3 - index]
    return total

/// Finds the entry named `target_entry` in the directory at `directory_path`, both in UTF-8, and
/// gives what it is with `File_Error.NONE`, or an entry of nothing with the error:
/// `File_Error.NOT_FOUND` when the directory has no such entry. It is the name of one entry, in
/// exactly the spelling that it has but for the case of its letters, and it is not a pattern: a
/// `*` or a `?` in either text is `File_Error.PATH_INVALID`. An empty directory is the current
/// one. The path is made on the stack, so the two texts and the separator that goes between them
/// have at most `FIND_PATH_BYTES_MAX` bytes, or the answer is `File_Error.BUFFER_TOO_SMALL`.
find_entry_first :: proc(directory_path: Str, target_entry: Str) (Directory_Entry, File_Error) ->
    nothing := Directory_Entry { is_directory = false, size_bytes = 0 }
    if has_wildcard(directory_path) or has_wildcard(target_entry) ->
        return nothing, .PATH_INVALID

    // The last byte is left as the zero that ends the path.
    path := [FIND_PATH_BYTES_MAX]Byte { .. = 0 }
    limit := path[0:FIND_PATH_BYTES_MAX - 1]
    at := put_utf8(limit, 0, directory_path)
    if at > 0 ->
        at = put_utf8(limit, at, "\\")
    if at >= 0 ->
        at = put_utf8(limit, at, target_entry)
    if at < 0 ->
        return nothing, .BUFFER_TOO_SMALL
    text, is_text := utf8_decode(path[0:at])

    wide_path := [FIND_PATH_BYTES_MAX * WIDE_PATH_BYTES_PER_BYTE]Byte { .. = 0 }
    characters := int_to_i32(length_of(wide_path) / WIDE_PATH_BYTES_PER_BYTE)
    if MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str_to_c_str(text), -1, %wide_path[0], characters) == 0 ->
        return nothing, .PATH_INVALID

    data := [FIND_DATA_BYTES]Byte { .. = 0 }
    handle := FindFirstFileW(%wide_path[0], %data[0])
    if handle == INVALID_HANDLE_VALUE ->
        return nothing, file_error_from(GetLastError())
    FindClose(handle)

    // The attribute of a directory is the bit 16 of the attributes, which is the lowest of what is
    // left when the byte is divided by 16, and there is no operator for a bit yet.
    sixteens := find_data_u32(data[:], FIND_DATA_ATTRIBUTES) / 16
    is_directory := sixteens - sixteens / 2 * 2 == 1
    size_bytes := find_data_u32(data[:], FIND_DATA_SIZE_HIGH) * 4294967296 + find_data_u32(data[:], FIND_DATA_SIZE_LOW)
    return Directory_Entry { is_directory = is_directory, size_bytes = size_bytes }, .NONE
