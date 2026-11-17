// A match takes any integer as its subject: an `Int`, a `Byte`, an `I32`, an `I64`, a `U32` or a
// `U64`, with patterns that are constants of its type, ranges, and cases that share a block.
// `error_from` gives the number of the error, because an enum is not passed to JavaScript.

package match_sized_integers

File_Error :: enum Int ->
    NONE
    NOT_FOUND
    ACCESS_DENIED
    OTHER

ERROR_FILE_NOT_FOUND : U32 : 2
ERROR_PATH_NOT_FOUND : U32 : 3
ERROR_ACCESS_DENIED : U32 : 5
LARGE : U32 : 3000000000
TOP : U64 : 9223372036854775807

error_from :: proc(code: U32) File_Error ->
    match code ->
        case ERROR_FILE_NOT_FOUND ->
        case ERROR_PATH_NOT_FOUND ->
            return .NOT_FOUND
        case ERROR_ACCESS_DENIED ->
            return .ACCESS_DENIED
        case ->
            return .OTHER

/// The number of the error, which is the order of its member.
error_number :: proc(code: U32) Int ->
    error := error_from(code)
    if error == File_Error.NOT_FOUND ->
        return 1
    if error == File_Error.ACCESS_DENIED ->
        return 2
    return 3

size_class :: proc(size: U32) Int ->
    match size ->
        case 0 ->
            return 0
        case 1..<4096 ->
            return 1
        case 4096..<=LARGE ->
            return 2
        case ->
            return 3

sign_class :: proc(n: I32) Int ->
    match n ->
        case -1 ->
            return 10
        case -100..<-1 ->
            return 11
        case 0 ->
            return 12
        case 1..<=100 ->
            return 13
        case ->
            return 14

wide_class :: proc(n: U64) Int ->
    match n ->
        case 0 ->
            return 0
        case 1..<=TOP ->
            return 1
        case ->
            return 2

signed_wide_class :: proc(n: I64) Int ->
    match n ->
        case -5_000_000_000 ->
            return 1
        case 5_000_000_000 ->
            return 2
        case ->
            return 0

large_size :: proc() Int ->
    big : U32 = 4000000000
    return size_class(big)

above_top :: proc() Int ->
    top : U64 = TOP
    above := top + 1
    return wide_class(above)
