#ifndef MIALU_H_X64
#define MIALU_H_X64

#include <stdint.h>

#include <mialu/array_slice.h>
#include <mialu/ast.h>
#include <mialu/common_types.h>
#include <mialu/str.h>

// Alphabetical, because nothing depends on the order.
typedef enum {
    /**
     * The operand of `%` has no address:
     * a parameter, the iterator of a for statement, a value that is computed, or a variable of an
     * array or array slice type.
     * The subject is its first name or string, or the name of the definition that it is in.
     */
    X64_BUILD_STATUS_ADDRESS_INVALID,
    /**
     * An operand of `+`, `-`, `*` or `/`, or the variable or the value of an assignment such as
     * `x += 1`, is not an `Int`, or not a float of the type of the operation when it is one of
     * floats, or the two operands are of different types.
     * The subject is the first name or string of the operand, or the name of the definition that it
     * is in.
     */
    X64_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID,
    /**
     * The first argument of `append` is not the address of a variable that is a dynamic array.
     * The subject is `append`.
     */
    X64_BUILD_STATUS_APPEND_TARGET_INVALID,
    /**
     * An array, a dynamic array or an array slice is compared with one of another of the three
     * kinds.
     * The subject is the first operand.
     */
    X64_BUILD_STATUS_COMPARISON_ARRAY_KIND_MISMATCH,
    /**
     * Two arrays whose lengths are known when the program is compiled are compared, and have
     * different lengths.
     * The subject is the first operand, `length_first` and `length_second` are the two lengths, and
     * `value_offset` is where the second operand is.
     */
    X64_BUILD_STATUS_COMPARISON_ARRAY_LENGTH_MISMATCH,
    /**
     * An element of an array literal, or the value that is assigned to an element or to a range of
     * elements, is not of the type of the elements.
     * The subject is its first name or string.
     */
    X64_BUILD_STATUS_ARRAY_ELEMENT_TYPE_MISMATCH,
    /**
     * What is subscripted, sliced, gone through by a for statement or assigned to as a range is not
     * an array or an array slice.
     * The subject is its first name or string.
     */
    X64_BUILD_STATUS_ARRAY_EXPECTED,
    /**
     * A variable of an array type is not defined with an array literal, or is assigned to as a
     * whole.
     * The subject is the name of the variable.
     */
    X64_BUILD_STATUS_ARRAY_INITIALIZER_INVALID,
    /**
     * An array literal has another number of elements than the length of its type.
     * The subject is the name of the type of the elements.
     */
    X64_BUILD_STATUS_ARRAY_LENGTH_MISMATCH,
    /**
     * A parameter of an array type, which has to be an array slice.
     * The subject is the name of the parameter.
     */
    X64_BUILD_STATUS_ARRAY_TYPE_MISPLACED,
    /**
     * The number of arguments of a call is not the number of parameters of the procedure.
     * The subject is the name of the procedure.
     */
    X64_BUILD_STATUS_CALL_ARGUMENT_COUNT_MISMATCH,
    /**
     * An argument of another type than its parameter has.
     * The subject is the name of the procedure.
     */
    X64_BUILD_STATUS_CALL_ARGUMENT_TYPE_MISMATCH,
    /**
     * The value of a call of a procedure that returns nothing.
     * The subject is its name.
     */
    X64_BUILD_STATUS_CALL_RESULT_MISSING,
    /**
     * The call of a variable, a parameter or a global variable that does not hold a pointer to a
     * procedure.
     * The subject is its name.
     */
    X64_BUILD_STATUS_CALL_TARGET_NOT_PROCEDURE,
    /**
     * The operands of a comparison are not two `Int` values, two `Rune` values or two floats of one
     * type, or not two `Bool` values for `==` and `not=`.
     * The subject is the first name or string of the comparison.
     */
    X64_BUILD_STATUS_COMPARISON_OPERAND_TYPE_INVALID,
    /**
     * The condition of an if statement is not a `Bool`.
     * The subject is the first name or string of the condition, or the name of the definition that
     * it is in.
     */
    X64_BUILD_STATUS_CONDITION_TYPE_INVALID,
    /**
     * More constants than the build has room to keep, see `X64_CONSTANT_COUNT_MAX`.
     * The subject is the name of the first one that does not fit.
     */
    X64_BUILD_STATUS_CONSTANT_COUNT_EXCEEDED,
    /**
     * A member of an enum that the enum does not have.
     * The subject is the name of the member.
     */
    X64_BUILD_STATUS_ENUM_MEMBER_UNKNOWN,
    /**
     * A member such as `.RED` where no enum is wanted, which does not say what enum it is of.
     * The subject is the name of the member, or of the variable that it is given to.
     */
    X64_BUILD_STATUS_ENUM_TYPE_UNKNOWN,
    /**
     * An expression that is nested deeper than `X64_EXPRESSION_DEPTH_MAX`.
     * The subject is the name of the definition that has it.
     */
    X64_BUILD_STATUS_EXPRESSION_TOO_DEEP,
    /**
     * A procedure declared in a `foreign js ->` block.
     * A program for the processor has no JavaScript to import from, only the C library.
     * The subject is the name of the procedure.
     * A field is accessed on a value that is not a struct or a pointer of one caret to one.
     * The subject is the name of the field.
     */
    X64_BUILD_STATUS_FIELD_BASE_INVALID,
    /**
     * A field is assigned through a parameter or an iterator, which cannot be assigned to.
     * The subject is the first name of the place.
     */
    X64_BUILD_STATUS_FIELD_TARGET_READ_ONLY,
    X64_BUILD_STATUS_FOREIGN_TARGET_UNSUPPORTED,
    /**
     * A foreign procedure whose library is not given as `system:Name.lib`, or that makes more
     * libraries than the build has room for.
     * The subject is the name of the procedure.
     */
    X64_BUILD_STATUS_LIBRARY_INVALID,
    /**
     * A name of a struct or an enum where a value is wanted, which is a type and not a value.
     * The subject is the name.
     */
    X64_BUILD_STATUS_IDENTIFIER_NOT_VALUE,
    /**
     * An index of a subscript or a bound of a slice is not an `Int`.
     * The subject is its first name or string.
     */
    X64_BUILD_STATUS_INDEX_TYPE_INVALID,
    /**
     * An operand of `and`, `or` or `not` is not a `Bool`.
     * The subject is the first name or string of the operand, or the name of the definition that it
     * is in.
     */
    X64_BUILD_STATUS_LOGICAL_OPERAND_TYPE_INVALID,
    /**
     * `cast(Type)` of a value that is not converted to that type:
     * the type must be `Int` and the value a `Byte`, a `Bool`, an `Int`, a `Rune` or a float, or
     * the type must be a float and the value a `Byte`, an `Int` or a float, or the type must be
     * `Rune` and the value a `Byte` or a `Rune`, or the type must be `[4]Byte` and the value a
     * `Rune`.
     * The subject is the name of the type.
     */
    X64_BUILD_STATUS_CAST_INVALID,
    /**
     * There is no procedure called `main`, or it is not `main :: proc() Int ->`.
     * The subject is `main` in the source in the second case, and has `bytes` NULL in the first,
     * which is about no place in the source.
     */
    X64_BUILD_STATUS_MAIN_INVALID,
    /**
     * The first argument of `make` is not the type of an array slice, which is what `make` makes.
     * The subject is the text of the type.
     */
    X64_BUILD_STATUS_MAKE_TYPE_INVALID,
    /**
     * Two cases of a match statement match a value in common.
     * The subject is the word `case` of the second.
     */
    X64_BUILD_STATUS_MATCH_CASE_DUPLICATE,
    /**
     * A match of an enum, without a default case and without `partial`, that has no case for a
     * member of the enum.
     * The subject is the word `match`.
     */
    X64_BUILD_STATUS_MATCH_NOT_EXHAUSTIVE,
    /**
     * The attribute `partial` of a match statement whose subject is not an enum.
     * The subject is the attribute.
     */
    X64_BUILD_STATUS_MATCH_PARTIAL_INVALID,
    /**
     * The pattern of a case is not known when the program is compiled.
     * The subject is the word `case`.
     */
    X64_BUILD_STATUS_MATCH_PATTERN_NOT_CONSTANT,
    /**
     * The pattern of a case is not of the type of the subject.
     * The subject is the word `case`.
     */
    X64_BUILD_STATUS_MATCH_PATTERN_TYPE_MISMATCH,
    /**
     * A range of a case that has no number in it.
     * The subject is the word `case`.
     */
    X64_BUILD_STATUS_MATCH_RANGE_EMPTY,
    /**
     * The subject of a match statement is not of a type that can be matched.
     * The subject is its first name or string.
     */
    X64_BUILD_STATUS_MATCH_SUBJECT_TYPE_INVALID,
    /**
     * A variable that is defined with `nil` and no type, which does not say what it points to.
     * The subject is the name of the variable.
     */
    X64_BUILD_STATUS_NIL_TYPE_UNKNOWN,
    X64_BUILD_STATUS_OK,
    X64_BUILD_STATUS_OUTPUT_TOO_LARGE,
    /**
     * `^` follows a value that is not a pointer.
     * The subject is its first name or string.
     */
    X64_BUILD_STATUS_POINTER_EXPECTED,
    /**
     * The value that is assigned through a pointer is not of the type that it points to.
     * The subject is the first name or string of the place.
     */
    X64_BUILD_STATUS_POINTER_TARGET_TYPE_MISMATCH,
    /**
     * The first number or the end of the range of a for statement is not an `Int`.
     * The subject is its first name or string, or the name of the definition that it is in.
     */
    X64_BUILD_STATUS_RANGE_TYPE_INVALID,
    /**
     * A `return` in a procedure that returns nothing.
     * The subject is the name of the procedure.
     */
    X64_BUILD_STATUS_RETURN_TYPE_MISSING,
    /**
     * A `return` that gives more values or fewer than the procedure has return types, and a call
     * that returns several values where one is wanted, or a definition of several names whose call
     * returns another number of values.
     * The subject is the name of the procedure, or of the first of the names.
     */
    X64_BUILD_STATUS_RETURN_COUNT_MISMATCH,
    X64_BUILD_STATUS_RESULT_COUNT_MISMATCH,
    /**
     * A returned value of another type than the procedure returns.
     * The subject is the name of the procedure.
     */
    X64_BUILD_STATUS_RETURN_TYPE_MISMATCH,
    /**
     * A struct literal whose name is not a struct, or one without a name where no struct is wanted.
     * The subject is its name, or the name of the definition that it is in.
     */
    X64_BUILD_STATUS_STRUCT_EXPECTED,
    /**
     * A field of a struct literal is given twice.
     * The subject is the second one.
     */
    X64_BUILD_STATUS_STRUCT_FIELD_DUPLICATE,
    /**
     * A struct literal gives some fields and not all of them.
     * The subject is its name.
     */
    X64_BUILD_STATUS_STRUCT_FIELD_MISSING,
    /**
     * A field is given or assigned a value of another type than it has.
     * The subject is the name of the field.
     */
    X64_BUILD_STATUS_STRUCT_FIELD_TYPE_MISMATCH,
    /**
     * A field that the struct does not have.
     * The subject is its name.
     */
    X64_BUILD_STATUS_STRUCT_FIELD_UNKNOWN,
    /**
     * A struct that contains itself, through its fields and the fields of its fields.
     * The subject is its first field.
     */
    X64_BUILD_STATUS_STRUCT_RECURSIVE,
    /**
     * `type_info_of` that is not followed by a field right away.
     * The subject is the type that it is asked about.
     */
    X64_BUILD_STATUS_TYPE_INFO_MISPLACED,
    /**
     * `type_info_of` of a type that is not an enum.
     * The subject is the type, or the name of the global variable that it is the value of.
     */
    X64_BUILD_STATUS_TYPE_INFO_UNSUPPORTED,
    /**
     * A type that the target does not have, of a parameter, a result or a variable.
     * The subject is the text of the type, or the name of the parameter or the variable if the type
     * is not a name.
     */
    X64_BUILD_STATUS_TYPE_UNSUPPORTED,
    /**
     * The case of a union match is not a member of the enum of the field that it is matched
     * against.
     * The subject is the member.
     */
    X64_BUILD_STATUS_UNION_CASE_UNKNOWN,
    /**
     * The field that a union match is matched against is not a field of the struct of an enum type,
     * outside of a case.
     * The subject is its name.
     */
    X64_BUILD_STATUS_UNION_DISCRIMINANT_INVALID,
    /**
     * A struct literal gives a field of a case that is not the case of the member that it gives the
     * matched field.
     * The subject is the field.
     */
    X64_BUILD_STATUS_UNION_FIELD_KIND_MISMATCH,
    /**
     * A field of a case of a union match is read or assigned where the program does not show that
     * its object is in the case.
     * The subject is the field.
     */
    X64_BUILD_STATUS_UNION_FIELD_UNMATCHED,
    /**
     * A struct literal gives a field of a case, but does not give the matched field a member that
     * is written out.
     * The subject is the field.
     */
    X64_BUILD_STATUS_UNION_KIND_NOT_CONSTANT,
    /**
     * Something that the target does not build yet.
     * The subject is the name of the definition that has it.
     */
    X64_BUILD_STATUS_UNSUPPORTED,
    /**
     * The value that a variable is defined with or assigned is not of the type of the variable.
     * The subject is the name of the variable.
     */
    X64_BUILD_STATUS_VARIABLE_TYPE_MISMATCH,
} X64_Build_Status;

enum {
    /**
     * The number of constants, the procedures, foreign ones too, and the strings, that a program
     * can have.
     * The build keeps one record for each in a fixed array, so that it needs no allocation.
     */
    X64_CONSTANT_COUNT_MAX = 1024,
    /**
     * The deepest expression that is built.
     * The build walks an expression with the path from its root in a fixed array instead of
     * recursing, and it is the bound of the analysis of constants and of the WebAssembly target as
     * well, so that the targets accept the same expressions.
     */
    X64_EXPRESSION_DEPTH_MAX = 1024,
};

// What kind of value a type is. Alphabetical, because nothing depends on the order.
// `X64_TYPE_KIND_DYNAMIC` is a dynamic array, `[dynamic]Int`: as a value it is the address of its
// first element and its number of elements, like an array slice, and the variable that holds it has
// a third word after them, the number of elements that there is room for.
// `X64_TYPE_KIND_NONE` is the type of a call of a procedure that returns nothing, and of what the
// target does not build.
// `X64_TYPE_KIND_NIL` is the type of `nil`, a pointer to nothing in particular, which is accepted
// where any pointer is wanted and is the type of no variable. `X64_TYPE_KIND_ENUM_LITERAL` is the
// type of a member of an enum written without the enum, such as `.RED`, which is accepted where an
// enum that has the member is wanted, and is the type of no variable either.
// `X64_TYPE_KIND_TUPLE` is the type of a call of a procedure that returns several values, whose
// `definition` is the procedure or procedure type that returns them. Its words are the words of the
// values one after the other, and it is the type of no variable.
typedef enum {
    X64_TYPE_KIND_ARRAY,
    X64_TYPE_KIND_BOOL,
    X64_TYPE_KIND_BYTE,
    X64_TYPE_KIND_C_STR,
    X64_TYPE_KIND_DYNAMIC,
    X64_TYPE_KIND_ENUM,
    X64_TYPE_KIND_ENUM_LITERAL,
    X64_TYPE_KIND_F32,
    X64_TYPE_KIND_F64,
    X64_TYPE_KIND_I32,
    X64_TYPE_KIND_I64,
    X64_TYPE_KIND_INT,
    X64_TYPE_KIND_NIL,
    X64_TYPE_KIND_NONE,
    X64_TYPE_KIND_POINTER,
    X64_TYPE_KIND_PROCEDURE,
    X64_TYPE_KIND_RUNE,
    X64_TYPE_KIND_SLICE,
    X64_TYPE_KIND_STR,
    X64_TYPE_KIND_STRUCT,
    X64_TYPE_KIND_TUPLE,
    X64_TYPE_KIND_U32,
    X64_TYPE_KIND_U64,
} X64_Type_Kind;

/**
 * A type of the target, with what the kind alone does not say. An array has `length` elements,
 * which is part of its type, and is -1 for an array type whose length is the number of elements of
 * its literal, `[const]Int`, which is equal to any length. For an array and an array slice,
 * `element_kind` and `element_definition` say what their elements are. `definition` is the
 * `AST_NODE_TYPE_STRUCT_TYPE` of a struct and the `AST_NODE_TYPE_ENUM_TYPE` of an enum, which is
 * what makes two of them the same type, and is `AST_NODE_INDEX_NONE` for a type that has no
 * definition.
 *
 * A pointer type such as `^^Int` has `depth`, the number of carets, which is at least 1, and the
 * type at the end of them, which is never a pointer, an array or an array slice: `pointee_kind` is
 * its kind, with `definition`. So a pointer is described without a type inside a type. The elements
 * of an array that are pointers are described the same way by `element_depth`,
 * `element_pointee_kind` and `element_definition`. `depth` and `element_depth` are zero when there
 * is no pointer.
 *
 * A pointer to a procedure, written `^proc(Int) Int`, has the kind `X64_TYPE_KIND_PROCEDURE`, and
 * its `definition` is the first procedure or procedure type of the program that has its signature,
 * so that signatures with the same types of parameters and return type are one type. A pointer to
 * it is a pointer whose `pointee_kind` is `X64_TYPE_KIND_PROCEDURE`.
 */
typedef struct {
    int64_t length;
    X64_Type_Kind kind;
    X64_Type_Kind element_kind;
    X64_Type_Kind pointee_kind;
    X64_Type_Kind element_pointee_kind;
    Ast_Node_Index definition;
    Ast_Node_Index element_definition;
    uint32_t depth;
    uint32_t element_depth;
} X64_Type;

/**
 * What the build works out about a node of the tree before it writes the code of a procedure,
 * which the code and the debug information both read. The build keeps one for each node in the
 * buffer that the caller gives it, so that it needs no allocation. `type` is the type of a
 * variable, for the `AST_NODE_TYPE_VARIABLE_DEFINITION` that defines it, of the iterator of an
 * `AST_NODE_TYPE_FOR_STATEMENT`, of the subject of an `AST_NODE_TYPE_MATCH_STATEMENT` and of an
 * `AST_NODE_TYPE_ARRAY_LITERAL`, and `frame_offset` is where the value of each of them is in the
 * frame of the procedure: at `rbp - frame_offset`, which for an array literal is its elements. For
 * an `AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION` they are its type and its place in `.data`, and for
 * an `AST_NODE_TYPE_TYPE_INFO_OF` the place of the table of the members of its enum there. With
 * debug information, the `frame_offset` of an `AST_NODE_TYPE_STRUCT_TYPE` and of an
 * `AST_NODE_TYPE_ENUM_TYPE` is where its entry is in `.debug_info`, and that of an
 * `AST_NODE_TYPE_FIELD` where the entry of its type is. The entries of other nodes are not used.
 */
typedef struct {
    X64_Type type;
    uint32_t frame_offset;
} X64_Node_Layout;

/**
 * Where a procedure that has a body starts. `offset` is counted from the start of the code, which
 * is where the program starts, so the first procedure is after the few instructions that start it.
 * `name` views the source that the syntax tree was parsed from.
 */
typedef struct {
    Str name;
    size_t offset;
} X64_Symbol;

// What a row of the debug information says about a place in the code. Alphabetical, because nothing
// depends on the order.
typedef enum {
    /**
     * A copy of a deferred statement ends at the place, which is a block of its own for a debugger.
     */
    X64_DEBUG_ROW_KIND_BLOCK_END,
    /**
     * A copy of a deferred statement starts at the place.
     */
    X64_DEBUG_ROW_KIND_BLOCK_START,
    /**
     * The `leave` of a procedure is at the place, after which the frame is the one of its caller.
     */
    X64_DEBUG_ROW_KIND_LEAVE,
    /**
     * A procedure or a statement starts at the place, which is where a debugger stops for its line.
     */
    X64_DEBUG_ROW_KIND_LINE,
} X64_Debug_Row_Kind;

/**
 * A place in the code that the debug information needs to know about, which the build notes while
 * it writes the code. `code_offset` is counted from the start of the code, and `source_offset` is
 * the offset in the source of what starts there, which is zero for `X64_DEBUG_ROW_KIND_LEAVE`.
 */
typedef struct {
    uint32_t code_offset;
    uint32_t source_offset;
    X64_Debug_Row_Kind kind;
} X64_Debug_Row;

enum {
    /**
     * The number of `X64_Debug_Row` elements that a build has room for for every node of the syntax
     * tree:
     * a statement or a procedure has the row of its line, and a `return` or the end of a procedure
     * has the row of its `leave`, and a copy of a deferred statement has the rows of where it
     * starts and ends.
     * A deferred statement is written again at each place where its block is left, so the rows are
     * bounded by the room, and the ones that do not fit are left out.
     */
    X64_DEBUG_ROWS_PER_NODE = 4,
};

/**
 * What a build with debug information needs. `source` is the text that the syntax tree was parsed
 * from, which the lines are counted in, and `source_path` is the absolute path of its file, which a
 * debugger opens and compares with the files that breakpoints are set in. `row_buffer` holds
 * `X64_Debug_Row` elements, at least `X64_DEBUG_ROWS_PER_NODE` for each node of the tree.
 */
typedef struct {
    Str source;
    Str source_path;
    Array_Slice row_buffer;
} X64_Debug;

/**
 * On `X64_BUILD_STATUS_OK`, `bytes` is the filled start of the output buffer, as `byte` elements,
 * and it is a complete executable file. Otherwise `bytes.length` is zero and `subject` is the text
 * in the source that the error is about, as a view into the source that the syntax tree was parsed
 * from, so the caller can work out the position. `subject` has `bytes` NULL when the error is not
 * about any place in the source. On success, `symbols` is the filled start of the symbol buffer,
 * as `X64_Symbol` elements, one for each procedure that has a body, in the order of the code.
 */
typedef struct {
    Array_Slice bytes;
    Array_Slice symbols;
    Str subject;
    // The two types of `X64_BUILD_STATUS_RETURN_TYPE_MISMATCH`, as a message names them: the return
    // type, and the type of the value that is returned. They have `bytes` NULL for every other
    // status.
    Str type_expected;
    Str type_found;
    // The byte offset in the source of the value that is returned, for the same status.
    uint32_t value_offset;
    // The two lengths of `X64_BUILD_STATUS_COMPARISON_ARRAY_LENGTH_MISMATCH`.
    int64_t length_first;
    int64_t length_second;
    X64_Build_Status status;
} X64_Build_Result;

/**
 * Builds an executable for Windows on x64 (a PE32+ file for the console) from the syntax tree below
 * `root`, which must be the `AST_NODE_TYPE_MODULE` node of a successful `parse`. `nodes` holds
 * `Ast_Node` elements, and `bindings` and `constants` hold the `Semantic_Binding` and the
 * `Constant_Value` elements that `semantic_analyze` made for the same tree. `layout_buffer` holds
 * an `X64_Node_Layout` for each node, which the build fills. The file is written to
 * `output_buffer`, which holds `byte` elements. The symbols are written to `symbol_buffer`, which
 * holds at least `X64_CONSTANT_COUNT_MAX` `X64_Symbol` elements, so that the code can be shown with
 * the names of its procedures.
 *
 * Every procedure is machine code that follows the calling convention of Windows on x64, so that
 * the C library can be called with it: the first four words of the arguments are in `rcx`, `rdx`,
 * `r8` and `r9`, the others on the stack above the 32 bytes that the caller makes for the callee to
 * keep the four in, and the result is in `rax`. The program starts in a few instructions of its own
 * that call `main`, which has to be `main :: proc() Int ->`, and pass its result to `exit` of the C
 * library, so that the output of the program is flushed before it ends and its result is the exit
 * code of the process.
 *
 * `Int` is a 64-bit integer, `Bool` is 0 or 1 in 64 bits, `C_Str` is the 64-bit address of
 * bytes that end with a zero, `Rune` is the code point of a character in a word, and `F32` is a
 * 32-bit float, which is kept in the low 32 bits of a
 * word, and `F64` is a 64-bit float, which is a word. A float is passed and returned as the calling
 * convention says: in `xmm0` to `xmm3` for the first four words of the arguments and in `xmm0` for
 * a result, also in the integer register of the word, so that a variadic procedure of the C library
 * finds it either way. A string literal is untyped, and the target does not build a `Str`,
 * so it is only accepted where a `C_Str` is wanted, which it then becomes, and as the argument of
 * `str_to_c_str`, which makes the `C_Str` too. A constant that is a number or a truth value is
 * loaded as its value, which the analysis worked out.
 *
 * The values of an expression are kept in the frame of its procedure while the expression is
 * worked out, so that the stack pointer does not move between the start and the end of a
 * procedure, and every call is made with the stack as the calling convention wants it.
 *
 * A foreign procedure that a `foreign libc ->` block declares is imported from `msvcrt.dll`, which
 * every installation of Windows has, by the name of the procedure. The compiler trusts the
 * declaration: `Int` is 64 bits wide, so a procedure of the C library that returns an `int` is not
 * declared with a result, or its upper 32 bits are whatever the library left there.
 *
 * When `debug` is not NULL, the file also has debug information in the DWARF format, version 4,
 * which gdb reads from a file of this format: the lines of the statements, the procedures with
 * their parameters and variables and where they are kept, the constants that are strings, and how
 * to find the frame of the caller of each procedure. The code is the same with it and without it,
 * so a program that is debugged runs as it does without a debugger. With NULL the file has none.
 *
 * The program is checked for the types of its values as it is built, and a program that the
 * target does not build is rejected with a status other than `X64_BUILD_STATUS_OK`, so that
 * nothing is built wrongly.
 */
X64_Build_Result x64_build(Array_Slice nodes, Array_Slice bindings, Array_Slice constants,
                           Ast_Node_Index root, Array_Slice layout_buffer,
                           Array_Slice output_buffer, Array_Slice symbol_buffer,
                           X64_Debug const *debug);

// The alignment of a struct in memory, which is the size of a word, since every field is made of
// whole words and a struct has no padding.
enum {
    X64_STRUCT_ALIGNMENT_BYTES = 8,
};

/**
 * Returns the size in bytes that a value of the struct `struct_node` takes in memory in a program
 * of this target, so that a tool can show it without building the program. `struct_node` must be
 * an `AST_NODE_TYPE_STRUCT_TYPE` of the tree below `root`, which must be the
 * `AST_NODE_TYPE_MODULE` node of a successful `parse`, and `nodes` holds its `Ast_Node` elements.
 * Every field is made of whole words of 8 bytes and there is no padding, see
 * `X64_STRUCT_ALIGNMENT_BYTES`. Returns `SIZE_MAX` when the structs are inside each other too deep,
 * which happens for a struct that contains itself.
 */
size_t x64_struct_size_bytes(Array_Slice nodes, Ast_Node_Index root, Ast_Node_Index struct_node);

#endif // MIALU_H_X64
