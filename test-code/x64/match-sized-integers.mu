// A match takes any integer as its subject: an `Int`, a `Byte`, an `I32`, an `I64`, a `U32` or a
// `U64`, with patterns that are constants of its type, ranges, and cases that share a block. The
// exit code of the program is the number of wrong results, 0.

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

/// 1 when the error is the expected one.
is_error :: proc(code: U32, expected: File_Error) Int ->
    if error_from(code) == expected ->
        return 1
    return 0

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

main :: proc() Int ->
    wrong := 0
    wrong += 1 - is_error(2, File_Error.NOT_FOUND)
    wrong += 1 - is_error(3, File_Error.NOT_FOUND)
    wrong += 1 - is_error(5, File_Error.ACCESS_DENIED)
    wrong += 1 - is_error(4, File_Error.OTHER)

    big : U32 = 4000000000
    wrong += size_class(0) - 0
    wrong += size_class(100) - 1
    wrong += size_class(4096) - 2
    wrong += size_class(LARGE) - 2
    wrong += size_class(big) - 3

    wrong += sign_class(-1) - 10
    wrong += sign_class(-50) - 11
    wrong += sign_class(0) - 12
    wrong += sign_class(100) - 13
    wrong += sign_class(101) - 14
    wrong += sign_class(-101) - 14

    zero : U64 = 0
    top : U64 = TOP
    above := top + 1
    wrong += wide_class(zero) - 0
    wrong += wide_class(top) - 1
    wrong += wide_class(above) - 2
    wrong += wide_class(zero - 1) - 2

    wrong += signed_wide_class(-5000000000) - 1
    wrong += signed_wide_class(5000000000) - 2
    wrong += signed_wide_class(5) - 0
    return wrong
