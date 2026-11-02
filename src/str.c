#include <assert.h>

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
