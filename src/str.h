#ifndef MIALU_STR_H
#define MIALU_STR_H

#include <stddef.h>

#include <mialu/static_assert.h>

/**
 * A length-based view into text it does not own, so that no terminator is needed.
 */
typedef struct {
    char const *bytes;
    size_t length;
} Str;

// The size assumes 64-bit pointers, which are the only targets this project builds for.
STATIC_ASSERT(sizeof(Str) == 16, str_size_is_16_bytes);

Str str_make(char const *bytes, size_t length);

#endif
