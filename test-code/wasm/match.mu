/// A match statement compares one value, its subject, with the pattern of each case below it, and
/// runs the block of the first case that matches. `case ->` is the default case, which runs when
/// no other case does.
match_with_integers :: proc() Str ->
    x := 5
    match x ->
        case 5 ->
            return "Match"
        case ->
            return "No match"

match_with_strings :: proc() Str ->
    command := "print"
    match command ->
        case "print" ->
            return "Executing print"
        case "run" ->
            return "Executing run"
        case ->
            return "Unsupported command"

match_with_ranges :: proc() Str ->
    x := 10
    match x ->
        case 0..<=5 ->
            return "Between 0 and 5"
        case 6..<=10 ->
            return "Between 6 and 10"
        case ->
            return "Some other range"

Color :: enum Int ->
    RED
    GREEN
    BLUE

/// A match of an enum must have a case for every member, so it needs no default case.
match_with_enum :: proc(color: Color) Str ->
    match color ->
        case .RED ->
            return "red"
        case .GREEN ->
            return "green"
        case .BLUE ->
            return "blue"

/// `#(partial)` lets a match of an enum leave members out. Then nothing runs for them.
match_partially_with_enum :: proc(color: Color) Str ->
    match #(partial) color ->
        case .RED ->
            return "red"
        case .GREEN ->
            return "green"
    return "unsupported"

Color_With_Alpha :: enum Int ->
    RED
    GREEN
    BLUE
    ALPHA

/// A case without a block runs the block of the next case, so several cases share one block.
is_alpha :: proc(color: Color_With_Alpha) Bool ->
    match color ->
        case .RED   ->
        case .GREEN ->
        case .BLUE  ->
            return false
        case .ALPHA ->
            return true

/// The member can be written with the name of its enum as well.
match_with_qualified_members :: proc(color: Color) Int ->
    match color ->
        case Color.RED ->
            return 1
        case Color.GREEN ->
            return 2
        case Color.BLUE ->
            return 3

LIMIT :: 100
SMALL : Int : 10

/// A pattern is anything known when the program is compiled: a literal, a constant, or arithmetic
/// of constants, such as `0 - 1`, which is how a negative number is written. A range is
/// `first..<end` without its end, or `first..<=end` with it.
classify :: proc(n: Int) Str ->
    match n ->
        case 0 - 1 ->
            return "minus one"
        case 0 ->
            return "zero"
        case 1..<SMALL ->
            return "small"
        case SMALL..<LIMIT ->
            return "medium"
        case LIMIT ->
            return "limit"
        case LIMIT + 1..<=LIMIT * 2 ->
            return "large"
        case ->
            return "other"

/// Values and ranges can share a block.
is_weekend_or_holiday :: proc(day: Int) Bool ->
    match day ->
        case 6..<=7 ->
        case 25 ->
            return true
        case ->
            return false

/// Strings are equal when they have the same bytes, so a string of another length, or of the same
/// length with another byte, does not match. The empty string is a pattern like any other.
command_number :: proc(command: Str) Int ->
    match command ->
        case "" ->
            return 0
        case "go" ->
            return 1
        case "stop" ->
        case "halt" ->
            return 2
        case "gone" ->
            return 3
        case ->
            return 0 - 1

parse_commands :: proc() Int ->
    total := 0
    total += command_number("go") * 1
    total += command_number("stop") * 10
    total += command_number("halt") * 100
    total += command_number("gone") * 1000
    total += command_number("") * 10000
    total += command_number("g") * 100000
    return total

/// A Byte is matched with numbers from 0 to 255.
byte_kind :: proc(b: Byte) Int ->
    match b ->
        case 0 ->
            return 0
        case 48..<=57 ->
            return 1
        case 65..<=90 ->
        case 97..<=122 ->
            return 2
        case 255 ->
            return 3
        case ->
            return 4

byte_kinds :: proc() Int ->
    zero : Byte = 0
    digit : Byte = 55
    upper : Byte = 70
    lower : Byte = 100
    full : Byte = 255
    space : Byte = 32
    total := byte_kind(zero) + byte_kind(digit) * 10 + byte_kind(upper) * 100
    total += byte_kind(lower) * 1000 + byte_kind(full) * 10000 + byte_kind(space) * 100000
    return total

/// Without a default case a match of a number can run no case at all, and the statements after
/// it run next, as after an if statement.
no_case_runs :: proc(n: Int) Int ->
    result := 1
    match n ->
        case 1 ->
            result = 10
        case 2 ->
            result = 20
    return result

calls #(static) := 0

next_number :: proc() Int ->
    calls += 1
    return calls

/// The subject is computed once, before the first case, so a call in it runs once however many
/// cases there are.
subject_is_computed_once :: proc() Int ->
    calls = 0
    match next_number() ->
        case 5 ->
            return 500
        case 4 ->
            return 400
        case 1 ->
            return calls * 100
        case ->
            return 0

/// A block of a case is a scope, so two cases can define variables of one name. In a loop,
/// `break` and `continue` in a case leave the loop or go on with it, because a match is not a loop.
sum_until_stop :: proc() Int ->
    total := 0
    for i in 0..<100 ->
        match i - i / 3 * 3 ->
            case 0 ->
                extra := i
                total += extra
            case 1 ->
                continue
            case ->
                extra := 0
                total += extra
        if i >= 9 ->
            break
    return total

/// Matches can be inside each other.
nested_match :: proc(color: Color, n: Int) Int ->
    match color ->
        case .RED ->
            match n ->
                case 0 ->
                    return 10
                case ->
                    return 11
        case .GREEN ->
        case .BLUE ->
            return 20

trace #(static) := 0

note :: proc(digit: Int) ->
    trace = trace * 10 + digit

/// A case is a block, so a `defer` in it runs when the case ends.
defer_in_case :: proc(n: Int) Int ->
    trace = 0
    match n ->
        case 1 ->
            defer note(1)
            note(2)
        case ->
            note(3)
    note(4)
    return trace

/// A match statement can be deferred, like an if statement.
note_deferred_match :: proc(color: Color) ->
    defer match color ->
        case .RED ->
            note(1)
        case .GREEN ->
            note(2)
        case .BLUE ->
            note(3)
    note(9)

deferred_match :: proc(color: Color) Int ->
    trace = 0
    note_deferred_match(color)
    return trace
