#include <stddef.h>
#include <stdint.h>

typedef struct {
    char const* data;
    size_t length;
} Str;

// A string literal as a length-based view of its bytes. `sizeof` counts the terminating zero byte
// of the C literal, which a `Str` does not have. The source file must be UTF-8. The braces alone
// are a constant initializer, so they work for a global too; `STR` makes a value inside a function.
#define STR_INIT(literal) {.data = (literal), .length = sizeof(literal) - 1}
#define STR(literal) ((Str)STR_INIT(literal))

Str const greeting = STR_INIT("Hello, world");

Str echo(Str text) {
    return text;
}

Str second(int64_t count, Str text, Str other) {
    (void)count;
    (void)text;
    return other;
}

Str empty() {
    return STR("");
}

// 23 bytes: "héllo, " is 8, "日本語" is 9, ", " is 2 and "😀" is 4.
Str const non_ascii = STR_INIT("héllo, 日本語, 😀");
