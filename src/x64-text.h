#ifndef MIALU_X64_TEXT_H
#define MIALU_X64_TEXT_H

#include <stddef.h>

#include <mialu/array_slice.h>
#include <mialu/str.h>

// Alphabetical, because nothing depends on the order.
typedef enum {
    // The file breaks the format, for example a header or the code ends in the middle of an
    // instruction.
    X64_TEXT_STATUS_EXECUTABLE_MALFORMED,
    // The file may be valid, but has an instruction that the formatter does not know.
    X64_TEXT_STATUS_EXECUTABLE_UNSUPPORTED,
    X64_TEXT_STATUS_OK,
    X64_TEXT_STATUS_OUTPUT_TOO_LARGE,
} X64_Text_Status;

/**
 * On `X64_TEXT_STATUS_OK`, `text` is the filled start of the output buffer. Otherwise `text` is
 * empty, and `error_offset` is the offset in the file of the byte that could not be formatted,
 * except for `X64_TEXT_STATUS_OUTPUT_TOO_LARGE`, which is not about any place in the file and has
 * `error_offset` zero.
 */
typedef struct {
    Str text;
    size_t error_offset;
    X64_Text_Status status;
} X64_Text_Result;

/**
 * Formats the code of the executable file in `executable`, which holds `byte` elements and is what
 * `x64_build` wrote, as assembly in the format of the inline assembly of the Odin programming
 * language: one instruction on a line, the operand that is written to first, registers with a `%`
 * in front, memory operands as `[base + displacement]`, labels as `.name:` and comments after
 * `//`. `symbols` holds the `X64_Symbol` elements that `x64_build` returned, which name the labels
 * of the procedures. The text is written to `output_buffer`, which holds `byte` elements.
 *
 * The formatter knows the instructions that `x64_build` writes, so it is updated together with it.
 * It is a reader of what the compiler wrote, not a disassembler for any program: it rejects what
 * it does not know.
 */
X64_Text_Result x64_text_format(Array_Slice executable, Array_Slice symbols,
                                Array_Slice output_buffer);

#endif
