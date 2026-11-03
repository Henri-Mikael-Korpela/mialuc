#ifndef MIALU_WASM_TEXT_H
#define MIALU_WASM_TEXT_H

#include <stddef.h>

#include <mialu/array_slice.h>
#include <mialu/str.h>

// Alphabetical, because nothing depends on the order.
typedef enum {
    // The module breaks the binary format, for example it ends in the middle of a section.
    WASM_TEXT_STATUS_MODULE_MALFORMED,
    // The module may be valid, but uses something that the formatter does not know.
    WASM_TEXT_STATUS_MODULE_UNSUPPORTED,
    WASM_TEXT_STATUS_OK,
    WASM_TEXT_STATUS_OUTPUT_TOO_LARGE,
} Wasm_Text_Status;

/**
 * On `WASM_TEXT_STATUS_OK`, `text` is the filled start of the output buffer. Otherwise `text` is
 * empty, and `error_offset` is the offset in the module of the byte that could not be formatted,
 * except for `WASM_TEXT_STATUS_OUTPUT_TOO_LARGE`, which is not about any place in the module and
 * has `error_offset` zero.
 */
typedef struct {
    Str text;
    size_t error_offset;
    Wasm_Text_Status status;
} Wasm_Text_Result;

/**
 * Formats the WebAssembly binary module in `module`, which holds `byte` elements, in the
 * WebAssembly text format, so that a person can read it. The text is written to `output_buffer`,
 * which holds `byte` elements.
 *
 * The formatter knows the parts of the format that `wasm_build` writes, so it is updated together
 * with it. It is not a validator: it rejects what it cannot format, but a module that it formats
 * is not checked to be valid.
 */
Wasm_Text_Result wasm_text_format(Array_Slice module, Array_Slice output_buffer);

#endif
