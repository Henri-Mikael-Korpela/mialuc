package errors

Color :: enum Int ->
    RED

main :: proc() Int ->
    c := Color.RED
    match #(loose) c ->
        case .RED ->
            c = Color.RED
    return 0
