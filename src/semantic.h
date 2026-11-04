#ifndef MIALU_SEMANTIC_H
#define MIALU_SEMANTIC_H

#include <stddef.h>
#include <stdint.h>

#include <mialu/array_slice.h>
#include <mialu/ast.h>
#include <mialu/str.h>

// Alphabetical, because nothing depends on the order. `SEMANTIC_BINDING_KIND_NONE` is what every
// node has that is not a use of a name.
typedef enum {
    SEMANTIC_BINDING_KIND_CONSTANT,
    // A `break` or a `continue`: the target is the loop or scope statement that it leaves or goes
    // on with.
    SEMANTIC_BINDING_KIND_JUMP_TARGET,
    SEMANTIC_BINDING_KIND_NONE,
    SEMANTIC_BINDING_KIND_PARAMETER,
    SEMANTIC_BINDING_KIND_VARIABLE,
} Semantic_Binding_Kind;

/**
 * What a use of a name stands for. `target` is the node that declares the name: an
 * `AST_NODE_TYPE_CONSTANT_DEFINITION` for a constant, an `AST_NODE_TYPE_PARAMETER` for a
 * parameter, or an `AST_NODE_TYPE_VARIABLE_DEFINITION` for a variable, or an
 * `AST_NODE_TYPE_FOR_STATEMENT` for its iterator, which is a variable too. `position` is the place of
 * the target among its kind of siblings, counted from zero: the index of the definition among the
 * definitions of the module, of the parameter among the parameters of its procedure, or of the
 * variable among the variables of its procedure, which are counted in the order of the source,
 * where the iterator of a for statement counts as one, also the ones in blocks, because each variable has a place of its own. A target and its position
 * are both kept, because a target alone would have to be searched for again by whoever needs the
 * position. For `kind`
 * `SEMANTIC_BINDING_KIND_NONE` the `target` is `AST_NODE_INDEX_NONE` and the `position` is zero.
 */
typedef struct {
    Ast_Node_Index target;
    uint32_t position;
    Semantic_Binding_Kind kind;
} Semantic_Binding;

// Alphabetical, because nothing depends on the order.
typedef enum {
    // The name that an assignment assigns to is not a variable, but a parameter or a constant.
    SEMANTIC_STATUS_ASSIGNMENT_TARGET_NOT_VARIABLE,
    // The name that an assignment assigns to is the iterator of a for statement, which is read-only.
    SEMANTIC_STATUS_ASSIGNMENT_TARGET_ITERATOR,
    // The name that is called is not a procedure: a variable, a parameter or a constant string.
    SEMANTIC_STATUS_CALL_TARGET_NOT_PROCEDURE,
    SEMANTIC_STATUS_DEFINITION_NAME_DUPLICATE,
    SEMANTIC_STATUS_IDENTIFIER_UNKNOWN,
    // A `break` that is not in a loop or a scope.
    SEMANTIC_STATUS_BREAK_OUTSIDE_SCOPE,
    // A `continue` that is not in the block of a loop.
    SEMANTIC_STATUS_JUMP_OUTSIDE_LOOP,
    // A scope with the label of a scope around it.
    SEMANTIC_STATUS_LABEL_DUPLICATE,
    // A `break` with a label that no scope around it has.
    SEMANTIC_STATUS_LABEL_UNKNOWN,
    SEMANTIC_STATUS_OK,
    SEMANTIC_STATUS_PARAMETER_NAME_DUPLICATE,
    SEMANTIC_STATUS_PARAMETER_NAME_SHADOWS,
    SEMANTIC_STATUS_VARIABLE_NAME_DUPLICATE,
    SEMANTIC_STATUS_VARIABLE_NAME_SHADOWS,
} Semantic_Status;

/**
 * On `SEMANTIC_STATUS_OK`, `bindings` is the filled start of the binding buffer, as
 * `Semantic_Binding` elements, with one element per syntax tree node: the element at the index of
 * a node is what that node stands for, if it is an `AST_NODE_TYPE_IDENTIFIER`, or the procedure
 * that it calls, if it is an `AST_NODE_TYPE_CALL`, which is a binding of the kind
 * `SEMANTIC_BINDING_KIND_CONSTANT` whose target is the definition of the procedure. Any other node has the kind
 * `SEMANTIC_BINDING_KIND_NONE` for any other node. Otherwise `bindings.length` is zero and
 * `subject` is the name that the error is about, as a view into the source that the syntax tree
 * was parsed from, so the caller can work out the position.
 */
typedef struct {
    Array_Slice bindings;
    Str subject;
    Semantic_Status status;
} Semantic_Result;

/**
 * Resolves the names of the syntax tree below `root`, which must be the `AST_NODE_TYPE_MODULE`
 * node of a successful `parse`. `nodes` holds `Ast_Node` elements. The bindings are written to
 * `binding_buffer`, which holds `Semantic_Binding` elements and must have room for one per node.
 *
 * There are two scopes. The module scope has the name of every constant definition, and a
 * definition can be used anywhere in the module, also above where it is defined. The scope of a
 * procedure is inside the module scope and has the names of its parameters and of its variables. A
 * variable can be used only below its definition, and not in its own initial value. The blocks of
 * an if statement are scopes inside the scope of the procedure: a variable that is defined in a
 * block can be used only in the rest of that block, including the blocks inside it, and not after
 * the block ends. The iterator of a for statement is a variable that is in scope in the block of the
 * statement only, not in its range, and it cannot be assigned to. A name is looked up in the innermost scope that uses it first, and then in the
 * scopes around it, up to the module scope.
 *
 * The name in a call, such as `f(1)`, must stand for a constant that is a procedure. It is looked
 * up among the constants only, because no other name can be the same as one of them.
 *
 * A scope statement is a block of its own, and a `break` leaves the innermost loop or scope that it
 * is in, also when it is in the block of an if statement inside it, which is not one to leave. A
 * `break` with a label leaves the scope with that label, which must be around it. Labels are
 * separate from the other names, but a scope cannot have the label of a scope around it. A
 * `continue` must be in the block of a for statement or a loop, and goes on with the innermost
 * one, also when scopes are in between. The jump is bound to the statement that it leaves or goes
 * on with, see `SEMANTIC_BINDING_KIND_JUMP_TARGET`.
 *
 * An assignment, such as `x += 1`, uses the name that it assigns to like any other use of a name,
 * and the name must stand for a variable: a parameter or a constant cannot be assigned to.
 *
 * A name cannot be declared again where it is already visible, so a name has one meaning
 * everywhere it can be used, and there is no shadowing. Two definitions cannot have one name, two
 * parameters of a procedure cannot, and a parameter cannot have the name of a constant. A variable
 * cannot have the name of a constant, of a parameter or of a variable that is in scope where it is
 * defined. Two blocks that are not inside each other can have a variable of one name each, because
 * neither is in scope in the other. A name that no scope has is an error as well.
 *
 * The analysis is about names only. It does not know what a type is, so whether the value that a
 * name stands for is of the right type is for the target to check.
 */
Semantic_Result semantic_analyze(Array_Slice nodes, Ast_Node_Index root,
                                 Array_Slice binding_buffer);

#endif
