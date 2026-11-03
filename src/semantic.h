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
    SEMANTIC_BINDING_KIND_NONE,
    SEMANTIC_BINDING_KIND_PARAMETER,
    SEMANTIC_BINDING_KIND_VARIABLE,
} Semantic_Binding_Kind;

/**
 * What a use of a name stands for. `target` is the node that declares the name: an
 * `AST_NODE_TYPE_CONSTANT_DEFINITION` for a constant, an `AST_NODE_TYPE_PARAMETER` for a
 * parameter, or an `AST_NODE_TYPE_VARIABLE_DEFINITION` for a variable. `position` is the place of
 * the target among its kind of siblings, counted from zero: the index of the definition among the
 * definitions of the module, of the parameter among the parameters of its procedure, or of the
 * variable among the variables of its procedure. A target and its position are both kept, because a target alone
 * would have to be searched for again by whoever needs the position. For `kind`
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
    SEMANTIC_STATUS_DEFINITION_NAME_DUPLICATE,
    SEMANTIC_STATUS_EXPRESSION_TOO_DEEP,
    SEMANTIC_STATUS_IDENTIFIER_UNKNOWN,
    SEMANTIC_STATUS_OK,
    SEMANTIC_STATUS_PARAMETER_NAME_DUPLICATE,
    SEMANTIC_STATUS_PARAMETER_NAME_SHADOWS,
    SEMANTIC_STATUS_VARIABLE_NAME_DUPLICATE,
    SEMANTIC_STATUS_VARIABLE_NAME_SHADOWS,
} Semantic_Status;

/**
 * On `SEMANTIC_STATUS_OK`, `bindings` is the filled start of the binding buffer, as
 * `Semantic_Binding` elements, with one element per syntax tree node: the element at the index of
 * a node is what that node stands for, if it is an `AST_NODE_TYPE_IDENTIFIER`, and has the kind
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
 * variable can be used only below its definition, and not in its own initial value. A name is
 * looked up in the scope of the procedure that uses it first, and then in the module scope.
 *
 * An assignment, such as `x += 1`, uses the name that it assigns to like any other use of a name,
 * and the name must stand for a variable: a parameter or a constant cannot be assigned to.
 *
 * A name cannot be declared again where it is already visible, so a name has one meaning
 * everywhere it can be used, and there is no shadowing. Two definitions cannot have one name, two
 * parameters of a procedure cannot, and a parameter cannot have the name of a constant. A variable
 * cannot have the name of a constant, of a parameter or of an earlier variable of its procedure. A name that
 * no scope has is an error as well.
 *
 * The analysis is about names only. It does not know what a type is, so whether the value that a
 * name stands for is of the right type is for the target to check.
 */
Semantic_Result semantic_analyze(Array_Slice nodes, Ast_Node_Index root,
                                 Array_Slice binding_buffer);

#endif
