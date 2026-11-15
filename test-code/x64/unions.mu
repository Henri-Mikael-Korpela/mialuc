// Unions, which are groups of fields of a struct with a field of an enum that says which group is
// used. A field of a case is read and assigned only where an if statement, or an `and` after a
// comparison, shows that the object is in the case, which the compiler checks. The exit code of
// the program is the number of wrong results, 0.

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

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

check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

token_value :: proc(token: Token) Int ->
    if token.kind == .INTEGER_LITERAL ->
        return token.integer_literal.value
    else if token.kind == .IDENTIFIER ->
        return 1
    return 0

/// A field of a case is read in an `and` after the comparison that shows the case.
is_big_number :: proc(token: ^Token) Bool ->
    return token.kind == .INTEGER_LITERAL and token.integer_literal.value > 100

/// A field of a case is assigned in the block of the if statement that shows the case.
double_number :: proc(token: ^Token) ->
    if token.kind == Token_Kind.INTEGER_LITERAL ->
        token.integer_literal.value *= 2

main :: proc() Int ->
    wrong := 0
    number := Token { kind = .INTEGER_LITERAL, integer_literal = { value = 42 } }
    name := Token { kind = .IDENTIFIER, identifier = { text = "count" } }
    plus := Token { kind = .OPERATOR_ADD }
    wrong += check("token_value(number) is %lld", token_value(number), 42)
    wrong += check("token_value(name) is %lld", token_value(name), 1)
    wrong += check("token_value(plus) is %lld", token_value(plus), 0)
    if is_big_number(%number) ->
        wrong += 1
    double_number(%number)
    double_number(%number)
    wrong += check("after doubling twice it is %lld", token_value(number), 168)
    if is_big_number(%number) == false ->
        wrong += 1
    if name.kind == .IDENTIFIER ->
        puts(str_to_c_str(name.identifier.text))
    return wrong
