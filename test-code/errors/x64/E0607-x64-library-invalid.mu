package errors

import foreign kernel32 "Kernel32.lib"

foreign kernel32 ->
    GetTickCount :: proc() Int -> ...

main :: proc() Int ->
    x := GetTickCount()
    return 0
