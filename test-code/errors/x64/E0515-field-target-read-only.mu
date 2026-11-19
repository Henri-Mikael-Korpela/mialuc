package errors

Point :: struct ->
    x: Int
    y: Int

move :: proc(p: Point) ->
    p.x = 3

main :: proc() Int ->
    return 0
