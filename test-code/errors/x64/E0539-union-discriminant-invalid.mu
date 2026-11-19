package errors

Token :: struct ->
    kind: Int
    union match kind ->
        case .A ->
            value: Int

main :: proc() Int ->
    return 0
