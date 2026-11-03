#include <assert.h>

#include <mialu/array_slice.h>

Array_Slice array_slice_make(void *elements, size_t length) {
    Array_Slice slice = {
        .elements = elements,
        .length = length,
    };

    assert(slice.elements == elements);
    assert(slice.length == length);
    return slice;
}
