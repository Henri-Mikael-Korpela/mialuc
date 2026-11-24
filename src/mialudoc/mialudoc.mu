/// mialudoc: a small HTTP server written in Mialu, which shows the documentation of the project to
/// a browser on the same machine, and serves the playground, which compiles Mialu in the browser.
///
///     mialudoc [port [directory [address]]]
///
/// The port is 8080, the directory is `.`, the root of the project, and the address is 127.0.0.1, when
/// they are left out. An address such as 0.0.0.0 makes the server reachable from other computers, which
/// a container needs. The server answers `GET` and `HEAD` requests, and `POST` for `/compile`:
///
///     /                       an index page with the logo of Mialu, and a list of the documents
///     /<name>                 the document `docs/<name>.md`, which is written in Markdown and is shown as HTML
///     /logo.png               the logo, `media/mialu-logo.png`
///     /tools/playground       the playground, `tests/wasm/playground.html`
///     /tools/internals        the page of the internals of the compiler, `tests/wasm/internals.html`
///     /api/internals          the tokens, syntax tree nodes, reserved words and operators of the compiler, as JSON
///     /compile                the source of a program in the body of a POST, built by the compiler, as JSON
///     /test-code/wasm/<file>  the examples of the playground
///     /tests/wasm/<file>      the files of `tests/wasm`
///
/// A name is made of letters, digits, `-` and `_`, and a file name has dots too but does not start with
/// one, so what is served is what the directories above hold and nothing that a path with `..` or a
/// slash could name. The connection is closed after each answer, and a request is answered before the
/// next one is read.
///
/// The memory of the answers is made once, with `make`, and an answer takes its buffers from it as
/// an arena of `mu:allocator`, which is reset after each answer, so the buffers are not on the stack
/// and nothing of one answer is left for the next. A module of the playground can be larger than the
/// arena, so its memory is made for its size, and given back after it is sent.
///
/// It is built by CMake with the target `mialudoc`, which runs `mialuc build --target x64-win32`,
/// and it works on Windows only, as the standard library does for now. The Dockerfile of the project
/// runs it under Wine, so it is the server of the playground on the other systems too.
///
/// The Markdown that it understands is what the documents of the project use: headings, paragraphs,
/// lists that are nested, tables, block quotes, fenced code, and `code`, **bold**, *emphasis* and
/// [links](#in-this-page) in a line. A link to `name.md` is a link to `/name`. A line of HTML in
/// a document is shown as text.
///
/// MISSING FEATURES. mialudoc is written to find what Mialu is missing for a program of this kind, and
/// where it had to work around one, a comment that starts with `MISSING:` says what the language
/// or its library does not have. They are all collected here as well, so that they can be added and
/// the workarounds removed:
///
///   - MISSING: a standard library of processes, so `CreateProcessA` and the procedures with it are declared
///     here, to run the compiler, with the structures that they take as bytes: `STARTUPINFOA`, which has
///     the handles at the byte 80, 88 and 96, `PROCESS_INFORMATION` and `SECURITY_ATTRIBUTES`.
///   - MISSING: the program and its package knowing where they are, so the compiler is found at
///     `<directory>/mialuc.exe` or `<directory>/build/mialuc.exe`, with the directory that is given, and
///     not next to the program.
///   - MISSING: a standard library of sockets, `mu:network` or the like. The procedures of Winsock are
///     declared here, and the structures that they take, `WSADATA` and `sockaddr_in`, are arrays of
///     bytes that are filled by hand, as there is no way to say the size and the layout of a struct
///     that is not made of words, or to take the size of one.
///   - MISSING: the files of a directory in `mu:filesystem`, so `FindFirstFileA` and the procedures
///     with it are declared here, and the name of a file is read from the bytes of `WIN32_FIND_DATAA`.
///   - MISSING: the size of a file in `mu:filesystem`, so `GetFileSizeEx` is declared here.
///   - MISSING: a way to see the bytes of a `Str`, see "Converting a string to bytes" in the
///     specification. Text is put in a buffer one character at a time with `put_text` and `emit_text`,
///     which make the UTF-8 of each character with a cast of the `Rune`, and a path is made of bytes
///     that are turned into a `Str` with `utf8_decode`.
///   - MISSING: joining strings when the program runs. `++` is for constants only, so a response is
///     built with the `put_` and `emit_` procedures.
///   - MISSING: the text of an integer when the program runs. `int_to_str` is for constants only, so
///     `put_int` makes the digits.
///   - MISSING: the remainder of a division. There is no operator for it, because `%` is the address of
///     a variable, so `a - a / b * b` is written where C writes `a % b`.
///   - MISSING: arguments of `main`, so the command line is read with `mu:process` and split by hand,
///     and the quotes that it may have are not looked at.
///   - MISSING: a `Rune` as a `Byte`. A rune literal is compared with a `Byte` with `==`, but it has no
///     order with one, and it is not an argument for a parameter of a `Byte`, so the numbers of the
///     characters are written, such as 48 for `'0'`, or the compared byte is made an `Int`.
///   - MISSING: the sizes of the integers in memory. An `I32` takes 8 bytes in a struct or an array,
///     so a pointer to one is not a pointer to an `int`. The `int` that `setsockopt` takes is a
///     `DWORD` in an array of 4 bytes, which is made by hand.
///   - MISSING: unsigned and small integers, `U16` and the byte order of the network, so the port
///     is split in two bytes by hand.
///   - MISSING: threads and a way to wait on many sockets, so the server answers one connection at
///     a time. A client that is slow is cut off by a timeout of the socket.
///   - MISSING: a log with the values of different types in a line. `printf` of the C library is
///     declared once with one number, and a line is printed in parts.
///   - MISSING: a `return` with no value, to leave a procedure that has no result early. `render_line`
///     and `emit_document_cards` have an `Int` result that is always 0, only to have one.
///   - MISSING: an array in a struct, so the levels of the lists of `Render` are digits of an `Int`.
///   - MISSING: a check that a procedure with a result returns on every path. `mialuc` builds one that
///     ends without a `return`, and it then runs on into whatever follows, which stopped this server
///     once, so it is a gap of the compiler and not a feature to add.
///   - MISSING: parentheses to group an expression, such as `a and (b or c)`.
///   - MISSING: a string constant of many lines, so the style sheet of the pages is a call of
///     `emit_text` for each of its lines.
package mialudoc

import "mu:allocator"
import "mu:filesystem"
import "mu:process"
import foreign libc
import foreign kernel32 "system:Kernel32.lib"
import foreign ws2_32 "system:Ws2_32.lib"

foreign libc ->
    fflush :: proc(stream: ^Byte) I32 -> ...
    printf :: proc(format: C_Str, number: Int) I32 -> ...
    puts   :: proc(content: C_Str) I32 -> ...

foreign kernel32 #(default_calling_convention("system")) ->
    // MISSING: `mu:filesystem` has no `file_size` and no way to list a directory, see above.
    FindClose      :: proc(handle: Int) I32 -> ...
    FindFirstFileA :: proc(pattern: C_Str, data: ^Byte) Int -> ...
    FindNextFileA  :: proc(handle: Int, data: ^Byte) I32 -> ...
    GetFileSizeEx  :: proc(file: Int, size: ^I64) I32 -> ...

/// Winsock, `Ws2_32.dll`. A `SOCKET` is the size of an `Int`, and an `int` is an `I32`. The addresses
/// and the buffers are `^Byte`, as the structures are bytes, see the MISSING notes above.
foreign ws2_32 #(default_calling_convention("system")) ->
    WSAStartup  :: proc(version: U32, data: ^Byte) I32 -> ...
    socket      :: proc(family: I32, kind: I32, protocol: I32) Int -> ...
    setsockopt  :: proc(handle: Int, level: I32, name: I32, value: ^Byte, length: I32) I32 -> ...
    bind        :: proc(handle: Int, address: ^Byte, length: I32) I32 -> ...
    listen      :: proc(handle: Int, backlog: I32) I32 -> ...
    accept      :: proc(handle: Int, address: ^Byte, length: ^Byte) Int -> ...
    recv        :: proc(handle: Int, buffer: ^Byte, length: I32, flags: I32) I32 -> ...
    send        :: proc(handle: Int, buffer: ^Byte, length: I32, flags: I32) I32 -> ...
    shutdown    :: proc(handle: Int, how: I32) I32 -> ...
    closesocket :: proc(handle: Int) I32 -> ...

DEFAULT_PORT :: 8080
DEFAULT_DIRECTORY :: "."

/// The version of Winsock that is asked for, 2.2, as the `WORD` that `MAKEWORD(2, 2)` makes.
WINSOCK_VERSION : U32 : 0x0202

/// `WSADATA` is 408 bytes on x64. 512 is enough, and the system writes it.
WSADATA_BYTES :: 512

AF_INET : I32 : 2
SOCK_STREAM : I32 : 1
IPPROTO_TCP : I32 : 6
SOL_SOCKET : I32 : 0xFFFF
SO_RCVTIMEO : I32 : 0x1006
SD_SEND : I32 : 1
INVALID_SOCKET :: -1
INVALID_HANDLE :: -1

/// `WIN32_FIND_DATAA` is 320 bytes, and the name of the file is a string of at most 260 bytes, a
/// zero byte included, that starts at the byte 44.
FIND_DATA_BYTES :: 320
FIND_DATA_NAME :: 44
FIND_NAME_BYTES :: 260

/// How long the server waits for a client to send its request, in milliseconds.
RECEIVE_TIMEOUT_MILLISECONDS :: 5000

/// The most connections that wait to be accepted.
BACKLOG : I32 : 16

REQUEST_BYTES :: 8192
HEAD_BYTES :: 1024
BODY_BYTES :: 256
OUTPUT_BYTES :: 16384
CHUNK_BYTES :: 16384
LINE_BYTES :: 16384
PATH_BYTES :: 1024
DIRECTORY_BYTES :: 256
TITLE_BYTES :: 256

/// The memory of the answer to one request, which the buffers of the answer are taken from, and
/// which is given back all at once when the answer is sent. An answer takes at most a few of the
/// buffers above at once, as a procedure gives back what it took when it returns.
REQUEST_ARENA_BYTES :: 1048576
LIST_LEVELS :: 6

STATUS_OK :: 200
STATUS_BAD_REQUEST :: 400
STATUS_NOT_FOUND :: 404
STATUS_METHOD_NOT_ALLOWED :: 405
STATUS_PAYLOAD_TOO_LARGE :: 413
STATUS_INTERNAL_ERROR :: 500

/// The numbers of the characters that the Markdown looks for. See MISSING: a `Rune` as a `Byte`.
BACKTICK :: 96
STAR :: 42
LEFT_BRACKET :: 91
RIGHT_BRACKET :: 93
LEFT_PARENTHESIS :: 40
RIGHT_PARENTHESIS :: 41
SPACE :: 32
HASH :: 35
DASH :: 45
DOT :: 46
QUESTION :: 63
CARRIAGE_RETURN :: 13

// ---------------------------------------------------------------------------------------------
// Bytes and text. The head of an answer is put in a buffer, and the pages go out through an Output.
// ---------------------------------------------------------------------------------------------

/// Puts one byte in `buffer` at `at`, and gives where the next one goes. A byte that does not fit
/// is dropped, and the position does not move, as an index outside of a buffer would stop the program.
put_byte :: proc(buffer: []Byte, at: Int, value: Byte) Int ->
    if at >= length_of(buffer) ->
        return at
    buffer[at] = value
    return at + 1

/// Puts the UTF-8 of `text` in `buffer` at `at`, and gives where the next byte goes.
/// MISSING: reading the bytes of a `Str`, which would make this a copy.
put_text :: proc(buffer: []Byte, at: Int, text: Str) Int ->
    position := at
    for character in text ->
        encoded := cast([4]Byte)character
        for index in 0..<utf8_length(character) ->
            position = put_byte(buffer, position, encoded[index])
    return position

/// The number of bytes that the UTF-8 of a character takes.
utf8_length :: proc(character: Rune) Int ->
    if character >= '\U00010000' ->
        return 4
    if character >= 'ࠀ' ->
        return 3
    if character >= '\u0080' ->
        return 2
    return 1

/// Puts the bytes of `bytes` in `buffer` at `at`, and gives where the next byte goes.
put_bytes :: proc(buffer: []Byte, at: Int, bytes: []Byte) Int ->
    position := at
    for value in bytes ->
        position = put_byte(buffer, position, value)
    return position

/// Puts the decimal digits of a number that is not negative in `buffer` at `at`, and gives where
/// the next byte goes. MISSING: `int_to_str` of a value that is known when the program runs.
put_int :: proc(buffer: []Byte, at: Int, number: Int) Int ->
    digits := [20]Byte { .. = 0 }
    count := 0
    rest := number
    for ->
        // MISSING: the remainder operator.
        quotient := rest / 10
        digits[count] = int_to_byte(rest - quotient * 10 + 48)
        count += 1
        rest = quotient
        if rest == 0 ->
            break
    position := at
    for index in 0..<count ->
        position = put_byte(buffer, position, digits[count - 1 - index])
    return position

/// Whether the bytes are the characters of `text`, which are all of the ASCII range here.
bytes_are :: proc(bytes: []Byte, text: Str) Bool ->
    if length_of(bytes) not= length_of(text) ->
        return false
    index := 0
    for character in text ->
        if cast(Int)bytes[index] not= cast(Int)character ->
            return false
        index += 1
    return true

/// Whether `bytes` start with the characters of `text`, which are all of the ASCII range here.
bytes_start_with :: proc(bytes: []Byte, text: Str) Bool ->
    if length_of(bytes) < length_of(text) ->
        return false
    return bytes_are(bytes[0:length_of(text)], text)

/// Whether `bytes` end with the characters of `text`, which are all of the ASCII range here.
bytes_end_with :: proc(bytes: []Byte, text: Str) Bool ->
    if length_of(bytes) < length_of(text) ->
        return false
    return bytes_are(bytes[length_of(bytes) - length_of(text):], text)

/// Whether the byte at `index` is `value`, which is false when there is no byte there. The index is
/// checked first, and the byte is only read when it is there, as `and` does not evaluate its right
/// operand otherwise.
byte_is :: proc(bytes: []Byte, index: Int, value: Int) Bool ->
    return index >= 0 and index < length_of(bytes) and cast(Int)bytes[index] == value

/// Whether the byte at `index` is a digit, which is false when there is no byte there.
is_digit_at :: proc(bytes: []Byte, index: Int) Bool ->
    return index >= 0 and index < length_of(bytes) and bytes[index] >= 48 and bytes[index] <= 57

/// Where the first byte that is `target` is, from `from` on, or -1.
find_byte :: proc(bytes: []Byte, from: Int, target: Int) Int ->
    for index in from..<length_of(bytes) ->
        if cast(Int)bytes[index] == target ->
            return index
    return -1

/// Where the first two bytes in a row that are `target` are, from `from` on, or -1.
find_pair :: proc(bytes: []Byte, from: Int, target: Int) Int ->
    for index in from..<length_of(bytes) - 1 ->
        if cast(Int)bytes[index] == target and cast(Int)bytes[index + 1] == target ->
            return index
    return -1

/// Where the bytes start after the spaces at their start.
trim_start :: proc(bytes: []Byte) Int ->
    index := 0
    for byte_is(bytes, index, SPACE) ->
        index += 1
    return index

/// Where the bytes end before the spaces at their end.
trim_end :: proc(bytes: []Byte) Int ->
    end := length_of(bytes)
    for byte_is(bytes, end - 1, SPACE) ->
        end -= 1
    // Bytes that are all spaces end where they start, so a range from one to the other is empty.
    start := trim_start(bytes)
    if end < start ->
        end = start
    return end

/// The bytes without the spaces at their start and at their end.
trim :: proc(bytes: []Byte) []Byte ->
    return bytes[trim_start(bytes):trim_end(bytes)]

/// Whether a byte can be in the name of a document: a letter, a digit, `-` or `_`.
is_name_byte :: proc(value: Byte) Bool ->
    if value >= 48 and value <= 57 ->
        return true
    if value >= 65 and value <= 90 ->
        return true
    if value >= 97 and value <= 122 ->
        return true
    return value == '-' or value == '_'

is_name :: proc(bytes: []Byte) Bool ->
    if length_of(bytes) == 0 ->
        return false
    for value in bytes ->
        if not is_name_byte(value) ->
            return false
    return true

/// Whether a byte is a punctuation mark of the ASCII range, which a backslash can make literal.
is_punctuation :: proc(value: Byte) Bool ->
    if value >= 33 and value <= 47 ->
        return true
    if value >= 58 and value <= 64 ->
        return true
    if value >= 91 and value <= 96 ->
        return true
    return value >= 123 and value <= 126

// ---------------------------------------------------------------------------------------------
// Sockets
// ---------------------------------------------------------------------------------------------

/// Sends all of `bytes` to the client, and says whether the system took them all.
send_all :: proc(client: Int, bytes: []Byte) Bool ->
    sent := 0
    for sent < length_of(bytes) ->
        count := cast(Int)send(client, %bytes[sent], int_to_i32(length_of(bytes) - sent), 0)
        if count <= 0 ->
            return false
        sent += count
    return true

/// Makes the socket that listens on `port` of the address `host`, which is its 4 bytes, and gives it, or
/// INVALID_SOCKET. The server is for the machine that it runs on, so it listens on 127.0.0.1 unless
/// it is told to be reachable from elsewhere, as it is in a container.
listen_on :: proc(port: Int, host: []Byte) Int ->
    data := [WSADATA_BYTES]Byte { .. = 0 }
    if WSAStartup(WINSOCK_VERSION, %data[0]) not= 0 ->
        puts("mialudoc: Winsock did not start")
        return INVALID_SOCKET

    listener := socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
    if listener == INVALID_SOCKET ->
        puts("mialudoc: no socket")
        return INVALID_SOCKET

    // A `sockaddr_in`: the family in 2 bytes, the port in 2 bytes with the high one first, the
    // address in 4 bytes, and 8 bytes that are not used. MISSING: a struct of this layout and `htons`.
    address := [16]Byte { .. = 0 }
    address[0] = int_to_byte(cast(Int)AF_INET)
    address[2] = int_to_byte(port / 256)
    address[3] = int_to_byte(port - port / 256 * 256)
    for index in 0..<4 ->
        address[4 + index] = host[index]
    if bind(listener, %address[0], 16) not= 0 ->
        puts("mialudoc: the port is not free")
        closesocket(listener)
        return INVALID_SOCKET
    if listen(listener, BACKLOG) not= 0 ->
        puts("mialudoc: it does not listen")
        closesocket(listener)
        return INVALID_SOCKET
    return listener

/// Reads the request of a client into `buffer` until its head, which ends with an empty line, is
/// there, or the buffer is full, or the client is gone, and gives how many bytes it read.
receive_head :: proc(client: Int, buffer: []Byte) Int ->
    total := 0
    for total < length_of(buffer) ->
        count := cast(Int)recv(client, %buffer[total], int_to_i32(length_of(buffer) - total), 0)
        if count <= 0 ->
            break
        total += count
        if find_head_end(buffer[0:total]) >= 0 ->
            break
    return total

/// Where the body of a request starts, which is after the empty line that ends its head, `CR LF CR LF`,
/// or -1 when the bytes have no such line.
find_head_end :: proc(bytes: []Byte) Int ->
    for index in 3..<length_of(bytes) ->
        if bytes[index] == '\n' and bytes[index - 1] == '\r' and bytes[index - 2] == '\n' and bytes[index - 3] == '\r' ->
            return index + 1
    return -1

// ---------------------------------------------------------------------------------------------
// Output: the bytes of a page are collected in a buffer that is sent when it is full
// ---------------------------------------------------------------------------------------------

/// Where the bytes of a page go. `is_open` is false after the client did not take some, and what is
/// put after that is dropped.
Output :: struct ->
    client: Int
    /// The bytes that are not sent yet are at the start of it.
    buffer: []Byte
    used: Int
    is_open: Bool

/// Makes the Output of a page for the client, whose buffer is taken from the arena of the request.
/// An Output that got no buffer, as the arena had no room, drops what is put in it.
output_make :: proc(client: Int, arena: ^mu.Arena_Allocator, size: Int) Output ->
    buffer, _ := mu.arena_allocate(arena, size)
    return Output { client = client, buffer = buffer, used = 0, is_open = true }

flush :: proc(out: ^Output) ->
    if out.used > 0 and out.is_open ->
        out.is_open = send_all(out.client, out.buffer[0:out.used])
    out.used = 0

emit_byte :: proc(out: ^Output, value: Byte) ->
    if out.used >= length_of(out.buffer) ->
        flush(out)
    if out.used < length_of(out.buffer) ->
        out.buffer[out.used] = value
        out.used += 1

emit_text :: proc(out: ^Output, text: Str) ->
    for character in text ->
        encoded := cast([4]Byte)character
        for index in 0..<utf8_length(character) ->
            emit_byte(out, encoded[index])

/// Puts a byte of text in the page, where `&`, `<`, `>` and `"` are what HTML has for them.
emit_escaped_byte :: proc(out: ^Output, value: Byte) ->
    if value == '&' ->
        emit_text(out, "&amp;")
    else if value == '<' ->
        emit_text(out, "&lt;")
    else if value == '>' ->
        emit_text(out, "&gt;")
    else if value == '"' ->
        emit_text(out, "&quot;")
    else ->
        emit_byte(out, value)

emit_escaped :: proc(out: ^Output, bytes: []Byte) ->
    for value in bytes ->
        emit_escaped_byte(out, value)

/// Puts text in the page without the marks of Markdown that it has, for a place that shows no
/// styles, such as the title of a tab.
emit_plain :: proc(out: ^Output, bytes: []Byte) ->
    for value in bytes ->
        if cast(Int)value not= BACKTICK and cast(Int)value not= STAR ->
            emit_escaped_byte(out, value)

/// Puts the name that a heading has as a link target: the letters in lower case, the digits, `-` and
/// `_`, and a `-` for a space, which is what the links of the documents expect.
emit_slug :: proc(out: ^Output, bytes: []Byte) ->
    for value in bytes ->
        if value >= 65 and value <= 90 ->
            emit_byte(out, int_to_byte(cast(Int)value + 32))
        else if value == ' ' ->
            emit_byte(out, int_to_byte(45))
        else if is_name_byte(value) or value >= 128 ->
            emit_byte(out, value)

/// Puts a link target: a link to `name.md` is a link to the page `name`, which has no suffix.
emit_href :: proc(out: ^Output, url: []Byte) ->
    index := 0
    for index < length_of(url) ->
        if bytes_start_with(url[index:], ".md") ->
            after := index + 3
            if after == length_of(url) or byte_is(url, after, HASH) ->
                index = after
                continue
        emit_escaped_byte(out, url[index])
        index += 1

// ---------------------------------------------------------------------------------------------
// Answers
// ---------------------------------------------------------------------------------------------

/// Sends the head of an answer with the status, the type and the length of the body, which is all the
/// headers that a browser needs. A length below zero is not known, and then the body goes on until
/// the connection is closed, which the head says it will be.
send_head :: proc(client: Int, code: Int, reason: Str, content_type: Str, length: Int) Bool ->
    head := [HEAD_BYTES]Byte { .. = 0 }
    at := put_text(head[:], 0, "HTTP/1.1 ")
    at = put_int(head[:], at, code)
    at = put_text(head[:], at, " ")
    at = put_text(head[:], at, reason)
    at = put_text(head[:], at, "\r\nServer: mialudoc\r\nContent-Type: ")
    at = put_text(head[:], at, content_type)
    // A file or a page that is rebuilt often, and a stale one in the cache would be confusing.
    at = put_text(head[:], at, "\r\nCache-Control: no-store")
    if length >= 0 ->
        at = put_text(head[:], at, "\r\nContent-Length: ")
        at = put_int(head[:], at, length)
    at = put_text(head[:], at, "\r\nConnection: close\r\n\r\n")
    return send_all(client, head[0:at])

/// Sends an answer that is a line of text, such as `404 Not Found`, and gives the code. The body is
/// left out for a `HEAD` request, which gets the head that a `GET` would.
send_error :: proc(client: Int, code: Int, reason: Str, with_body: Bool) Int ->
    body := [BODY_BYTES]Byte { .. = 0 }
    at := put_int(body[:], 0, code)
    at = put_text(body[:], at, " ")
    at = put_text(body[:], at, reason)
    at = put_text(body[:], at, "\n")
    if send_head(client, code, reason, "text/plain; charset=utf-8", at) and with_body ->
        send_all(client, body[0:at])
    return code

/// Opens the file whose path is the bytes, which are UTF-8 and are turned into the `Str` that the
/// standard library takes.
open_path :: proc(path: []Byte, wide_path: []Byte) (mu.File, mu.File_Error) ->
    text, is_text := utf8_decode(path)
    if not is_text ->
        return mu.File { handle = INVALID_HANDLE }, .PATH_INVALID
    file, error := mu.file_open(text, .READ, wide_path)
    return file, error

/// Sends the file at `path` as it is, in chunks, and gives the code of the answer.
send_file :: proc(client: Int, arena: ^mu.Arena_Allocator, path: []Byte, content_type: Str, with_body: Bool) Int ->
    wide_path := [PATH_BYTES * 2 + 2]Byte { .. = 0 }
    file, open_error := open_path(path, wide_path[:])
    if open_error not= .NONE ->
        return send_error(client, STATUS_NOT_FOUND, "Not Found", with_body)
    defer mu.file_close(file)

    size : I64 = 0
    if GetFileSizeEx(file.handle, %size) == 0 ->
        return send_error(client, STATUS_NOT_FOUND, "Not Found", with_body)
    if not send_head(client, STATUS_OK, "OK", content_type, cast(Int)size) ->
        return STATUS_OK
    if not with_body ->
        return STATUS_OK

    chunk, _ := mu.arena_allocate(arena, CHUNK_BYTES)
    for ->
        count, read_error := mu.file_read(file, chunk)
        if read_error not= .NONE or count == 0 ->
            break
        if not send_all(client, chunk[0:count]) ->
            break
    return STATUS_OK

// ---------------------------------------------------------------------------------------------
// The look of the pages
// ---------------------------------------------------------------------------------------------

/// Puts the start of a page: the head, with the style sheet, and the bar at the top. `title` is
/// the text of the tab. MISSING: a string constant of many lines.
emit_page_start :: proc(out: ^Output, title: []Byte) ->
    emit_text(out, "<!doctype html>\n<html lang='en'>\n<head>\n<meta charset='utf-8'>\n")
    emit_text(out, "<meta name='viewport' content='width=device-width, initial-scale=1'>\n<title>")
    emit_plain(out, title)
    emit_text(out, " · Mialu</title>\n<link rel='icon' href='/logo.png'>\n<style>\n")
    emit_text(out, ":root { --bg: #090b10; --surface: #11141b; --raised: #171b24; --line: #242a37; --text: #d9dde7; --muted: #8e97ac; --accent: #ff8a1f; --accent-2: #ff4d2e; --gold: #ffc43d; color-scheme: dark; }\n")
    emit_text(out, "* { box-sizing: border-box; }\n")
    emit_text(out, "html { scroll-behavior: smooth; scroll-padding-top: 5rem; }\n")
    emit_text(out, "body { margin: 0; color: var(--text); font: 16px/1.7 system-ui, 'Segoe UI', Roboto, sans-serif; background: radial-gradient(900px 500px at 85% -10%, rgba(255, 98, 46, .16), transparent 60%), radial-gradient(800px 500px at -10% 10%, rgba(255, 196, 61, .08), transparent 60%), var(--bg); background-attachment: fixed; min-height: 100vh; }\n")
    emit_text(out, "a { color: var(--gold); text-decoration: none; } a:hover { color: var(--accent); text-decoration: underline; }\n")
    emit_text(out, ".bar { position: sticky; top: 0; z-index: 5; display: flex; align-items: center; gap: .75rem; padding: .6rem 1.5rem; background: rgba(9, 11, 16, .75); backdrop-filter: blur(12px); border-bottom: 1px solid var(--line); }\n")
    emit_text(out, ".bar a { display: flex; align-items: center; gap: .6rem; color: var(--text); font-weight: 650; letter-spacing: .02em; text-decoration: none; }\n")
    emit_text(out, ".bar img { width: 30px; height: 30px; }\n")
    emit_text(out, ".bar nav { display: flex; gap: .25rem; margin-left: 1rem; }\n")
    emit_text(out, ".bar nav a { padding: .3rem .8rem; border-radius: 8px; color: var(--muted); font-weight: 500; letter-spacing: 0; }\n")
    emit_text(out, ".bar nav a:hover { color: var(--text); background: var(--raised); }\n")
    emit_text(out, ".bar nav a[aria-current='page'] { color: var(--text); background: var(--raised); }\n")
    emit_text(out, "main { max-width: 52rem; margin: 0 auto; padding: 2.5rem 1.5rem 6rem; }\n")
    emit_text(out, "h1, h2, h3, h4, h5, h6 { line-height: 1.25; margin: 2.2em 0 .6em; font-weight: 700; letter-spacing: -.01em; }\n")
    emit_text(out, "h1 { font-size: 2.4rem; margin-top: 0; background: linear-gradient(90deg, var(--gold), var(--accent) 55%, var(--accent-2)); -webkit-background-clip: text; background-clip: text; color: transparent; }\n")
    emit_text(out, "h2 { font-size: 1.6rem; padding-bottom: .35em; border-bottom: 1px solid var(--line); } h3 { font-size: 1.25rem; } h4 { font-size: 1.05rem; }\n")
    emit_text(out, "p, ul, ol, table, blockquote, pre { margin: 0 0 1.1em; } li { margin: .2em 0; } li > ul, li > ol { margin: .2em 0 .2em; }\n")
    emit_text(out, "code { font: .9em/1.5 'Cascadia Code', 'Cascadia Mono', Consolas, 'SFMono-Regular', monospace; background: var(--raised); border: 1px solid var(--line); border-radius: 6px; padding: .1em .4em; color: #ffd9a8; }\n")
    emit_text(out, "pre { background: var(--surface); border: 1px solid var(--line); border-radius: 12px; padding: 1rem 1.2rem; overflow-x: auto; box-shadow: 0 8px 30px rgba(0, 0, 0, .35); }\n")
    emit_text(out, "pre code { background: none; border: 0; padding: 0; color: #e6e9f1; font-size: .88rem; }\n")
    emit_text(out, "blockquote { border-left: 3px solid var(--accent); background: rgba(255, 138, 31, .07); border-radius: 0 10px 10px 0; padding: .6rem 1.1rem; color: #e8d9c6; } blockquote p { margin: .4em 0; }\n")
    emit_text(out, ".table { overflow-x: auto; margin: 0 0 1.2em; border: 1px solid var(--line); border-radius: 12px; } table { border-collapse: collapse; width: 100%; margin: 0; font-size: .93rem; }\n")
    emit_text(out, "th, td { text-align: left; padding: .55rem .9rem; border-bottom: 1px solid var(--line); vertical-align: top; } th { background: var(--surface); color: var(--gold); font-weight: 650; } tr:last-child td { border-bottom: 0; } tbody tr:hover { background: rgba(255, 255, 255, .025); }\n")
    emit_text(out, "hr { border: 0; border-top: 1px solid var(--line); margin: 2rem 0; }\n")
    emit_text(out, ".hero { text-align: center; padding: 3.5rem 0 2rem; } .hero img { width: min(260px, 60vw); height: auto; filter: drop-shadow(0 0 40px rgba(255, 98, 46, .45)); }\n")
    emit_text(out, ".hero h1 { font-size: clamp(2.8rem, 8vw, 4.5rem); margin: .2em 0 .1em; letter-spacing: -.03em; } .hero p { color: var(--muted); font-size: 1.15rem; margin: 0; }\n")
    emit_text(out, ".grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(17rem, 1fr)); gap: 1rem; list-style: none; padding: 0; margin: 2rem 0 0; }\n")
    emit_text(out, ".card { display: block; height: 100%; padding: 1.1rem 1.25rem; background: linear-gradient(160deg, var(--raised), var(--surface)); border: 1px solid var(--line); border-radius: 14px; color: var(--text); transition: transform .15s, border-color .15s, box-shadow .15s; }\n")
    emit_text(out, ".card:hover { transform: translateY(-3px); border-color: var(--accent); box-shadow: 0 10px 34px rgba(255, 98, 46, .18); text-decoration: none; color: var(--text); }\n")
    emit_text(out, ".card strong { display: block; font-size: 1.05rem; line-height: 1.35; } .card span { display: block; margin-top: .35rem; color: var(--muted); font: .8rem 'Cascadia Mono', Consolas, monospace; }\n")
    emit_text(out, ".label { text-transform: uppercase; letter-spacing: .14em; font-size: .78rem; color: var(--muted); margin: 3rem 0 0; }\n")
    emit_text(out, "footer { text-align: center; color: var(--muted); font-size: .85rem; padding-bottom: 3rem; }\n")
    emit_text(out, "</style>\n</head>\n<body>\n<div class='bar'><a href='/'><img src='/logo.png' alt=''>Mialu</a>")
    emit_text(out, "<nav aria-label='Pages'><a href='/' aria-current='page'>Documentation</a>")
    emit_text(out, "<a href='/tools/playground'>Playground</a><a href='/tools/internals'>Compiler internals</a></nav></div>\n")

emit_page_end :: proc(out: ^Output) ->
    emit_text(out, "</main>\n<footer>Served by mialudoc, which is written in Mialu.</footer>\n</body>\n</html>\n")

// ---------------------------------------------------------------------------------------------
// Markdown
// ---------------------------------------------------------------------------------------------

/// Puts the text of a line of Markdown as HTML: `code`, **bold**, *emphasis*, [links](url) and
/// the characters that a backslash makes literal.
render_inline :: proc(out: ^Output, text: []Byte) ->
    index := 0
    for index < length_of(text) ->
        value := text[index]
        rest := length_of(text) - index

        if cast(Int)value == BACKTICK ->
            end := find_byte(text, index + 1, BACKTICK)
            if end >= 0 ->
                emit_text(out, "<code>")
                emit_escaped(out, text[index + 1:end])
                emit_text(out, "</code>")
                index = end + 1
                continue

        if cast(Int)value == STAR and byte_is(text, index + 1, STAR) ->
            end := find_pair(text, index + 2, STAR)
            if end > index + 2 ->
                emit_text(out, "<strong>")
                render_inline(out, text[index + 2:end])
                emit_text(out, "</strong>")
                index = end + 2
                continue

        if cast(Int)value == STAR and rest > 1 and not byte_is(text, index + 1, SPACE) ->
            end := find_byte(text, index + 1, STAR)
            if end > index + 1 ->
                emit_text(out, "<em>")
                render_inline(out, text[index + 1:end])
                emit_text(out, "</em>")
                index = end + 1
                continue

        if cast(Int)value == LEFT_BRACKET ->
            middle := find_byte(text, index + 1, RIGHT_BRACKET)
            if middle >= 0 and byte_is(text, middle + 1, LEFT_PARENTHESIS) ->
                end := find_byte(text, middle + 2, RIGHT_PARENTHESIS)
                if end >= 0 ->
                    emit_text(out, "<a href=\"")
                    emit_href(out, text[middle + 2:end])
                    emit_text(out, "\">")
                    render_inline(out, text[index + 1:middle])
                    emit_text(out, "</a>")
                    index = end + 1
                    continue

        if value == '\\' and rest > 1 ->
            if is_punctuation(text[index + 1]) ->
                emit_escaped_byte(out, text[index + 1])
                index += 2
                continue

        emit_escaped_byte(out, value)
        index += 1

/// What is known about the Markdown that has been read: which block is open, so that the line that
/// comes next can continue or close it. `indents` has the indentation of each level of list, a digit of base 256
/// for each, and `numbered` has a digit of base 2 for each, which is 1 when the level is numbered.
/// MISSING: an array in a struct, which would hold these as arrays.
Render :: struct ->
    in_code: Bool
    code_indent: Int
    in_paragraph: Bool
    in_quote: Bool
    in_table: Bool
    table_row: Int
    list_depth: Int
    indents: Int
    numbered: Int

close_paragraph :: proc(out: ^Output, state: ^Render) ->
    if state.in_paragraph ->
        emit_text(out, "</p>\n")
        state.in_paragraph = false

close_quote :: proc(out: ^Output, state: ^Render) ->
    if state.in_quote ->
        emit_text(out, "</p></blockquote>\n")
        state.in_quote = false

close_table :: proc(out: ^Output, state: ^Render) ->
    if state.in_table ->
        if state.table_row == 1 ->
            emit_text(out, "</thead><tbody>")
        emit_text(out, "</tbody></table></div>\n")
        state.in_table = false

/// A number to the power of an exponent.
power :: proc(base: Int, exponent: Int) Int ->
    result := 1
    for step in 0..<exponent ->
        result *= base
    return result

/// The digit of `packed` at `place`, where `packed` is a number whose digits are of base `base`.
digit_at :: proc(packed: Int, base: Int, place: Int) Int ->
    unit := power(base, place)
    next := unit * base
    return packed / unit - packed / next * base

/// `packed` with the digit at `place` set to `value`.
with_digit :: proc(packed: Int, base: Int, place: Int, value: Int) Int ->
    change := value - digit_at(packed, base, place)
    return packed + change * power(base, place)

/// Closes the innermost level of list, and the item that is open in it.
close_list_level :: proc(out: ^Output, state: ^Render) ->
    if digit_at(state.numbered, 2, state.list_depth - 1) == 1 ->
        emit_text(out, "</li></ol>\n")
    else ->
        emit_text(out, "</li></ul>\n")
    state.list_depth -= 1

close_lists :: proc(out: ^Output, state: ^Render) ->
    for state.list_depth > 0 ->
        close_list_level(out, state)

/// Closes the blocks that a list item can be followed by without ending the list.
close_flow :: proc(out: ^Output, state: ^Render) ->
    close_paragraph(out, state)
    close_quote(out, state)
    close_table(out, state)

close_everything :: proc(out: ^Output, state: ^Render) ->
    close_flow(out, state)
    close_lists(out, state)

/// Gives how many bytes the marker of a list item takes, `- `, `* ` or `12. `, and whether it is a
/// number. It is 0 when the text does not start with one.
list_marker :: proc(text: []Byte) (Int, Bool) ->
    // MISSING: parentheses to group a condition, so the two bullets are two conditions.
    if byte_is(text, 1, SPACE) and byte_is(text, 0, DASH) ->
        return 2, false
    if byte_is(text, 1, SPACE) and byte_is(text, 0, STAR) ->
        return 2, false
    digits := 0
    for is_digit_at(text, digits) ->
        digits += 1
    if digits > 0 and byte_is(text, digits, DOT) and byte_is(text, digits + 1, SPACE) ->
        return digits + 2, true
    return 0, false

/// Whether the line of a table is the one under the headings, which has only `|`, `-`, `:` and spaces.
is_table_separator :: proc(text: []Byte) Bool ->
    for value in text ->
        if value not= '|' and value not= '-' and value not= ':' and value not= ' ' ->
            return false
    return true

/// Puts the cells of a row of a table, which are between the `|` of the line, but not those that are
/// in `code` or have a backslash before them.
render_table_row :: proc(out: ^Output, text: []Byte, is_head: Bool) ->
    emit_text(out, "<tr>")
    cell_start := 1
    is_code := false
    is_escaped := false
    for position in 1..<length_of(text) ->
        value := text[position]
        if is_escaped ->
            is_escaped = false
            continue
        if value == '\\' ->
            is_escaped = true
        if cast(Int)value == BACKTICK ->
            is_code = not is_code
        if value == '|' and not is_code ->
            render_table_cell(out, text[cell_start:position], is_head)
            cell_start = position + 1
    if cell_start < length_of(text) ->
        rest := text[cell_start:]
        if trim_start(rest) < length_of(rest) ->
            render_table_cell(out, rest, is_head)
    emit_text(out, "</tr>")

render_table_cell :: proc(out: ^Output, cell: []Byte, is_head: Bool) ->
    trimmed := trim(cell)
    if is_head ->
        emit_text(out, "<th>")
    else ->
        emit_text(out, "<td>")
    render_inline(out, trimmed)
    if is_head ->
        emit_text(out, "</th>")
    else ->
        emit_text(out, "</td>")

/// Puts the HTML of one line of Markdown, and keeps in `state` what the next line needs to know.
render_line :: proc(out: ^Output, state: ^Render, line: []Byte) Int ->
    indent := trim_start(line)
    text := line[indent:]

    if state.in_code ->
        if bytes_start_with(text, "```") ->
            emit_text(out, "</code></pre>\n")
            state.in_code = false
            return 0
        skip := indent
        if skip > state.code_indent ->
            skip = state.code_indent
        emit_escaped(out, line[skip:])
        emit_text(out, "\n")
        return 0

    if length_of(text) == 0 ->
        close_flow(out, state)
        return 0

    if bytes_start_with(text, "```") ->
        if indent == 0 ->
            close_lists(out, state)
        close_flow(out, state)
        language := text[3:]
        language = trim(language)
        if length_of(language) > 0 ->
            emit_text(out, "<pre><code class=\"language-")
            emit_escaped(out, language)
            emit_text(out, "\">")
        else ->
            emit_text(out, "<pre><code>")
        state.in_code = true
        state.code_indent = indent
        return 0

    if indent == 0 and text[0] == '#' ->
        level := 0
        for byte_is(text, level, HASH) ->
            level += 1
        if level <= 6 and byte_is(text, level, SPACE) ->
            close_everything(out, state)
            heading := text[level + 1:]
            emit_text(out, "<h")
            emit_byte(out, int_to_byte(48 + level))
            emit_text(out, " id=\"")
            emit_slug(out, heading)
            emit_text(out, "\">")
            render_inline(out, heading)
            emit_text(out, "</h")
            emit_byte(out, int_to_byte(48 + level))
            emit_text(out, ">\n")
            return 0

    if bytes_are(text, "---") ->
        close_everything(out, state)
        emit_text(out, "<hr>\n")
        return 0

    if indent == 0 and text[0] == '>' ->
        close_paragraph(out, state)
        close_table(out, state)
        close_lists(out, state)
        content := text[1:]
        if byte_is(content, 0, SPACE) ->
            content = content[1:]
        if not state.in_quote ->
            emit_text(out, "<blockquote><p>")
            state.in_quote = true
        if length_of(content) == 0 ->
            emit_text(out, "</p><p>")
        else ->
            render_inline(out, content)
            emit_text(out, "\n")
        return 0

    if indent == 0 and text[0] == '|' ->
        close_paragraph(out, state)
        close_quote(out, state)
        close_lists(out, state)
        if not state.in_table ->
            emit_text(out, "<div class=\"table\"><table><thead>")
            state.in_table = true
            state.table_row = 0
            render_table_row(out, text, true)
        else if state.table_row == 1 ->
            emit_text(out, "</thead><tbody>")
            if not is_table_separator(text) ->
                render_table_row(out, text, false)
        else ->
            render_table_row(out, text, false)
        state.table_row += 1
        return 0

    marker, is_numbered := list_marker(text)
    if marker > 0 ->
        close_paragraph(out, state)
        close_quote(out, state)
        close_table(out, state)
        // The levels that are deeper than this item are over.
        for state.list_depth > 0 and indent < digit_at(state.indents, 256, state.list_depth - 1) ->
            close_list_level(out, state)
        if state.list_depth > 0 and indent == digit_at(state.indents, 256, state.list_depth - 1) ->
            emit_text(out, "</li><li>")
        else if state.list_depth < LIST_LEVELS ->
            if is_numbered ->
                emit_text(out, "<ol><li>")
                state.numbered = with_digit(state.numbered, 2, state.list_depth, 1)
            else ->
                emit_text(out, "<ul><li>")
                state.numbered = with_digit(state.numbered, 2, state.list_depth, 0)
            state.indents = with_digit(state.indents, 256, state.list_depth, indent)
            state.list_depth += 1
        else ->
            emit_text(out, "</li><li>")
        render_inline(out, text[marker:])
        return 0

    // A line that is indented goes on with the item that is open, and any other ends the list.
    if state.list_depth > 0 and indent > 0 ->
        emit_text(out, "\n")
        render_inline(out, text)
        return 0
    close_lists(out, state)
    close_quote(out, state)
    close_table(out, state)
    if state.in_paragraph ->
        emit_text(out, "\n")
    else ->
        emit_text(out, "<p>")
        state.in_paragraph = true
    render_inline(out, text)
    return 0

/// Reads the lines of a file through a chunk of bytes that is read at once.
Line_Reader :: struct ->
    file: mu.File
    /// The bytes that are read at once, of which `count` are read and the ones from `position` on
    /// are not taken yet.
    chunk: []Byte
    position: Int
    count: Int

/// Makes a reader of the lines of `file`, whose chunk is taken from the arena. A reader that got no
/// chunk, as the arena had no room, reads no lines.
line_reader_make :: proc(file: mu.File, arena: ^mu.Arena_Allocator) Line_Reader ->
    chunk, _ := mu.arena_allocate(arena, CHUNK_BYTES)
    return Line_Reader { file = file, chunk = chunk, position = 0, count = 0 }

/// Reads the next line into `line`, without its end, and gives how long it is, and whether there was
/// a line. A line that does not fit is cut where `line` ends, and the rest of it is dropped.
/// MISSING: `file_read_line` of the standard library reads a byte at a time, which is too slow for this.
read_line :: proc(reader: ^Line_Reader, line: []Byte) (Int, Bool) ->
    length := 0
    has_line := false
    for ->
        if reader.position >= reader.count ->
            count, error := mu.file_read(reader.file, reader.chunk)
            if error not= .NONE or count == 0 ->
                break
            reader.position = 0
            reader.count = count
        value := reader.chunk[reader.position]
        reader.position += 1
        has_line = true
        if value == '\n' ->
            break
        if length < length_of(line) ->
            line[length] = value
            length += 1
    if not has_line ->
        return 0, false
    if byte_is(line, length - 1, CARRIAGE_RETURN) ->
        length -= 1
    return length, true

/// Reads the whole file and puts it in the page as HTML.
render_document :: proc(out: ^Output, arena: ^mu.Arena_Allocator, file: mu.File) ->
    // What is taken from the arena is given back when this returns, for what the page puts next.
    mark := arena.used
    defer arena.used = mark
    line, _ := mu.arena_allocate(arena, LINE_BYTES)
    reader := line_reader_make(file, arena)
    state := Render { in_code = false, code_indent = 0, in_paragraph = false, in_quote = false, in_table = false, table_row = 0, list_depth = 0, indents = 0, numbered = 0 }
    for ->
        length, has_line := read_line(%reader, line)
        if not has_line ->
            break
        render_line(out, %state, line[0:length])
    if state.in_code ->
        emit_text(out, "</code></pre>\n")
    close_everything(out, %state)

// ---------------------------------------------------------------------------------------------
// The pages
// ---------------------------------------------------------------------------------------------

/// Puts in `path` the path of a file: the directory, the text, the name and the end.
make_path :: proc(path: []Byte, directory: []Byte, before: Str, name: []Byte, after: Str) Int ->
    at := put_bytes(path, 0, directory)
    at = put_text(path, at, before)
    at = put_bytes(path, at, name)
    return put_text(path, at, after)

/// Puts the title of the document at `path` in `title`, which is the text of its first heading, and
/// gives how long it is. It is `fallback` when there is no heading.
read_title :: proc(arena: ^mu.Arena_Allocator, path: []Byte, title: []Byte, fallback: []Byte) Int ->
    wide_path := [PATH_BYTES * 2 + 2]Byte { .. = 0 }
    file, open_error := open_path(path, wide_path[:])
    if open_error not= .NONE ->
        return put_bytes(title, 0, fallback)
    defer mu.file_close(file)

    mark := arena.used
    defer arena.used = mark
    line := [TITLE_BYTES]Byte { .. = 0 }
    reader := line_reader_make(file, arena)
    length, has_line := read_line(%reader, line[:])
    if has_line and bytes_start_with(line[0:length], "# ") ->
        return put_bytes(title, 0, line[2:length])
    return put_bytes(title, 0, fallback)

/// Puts a card for each document of the directory `docs`, which is a link to its page and has its
/// title. The files are found with `FindFirstFileA`, and are in the order that the system gives them,
/// which is the one of their names.
emit_document_cards :: proc(out: ^Output, arena: ^mu.Arena_Allocator, directory: []Byte) Int ->
    pattern_bytes := [PATH_BYTES]Byte { .. = 0 }
    pattern_length := make_path(pattern_bytes[:], directory, "/docs/", pattern_bytes[0:0], "*.md")
    pattern, is_text := utf8_decode(pattern_bytes[0:pattern_length])
    if not is_text ->
        return 0

    data := [FIND_DATA_BYTES]Byte { .. = 0 }
    handle := FindFirstFileA(str_to_c_str(pattern), %data[0])
    if handle == INVALID_HANDLE ->
        return 0
    emit_text(out, "<ul class=\"grid\">\n")
    for ->
        name_length := 0
        for name_length < FIND_NAME_BYTES and data[FIND_DATA_NAME + name_length] not= 0 ->
            name_length += 1
        file_name := data[FIND_DATA_NAME:FIND_DATA_NAME + name_length]
        if bytes_end_with(file_name, ".md") and is_name(file_name[0:name_length - 3]) ->
            name := file_name[0:name_length - 3]
            path := [PATH_BYTES]Byte { .. = 0 }
            path_length := make_path(path[:], directory, "/docs/", name, ".md")
            title := [TITLE_BYTES]Byte { .. = 0 }
            title_length := read_title(arena, path[0:path_length], title[:], name)
            emit_text(out, "<li><a class=\"card\" href=\"/")
            emit_escaped(out, name)
            emit_text(out, "\"><strong>")
            render_inline(out, title[0:title_length])
            emit_text(out, "</strong><span>")
            emit_escaped(out, file_name)
            emit_text(out, "</span></a></li>\n")
        if FindNextFileA(handle, %data[0]) == 0 ->
            break
    emit_text(out, "</ul>\n")
    FindClose(handle)
    return 0

/// Sends the index page: the logo, and the list of the documents.
send_index :: proc(client: Int, arena: ^mu.Arena_Allocator, directory: []Byte, with_body: Bool) Int ->
    if not send_head(client, STATUS_OK, "OK", "text/html; charset=utf-8", -1) or not with_body ->
        return STATUS_OK
    out := output_make(client, arena, OUTPUT_BYTES)
    title := [TITLE_BYTES]Byte { .. = 0 }
    emit_page_start(%out, title[0:put_text(title[:], 0, "Documentation")])
    emit_text(%out, "<main>\n<div class=\"hero\"><img src=\"/logo.png\" alt=\"The logo of Mialu\"><h1>Mialu</h1>")
    emit_text(%out, "<p>The documentation of the programming language</p></div>\n")
    emit_text(%out, "<p class=\"label\">Documents</p>\n")
    emit_document_cards(%out, arena, directory)
    emit_page_end(%out)
    flush(%out)
    return STATUS_OK

/// Sends the document `docs/<name>.md` as a page of HTML.
send_document :: proc(client: Int, arena: ^mu.Arena_Allocator, directory: []Byte, name: []Byte, with_body: Bool) Int ->
    path := [PATH_BYTES]Byte { .. = 0 }
    path_length := make_path(path[:], directory, "/docs/", name, ".md")
    wide_path := [PATH_BYTES * 2 + 2]Byte { .. = 0 }
    file, open_error := open_path(path[0:path_length], wide_path[:])
    if open_error not= .NONE ->
        return send_error(client, STATUS_NOT_FOUND, "Not Found", with_body)
    defer mu.file_close(file)

    if not send_head(client, STATUS_OK, "OK", "text/html; charset=utf-8", -1) or not with_body ->
        return STATUS_OK
    title := [TITLE_BYTES]Byte { .. = 0 }
    title_length := read_title(arena, path[0:path_length], title[:], name)

    out := output_make(client, arena, OUTPUT_BYTES)
    emit_page_start(%out, title[0:title_length])
    emit_text(%out, "<main>\n")
    render_document(%out, arena, file)
    emit_page_end(%out)
    flush(%out)
    return STATUS_OK

// ---------------------------------------------------------------------------------------------
// The files of the playground
// ---------------------------------------------------------------------------------------------

/// Whether a byte can be in a word of code: a letter, a digit or `_`.
is_word_byte :: proc(value: Byte) Bool ->
    if value == '_' ->
        return true
    if value >= 48 and value <= 57 ->
        return true
    if value >= 65 and value <= 90 ->
        return true
    return value >= 97 and value <= 122

/// Whether the byte at `index` is a byte of a word of code, which is false when there is no byte there.
is_word_at :: proc(bytes: []Byte, index: Int) Bool ->
    return index >= 0 and index < length_of(bytes) and is_word_byte(bytes[index])

/// Whether two sequences of bytes are the same.
bytes_equal :: proc(first: []Byte, second: []Byte) Bool ->
    if length_of(first) not= length_of(second) ->
        return false
    for index in 0..<length_of(first) ->
        if first[index] not= second[index] ->
            return false
    return true

/// Whether `word` is one of the words of `list`, which are separated by spaces.
list_has :: proc(list: Str, word: []Byte) Bool ->
    matched := 0
    is_match := true
    for character in list ->
        if character == ' ' ->
            if is_match and matched == length_of(word) ->
                return true
            matched = 0
            is_match = true
        else ->
            if matched < length_of(word) ->
                if cast(Int)word[matched] not= cast(Int)character ->
                    is_match = false
            else ->
                is_match = false
            matched += 1
    return is_match and matched == length_of(word)

/// Where the bytes end before the line ends that are at their end.
trim_lines :: proc(bytes: []Byte) Int ->
    end := length_of(bytes)
    for byte_is(bytes, end - 1, 10) or byte_is(bytes, end - 1, CARRIAGE_RETURN) or byte_is(bytes, end - 1, SPACE) ->
        end -= 1
    return end

/// Whether a name is that of a file of one of the directories that are served as they are: it has
/// letters, digits, `-`, `_` and dots, and does not start with a dot, so it cannot be `..`.
is_file_name :: proc(bytes: []Byte) Bool ->
    if length_of(bytes) == 0 or byte_is(bytes, 0, DOT) ->
        return false
    for value in bytes ->
        if not is_name_byte(value) and value not= '.' ->
            return false
    return true

/// What a file with the name is for a browser, by the end of its name.
content_type_of :: proc(name: []Byte) Str ->
    dot := -1
    for index in 0..<length_of(name) ->
        if name[index] == '.' ->
            dot = index
    if dot < 0 ->
        return "application/octet-stream"
    extension := name[dot + 1:]
    if bytes_are(extension, "html") ->
        return "text/html; charset=utf-8"
    if bytes_are(extension, "js") ->
        return "text/javascript; charset=utf-8"
    if bytes_are(extension, "json") ->
        return "application/json"
    if bytes_are(extension, "wasm") ->
        return "application/wasm"
    if bytes_are(extension, "mu") or bytes_are(extension, "txt") ->
        return "text/plain; charset=utf-8"
    return "application/octet-stream"

/// Sends the file `name` of the directory `folder` of the project, which has a path that starts and
/// ends with a slash, such as `/tests/wasm/`, and gives the code of the answer.
send_static :: proc(client: Int, arena: ^mu.Arena_Allocator, directory: []Byte, folder: Str, name: []Byte, with_body: Bool) Int ->
    if not is_file_name(name) ->
        return send_error(client, STATUS_NOT_FOUND, "Not Found", with_body)
    path := [PATH_BYTES]Byte { .. = 0 }
    path_length := make_path(path[:], directory, folder, name, "")
    return send_file(client, arena, path[0:path_length], content_type_of(name), with_body)

/// Sends a page of the project that is a file, such as `/tests/wasm/playground.html`.
send_page :: proc(client: Int, arena: ^mu.Arena_Allocator, directory: []Byte, folder: Str, file_name: Str, with_body: Bool) Int ->
    path := [PATH_BYTES]Byte { .. = 0 }
    path_length := make_path(path[:], directory, folder, path[0:0], file_name)
    return send_file(client, arena, path[0:path_length], "text/html; charset=utf-8", with_body)

// ---------------------------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------------------------

/// The hexadecimal digit of a number from 0 to 15.
hex_digit :: proc(value: Int) Byte ->
    if value < 10 ->
        return int_to_byte(48 + value)
    return int_to_byte(87 + value)

/// Puts the bytes in the page as the inside of a JSON string. A carriage return is left out, as the
/// text of a line on Windows has one before its line feed. With `strip_links` a link of Markdown,
/// `[text](url)`, is only its text.
emit_json_bytes :: proc(out: ^Output, bytes: []Byte, strip_links: Bool) Int ->
    index := 0
    for index < length_of(bytes) ->
        value := bytes[index]
        if strip_links and value == '[' ->
            middle := find_byte(bytes, index + 1, RIGHT_BRACKET)
            if middle >= 0 and byte_is(bytes, middle + 1, LEFT_PARENTHESIS) ->
                end := find_byte(bytes, middle + 2, RIGHT_PARENTHESIS)
                if end >= 0 ->
                    emit_json_bytes(out, bytes[index + 1:middle], false)
                    index = end + 1
                    continue

        // The carriage return before a line feed is how Windows ends a line, and is not text. One
        // that is not is, such as the one that a string of the program has.
        if value == '\r' and byte_is(bytes, index + 1, 10) ->
            index += 1
            continue
        emit_json_byte(out, value)
        index += 1
    return 0

/// Puts a byte in the page as it is in a JSON string, which is itself unless it needs an escape.
emit_json_byte :: proc(out: ^Output, value: Byte) Int ->
    if value == '"' ->
        emit_text(out, "\\\"")
    else if value == '\\' ->
        emit_text(out, "\\\\")
    else if value == '\n' ->
        emit_text(out, "\\n")
    else if value == '\t' ->
        emit_text(out, "\\t")
    else if value == '\r' ->
        emit_text(out, "\\r")
    else if value < 32 ->
        emit_text(out, "\\u00")
        emit_byte(out, hex_digit(cast(Int)value / 16))
        emit_byte(out, hex_digit(cast(Int)value - cast(Int)value / 16 * 16))
    else ->
        emit_byte(out, value)
    return 0

/// Puts the file `name` of the directory of the playground in the page as a JSON string, or `null`
/// when it cannot be read, a chunk at a time, so that it can be larger than any buffer.
emit_json_work_file :: proc(out: ^Output, arena: ^mu.Arena_Allocator, directory: []Byte, name: Str) Int ->
    path := [PATH_BYTES]Byte { .. = 0 }
    path_length := work_path(path[:], directory, name)
    wide_path := [PATH_BYTES * 2 + 2]Byte { .. = 0 }
    file, open_error := open_path(path[0:path_length], wide_path[:])
    if open_error not= .NONE ->
        emit_text(out, "null")
        return 0
    defer mu.file_close(file)

    mark := arena.used
    defer arena.used = mark
    chunk, _ := mu.arena_allocate(arena, CHUNK_BYTES)
    is_carriage_return := false
    emit_text(out, "\"")
    for ->
        count, error := mu.file_read(file, chunk)
        if error not= .NONE or count == 0 ->
            break
        for index in 0..<count ->
            value := chunk[index]
            // A carriage return waits for the byte after it, which may be in the next chunk.
            if is_carriage_return and value not= '\n' ->
                emit_json_byte(out, int_to_byte(13))
            is_carriage_return = value == '\r'
            if not is_carriage_return ->
                emit_json_byte(out, value)
    if is_carriage_return ->
        emit_json_byte(out, int_to_byte(13))
    emit_text(out, "\"")
    return 0

/// Puts the bytes in the page as a JSON string.
emit_json_string :: proc(out: ^Output, bytes: []Byte) Int ->
    emit_text(out, "\"")
    emit_json_bytes(out, bytes, false)
    emit_text(out, "\"")
    return 0

/// Puts the bytes in the page as a JSON string of their lower case, which are all ASCII here.
emit_json_lower :: proc(out: ^Output, bytes: []Byte) Int ->
    emit_text(out, "\"")
    for value in bytes ->
        if value >= 65 and value <= 90 ->
            emit_byte(out, int_to_byte(cast(Int)value + 32))
        else ->
            emit_byte(out, value)
    emit_text(out, "\"")
    return 0

/// Puts the base64 of the bytes in the page, with the `=` that fill the last group.
emit_base64 :: proc(out: ^Output, bytes: []Byte) Int ->
    index := 0
    for index < length_of(bytes) ->
        first := cast(Int)bytes[index]
        second := 0
        third := 0
        if index + 1 < length_of(bytes) ->
            second = cast(Int)bytes[index + 1]
        if index + 2 < length_of(bytes) ->
            third = cast(Int)bytes[index + 2]
        group := first * 65536 + second * 256 + third
        high := group / 262144
        emit_byte(out, base64_digit(high))
        emit_byte(out, base64_digit(group / 4096 - high * 64))
        if index + 1 < length_of(bytes) ->
            emit_byte(out, base64_digit(group / 64 - group / 4096 * 64))
        else ->
            emit_byte(out, int_to_byte(61))
        if index + 2 < length_of(bytes) ->
            emit_byte(out, base64_digit(group - group / 64 * 64))
        else ->
            emit_byte(out, int_to_byte(61))
        index += 3
    return 0

/// The character of a number from 0 to 63 in base64: `A` to `Z`, `a` to `z`, `0` to `9`, `+` and `/`.
base64_digit :: proc(value: Int) Byte ->
    if value < 26 ->
        return int_to_byte(65 + value)
    if value < 52 ->
        return int_to_byte(97 + value - 26)
    if value < 62 ->
        return int_to_byte(48 + value - 52)
    if value == 62 ->
        return int_to_byte(43)
    return int_to_byte(47)

// ---------------------------------------------------------------------------------------------
// The internals of the compiler: /api/internals
//
// The page of the internals shows the tokens, the nodes of the syntax tree, the reserved words and
// the operators. They are read from the sources of the compiler, `src/tokenizer.h` and `src/ast.h`,
// and from the specification of the language, so the page cannot go out of date. The answer is
//
//     {"tokens": [{"name", "value", "spelling", "category", "description"}],
//      "ast_nodes": [{"name", "category", "description"}],
//      "reserved_words": [{"word", "status", "category", "token"}],
//      "operators": [{"operator", "description", "precedence", "example", "category"}]}
// ---------------------------------------------------------------------------------------------

COMMA :: 44
QUOTE :: 39
GRAVE :: 96

/// Opens a file of the project, whose path is below the directory, for reading, and gives it.
open_project_file :: proc(directory: []Byte, below: Str, wide_path: []Byte) (mu.File, mu.File_Error) ->
    path := [PATH_BYTES]Byte { .. = 0 }
    path_length := make_path(path[:], directory, below, path[0:0], "")
    file, error := open_path(path[0:path_length], wide_path)
    return file, error

/// The members of the enum `Token_Kind` of the tokenizer, each with the comment above it as its
/// description. A comment about a group of members is not about the next one.
emit_tokens :: proc(out: ^Output, arena: ^mu.Arena_Allocator, directory: []Byte) Int ->
    wide_path := [PATH_BYTES * 2 + 2]Byte { .. = 0 }
    file, open_error := open_project_file(directory, "/src/tokenizer.h", wide_path[:])
    if open_error not= .NONE ->
        return 0
    defer mu.file_close(file)

    // What is taken from the arena is given back when this returns, for what the page puts next.
    mark := arena.used
    defer arena.used = mark
    line, _ := mu.arena_allocate(arena, LINE_BYTES)
    comment := [1024]Byte { .. = 0 }
    comment_length := 0
    reader := line_reader_make(file, arena)
    in_enum := false
    is_first := true
    for ->
        length, has_line := read_line(%reader, line)
        if not has_line ->
            break
        text := line[0:length]
        stripped := trim(text)
        if not in_enum ->
            if bytes_are(stripped, "typedef enum {") ->
                in_enum = true
            continue
        if bytes_start_with(stripped, "} Token_Kind;") ->
            break
        if bytes_start_with(stripped, "//") ->
            rest := stripped[2:]
            rest = trim(rest)
            if comment_length > 0 ->
                comment_length = put_byte(comment[:], comment_length, 32)
            comment_length = put_bytes(comment[:], comment_length, rest)
            continue

        // A member is `TOKEN_KIND_NAME,`, or has a value: `TOKEN_KIND_NAME = '*',` or `= 256,`.
        name_end := 11
        if length_of(stripped) < 11 ->
            name_end = length_of(stripped)
        for is_word_at(stripped, name_end) ->
            name_end += 1
        is_member := bytes_start_with(stripped, "TOKEN_KIND_") and name_end > 11
        value_start := 0
        value_end := 0
        if is_member ->
            is_member = false
            if byte_is(stripped, name_end, COMMA) ->
                is_member = true
            else if bytes_start_with(stripped[name_end:], " = ") ->
                start := name_end + 3
                if byte_is(stripped, start, QUOTE) and byte_is(stripped, start + 2, QUOTE) and byte_is(stripped, start + 3, COMMA) ->
                    is_member = true
                    value_start = start
                    value_end = start + 3
                else ->
                    digits := start
                    for is_digit_at(stripped, digits) ->
                        digits += 1
                    if digits > start and byte_is(stripped, digits, COMMA) ->
                        is_member = true
                        value_start = start
                        value_end = digits
        if not is_member ->
            comment_length = 0
            continue

        name := stripped[11:name_end]
        description := comment[0:comment_length]
        if bytes_start_with(description, "Tokens consisting of") ->
            description = comment[0:0]
        comment_length = 0

        has_value := value_end > value_start
        is_character := has_value and byte_is(stripped, value_start, QUOTE)
        is_keyword := bytes_start_with(name, "KEYWORD_")

        if is_first ->
            is_first = false
        else ->
            emit_text(out, ",")
        emit_text(out, "{\"name\":\"TOKEN_KIND_")
        emit_bytes_text(out, name)
        emit_text(out, "\",\"value\":")
        if has_value ->
            emit_json_string(out, stripped[value_start:value_end])
        else ->
            emit_text(out, "null")
        emit_text(out, ",\"spelling\":")
        if is_character ->
            emit_json_string(out, stripped[value_start + 1:value_start + 2])
        else if is_keyword ->
            emit_json_lower(out, name[8:])
        else ->
            emit_text(out, "\"\"")

        category := "Multi-character"
        if is_keyword ->
            category = "Keyword"
        else if is_character ->
            category = "Single character"
        if list_has("IDENTIFIER INTEGER_LITERAL FLOAT_LITERAL RUNE_LITERAL STRING_LITERAL LABEL", name) ->
            category = "Names and literals"
        if list_has("INDENTATION COMMENT DOCUMENTATION_COMMENT END", name) ->
            category = "Layout and comments"
        emit_text(out, ",\"category\":\"")
        emit_text(out, category)
        emit_text(out, "\",\"description\":")
        emit_json_string(out, description)
        emit_text(out, "}")
    return 0

/// Puts the bytes in the page as they are, which are names that need no escape.
emit_bytes_text :: proc(out: ^Output, bytes: []Byte) Int ->
    for value in bytes ->
        emit_byte(out, value)
    return 0

/// The start of the lines of the comment of `ast.h` that describes a node, and what the lines that
/// go on with it start with.
AST_ENTRY :: " * - `AST_NODE_TYPE_"
AST_CONTINUATION :: " *   "

/// Puts in `description` what the big comment of `ast.h` says of the node `name`, which is an entry
/// ` * - `AST_NODE_TYPE_NAME`: text` and the lines that go on with it, and gives how long it is.
find_ast_description :: proc(arena: ^mu.Arena_Allocator, directory: []Byte, name: []Byte, description: []Byte) Int ->
    wide_path := [PATH_BYTES * 2 + 2]Byte { .. = 0 }
    file, open_error := open_project_file(directory, "/src/ast.h", wide_path[:])
    if open_error not= .NONE ->
        return 0
    defer mu.file_close(file)

    // What is taken from the arena is given back when this returns, for what the page puts next.
    mark := arena.used
    defer arena.used = mark
    line, _ := mu.arena_allocate(arena, LINE_BYTES)
    reader := line_reader_make(file, arena)
    length := 0
    is_found := false
    for ->
        count, has_line := read_line(%reader, line)
        if not has_line ->
            break
        text := line[0:count]
        if bytes_start_with(text, AST_ENTRY) ->
            if is_found ->
                break
            start := length_of(AST_ENTRY)
            end := find_byte(text, start, GRAVE)
            if end >= 0 and bytes_equal(text[start:end], name) ->
                is_found = true
                rest := text[end + 1:]
                // The name is followed by a colon, and a space that is not part of the description.
                if byte_is(rest, 0, 58) ->
                    rest = rest[1:]
                rest = trim(rest)
                length = put_bytes(description, 0, rest)
        else if is_found ->
            if bytes_start_with(text, AST_CONTINUATION) ->
                rest := text[length_of(AST_CONTINUATION):]
                rest = trim(rest)
                length = put_byte(description, length, 32)
                length = put_bytes(description, length, rest)
            else ->
                break
    return length

/// The members of the enum `Ast_Node_Type` of `ast.h`, which is the first enum of the file, each
/// with its group and its entry of the big comment.
emit_ast_nodes :: proc(out: ^Output, arena: ^mu.Arena_Allocator, directory: []Byte) Int ->
    wide_path := [PATH_BYTES * 2 + 2]Byte { .. = 0 }
    file, open_error := open_project_file(directory, "/src/ast.h", wide_path[:])
    if open_error not= .NONE ->
        return 0
    defer mu.file_close(file)

    // What is taken from the arena is given back when this returns, for what the page puts next.
    mark := arena.used
    defer arena.used = mark
    line, _ := mu.arena_allocate(arena, LINE_BYTES)
    description, _ := mu.arena_allocate(arena, 2048)
    reader := line_reader_make(file, arena)
    in_enum := false
    is_first := true
    for ->
        length, has_line := read_line(%reader, line)
        if not has_line ->
            break
        text := line[0:length]
        stripped := trim(text)
        if not in_enum ->
            if bytes_are(stripped, "typedef enum {") ->
                in_enum = true
            continue
        if bytes_start_with(stripped, "} Ast_Node_Type;") ->
            break
        name_end := 14
        if length_of(stripped) < 14 ->
            name_end = length_of(stripped)
        for is_word_at(stripped, name_end) ->
            name_end += 1
        if bytes_start_with(stripped, "AST_NODE_TYPE_") and name_end > 14 and byte_is(stripped, name_end, COMMA) ->
            name := stripped[14:name_end]
            category := "Other"
            if list_has("ARRAY_TYPE ENUM_TYPE POINTER_TYPE PROCEDURE_TYPE STRUCT_TYPE TYPE_NAME", name) ->
                category = "Type"
            else if list_has("ADDITION AND CAST COMPARISON CONCATENATION DIVISION MULTIPLICATION NOT OR SUBTRACTION ADDRESS_OF DEREFERENCE", name) ->
                category = "Operator"
            else if list_has("CALL FIELD_ACCESS IDENTIFIER SLICE SUBSCRIPT TYPE_INFO_OF", name) ->
                category = "Expression"
            else if bytes_end_with(name, "_DEFINITION") ->
                category = "Definition"
            else if bytes_end_with(name, "_STATEMENT") ->
                category = "Statement"
            else if bytes_end_with(name, "_LITERAL") ->
                category = "Literal"
            else if bytes_end_with(name, "_ASSIGNMENT") ->
                category = "Statement"

            description_length := find_ast_description(arena, directory, name, description)
            if is_first ->
                is_first = false
            else ->
                emit_text(out, ",")
            emit_text(out, "{\"name\":\"AST_NODE_TYPE_")
            emit_bytes_text(out, name)
            emit_text(out, "\",\"category\":\"")
            emit_text(out, category)
            emit_text(out, "\",\"description\":")
            emit_json_string(out, description[0:description_length])
            emit_text(out, "}")
    return 0

/// The group of a reserved word. The specification lists them only as used, literal and unused, so the
/// finer groups are kept here. A word that is in none is in the group "Other".
keyword_category :: proc(word: []Byte, is_unused: Bool) Str ->
    if is_unused ->
        if list_has("async await catch do finally goto loop switch throw throws try where while yield", word) ->
            return "Control flow"
        if list_has("abstract alias class distinct extern final gen generate impl interface macro mutable trait virtual", word) ->
            return "Definitions"
        if list_has("private public safe unsafe", word) ->
            return "Visibility and safety"
        if list_has("as delete is new of operator override ref self super this transmute type_id_of type_of use", word) ->
            return "Operators and conversions"
        return "Other"
    if list_has("break case continue defer else for if in match return", word) ->
        return "Control flow"
    if list_has("cast", word) ->
        return "Conversions"
    if list_has("const dynamic enum foreign import package proc struct type_id union", word) ->
        return "Definitions"
    if list_has("and not or", word) ->
        return "Logical operators"
    if list_has("type_info_of", word) ->
        return "Reflection"
    if list_has("false nil true", word) ->
        return "Values"
    return "Other"

/// The reserved words of the specification, which has them in three blocks of code below the heading
/// "Reserved words": the keywords in use, the literal words and the unused keywords.
emit_reserved_words :: proc(out: ^Output, arena: ^mu.Arena_Allocator, directory: []Byte) Int ->
    wide_path := [PATH_BYTES * 2 + 2]Byte { .. = 0 }
    file, open_error := open_project_file(directory, "/docs/mialu-programming-language.md", wide_path[:])
    if open_error not= .NONE ->
        return 0
    defer mu.file_close(file)

    // What is taken from the arena is given back when this returns, for what the page puts next.
    mark := arena.used
    defer arena.used = mark
    line, _ := mu.arena_allocate(arena, LINE_BYTES)
    reader := line_reader_make(file, arena)
    in_section := false
    in_block := false
    blocks := 0
    is_first := true
    for ->
        length, has_line := read_line(%reader, line)
        if not has_line ->
            break
        text := line[0:length]
        if not in_section ->
            if bytes_are(text, "## Reserved words") ->
                in_section = true
            continue
        if in_block ->
            if bytes_start_with(text, "```") ->
                in_block = false
                blocks += 1
                continue
        else ->
            if bytes_start_with(text, "## ") ->
                break
            if bytes_start_with(text, "```") ->
                in_block = true
            continue
        if blocks > 2 ->
            continue

        status := "Keyword in use"
        if blocks == 1 ->
            status = "Literal word"
        else if blocks == 2 ->
            status = "Unused keyword"
        start := 0
        for start < length ->
            if text[start] == ' ' ->
                start += 1
                continue
            end := start
            for end < length and not byte_is(text, end, SPACE) ->
                end += 1
            word := text[start:end]
            if is_first ->
                is_first = false
            else ->
                emit_text(out, ",")
            emit_text(out, "{\"word\":")
            emit_json_string(out, word)
            emit_text(out, ",\"status\":\"")
            emit_text(out, status)
            emit_text(out, "\",\"category\":\"")
            emit_text(out, keyword_category(word, blocks == 2))
            emit_text(out, "\",\"token\":")
            if blocks == 2 ->
                emit_text(out, "null")
            else ->
                emit_text(out, "\"TOKEN_KIND_KEYWORD_")
                for value in word ->
                    if value >= 97 and value <= 122 ->
                        emit_byte(out, int_to_byte(cast(Int)value - 32))
                    else ->
                        emit_byte(out, value)
                emit_text(out, "\"")
            emit_text(out, "}")
            start = end
    return 0

/// The group of an operator of the precedence.
operator_category :: proc(precedence: Int) Str ->
    if precedence == 0 ->
        return "Definition and assignment"
    if precedence <= 3 ->
        return "Logical"
    if precedence == 4 ->
        return "Comparison"
    if precedence <= 6 ->
        return "Arithmetic"
    return "Address, access and pointers"

/// The most precedence that an operator has, for `emit_operators`, which goes through the table once
/// for each. The specification is not to have more.
OPERATOR_PRECEDENCE_MAX :: 15

/// Puts the rows of the table of operators that have the precedence, which are the rows of the
/// section "Operators" of the specification that have four cells and a number in the third.
emit_operators_of :: proc(out: ^Output, arena: ^mu.Arena_Allocator, directory: []Byte, wanted: Int, is_first: ^Bool) Int ->
    wide_path := [PATH_BYTES * 2 + 2]Byte { .. = 0 }
    file, open_error := open_project_file(directory, "/docs/mialu-programming-language.md", wide_path[:])
    if open_error not= .NONE ->
        return 0
    defer mu.file_close(file)

    // What is taken from the arena is given back when this returns, for what the page puts next.
    mark := arena.used
    defer arena.used = mark
    line, _ := mu.arena_allocate(arena, LINE_BYTES)
    reader := line_reader_make(file, arena)
    in_section := false
    for ->
        length, has_line := read_line(%reader, line)
        if not has_line ->
            break
        text := line[0:length]
        if not in_section ->
            if bytes_are(text, "## Operators") ->
                in_section = true
            continue
        if bytes_start_with(text, "## ") ->
            break
        text = text[0:trim_end(text)]
        if not byte_is(text, 0, 124) or not byte_is(text, length_of(text) - 1, 124) ->
            continue

        // The cells are between the five bars of the row. A row with other bars is not one.
        bars := [6]Int { .. = 0 }
        count := 0
        for index in 0..<length_of(text) ->
            if text[index] == '|' ->
                if count < 6 ->
                    bars[count] = index
                count += 1
        if count not= 5 ->
            continue
        cell := text[bars[0] + 1:bars[1]]
        operator_text := trim(cell)
        cell = text[bars[1] + 1:bars[2]]
        description := trim(cell)
        cell = text[bars[2] + 1:bars[3]]
        precedence_text := trim(cell)
        cell = text[bars[3] + 1:bars[4]]
        example := trim(cell)
        is_number := length_of(precedence_text) > 0
        for value in precedence_text ->
            if value < 48 or value > 57 ->
                is_number = false
        if not is_number ->
            continue
        precedence := 0
        for value in precedence_text ->
            precedence = precedence * 10 + cast(Int)value - 48
        if precedence not= wanted ->
            continue

        // The backticks around an operator are not part of it.
        operator_start := 0
        for byte_is(operator_text, operator_start, GRAVE) ->
            operator_start += 1
        operator_end := length_of(operator_text)
        for operator_end > operator_start and byte_is(operator_text, operator_end - 1, GRAVE) ->
            operator_end -= 1

        if is_first^ ->
            is_first^ = false
        else ->
            emit_text(out, ",")
        emit_text(out, "{\"operator\":")
        emit_json_string(out, operator_text[operator_start:operator_end])
        emit_text(out, ",\"description\":\"")
        emit_json_bytes(out, description, true)
        emit_text(out, "\",\"precedence\":")
        emit_bytes_text(out, precedence_text)
        emit_text(out, ",\"example\":")
        emit_json_string(out, example)
        emit_text(out, ",\"category\":\"")
        emit_text(out, operator_category(precedence))
        emit_text(out, "\"}")
    return 0

/// Puts the operators, from the one that binds loosest to the one that binds tightest, with the
/// operators that are not in expressions, whose precedence is 0, last.
emit_operators :: proc(out: ^Output, arena: ^mu.Arena_Allocator, directory: []Byte) Int ->
    is_first := true
    for wanted in 1..<=OPERATOR_PRECEDENCE_MAX ->
        emit_operators_of(out, arena, directory, wanted, %is_first)
    emit_operators_of(out, arena, directory, 0, %is_first)
    return 0

/// Sends the answer of /api/internals.
send_internals :: proc(client: Int, arena: ^mu.Arena_Allocator, directory: []Byte, with_body: Bool) Int ->
    wide_path := [PATH_BYTES * 2 + 2]Byte { .. = 0 }
    file, open_error := open_project_file(directory, "/docs/mialu-programming-language.md", wide_path[:])
    if open_error not= .NONE ->
        return send_error(client, STATUS_INTERNAL_ERROR, "Cannot read the sources of the compiler", with_body)
    mu.file_close(file)

    if not send_head(client, STATUS_OK, "OK", "application/json", -1) or not with_body ->
        return STATUS_OK
    out := output_make(client, arena, OUTPUT_BYTES)
    emit_text(%out, "{\"tokens\":[")
    emit_tokens(%out, arena, directory)
    emit_text(%out, "],\"ast_nodes\":[")
    emit_ast_nodes(%out, arena, directory)
    emit_text(%out, "],\"reserved_words\":[")
    emit_reserved_words(%out, arena, directory)
    emit_text(%out, "],\"operators\":[")
    emit_operators(%out, arena, directory)
    emit_text(%out, "]}")
    flush(%out)
    return STATUS_OK

// ---------------------------------------------------------------------------------------------
// The compiler as a process: /compile
//
// The playground posts the source of a program, and gets what the compiler makes of it as JSON:
//
//     {"error": null, "ast": "<syntax tree>", "wasm": "<module in base64>", "wat": "<module text>",
//      "asm": "<assembly>", "asm_error": null}
//     {"error": "<what the compiler printed>", "ast": "<syntax tree or null>", "wasm": null,
//      "wat": null, "asm": null, "asm_error": null}
//
// The syntax tree is the one that `mialuc dump-ast` prints, and the assembly is the one that
// `mialuc dump-asm --target x64-win32` prints, which is for a program that starts with `main`, so for
// the programs of WebAssembly it is often what the compiler says it cannot do, in "asm_error". A
// program that does not compile is a normal answer too, with status 200.
//
// The compiler is `mialuc.exe` next to mialudoc in a package of a release, or `build/mialuc.exe` of the project, which runs in the directory `build/playground`
// with the source in it as `playground.mu`, so that its messages name the source without a path.
// MISSING: a standard library of processes. The procedures of Windows that start one are declared
// here, with the structures that they take as bytes, as for the sockets.
// ---------------------------------------------------------------------------------------------

foreign kernel32 #(default_calling_convention("system")) ->
    CloseHandle          :: proc(handle: Int) I32 -> ...
    CreateDirectoryA     :: proc(path: ^Byte, security: ^Byte) I32 -> ...
    CreateFileA          :: proc(file_name: ^Byte, desired_access: U32, share_mode: U32, security: ^Byte, disposition: U32, attributes: U32, template: Int) Int -> ...
    CreateProcessA       :: proc(application: ^Byte, command_line: ^Byte, process_security: ^Byte, thread_security: ^Byte, inherit_handles: I32, flags: U32, environment: ^Byte, directory: ^Byte, startup: ^Byte, process: ^Byte) I32 -> ...
    GetFileAttributesA   :: proc(path: ^Byte) U32 -> ...
    GetExitCodeProcess   :: proc(process: Int, code: ^U32) I32 -> ...
    TerminateProcess     :: proc(process: Int, code: U32) I32 -> ...
    WaitForSingleObject  :: proc(handle: Int, milliseconds: U32) U32 -> ...

GENERIC_READ : U32 : 0x80000000
GENERIC_WRITE : U32 : 0x40000000
FILE_SHARE_READ : U32 : 1
FILE_SHARE_WRITE : U32 : 2
CREATE_ALWAYS : U32 : 2
OPEN_EXISTING : U32 : 3
FILE_ATTRIBUTE_NORMAL : U32 : 0x80
CREATE_NO_WINDOW : U32 : 0x08000000
INVALID_FILE_ATTRIBUTES : U32 : 0xFFFFFFFF

PLAYGROUND_DIRECTORY :: "/build/playground"
SOURCE_BYTES_MAX :: 1048576
COMPILE_TIMEOUT_MILLISECONDS :: 10000
MESSAGE_BYTES :: 8192

/// What `run_compiler` gives when the compiler could not be started, and when it did not finish.
RUN_FAILED :: -1
RUN_TIMED_OUT :: -2

/// Puts in `path` the path of a file of the directory of the playground.
work_path :: proc(path: []Byte, directory: []Byte, name: Str) Int ->
    at := put_bytes(path, 0, directory)
    at = put_text(path, at, PLAYGROUND_DIRECTORY)
    at = put_text(path, at, "/")
    return put_text(path, at, name)

/// Puts the number in `buffer` at `at` as 8 bytes, the lowest first, as a `HANDLE` is in a structure.
/// MISSING: a struct with the layout of a structure of Windows.
put_u64 :: proc(buffer: []Byte, at: Int, value: Int) Int ->
    rest := value
    for index in 0..<8 ->
        quotient := rest / 256
        buffer[at + index] = int_to_byte(rest - quotient * 256)
        rest = quotient
    return 0

/// Reads 8 bytes of `buffer` at `at`, the lowest first, as a number.
get_u64 :: proc(buffer: []Byte, at: Int) Int ->
    total := 0
    for index in 0..<8 ->
        total = total * 256 + cast(Int)buffer[at + 7 - index]
    return total

/// Where the compiler is below the directory of the project: next to mialudoc, as in a package of a release,
/// or else in `build`, where the build of the project puts it.
compiler_location :: proc(directory: []Byte) Str ->
    path := [PATH_BYTES]Byte { .. = 0 }
    put_text(path[:], put_bytes(path[:], 0, directory), "/mialuc.exe")
    if GetFileAttributesA(%path[0]) not= INVALID_FILE_ATTRIBUTES ->
        return "/mialuc.exe"
    return "/build/mialuc.exe"

/// Runs `mialuc` of the project with the arguments in the directory of the playground, with what it
/// prints as the standard output and the standard error in the two files that are named, and gives its
/// exit code, or RUN_FAILED or RUN_TIMED_OUT.
run_compiler :: proc(directory: []Byte, arguments: Str, output_name: Str, errors_name: Str) Int ->
    work := [PATH_BYTES]Byte { .. = 0 }
    put_text(work[:], put_bytes(work[:], 0, directory), PLAYGROUND_DIRECTORY)
    output_path := [PATH_BYTES]Byte { .. = 0 }
    work_path(output_path[:], directory, output_name)
    errors_path := [PATH_BYTES]Byte { .. = 0 }
    work_path(errors_path[:], directory, errors_name)
    null_path := [8]Byte { .. = 0 }
    put_text(null_path[:], 0, "NUL")

    // A handle that the compiler is to have is made inheritable by the structure with a 1 at its 16th byte.
    security := [24]Byte { .. = 0 }
    security[0] = 24
    security[16] = 1
    output := CreateFileA(%output_path[0], GENERIC_WRITE, FILE_SHARE_READ, %security[0], CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0)
    errors := CreateFileA(%errors_path[0], GENERIC_WRITE, FILE_SHARE_READ, %security[0], CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0)
    input := CreateFileA(%null_path[0], GENERIC_READ, FILE_SHARE_READ + FILE_SHARE_WRITE, %security[0], OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0)

    command := [PATH_BYTES * 2]Byte { .. = 0 }
    at := put_text(command[:], 0, "\"")
    at = put_bytes(command[:], at, directory)
    at = put_text(command[:], at, compiler_location(directory))
    at = put_text(command[:], at, "\" ")
    put_text(command[:], at, arguments)

    // `STARTUPINFOA` is 104 bytes: its size, the flag that says the three standard handles are
    // given, which is 0x100 at the byte 60, and the handles at 80, 88 and 96.
    startup := [104]Byte { .. = 0 }
    startup[0] = 104
    startup[61] = 1
    put_u64(startup[:], 80, input)
    put_u64(startup[:], 88, output)
    put_u64(startup[:], 96, errors)
    // `PROCESS_INFORMATION` is the handle of the process and the one of its thread, and two ids.
    info := [24]Byte { .. = 0 }

    result := RUN_FAILED
    created := CreateProcessA(nil, %command[0], nil, nil, 1, CREATE_NO_WINDOW, nil, %work[0], %startup[0], %info[0])
    if created not= 0 ->
        process := get_u64(info[:], 0)
        thread := get_u64(info[:], 8)
        if WaitForSingleObject(process, COMPILE_TIMEOUT_MILLISECONDS) == 0 ->
            code : U32 = 0
            GetExitCodeProcess(process, %code)
            result = cast(Int)code
        else ->
            TerminateProcess(process, 1)
            result = RUN_TIMED_OUT
        CloseHandle(thread)
        CloseHandle(process)
    CloseHandle(output)
    CloseHandle(errors)
    CloseHandle(input)
    return result

/// Reads the file `name` of the directory of the playground into `data`, and gives how many bytes it has,
/// or -1 when it cannot be read.
read_work_file :: proc(directory: []Byte, name: Str, data: []Byte) Int ->
    path := [PATH_BYTES]Byte { .. = 0 }
    path_length := work_path(path[:], directory, name)
    text, is_text := utf8_decode(path[0:path_length])
    if not is_text ->
        return -1
    wide_path := [PATH_BYTES * 2 + 2]Byte { .. = 0 }
    count, error := mu.file_read_all(text, wide_path[:], data)
    if error not= .NONE ->
        return -1
    return count

/// The size of the file `name` of the directory of the playground in bytes, or 0 when it cannot be read.
work_file_size :: proc(directory: []Byte, name: Str) Int ->
    path := [PATH_BYTES]Byte { .. = 0 }
    path_length := work_path(path[:], directory, name)
    wide_path := [PATH_BYTES * 2 + 2]Byte { .. = 0 }
    file, open_error := open_path(path[0:path_length], wide_path[:])
    if open_error not= .NONE ->
        return 0
    defer mu.file_close(file)
    size : I64 = 0
    if GetFileSizeEx(file.handle, %size) == 0 ->
        return 0
    return cast(Int)size

/// Puts in `message` what the compiler said when it did not do what was asked, which is what it printed
/// as errors, or else as output, and gives how long it is. It is 0 when the compiler did well.
compiler_message :: proc(directory: []Byte, status: Int, errors_name: Str, output_name: Str, message: []Byte) Int ->
    if status == 0 ->
        return 0
    if status == RUN_TIMED_OUT ->
        return put_text(message, 0, "The compiler did not finish in 10 s.")
    length := read_work_file(directory, errors_name, message)
    if length > 0 ->
        length = trim_lines(message[0:length])
    if length <= 0 ->
        length = read_work_file(directory, output_name, message)
        if length > 0 ->
            length = trim_lines(message[0:length])
    if length <= 0 ->
        at := put_text(message, 0, "The compiler exited with ")
        return put_int(message, at, status)
    return length

/// Goes through what `dump-ast` printed, which is the tokens, an empty line and then the tree, which starts with
/// its root, the line `MODULE`. It says whether the tree is there. With `is_emitting` it puts the tree in the
/// page as a JSON string, without the empty lines that end it, a line at a time so it can be of any size.
walk_tree :: proc(out: ^Output, arena: ^mu.Arena_Allocator, directory: []Byte, is_emitting: Bool) Bool ->
    path := [PATH_BYTES]Byte { .. = 0 }
    path_length := work_path(path[:], directory, "ast.txt")
    wide_path := [PATH_BYTES * 2 + 2]Byte { .. = 0 }
    file, open_error := open_path(path[0:path_length], wide_path[:])
    if open_error not= .NONE ->
        return false
    defer mu.file_close(file)

    // What is taken from the arena is given back when this returns, for what the page puts next.
    mark := arena.used
    defer arena.used = mark
    line, _ := mu.arena_allocate(arena, LINE_BYTES)
    reader := line_reader_make(file, arena)
    is_found := false
    was_empty := false
    is_first := true
    empty_lines := 0
    for ->
        length, has_line := read_line(%reader, line)
        if not has_line ->
            break
        if not is_found ->
            if was_empty and bytes_start_with(line[0:length], "MODULE") ->
                is_found = true
                if not is_emitting ->
                    break
                emit_text(out, "\"")
            was_empty = length == 0
        if is_found ->
            // Empty lines are put when something follows them, so the ones at the end are left out.
            if length == 0 ->
                empty_lines += 1
            else ->
                if is_first ->
                    is_first = false
                else ->
                    emit_text(out, "\\n")
                for step in 0..<empty_lines ->
                    emit_text(out, "\\n")
                empty_lines = 0
                emit_json_bytes(out, line[0:length], false)
    if is_found and is_emitting ->
        emit_text(out, "\"")
    return is_found

/// Finds in the head of a request the length of its body, which is the header `Content-Length`, and
/// gives it, or -1.
content_length_of :: proc(head: []Byte) Int ->
    start := 0
    for start < length_of(head) ->
        end := find_byte(head, start, 10)
        if end < 0 ->
            end = length_of(head)
        // A line ends with a carriage return and a line feed, and the carriage return is not part of it.
        field := head[start:start + trim_lines(head[start:end])]
        if length_of(field) > 15 ->
            name := field[0:15]
            matches := true
            index := 0
            for character in "content-length:" ->
                lower := cast(Int)name[index]
                if lower >= 65 and lower <= 90 ->
                    lower += 32
                if lower not= cast(Int)character ->
                    matches = false
                index += 1
            if matches ->
                rest := field[15:]
                rest = trim(rest)
                digits, is_number := try_str_to_int(utf8_decode_or_empty(rest))
                if is_number ->
                    return digits
                return -1
        start = end + 1
    return -1

/// The text of the bytes, which is empty when they are not UTF-8.
utf8_decode_or_empty :: proc(bytes: []Byte) Str ->
    text, is_text := utf8_decode(bytes)
    return text

/// Writes all of `bytes` to the file, and says whether the system took them all.
write_all :: proc(file: mu.File, bytes: []Byte) Bool ->
    total := 0
    for total < length_of(bytes) ->
        count, error := mu.file_write(file, bytes[total:])
        if error not= .NONE or count == 0 ->
            return false
        total += count
    return true

/// Writes the source of a request to `playground.mu`: first what came with the head, and then what the client
/// still sends, until it has sent `length` bytes. It says whether all of it was written.
receive_source :: proc(client: Int, arena: ^mu.Arena_Allocator, directory: []Byte, first: []Byte, length: Int) Bool ->
    path := [PATH_BYTES]Byte { .. = 0 }
    path_length := work_path(path[:], directory, "playground.mu")
    text, is_text := utf8_decode(path[0:path_length])
    if not is_text ->
        return false
    wide_path := [PATH_BYTES * 2 + 2]Byte { .. = 0 }
    file, open_error := mu.file_open(text, .WRITE, wide_path[:])
    if open_error not= .NONE ->
        return false
    defer mu.file_close(file)

    part := first
    if length_of(part) > length ->
        part = first[0:length]
    if not write_all(file, part) ->
        return false
    received := length_of(part)
    chunk, _ := mu.arena_allocate(arena, CHUNK_BYTES)
    for received < length ->
        wanted := length - received
        if wanted > length_of(chunk) ->
            wanted = length_of(chunk)
        if wanted == 0 ->
            return false
        count := cast(Int)recv(client, %chunk[0], int_to_i32(wanted), 0)
        if count <= 0 ->
            return false
        if not write_all(file, chunk[0:count]) ->
            return false
        received += count
    return true

/// Sends an answer of /compile that has an error and nothing else.
send_compile_failure :: proc(client: Int, arena: ^mu.Arena_Allocator, message: []Byte) Int ->
    if not send_head(client, STATUS_OK, "OK", "application/json", -1) ->
        return STATUS_OK
    out := output_make(client, arena, BODY_BYTES * 8)
    emit_text(%out, "{\"error\":")
    emit_json_string(%out, message)
    emit_text(%out, ",\"ast\":null,\"wasm\":null,\"wat\":null,\"asm\":null,\"asm_error\":null}")
    flush(%out)
    return STATUS_OK

/// Answers a request of /compile, whose head is `request[0:head_end]`: the source is the body, which is
/// built for both targets of WebAssembly, and for the one of x64, to show its assembly.
/// The syntax tree, the text of the module and the assembly are sent from their files, so they can be
/// of any size. The module in base64 is read into memory that is made for its size, so it can be too.
send_compile :: proc(client: Int, arena: ^mu.Arena_Allocator, directory: []Byte, request: []Byte, head_end: Int) Int ->
    length := content_length_of(request[0:head_end])
    if length < 0 ->
        return send_error(client, STATUS_BAD_REQUEST, "Bad Request", true)
    if length > SOURCE_BYTES_MAX ->
        return send_error(client, STATUS_PAYLOAD_TOO_LARGE, "Payload Too Large", true)

    // The directory `build` is not in a package of a release, which has the compiler next to mialudoc.
    build := [PATH_BYTES]Byte { .. = 0 }
    put_text(build[:], put_bytes(build[:], 0, directory), "/build")
    CreateDirectoryA(%build[0], nil)
    work := [PATH_BYTES]Byte { .. = 0 }
    put_text(work[:], put_bytes(work[:], 0, directory), PLAYGROUND_DIRECTORY)
    CreateDirectoryA(%work[0], nil)
    if not receive_source(client, arena, directory, request[head_end:], length) ->
        return send_error(client, STATUS_BAD_REQUEST, "Bad Request", true)

    message, _ := mu.arena_allocate(arena, MESSAGE_BYTES)
    tree_status := run_compiler(directory, "dump-ast playground.mu", "ast.txt", "ast.err")
    if tree_status == RUN_FAILED ->
        at := put_text(message, 0, "Cannot find the compiler at ")
        at = put_bytes(message, at, directory)
        at = put_text(message, at, compiler_location(directory))
        at = put_text(message, at, ". Build it first, see Getting Started.")
        return send_compile_failure(client, arena, message[0:at])
    assembly_status := run_compiler(directory, "dump-asm --target x64-win32 playground.mu", "asm.txt", "asm.err")
    wasm_status := run_compiler(directory, "build --target wasm playground.wasm playground.mu", "wasm.txt", "wasm.err")
    wat_status := run_compiler(directory, "build --target wat playground.wat playground.mu", "wat.txt", "wat.err")

    // The first target that did not build is the error, as the compiler says it.
    error_length := compiler_message(directory, wasm_status, "wasm.err", "wasm.txt", message)
    if error_length == 0 ->
        error_length = compiler_message(directory, wat_status, "wat.err", "wat.txt", message)
    is_built := wasm_status == 0 and wat_status == 0

    if not send_head(client, STATUS_OK, "OK", "application/json", -1) ->
        return STATUS_OK
    out := output_make(client, arena, OUTPUT_BYTES)

    emit_text(%out, "{\"error\":")
    if is_built ->
        emit_text(%out, "null")
    else ->
        emit_json_string(%out, message[0:error_length])

    emit_text(%out, ",\"ast\":")
    if walk_tree(%out, arena, directory, false) ->
        walk_tree(%out, arena, directory, true)
    else ->
        emit_text(%out, "null")

    emit_text(%out, ",\"wasm\":")
    // The module can be larger than the arena, so its memory is made for it, and given back when
    // the answer is sent.
    data := make([]Byte, work_file_size(directory, "playground.wasm"))
    defer destroy(data)
    count := read_work_file(directory, "playground.wasm", data)
    if is_built and count > 0 and count == length_of(data) ->
        emit_text(%out, "\"")
        emit_base64(%out, data[0:count])
        emit_text(%out, "\"")
    else ->
        emit_text(%out, "null")

    emit_text(%out, ",\"wat\":")
    if is_built ->
        emit_json_work_file(%out, arena, directory, "playground.wat")
    else ->
        emit_text(%out, "null")

    emit_text(%out, ",\"asm\":")
    if assembly_status == 0 ->
        emit_json_work_file(%out, arena, directory, "asm.txt")
    else ->
        emit_text(%out, "null")

    emit_text(%out, ",\"asm_error\":")
    if assembly_status == 0 ->
        emit_text(%out, "null")
    else ->
        error_length = compiler_message(directory, assembly_status, "asm.err", "asm.txt", message)
        emit_json_string(%out, message[0:error_length])
    emit_text(%out, "}")
    flush(%out)
    return STATUS_OK

// ---------------------------------------------------------------------------------------------
// Requests
// ---------------------------------------------------------------------------------------------

/// Splits the line of a request, `GET /target HTTP/1.1`, at its two spaces, in the method, the target
/// and the version, and says whether the line has them.
split_request_line :: proc(line: []Byte) ([]Byte, []Byte, []Byte, Bool) ->
    first := -1
    second := -1
    for index in 0..<length_of(line) ->
        if line[index] == ' ' ->
            if first < 0 ->
                first = index
            else if second < 0 ->
                second = index
    if first <= 0 or second <= first + 1 ->
        return line[0:0], line[0:0], line[0:0], false
    return line[0:first], line[first + 1:second], line[second + 1:], true

/// Answers the request that is in `request`, and gives the code of the answer.
answer :: proc(client: Int, arena: ^mu.Arena_Allocator, request: []Byte, directory: []Byte) Int ->
    line_end := 0
    for line_end < length_of(request) ->
        if request[line_end] == '\r' or request[line_end] == '\n' ->
            break
        line_end += 1
    if line_end == length_of(request) ->
        return send_error(client, STATUS_BAD_REQUEST, "Bad Request", true)

    line := request[0:line_end]
    method, target, version, is_line := split_request_line(line)
    if not is_line or not bytes_start_with(version, "HTTP/") ->
        return send_error(client, STATUS_BAD_REQUEST, "Bad Request", true)

    is_head := bytes_are(method, "HEAD")
    is_post := bytes_are(method, "POST")
    if not is_head and not is_post and not bytes_are(method, "GET") ->
        return send_error(client, STATUS_METHOD_NOT_ALLOWED, "Method Not Allowed", true)

    // What follows a `?` or a `#` is not part of the page.
    end := 0
    for end < length_of(target) and not byte_is(target, end, QUESTION) and not byte_is(target, end, HASH) ->
        end += 1
    target = target[0:end]
    with_body := not is_head

    // The only page that is posted to is the compiler of the playground.
    if is_post ->
        head_end := find_head_end(request)
        if bytes_are(target, "/compile") and head_end >= 0 ->
            return send_compile(client, arena, directory, request, head_end)
        return send_error(client, STATUS_METHOD_NOT_ALLOWED, "Method Not Allowed", true)

    if bytes_are(target, "/") ->
        return send_index(client, arena, directory, with_body)
    if bytes_are(target, "/logo.png") ->
        path := [PATH_BYTES]Byte { .. = 0 }
        path_length := make_path(path[:], directory, "/media/mialu-logo.png", path[0:0], "")
        return send_file(client, arena, path[0:path_length], "image/png", with_body)
    // The playground and the page of the internals of the compiler are files of the project, and so are
    // the programs that the playground offers as examples, which its page asks for.
    if bytes_are(target, "/tools/playground") ->
        return send_page(client, arena, directory, "/tests/wasm/", "playground.html", with_body)
    if bytes_are(target, "/tools/internals") ->
        return send_page(client, arena, directory, "/tests/wasm/", "internals.html", with_body)
    if bytes_are(target, "/api/internals") ->
        return send_internals(client, arena, directory, with_body)
    if bytes_start_with(target, "/test-code/wasm/") ->
        return send_static(client, arena, directory, "/test-code/wasm/", target[length_of("/test-code/wasm/"):], with_body)
    if bytes_start_with(target, "/tests/wasm/") ->
        return send_static(client, arena, directory, "/tests/wasm/", target[length_of("/tests/wasm/"):], with_body)
    if length_of(target) > 1 and target[0] == '/' and is_name(target[1:]) ->
        return send_document(client, arena, directory, target[1:], with_body)
    return send_error(client, STATUS_NOT_FOUND, "Not Found", with_body)

/// Reads the request of a client, answers it, and closes the connection. Everything that the answer
/// takes from the arena is given back after it.
serve_client :: proc(client: Int, arena: ^mu.Arena_Allocator, directory: []Byte) ->
    defer mu.arena_reset(arena)
    // A client that does not send its request is cut off, so it does not hold the server. The
    // timeout is a `DWORD` of 4 bytes, the lowest first. MISSING: the sizes of integers in memory.
    timeout := [4]Byte { .. = 0 }
    timeout[0] = int_to_byte(RECEIVE_TIMEOUT_MILLISECONDS - RECEIVE_TIMEOUT_MILLISECONDS / 256 * 256)
    timeout[1] = int_to_byte(RECEIVE_TIMEOUT_MILLISECONDS / 256)
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, %timeout[0], 4)

    request, _ := mu.arena_allocate(arena, REQUEST_BYTES)
    count := receive_head(client, request)
    if count > 0 ->
        code := answer(client, arena, request[0:count], directory)
        log_request(request[0:count], code)

    shutdown(client, SD_SEND)
    closesocket(client)

/// Prints the code and the request line, such as `200 GET /mialu-cli HTTP/1.1`.
log_request :: proc(request: []Byte, code: Int) ->
    line_end := 0
    for line_end < length_of(request) ->
        if request[line_end] == '\r' or request[line_end] == '\n' ->
            break
        line_end += 1
    line, is_text := utf8_decode(request[0:line_end])
    printf("%lld ", code)
    if is_text ->
        puts(str_to_c_str(line))
    else ->
        puts("(not text)")
    // The output is a pipe or a file when it is redirected, which the C library buffers.
    fflush(nil)

// ---------------------------------------------------------------------------------------------
// The program
// ---------------------------------------------------------------------------------------------

/// Gives where the word of `text` that starts at or after `from` starts and ends, which are the same
/// when there is none. The words are separated by spaces.
next_word :: proc(text: []Byte, from: Int) (Int, Int) ->
    start := from
    for byte_is(text, start, SPACE) ->
        start += 1
    end := start
    for end < length_of(text) and not byte_is(text, end, SPACE) ->
        end += 1
    return start, end

/// Reads the four numbers of an address such as `0.0.0.0` into `host`, and says whether it is one.
read_host :: proc(text: []Byte, host: []Byte) Bool ->
    start := 0
    for index in 0..<4 ->
        end := find_byte(text, start, DOT)
        if end < 0 or index == 3 ->
            end = length_of(text)
        if index < 3 and end == length_of(text) ->
            return false
        number, is_number := try_str_to_int(utf8_decode_or_empty(text[start:end]))
        if not is_number or number < 0 or number > 255 ->
            return false
        host[index] = int_to_byte(number)
        start = end + 1
    return true

/// Reads the port, the directory and the address from the arguments of the program, which are words
/// that are separated by spaces, as `process_arguments_start` leaves them, and gives the port and
/// the length of the directory, and whether they are valid. MISSING: arguments of `main`.
read_arguments :: proc(directory: []Byte, host: []Byte) (Int, Int, Bool) ->
    command_line := [512]Byte { .. = 0 }
    length := mu.process_command_line(command_line[:])
    start := mu.process_arguments_start(command_line[0:length])
    arguments := command_line[start:length]

    host[0] = 127
    host[3] = 1
    port := DEFAULT_PORT
    directory_length := put_text(directory, 0, DEFAULT_DIRECTORY)

    port_start, port_end := next_word(arguments, 0)
    if port_end > port_start ->
        text, is_text := utf8_decode(arguments[port_start:port_end])
        number, is_number := try_str_to_int(text)
        if not is_text or not is_number or number < 1 or number > 65535 ->
            return 0, 0, false
        port = number

    directory_start, directory_end := next_word(arguments, port_end)
    if directory_end > directory_start ->
        directory_length = put_bytes(directory, 0, arguments[directory_start:directory_end])
        // The slash that ends a directory is not needed, as the paths put one after it.
        if directory_length > 1 and directory[directory_length - 1] == '/' ->
            directory_length -= 1

    host_start, host_end := next_word(arguments, directory_end)
    if host_end > host_start ->
        if not read_host(arguments[host_start:host_end], host) ->
            return 0, 0, false
    return port, directory_length, true

main :: proc() Int ->
    directory := [DIRECTORY_BYTES]Byte { .. = 0 }
    host := [4]Byte { .. = 0 }
    port, directory_length, is_valid := read_arguments(directory[:], host[:])
    if not is_valid ->
        puts("usage: mialudoc [port [directory [address]]]")
        return 2

    listener := listen_on(port, host[:])
    if listener == INVALID_SOCKET ->
        return 1

    printf("mialudoc: http://localhost:%lld/ shows the project at ", port)
    name, is_text := utf8_decode(directory[0:directory_length])
    if is_text ->
        puts(str_to_c_str(name))
    fflush(nil)

    // The memory of the answers is made once, and each answer takes what it needs from it, as an
    // arena, which is given back all at once after the answer.
    memory := make([]Byte, REQUEST_ARENA_BYTES)
    defer destroy(memory)
    arena := mu.Arena_Allocator { memory = memory, used = 0 }

    for ->
        client := accept(listener, nil, nil)
        if client == INVALID_SOCKET ->
            continue
        serve_client(client, %arena, directory[0:directory_length])
    return 0
