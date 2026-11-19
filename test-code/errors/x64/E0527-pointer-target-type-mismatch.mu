package errors

main :: proc() Int ->
    x := 3
    p := %x
    p^ = "a"
    return 0
