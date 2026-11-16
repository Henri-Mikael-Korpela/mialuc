#ifndef MIALU_CONSTANT_H
#define MIALU_CONSTANT_H

#include <stdbool.h>
#include <stdint.h>

#include <mialu/ast.h>
#include <mialu/semantic.h>

enum {
    // The deepest that the evaluation goes: operators inside each other, and the constants that a
    // name leads to, which take two places each, the name and the definition. The evaluation keeps
    // this path in a fixed array instead of calling itself. It is the bound of the WebAssembly
    // target for an expression as well, so that a constant is never too deep for a target that
    // could write the same expression.
    CONSTANT_EXPRESSION_DEPTH_MAX = 1024,
};

/**
 * The type of a value that is known when the program is compiled. A literal has an untyped type: it
 * has a kind, a number, a truth value or a string, but no size, so it becomes the type that the
 * place it is used in wants, when its value fits in that type. A constant that is defined with a
 * type, such as `LIMIT : Byte : 200`, has that type, as any other value does, and does not change.
 * Alphabetical, because nothing depends on the order.
 */
typedef enum {
    CONSTANT_TYPE_BOOL,
    CONSTANT_TYPE_BYTE,
    // A constant that is defined with the type `F32`, or arithmetic of one with an untyped float.
    // Its value is a number that an `F32` can hold.
    CONSTANT_TYPE_F32,
    // A constant that is defined with the type `F64`, or arithmetic of one with an untyped float.
    CONSTANT_TYPE_F64,
    CONSTANT_TYPE_INT,
    CONSTANT_TYPE_STR,
    // `true`, `false`, a comparison of constants, and `and`, `or` and `not` of untyped truth
    // values.
    CONSTANT_TYPE_UNTYPED_BOOL,
    // A float literal, and arithmetic of untyped floats. Its value is a `double`.
    CONSTANT_TYPE_UNTYPED_FLOAT,
    // An integer literal, and arithmetic of untyped integers.
    CONSTANT_TYPE_UNTYPED_INTEGER,
    // A string literal.
    CONSTANT_TYPE_UNTYPED_STRING,
} Constant_Type;

// Alphabetical, because nothing depends on the order.
typedef enum {
    // A constant that is defined in terms of itself, through any number of others. `error` is the
    // name where the circle closes.
    CONSTANT_STATUS_CYCLE,
    // The expression, with the constants that it goes through, is deeper than
    // `CONSTANT_EXPRESSION_DEPTH_MAX`. `error` is the node where the limit was reached.
    CONSTANT_STATUS_DEPTH_EXCEEDED,
    // A division by zero, which has no value, also of floats, which have an infinity for it in a
    // running program but not in a constant. `error` is the division.
    CONSTANT_STATUS_DIVISION_BY_ZERO,
    // The definition of a constant whose value is being worked out. Meeting it again means that
    // its value depends on itself. No entry has it once the evaluation that set it is done.
    CONSTANT_STATUS_EVALUATING,
    // Not every part of the expression is known when the program is compiled, such as a variable
    // or a call, so it is not a constant. That is not an error in a procedure, where the value is
    // computed when the program runs, but it is one for the value of a constant.
    CONSTANT_STATUS_NOT_CONSTANT,
    CONSTANT_STATUS_OK,
    // An operator is given operands that it does not take, such as `true + 1`, a typed `Byte` in
    // arithmetic, an `Int` and a `Byte` together or two strings in a comparison. `error` is the
    // operator.
    CONSTANT_STATUS_OPERAND_TYPE_INVALID,
    // A result that does not fit in a 64-bit `Int`, or a float result that is infinite. Constant
    // arithmetic does not wrap around as the arithmetic of a running program does, because the
    // number is wrong as soon as it would. `error` is the operator.
    CONSTANT_STATUS_OVERFLOW,
    // A typed constant whose type is not `Int`, `Byte`, `F32`, `F64`, `Bool` or `Str`, which are
    // the types that a constant can have. `error` is the definition.
    CONSTANT_STATUS_TYPE_INVALID,
    // A value that is not of the type that it is given, such as a string for a constant of type
    // `Int`, or a typed `Int` for one of type `Byte`. `error` is the definition.
    CONSTANT_STATUS_TYPE_MISMATCH,
    // A node that was not evaluated. Every entry of the table starts with it.
    CONSTANT_STATUS_UNKNOWN,
    // An untyped integer that does not fit in the `Byte` that it becomes, such as 300, in the
    // value of a constant of type `Byte` or compared with a typed `Byte` constant, and an untyped
    // float that is above the largest `F32` in a constant of type `F32` or compared with a typed
    // `F32` constant. `error` is the definition or the comparison.
    CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE,
} Constant_Status;

/**
 * What the evaluation found out about one node. On `CONSTANT_STATUS_OK`, `type` is the type of the
 * value, `integer` is the number of an integer and 1 or 0 for `true` or `false`, and `string` is
 * the string literal of a string, whose bytes the targets already have, or `AST_NODE_INDEX_NONE`
 * when the value is not a string. On an error `error` is the node that the error is about, see
 * `Constant_Status`, and is `AST_NODE_INDEX_NONE` otherwise. `floating` is the number of a float,
 * which is the value that an `F32` holds when the type is `CONSTANT_TYPE_F32`, and is zero for
 * the types that are not floats.
 */
typedef struct {
    double floating;
    int64_t integer;
    Ast_Node_Index string;
    Ast_Node_Index error;
    Constant_Type type;
    Constant_Status status;
} Constant_Value;

/**
 * Works out whether `expression`, a node of the syntax tree in `nodes`, is a constant, and its
 * value, and writes that to `values`, which has one `Constant_Value` for each node, together with
 * what it found out about every node below it and every constant that it uses. `bindings` resolves
 * the names, one element per node, as `semantic_analyze` gives them. Every entry of `values` must
 * start as `CONSTANT_STATUS_UNKNOWN`, and an entry that is filled is not worked out again, so that
 * each node is evaluated once however many times it is used: the value of `B :: A + A` does not
 * work out `A` twice. Returns the entry of `expression`.
 *
 * An expression is a constant when it is a literal, the name of a constant whose value is one, or
 * an operator whose operands are constants: arithmetic of integers or of floats, a comparison, and
 * `and`, `or` and `not` of truth values. Anything else, such as a variable, a call or `nil`, is
 * `CONSTANT_STATUS_NOT_CONSTANT`. The definition of a constant, an
 * `AST_NODE_TYPE_CONSTANT_DEFINITION`, can be evaluated too, and its entry is the value that a use
 * of its name has, which is its value converted to its type when it has one.
 *
 * Arithmetic is exact, so a step whose result does not fit in 64 bits, or that divides by zero, is
 * an error rather than a number that wrapped around or a trap. Arithmetic of floats is done with
 * `double` and then rounded to the type, which for `+`, `-`, `*` and `/` of `F32` values is the
 * correctly rounded result that the running program has, and for `F64` values is the result that
 * the program has. An untyped operand takes the type
 * of a typed one, if it fits in it, so `LIMIT + 1` is an `Int` when `LIMIT : Int : 5`, and two
 * untyped operands give an untyped result. A comparison gives an untyped truth value, so that it
 * can be used wherever a `Bool` is.
 *
 * The operands are worked out from left to right, and an operator is not a constant as soon as an
 * operand is not, so an error in an operand of an expression that is not a constant, such as the
 * `1 / 0` of `x + 1 / 0`, is in the entry of that operand only. The analysis evaluates every
 * operator of a program for that reason, so that a target that builds a program which passed it
 * finds only `CONSTANT_STATUS_OK` or `CONSTANT_STATUS_NOT_CONSTANT` in the entries that it reads.
 */
Constant_Value constant_evaluate(Ast_Node const *nodes, size_t node_count,
                                 Semantic_Binding const *bindings, Constant_Value *values,
                                 Ast_Node_Index expression);

/**
 * Returns whether the value of `definition`, an `AST_NODE_TYPE_CONSTANT_DEFINITION`, is an
 * expression, which must then be a constant: anything but a procedure, a struct, an enum, or the
 * member of an enum such as `Color.RED`, which is written as a field of the name of the enum.
 */
bool constant_definition_is_value(Ast_Node const *nodes, Ast_Node_Index definition);

/**
 * Returns whether `value`, which must be `CONSTANT_STATUS_OK`, is an untyped integer that fits in a
 * `Byte`, which is a number from 0 to 255, so that it becomes one where a `Byte` is wanted.
 */
bool constant_is_byte(Constant_Value value);

/**
 * Returns whether `value`, which must be `CONSTANT_STATUS_OK`, is an untyped float that an `F32`
 * can hold, which is every number up to the largest `F32`, about 3.4e38, so that it becomes one
 * where an `F32` is wanted. Numbers with more digits than an `F32` has are rounded, so they hold.
 */
bool constant_is_f32(Constant_Value value);

/**
 * Returns the type that a value of `type` has where nothing says which type it should be, such as
 * the value of `x := 42`: `Int` for an untyped integer, `F64` for an untyped float, `Bool` for an
 * untyped truth value and `Str` for an untyped string. A typed value keeps its type.
 */
Constant_Type constant_type_default(Constant_Type type);

#endif
