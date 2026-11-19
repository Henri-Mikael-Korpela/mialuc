package errors

import foreign js "functions"

foreign js ->
    alert :: proc(msg: Str) -> ...

main :: proc() Int ->
    alert("a")
    return 0
