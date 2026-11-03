greeting :: "Hello, world"

echo :: proc(text: Str) Str ->
    return text

second :: proc(count: Int, text: Str, other: Str) Str ->
    return other

empty :: proc() Str ->
    return ""

non_ascii :: "héllo, 日本語, 😀"
