import "mu/libc"

main :: proc() Int ->
    libc.printf("Hello, world!\n")
    return 0