import foreign js "functions"

foreign js ->
    alert :: proc(msg: Str) -> ...

print_hello :: proc() ->
    alert("Hello, world!")