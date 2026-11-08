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
    // More procedures than the build has room to keep, see `X64_PROCEDURE_COUNT_MAX`.
    X64_BUILD_STATUS_PROCEDURE_COUNT_EXCEEDED,
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
} X64_Build_Status;

enum {
    // The registers that carry arguments in the calling convention of Windows on x64 are `rcx`,
    // `rdx`, `r8` and `r9`. The target passes no argument on the stack, so no procedure and no call
    // has more.
    X64_PARAMETER_COUNT_MAX = 4,
    // The number of procedures, foreign ones too, that a program can have. The build keeps one
    // record for each in a fixed array, so that it needs no allocation.
    X64_PROCEDURE_COUNT_MAX = 1024,
};

/**
 * On `X64_BUILD_STATUS_OK`, `bytes` is the filled start of the output buffer, as `byte` elements,
 * and it is a complete executable file. Otherwise `bytes.length` is zero and `subject` is the text
 * in the source that the error is about, as a view into the source that the syntax tree was parsed
 * from, so the caller can work out the position. `subject` has `bytes` NULL when the error is not
 * about any place in the source.
 */
typedef struct {
    Array_Slice bytes;
    Str subject;
    X64_Build_Status status;
} X64_Build_Result;

/**
 * Builds an executable for Windows on x64 (a PE32+ file for the console) from the syntax tree below
 * `root`, which must be the `AST_NODE_TYPE_MODULE` node of a successful `parse`. `nodes` holds
 * `Ast_Node` elements, and `bindings` holds the `Semantic_Binding` elements that `semantic_analyze`
 * made for the same tree. The file is written to `output_buffer`, which holds `byte` elements.
 *
 * Every procedure is machine code that follows the calling convention of Windows on x64, so that
 * the C library can be called with it. The program starts in a few instructions of its own that
 * call `main`, which has to be `main :: proc() Int ->`, and pass its result to `exit` of the C
 * library, so that the output of the program is flushed before it ends and its result is the exit
 * code of the process.
 *
 * `Int` is a 64-bit integer, and `C_Str` is the 64-bit address of bytes that end with a zero. A
 * string literal is a `Str`, which the target does not build, so it is only the argument of
 * `str_to_c_str`, which makes the `C_Str`.
 *
 * A foreign procedure that a `foreign libc ->` block declares is imported from `msvcrt.dll`, which
 * every installation of Windows has, by the name of the procedure. The compiler trusts the
 * declaration: `Int` is 64 bits wide, so a procedure of the C library that returns an `int` is not
 * declared with a result, or its upper 32 bits are whatever the library left there.
 *
 * Only a small part of the language is built: procedures with up to `X64_PARAMETER_COUNT_MAX`
 * parameters of the types `Int` and `C_Str`, whose statements are calls and returns, and whose
 * expressions are integer and string literals, parameters, and calls with such arguments. The rest
 * is rejected with a status other than `X64_BUILD_STATUS_OK`, so that nothing is built wrongly.
 */
X64_Build_Result x64_build(Array_Slice nodes, Array_Slice bindings, Ast_Node_Index root,
                           Array_Slice output_buffer);

#endif
