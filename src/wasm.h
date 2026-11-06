#ifndef MIALU_WASM_H
#define MIALU_WASM_H

#include <mialu/array_slice.h>
#include <mialu/ast.h>
#include <mialu/common_types.h>
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

    // The flag of an element segment that is copied into table 0 when the module is instantiated,
    // at the index that a constant expression gives, and has function indexes as its elements.
    WASM_ELEMENT_SEGMENT_ACTIVE = 0x00,

    // The kind of thing that an import brings in. Only functions are imported.
    WASM_IMPORT_KIND_FUNCTION = 0x00,

    // The flag of a global that its instructions can change, as opposed to 0x00 for one that is
    // fixed.
    WASM_GLOBAL_MUTABLE = 0x01,

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
    WASM_OPCODE_BLOCK = 0x02,
    WASM_OPCODE_BR = 0x0C,
    WASM_OPCODE_BR_IF = 0x0D,
    WASM_OPCODE_CALL = 0x10,
    WASM_OPCODE_CALL_INDIRECT = 0x11,
    WASM_OPCODE_DROP = 0x1A,
    WASM_OPCODE_ELSE = 0x05,
    WASM_OPCODE_END = 0x0B,
    WASM_OPCODE_GLOBAL_GET = 0x23,
    WASM_OPCODE_GLOBAL_SET = 0x24,
    WASM_OPCODE_I32_ADD = 0x6A,
    WASM_OPCODE_I32_CONST = 0x41,
    WASM_OPCODE_I32_AND = 0x71,
    WASM_OPCODE_I32_EQ = 0x46,
    WASM_OPCODE_I32_EQZ = 0x45,
    WASM_OPCODE_I32_GE_U = 0x4F,
    WASM_OPCODE_I32_LOAD = 0x28,
    WASM_OPCODE_I32_LT_S = 0x48,
    WASM_OPCODE_I32_MUL = 0x6C,
    WASM_OPCODE_I32_NE = 0x47,
    WASM_OPCODE_I32_OR = 0x72,
    WASM_OPCODE_I32_SHL = 0x74,
    WASM_OPCODE_I32_STORE = 0x36,
    WASM_OPCODE_I32_SUB = 0x6B,
    WASM_OPCODE_I32_WRAP_I64 = 0xA7,
    WASM_OPCODE_I64_ADD = 0x7C,
    WASM_OPCODE_I64_CONST = 0x42,
    WASM_OPCODE_I64_DIV_S = 0x7F,
    WASM_OPCODE_I64_EQ = 0x51,
    WASM_OPCODE_I64_EXTEND_I32_U = 0xAD,
    WASM_OPCODE_I64_GE_S = 0x59,
    WASM_OPCODE_I64_GT_S = 0x55,
    WASM_OPCODE_I64_GT_U = 0x56,
    WASM_OPCODE_I64_LE_S = 0x57,
    WASM_OPCODE_I64_LE_U = 0x58,
    WASM_OPCODE_I64_LOAD = 0x29,
    WASM_OPCODE_I64_LT_S = 0x53,
    WASM_OPCODE_I64_MUL = 0x7E,
    WASM_OPCODE_I64_NE = 0x52,
    WASM_OPCODE_I64_STORE = 0x37,
    WASM_OPCODE_I64_SUB = 0x7D,
    WASM_OPCODE_IF = 0x04,
    WASM_OPCODE_LOCAL_GET = 0x20,
    WASM_OPCODE_LOCAL_SET = 0x21,
    WASM_OPCODE_LOCAL_TEE = 0x22,
    WASM_OPCODE_LOOP = 0x03,
    // The prefix of the instructions that came after the first version of the format, and the
    // second byte of `memory.copy`, the only one of them that the compiler writes. Both of the
    // memory operands that follow it are memory 0.
    WASM_OPCODE_PREFIX_MISC = 0xFC,
    WASM_OPCODE_MISC_MEMORY_COPY = 0x0A,
    WASM_OPCODE_RETURN = 0x0F,
    WASM_OPCODE_UNREACHABLE = 0x00,

    // The ids of the kinds of section. Custom sections are never written, but may be read.
    WASM_SECTION_ID_CODE = 10,
    WASM_SECTION_ID_CUSTOM = 0,
    WASM_SECTION_ID_DATA = 11,
    WASM_SECTION_ID_ELEMENT = 9,
    WASM_SECTION_ID_EXPORT = 7,
    WASM_SECTION_ID_FUNCTION = 3,
    WASM_SECTION_ID_GLOBAL = 6,
    WASM_SECTION_ID_IMPORT = 2,
    WASM_SECTION_ID_MEMORY = 5,
    WASM_SECTION_ID_TABLE = 4,
    WASM_SECTION_ID_TYPE = 1,

    // The flag of a table that has a minimum size but no maximum, like a memory has.
    WASM_TABLE_LIMITS_MIN_ONLY = 0x00,

    // The type of the elements of a table, which are references to functions.
    WASM_TYPE_FUNCREF = 0x70,
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
    // The operand of `%` has no address that can be taken: a value such as a number or a call, a
    // parameter, the iterator of a for statement, a global variable, or an array or an array
    // slice, which cannot be pointed to. A variable of such a type whose address is taken is this
    // too. The subject is the name in the operand.
    WASM_BUILD_STATUS_ADDRESS_INVALID,
    WASM_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID,
    // An element of an array, or a value that is assigned to one, that is not an `Int`.
    WASM_BUILD_STATUS_ARRAY_ELEMENT_TYPE_MISMATCH,
    // A value that has to be an array or an array slice, and is not: what is indexed, sliced, gone
    // through by a for statement, or copied to a range of elements.
    WASM_BUILD_STATUS_ARRAY_EXPECTED,
    // A variable of an array type that is not given an array literal.
    WASM_BUILD_STATUS_ARRAY_INITIALIZER_INVALID,
    // An array literal with another number of elements than the length of its type.
    WASM_BUILD_STATUS_ARRAY_LENGTH_MISMATCH,
    // An array as the type of a parameter, or an array or an array slice as the type of a global
    // variable. The subject is the type of the elements.
    WASM_BUILD_STATUS_ARRAY_TYPE_MISPLACED,
    WASM_BUILD_STATUS_CALL_ARGUMENT_COUNT_MISMATCH,
    WASM_BUILD_STATUS_CALL_ARGUMENT_TYPE_MISMATCH,
    WASM_BUILD_STATUS_CALL_RESULT_MISSING,
    // A call of a variable, a parameter or a global variable that does not hold a pointer to a
    // procedure. The subject is the name.
    WASM_BUILD_STATUS_CALL_TARGET_NOT_PROCEDURE,
    WASM_BUILD_STATUS_COMPARISON_OPERAND_TYPE_INVALID,
    WASM_BUILD_STATUS_CONDITION_TYPE_INVALID,
    WASM_BUILD_STATUS_DEFINITION_NAME_RESERVED,
    // `.RED` where no enum is known to give it one: the value of a variable that has no written
    // type, an operand that is compared with another member without an enum, an argument of a
    // procedure that is not one, and so on. The subject is the name of the member.
    WASM_BUILD_STATUS_ENUM_TYPE_UNKNOWN,
    // A member that the enum does not have, as in `Color.PURPLE` and in `.PURPLE` where the enum is
    // `Color`. The subject is the name of the member.
    WASM_BUILD_STATUS_ENUM_MEMBER_UNKNOWN,
    WASM_BUILD_STATUS_EXPRESSION_TOO_DEEP,
    // A field is read or assigned through something that is not a variable, a parameter or an
    // element of an array, such as the result of a call. The subject is the name of the field.
    WASM_BUILD_STATUS_FIELD_BASE_INVALID,
    // A field is assigned through a parameter or the iterator of a for statement, which cannot be
    // assigned to. Through a pointer it can be, as the pointer is not changed. The subject is the
    // name.
    WASM_BUILD_STATUS_FIELD_TARGET_READ_ONLY,
    WASM_BUILD_STATUS_IDENTIFIER_NOT_VALUE,
    // An index, or the end of a slice, that is not an `Int`.
    WASM_BUILD_STATUS_INDEX_TYPE_INVALID,
    WASM_BUILD_STATUS_LOGICAL_OPERAND_TYPE_INVALID,
    // `nil` where no pointer type is known to give it one: the value of a variable that has no
    // written type, as in `p := nil`. The subject is the name of the variable.
    WASM_BUILD_STATUS_NIL_TYPE_UNKNOWN,
    WASM_BUILD_STATUS_OK,
    WASM_BUILD_STATUS_OUTPUT_TOO_LARGE,
    // A value that is followed with `^`, or assigned through, and is not a pointer.
    WASM_BUILD_STATUS_POINTER_EXPECTED,
    // A value that is assigned through a pointer, as in `p^ = x`, and is not of the type that the
    // pointer points to. The subject is the name of the pointer.
    WASM_BUILD_STATUS_POINTER_TARGET_TYPE_MISMATCH,
    WASM_BUILD_STATUS_RANGE_TYPE_INVALID,
    WASM_BUILD_STATUS_RETURN_TYPE_MISMATCH,
    WASM_BUILD_STATUS_RETURN_TYPE_MISSING,
    // A field is accessed on a value that is not a struct. The subject is the name before the dot.
    WASM_BUILD_STATUS_STRUCT_EXPECTED,
    // A field that is given a value twice in one struct literal. The subject is the second one.
    WASM_BUILD_STATUS_STRUCT_FIELD_DUPLICATE,
    // A value that is not of the type of the field that it is given to or assigned to.
    WASM_BUILD_STATUS_STRUCT_FIELD_TYPE_MISMATCH,
    // A field that the struct does not have, in a struct literal or an access.
    WASM_BUILD_STATUS_STRUCT_FIELD_UNKNOWN,
    // A struct that has itself as a field, directly or through other structs, so it would be
    // infinitely large. The subject is the name of the first field of the struct.
    WASM_BUILD_STATUS_STRUCT_RECURSIVE,
    // `type_info_of(Type)` that is not directly followed by a field, as in `info := type_info_of(
    // Color)`, or that is the value of a global variable with more than one field after it. The
    // information only exists at compile time, so only its fields can be used. The subject is the
    // name of the type, or of the global variable.
    WASM_BUILD_STATUS_TYPE_INFO_MISPLACED,
    // `type_info_of(Type)` of a type that the target has no information about, which is every type
    // that is not an enum. The subject is the name of the type.
    WASM_BUILD_STATUS_TYPE_INFO_UNSUPPORTED,
    WASM_BUILD_STATUS_TYPE_UNSUPPORTED,
    // A case of a union match that is not a member of the enum of the field that is matched. The
    // subject is the member.
    WASM_BUILD_STATUS_UNION_CASE_UNKNOWN,
    // The field that a union match matches is not a field of the struct that the union match is
    // in, is not of an enum type, or is itself in a case. The subject is its name.
    WASM_BUILD_STATUS_UNION_DISCRIMINANT_INVALID,
    // A struct object that gives a field of a case of a union match, and a value for the matched
    // field that is not the member of the case. The subject is the name of the field of the case.
    WASM_BUILD_STATUS_UNION_FIELD_KIND_MISMATCH,
    // A field of a case of a union match that is used where the compiler cannot tell that the
    // matched field is the member of the case: not inside an if statement that compares them, or
    // in one where the matched field is changed. The subject is the name of the field.
    WASM_BUILD_STATUS_UNION_FIELD_UNMATCHED,
    // A struct object that gives a field of a case of a union match, and a value for the matched
    // field that is not a member of the enum written out, such as `.IDENTIFIER`, so the case is
    // not known when the program is compiled. The subject is the name of the field of the case.
    WASM_BUILD_STATUS_UNION_KIND_NOT_CONSTANT,
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
 * Every constant definition becomes a function that the module exports under the name of the
 * definition. A global variable becomes a mutable global of the module, or two when it is a `Str`,
 * in the order of the source, which is not exported. Its initial value is a literal, and it is set
 * once when the module is instantiated, so the global keeps its value between calls. A use of one
 * in a procedure is a `global.get`, and an assignment to one is a `global.set`. A variable of a
 * procedure is a local of its function, or two locals when it is a `Str`, after the
 * parameters. The iterator of a for statement is a local too, followed by one more local that holds
 * the end of its range, which is read once before the loop. A definition whose value is a string
 * literal becomes a function without parameters that returns the string. A use of such a constant
 * in a procedure is a call of that function, so the constant can be used above its definition as
 * well.
 *
 * `Int` is a 64-bit integer (`i64`), so callers from JavaScript pass and receive `BigInt`.
 * `Str` is two 32-bit integers (`i32`), the address of its first byte in memory and its length
 * in bytes, which are UTF-8. So a parameter of that type is two WebAssembly parameters, and a
 * result of that type is two WebAssembly results, which JavaScript receives as an array. The bytes
 * of the string literals are in the first bytes of the memory, which the module exports as
 * `memory`. The module has no memory, and so no export of that name, when the program has no
 * string literal and no array type. A definition cannot be called `memory`, because the name is
 * taken.
 *
 * An array slice is two 32-bit integers as well: the address of its first element and its length
 * in elements, which are `Int` values of 8 bytes. So a parameter that is an array slice is two
 * WebAssembly parameters, and a caller from JavaScript passes the address of elements that it
 * wrote to the memory of the module, and their number.
 *
 * A struct is the values of its fields, one after the other, in the order of the definition, so it
 * is as many WebAssembly values as its fields take together, where a field that is a struct takes
 * its own fields in its place. A parameter that is a struct is that many parameters, and a result
 * is that many results, which JavaScript receives as an array. A variable of a struct type is that
 * many locals. A struct is never stored in memory as a whole, except as an element of an array or
 * where a pointer points to it: every value of it takes 8 bytes there, whether it is a 32-bit or a
 * 64-bit one.
 *
 * The fields of the cases of a union match are fields of the struct, in the order of the source, so
 * they take their own values and the cases do not share them. The target checks that they are only
 * given and used where the object is known to be in their case, see
 * `WASM_BUILD_STATUS_UNION_FIELD_UNMATCHED`, and does not need to know about the cases to write the
 * code of a field.
 *
 * A pointer, written `^Int`, is one 32-bit integer: the address in the memory of the module of what
 * it points to, which takes 8 bytes for each of its values, like an element of an array. So a
 * parameter that is a pointer is one WebAssembly parameter, and a caller from JavaScript passes the
 * address of what it wrote to the memory. A field of a pointer to a struct is accessed as a field
 * of the struct, `p.age` for `p^.age`, wherever a pointer to a struct is met in a chain of fields.
 * `%x` is the address of a variable `x`, which is kept in
 * the frame of its procedure, on the stack of the arrays, instead of in locals when its address is
 * taken somewhere. `p^` loads from the address that `p` is, and traps when it is zero, which is
 * what a pointer that was not given a value is. A pointer to a variable is good until the
 * procedure that has the variable returns.
 *
 * An enum is one `Int` (`i64`): the number of the member, counted from zero in the order of the
 * definition, whose type `enum Int ->` writes and has to be `Int`. A member, written `Color.RED`
 * or `.RED`, is the `i64.const` of its number,
 * and a constant that is a member is a function without parameters that returns it, like a
 * constant string. An enum is a type and no function, like a struct is. Members are compared with
 * `i64.eq` and `i64.ne`, and the default of an enum is 0.
 *
 * A pointer to a procedure, written `^proc(Int) Int`, is one 32-bit integer as well, but it is not
 * an address in memory: it is a slot of the one table of the module, which has every function of
 * the module from slot 1 on. So the pointer to a procedure is its function index plus one, and 0,
 * which is `nil`, is a slot that has no function. The name of a procedure that is used as a value
 * is the `i32.const` of its slot, and a call of a variable, a parameter or a global variable is the
 * arguments, the pointer and `call_indirect`, which traps for `nil`. The types of the signatures
 * that no procedure has the first of come after the types of the functions in the type section.
 * The module has a table and an element segment only when the program uses such a pointer.
 *
 * An array is on a stack in the memory of the module: a procedure that has an array literal makes
 * room for all of its array literals when it starts, and gives it back when it returns. Only the
 * elements are there, and a variable of an array type is a local that holds the address of the
 * elements, so an array cannot be returned from the procedure, but a slice of it can be passed to
 * the procedures that it calls. The stack is 1 MiB, and a procedure that would go past its end
 * traps. Reading or writing an element outside an array or an array slice traps as well, and so
 * does copying to a range of elements from a range of another length.
 *
 * The syntax tree can be valid for the parser but not for the target, for example when it uses a
 * struct as a value. Such trees are rejected with a status other than
 * `WASM_BUILD_STATUS_OK`.
 */
Wasm_Build_Result wasm_build(Array_Slice nodes, Array_Slice bindings, Ast_Node_Index root,
                             Array_Slice output_buffer);

#endif
