package errors

main :: proc() Int ->
    :outer ->
        :outer ->
            break outer
    return 0
