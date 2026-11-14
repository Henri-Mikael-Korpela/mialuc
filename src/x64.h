#ifndef MIALU_X64_H
#define MIALU_X64_H

#include <mialu/array_slice.h>
#include <mialu/ast.h>
#include <mialu/common_types.h>
#include <mialu/str.h>

// Alphabetical, because nothing depends on the order.
typedef enum {
    // The number of arguments of a call is not the number of parameters of the procedure. The
    // subject is the name of the procedure.
    X64_BUILD_STATUS_CALL_ARGUMENT_COUNT_MISMATCH,
    // An argument of another type than its parameter has. The subject is the name of the
    // procedure.
    X64_BUILD_STATUS_CALL_ARGUMENT_TYPE_MISMATCH,
    // The value of a call of a procedure that returns nothing. The subject is its name.
    X64_BUILD_STATUS_CALL_RESULT_MISSING,
    // More constants than the build has room to keep, see `X64_CONSTANT_COUNT_MAX`. The subject
    // is the name of the first one that does not fit.
    X64_BUILD_STATUS_CONSTANT_COUNT_EXCEEDED,
    // A procedure declared in a `foreign js ->` block. A program for the processor has no
    // JavaScript to import from, only the C library. The subject is the name of the procedure.
    X64_BUILD_STATUS_FOREIGN_TARGET_UNSUPPORTED,
    // There is no procedure called `main`, or it is not `main :: proc() Int ->`. The subject is
    // `main` in the source in the second case, and has `bytes` NULL in the first, which is about
    // no place in the source.
    X64_BUILD_STATUS_MAIN_INVALID,
    X64_BUILD_STATUS_OK,
    X64_BUILD_STATUS_OUTPUT_TOO_LARGE,
    // A procedure with more parameters than the registers that carry the arguments of a call, see
    // `X64_PARAMETER_COUNT_MAX`. The subject is the name of the procedure.
    X64_BUILD_STATUS_PARAMETER_COUNT_UNSUPPORTED,
    // A `return` in a procedure that returns nothing. The subject is the name of the procedure.
    X64_BUILD_STATUS_RETURN_TYPE_MISSING,
    // A returned value of another type than the procedure returns. The subject is the name of
    // the procedure.
    X64_BUILD_STATUS_RETURN_TYPE_MISMATCH,
    // A type of a parameter or of a result other than `Int` and `C_Str`. The subject is the
    // text of the type, or the name of the parameter if the type is not a name.
    X64_BUILD_STATUS_TYPE_UNSUPPORTED,
    // Something that the target does not build yet. The subject is the name of the definition
    // that has it.
    X64_BUILD_STATUS_UNSUPPORTED,
    // The value that a variable is defined with or assigned is not of the type of the variable.
    // The subject is the name of the variable.
    X64_BUILD_STATUS_VARIABLE_TYPE_MISMATCH,
} X64_Build_Status;

enum {
    // The registers that carry arguments in the calling convention of Windows on x64 are `rcx`,
    // `rdx`, `r8` and `r9`. The target passes no argument on the stack, so no procedure and no call
    // has more.
    X64_PARAMETER_COUNT_MAX = 4,
    // The number of constants, the procedures, foreign ones too, and the strings, that a program
    // can have. The build keeps one record for each in a fixed array, so that it needs no
    // allocation.
    X64_CONSTANT_COUNT_MAX = 1024,
};

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
    // The `leave` of a procedure is at the place, after which the frame is the one of its caller.
    X64_DEBUG_ROW_KIND_LEAVE,
    // A procedure or a statement starts at the place, which is where a debugger stops for its line.
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
    // The number of `X64_Debug_Row` elements that a build needs at most for every node of the
    // syntax tree: a statement or a procedure has the row of its line, and a `return` or the end of
    // a procedure has the row of its `leave`.
    X64_DEBUG_ROWS_PER_NODE = 2,
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
    X64_Build_Status status;
} X64_Build_Result;

/**
 * Builds an executable for Windows on x64 (a PE32+ file for the console) from the syntax tree below
 * `root`, which must be the `AST_NODE_TYPE_MODULE` node of a successful `parse`. `nodes` holds
 * `Ast_Node` elements, and `bindings` and `constants` hold the `Semantic_Binding` and the
 * `Constant_Value` elements that `semantic_analyze` made for the same tree. The file is written to
 * `output_buffer`, which holds `byte` elements. The symbols are written to `symbol_buffer`, which
 * holds at least `X64_CONSTANT_COUNT_MAX` `X64_Symbol` elements, so that the code can be shown with
 * the names of its procedures.
 *
 * Every procedure is machine code that follows the calling convention of Windows on x64, so that
 * the C library can be called with it. The program starts in a few instructions of its own that
 * call `main`, which has to be `main :: proc() Int ->`, and pass its result to `exit` of the C
 * library, so that the output of the program is flushed before it ends and its result is the exit
 * code of the process.
 *
 * `Int` is a 64-bit integer, and `C_Str` is the 64-bit address of bytes that end with a zero. A
 * string literal is untyped, and the target does not build a `Str`, so it is only accepted where a
 * `C_Str` is wanted, which it then becomes, and as the argument of `str_to_c_str`, which makes the
 * `C_Str` too. A constant that is a number is loaded as its value, which the analysis worked out.
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
 * Only a small part of the language is built: procedures with up to `X64_PARAMETER_COUNT_MAX`
 * parameters of the types `Int` and `C_Str`, whose statements are calls, returns, definitions of
 * variables of those types and assignments to them with `=`, and whose expressions are constants
 * that are numbers, strings where a `C_Str` is wanted, parameters, variables, and calls with such
 * arguments. The rest is rejected with a status other than `X64_BUILD_STATUS_OK`, so that nothing
 * is built wrongly.
 */
X64_Build_Result x64_build(Array_Slice nodes, Array_Slice bindings, Array_Slice constants,
                           Ast_Node_Index root, Array_Slice output_buffer,
                           Array_Slice symbol_buffer, X64_Debug const *debug);

#endif
