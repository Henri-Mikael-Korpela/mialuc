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
        case .INTEGER_LITERAL ->
            integer_literal: struct ->
                value: Int

main :: proc() Int ->
    k := Token_Kind.IDENTIFIER
    t := Token { kind = k, identifier = { text = "a" } }
    return 0
