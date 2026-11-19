package errors

Token_Kind :: enum Int ->
    IDENTIFIER
    INTEGER_LITERAL

Token :: struct ->
    kind: Token_Kind
    union match kind ->
        case .IDENTIFIER ->
            identifier: struct ->
                text: Str
        case .NOPE ->
            integer_literal: struct ->
                value: Int

main :: proc() Int ->
    return 0
