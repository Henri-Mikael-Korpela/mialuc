#ifndef MIALU_WASM_H
#define MIALU_WASM_H

#include <stddef.h>

#include <mialu/array_slice.h>
#include <mialu/ast.h>
#include <mialu/common_types.h>
#include <mialu/semantic.h>
#include <mialu/str.h>

// Constants of the WebAssembly binary format, see https://webassembly.github.io/spec/core/binary/.
// They are shared by the writer in wasm.c and the reader in wasm-text.c, so that the two cannot
// disagree. They are an `enum` rather than `static const`, because a file that uses only some of
// them would get warnings about the rest.
enum {
    // The flag of a data segment that is copied into memory 0 when the module is instantiated, at
    // the address that a constant expression gives.
    WASM_DATA_SEGMENT_ACTIVE = 0x00,

    // The kinds of thing that an export makes visible.
    WASM_EXPORT_KIND_FUNCTION = 0x00,
    WASM_EXPORT_KIND_MEMORY = 0x02,

    // A 32-bit value takes 7 bits per byte, so at most 5 bytes in LEB128.
    WASM_LEB128_32_BYTES_MAX = 5,
    // A 64-bit value takes at most 10 bytes in LEB128.
    WASM_LEB128_64_BYTES_MAX = 10,

    // The flag of a memory that has a minimum size but no maximum.
    WASM_MEMORY_LIMITS_MIN_ONLY = 0x00,
    // A page of memory is 64 KiB.
    WASM_MEMORY_PAGE_SIZE_BYTES = 65536,

    // The block type of a structured instruction that takes and leaves no values.
    WASM_BLOCK_TYPE_EMPTY = 0x40,
    // Instructions.
    WASM_OPCODE_CALL = 0x10,
    WASM_OPCODE_DROP = 0x1A,
    WASM_OPCODE_ELSE = 0x05,
    WASM_OPCODE_END = 0x0B,
    WASM_OPCODE_I32_CONST = 0x41,
    WASM_OPCODE_I32_AND = 0x71,
    WASM_OPCODE_I32_EQ = 0x46,
    WASM_OPCODE_I32_EQZ = 0x45,
    WASM_OPCODE_I32_NE = 0x47,
    WASM_OPCODE_I32_OR = 0x72,
    WASM_OPCODE_I64_ADD = 0x7C,
    WASM_OPCODE_I64_CONST = 0x42,
    WASM_OPCODE_I64_DIV_S = 0x7F,
    WASM_OPCODE_I64_EQ = 0x51,
    WASM_OPCODE_I64_GE_S = 0x59,
    WASM_OPCODE_I64_GT_S = 0x55,
    WASM_OPCODE_I64_LE_S = 0x57,
    WASM_OPCODE_I64_LT_S = 0x53,
    WASM_OPCODE_I64_MUL = 0x7E,
    WASM_OPCODE_I64_NE = 0x52,
    WASM_OPCODE_I64_SUB = 0x7D,
    WASM_OPCODE_IF = 0x04,
    WASM_OPCODE_LOCAL_GET = 0x20,
    WASM_OPCODE_LOCAL_SET = 0x21,
    WASM_OPCODE_RETURN = 0x0F,
    WASM_OPCODE_UNREACHABLE = 0x00,

    // The ids of the kinds of section. Custom sections are never written, but may be read.
    WASM_SECTION_ID_CODE = 10,
    WASM_SECTION_ID_CUSTOM = 0,
    WASM_SECTION_ID_DATA = 11,
    WASM_SECTION_ID_EXPORT = 7,
    WASM_SECTION_ID_FUNCTION = 3,
    WASM_SECTION_ID_MEMORY = 5,
    WASM_SECTION_ID_TYPE = 1,

    // The marker of a function type, and the types of a 32-bit and a 64-bit integer.
    WASM_TYPE_FUNCTION = 0x60,
    WASM_TYPE_I32 = 0x7F,
    WASM_TYPE_I64 = 0x7E,
};

/**
 * The first eight bytes of every module: "\0asm", and version 1 as a 32-bit little-endian number.
 */
extern byte const WASM_MODULE_HEADER[8];

// Alphabetical, because nothing depends on the order.
typedef enum {
    WASM_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID,
    WASM_BUILD_STATUS_CALL_ARGUMENT_COUNT_MISMATCH,
    WASM_BUILD_STATUS_CALL_ARGUMENT_TYPE_MISMATCH,
    WASM_BUILD_STATUS_CALL_RESULT_MISSING,
    WASM_BUILD_STATUS_COMPARISON_OPERAND_TYPE_INVALID,
    WASM_BUILD_STATUS_CONDITION_TYPE_INVALID,
    WASM_BUILD_STATUS_DEFINITION_NAME_RESERVED,
    WASM_BUILD_STATUS_EXPRESSION_TOO_DEEP,
    WASM_BUILD_STATUS_IDENTIFIER_NOT_VALUE,
    WASM_BUILD_STATUS_LOGICAL_OPERAND_TYPE_INVALID,
    WASM_BUILD_STATUS_OK,
    WASM_BUILD_STATUS_OUTPUT_TOO_LARGE,
    WASM_BUILD_STATUS_RETURN_TYPE_MISMATCH,
    WASM_BUILD_STATUS_RETURN_TYPE_MISSING,
    WASM_BUILD_STATUS_TYPE_UNSUPPORTED,
    WASM_BUILD_STATUS_VARIABLE_TYPE_MISMATCH,
} Wasm_Build_Status;

/**
 * On `WASM_BUILD_STATUS_OK`, `bytes` is the filled start of the output buffer, as `byte`
 * elements, and it is a complete WebAssembly binary module. Otherwise `bytes.length` is zero and
 * `subject` is the text in the source that the error is about, as a view into the source that the
 * syntax tree was parsed from, so the caller can work out the position. `subject` has `bytes`
 * NULL for `WASM_BUILD_STATUS_OUTPUT_TOO_LARGE`, which is not about any place in the source.
 */
typedef struct {
    Array_Slice bytes;
    Str subject;
    Wasm_Build_Status status;
} Wasm_Build_Result;

/**
 * Builds a WebAssembly binary module from the syntax tree below `root`, which must be the
 * `AST_NODE_TYPE_MODULE` node of a successful `parse`. `nodes` holds `Ast_Node` elements, and
 * `bindings` holds the `Semantic_Binding` elements that `semantic_analyze` made for the same tree,
 * so every name in the tree is known to stand for a parameter or a constant. The module is written
 * to `output_buffer`, which holds `byte` elements.
 *
 * Every definition becomes a function that the module exports under the name of the definition.
 * A variable of a procedure is a local of its function, or two locals when it is a `Str`, after the
 * parameters. A definition whose value is a string literal becomes a function without parameters
 * that returns the string. A use of such a constant in a procedure is a call of that function, so the constant
 * can be used above its definition as well.
 *
 * `Int` is a 64-bit integer (`i64`), so callers from JavaScript pass and receive `BigInt`.
 * `Str` is two 32-bit integers (`i32`), the address of its first byte in memory and its length
 * in bytes, which are UTF-8. So a parameter of that type is two WebAssembly parameters, and a
 * result of that type is two WebAssembly results, which JavaScript receives as an array. The bytes
 * of the string literals are in the first bytes of the memory, which the module exports as
 * `memory`. The module has no memory, and so no export of that name, when the program has no
 * string literal. A definition cannot be called `memory`, because the name is taken.
 *
 * The syntax tree can be valid for the parser but not for the target, for example when it uses a
 * procedure as a value. Such trees are rejected with a status other than
 * `WASM_BUILD_STATUS_OK`.
 */
Wasm_Build_Result wasm_build(Array_Slice nodes, Array_Slice bindings, Ast_Node_Index root,
                             Array_Slice output_buffer);

#endif
