#ifndef MIALU_ARRAY_SLICE_H
#define MIALU_ARRAY_SLICE_H

#include <mialu/static_assert.h>

/**
 * A view of `length` consecutive elements starting at `elements`. It does not own the memory, and
 * does not know the element size, because C99 has no generics; the code that creates the slice and
 * the code that reads it must agree on the element type. `length` counts elements, not bytes.
 */
typedef struct {
    void *elements;
    size_t length;
} Array_Slice;

// The size assumes 64-bit pointers, which are the only targets this project builds for.
STATIC_ASSERT(sizeof(Array_Slice) == 16, array_slice_size_is_16_bytes);

#define STR_EMPTY str_make(NULL, 0)

#endif
