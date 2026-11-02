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

/**
 * An empty `Str` whose `bytes` is NULL. It is how a function reports "no text", which is different
 * from an empty file, whose `Str` has a non-NULL `bytes` and a `length` of zero. This is a macro
 * because a compound literal cannot be a `static const` initializer in C99; it has no side effects
 * and uses no arguments.
 */
#define STR_EMPTY ((Str){.bytes = NULL, .length = 0})

/**
 * Makes a `Str` over `length` bytes at `bytes`. `bytes` may be NULL only when `length` is zero.
 */
Str str_make(char const *bytes, size_t length);

#endif
