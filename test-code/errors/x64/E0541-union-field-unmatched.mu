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
    t := Token { kind = .INTEGER_LITERAL, integer_literal = { value = 1 } }
    return t.integer_literal.value
    return 0
