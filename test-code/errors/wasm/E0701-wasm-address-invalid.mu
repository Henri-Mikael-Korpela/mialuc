package errors

Point :: struct ->
    x: Int
    y: Int

f :: proc(p: Point) ->
    q := %p.x

main :: proc() Int ->
    return 0
