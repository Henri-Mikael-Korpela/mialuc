#include <mialu/str.h>

Str str_make(char const *bytes, size_t length) {
    return (Str){
        .bytes = bytes,
        .length = length,
    };
}
