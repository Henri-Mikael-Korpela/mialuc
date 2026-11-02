#include <assert.h>
#include <string.h>

#include <mialu/str.h>

Str str_make(char const *bytes, size_t length) {
    assert(bytes != NULL || length == 0);

    Str str = {
        .bytes = bytes,
        .length = length,
    };

    assert(str.length == length);
    return str;
}

bool str_equals(Str left, Str right) {
    assert(left.bytes != NULL || left.length == 0);
    assert(right.bytes != NULL || right.length == 0);

    // Lengths are compared first, which also keeps `memcmp` from reading past either string.
    if (left.length == right.length) {
        return left.length == 0 || memcmp(left.bytes, right.bytes, left.length) == 0;
    }
    return false;
}

Str_Position str_position_at(Str source, size_t offset) {
    assert(source.bytes != NULL || source.length == 0);
    assert(offset <= source.length);

    Str_Position position = {.line = 1, .column = 1};
    for (size_t i = 0; i < offset; i += 1) {
        if (source.bytes[i] == '\n') {
            position.line += 1;
            position.column = 1;
        } else {
            position.column += 1;
        }
    }

    return position;
}
