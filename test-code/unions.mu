Token_Kind :: enum Int ->
    IDENTIFIER
    INTEGER_LITERAL
    OPERATOR_ADD

Token :: struct ->
    kind: Token_Kind
    union match kind ->
        case .IDENTIFIER ->
            identifier: struct ->
                text: Str
        case .INTEGER_LITERAL ->
            integer_literal: struct ->
                value: Int

test_tokens :: proc() Token ->
    tokens := [const]Token {
        Token {
            kind = .IDENTIFIER,
            identifier = {
                text = "x",
            },
        },
        Token {
            kind = .OPERATOR_ADD,
        },
        Token {
            kind = .INTEGER_LITERAL,
            integer_literal = {
                value = 16,
            },
        },
    }
    return tokens[0]

first_token_text :: proc() Str ->
    tokens := [const]Token {
        Token {
            kind = .IDENTIFIER,
            identifier = {
                text = "x",
            },
        },
    }
    token := tokens[0]
    if token.kind == .IDENTIFIER ->
        return token.identifier.text
    return ""

value_of_integer_token :: proc() Int ->
    token := Token {
        kind = .INTEGER_LITERAL,
        integer_literal = { value = 16 },
    }
    if token.kind == .INTEGER_LITERAL ->
        return token.integer_literal.value
    return 0

value_of_token :: proc(token: Token) Int ->
    if token.kind == .INTEGER_LITERAL ->
        return token.integer_literal.value
    else if token.kind == .IDENTIFIER ->
        return length_of_text(token.identifier.text)
    return 99

length_of_text :: proc(text: Str) Int ->
    return 5

pass_token_to_proc :: proc() Int ->
    a := value_of_token(Token { kind = .INTEGER_LITERAL, integer_literal = { value = 7 } })
    b := value_of_token(Token { kind = .IDENTIFIER, identifier = { text = "abc" } })
    c := value_of_token(Token { kind = .OPERATOR_ADD })
    return a * 100 + b * 10 + c

assign_field_of_matched_union :: proc() Int ->
    token := Token { kind = .INTEGER_LITERAL, integer_literal = { value = 1 } }
    if token.kind == .INTEGER_LITERAL ->
        token.integer_literal.value += 41
        return token.integer_literal.value
    return 0

change_the_kind_by_assigning_an_object :: proc() Int ->
    token := Token { kind = .IDENTIFIER, identifier = { text = "x" } }
    token = Token { kind = .INTEGER_LITERAL, integer_literal = { value = 9 } }
    if token.kind == .INTEGER_LITERAL ->
        return token.integer_literal.value
    return 0

kind_and_field_in_one_condition :: proc(token: Token) Bool ->
    return token.kind == .INTEGER_LITERAL and token.integer_literal.value == 3

omitted_kind_is_the_first_member :: proc() Int ->
    token := Token { identifier = { text = "first" } }
    if token.kind == .IDENTIFIER ->
        return 1
    return 0

through_a_pointer :: proc(token: ^Token) Int ->
    if token.kind == .INTEGER_LITERAL ->
        return token.integer_literal.value
    return 0

use_pointer_to_token :: proc() Int ->
    token := Token { kind = .INTEGER_LITERAL, integer_literal = { value = 12 } }
    return through_a_pointer(%token)
