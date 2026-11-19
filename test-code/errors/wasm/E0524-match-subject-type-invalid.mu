package errors

Point :: struct ->
    x: Int
    y: Int

main :: proc() Int ->
    p := Point { x = 1, y = 2 }
    match p ->
        case ->
            return 1
    return 0
