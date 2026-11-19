package errors

Color :: enum Int ->
    RED
    GREEN

main :: proc() Int ->
    c := Color.RED
    match c ->
        case .RED ->
            c = Color.GREEN
    return 0
