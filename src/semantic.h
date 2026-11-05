#ifndef MIALU_SEMANTIC_H
#define MIALU_SEMANTIC_H

#include <stdbool.h>
#include <stdint.h>

#include <mialu/array_slice.h>
#include <mialu/ast.h>
#include <mialu/str.h>

// Alphabetical, because nothing depends on the order. `SEMANTIC_BINDING_KIND_NONE` is what every
// node has that is not a use of a name.
typedef enum {
    SEMANTIC_BINDING_KIND_CONSTANT,
    // A global variable, which is defined at file scope and can be assigned to.
    SEMANTIC_BINDING_KIND_GLOBAL,
    // A `break` or a `continue`: the target is the loop or scope statement that it leaves or goes
    // on with.
    SEMANTIC_BINDING_KIND_JUMP_TARGET,
    SEMANTIC_BINDING_KIND_NONE,
    SEMANTIC_BINDING_KIND_PARAMETER,
    SEMANTIC_BINDING_KIND_VARIABLE,
} Semantic_Binding_Kind;

/**
 * What a use of a name stands for. `target` is the node that declares the name: an
 * `AST_NODE_TYPE_CONSTANT_DEFINITION` for a constant, an `AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION`
 * for a global variable, an `AST_NODE_TYPE_PARAMETER` for a parameter, or an
 * `AST_NODE_TYPE_VARIABLE_DEFINITION` for a variable, or an `AST_NODE_TYPE_FOR_STATEMENT` for
 * its iterator, which is a variable too. `position` is the place of the target among its kind of
 * siblings, counted from zero: the index of the constant among the constant definitions of the
 * module, of the global variable among the global variables of the module, of
 * the parameter among the parameters of its procedure, or of the variable among the variables of
 * its procedure, which are counted in the order of the source, where the iterator of a for
 * statement counts as one, also the ones in blocks, because each variable has a place of its own. A
 * target and its position are both kept, because a target alone would have to be searched for again
 * by whoever needs the position. For `kind` `SEMANTIC_BINDING_KIND_NONE` the `target` is
 * `AST_NODE_INDEX_NONE` and the `position` is zero.
 *
 * `is_address_taken` is not about a use of a name but about a definition: it is set on the binding
 * of an `AST_NODE_TYPE_VARIABLE_DEFINITION` or an `AST_NODE_TYPE_FOR_STATEMENT` whose variable has
 * its address taken somewhere, as in `%x`, `%x.field` or `%x.a.b`. It is false for every other
 * node. A target that keeps variables in places that have no address, such as WebAssembly locals,
 * needs to know which variables are not among them, and the analysis already meets every use.
 */
typedef struct {
    Ast_Node_Index target;
    uint32_t position;
    Semantic_Binding_Kind kind;
    bool is_address_taken;
} Semantic_Binding;

// Alphabetical, because nothing depends on the order.
typedef enum {
    // The name that an assignment assigns to is not a variable, but a parameter or a constant.
    SEMANTIC_STATUS_ASSIGNMENT_TARGET_NOT_VARIABLE,
    // An attribute list names the same attribute twice. The subject is the second one.
    SEMANTIC_STATUS_ATTRIBUTE_DUPLICATE,
    // An attribute that the language does not have.
    SEMANTIC_STATUS_ATTRIBUTE_UNKNOWN,
    // The name that an assignment assigns to is the iterator of a for statement, which is
    // read-only.
    SEMANTIC_STATUS_ASSIGNMENT_TARGET_ITERATOR,
    // The name that is called is not a procedure: a variable, a parameter or a constant string.
    SEMANTIC_STATUS_CALL_TARGET_NOT_PROCEDURE,
    SEMANTIC_STATUS_DEFINITION_NAME_DUPLICATE,
    // Two fields of one struct with the same name. The subject is the second one.
    SEMANTIC_STATUS_FIELD_NAME_DUPLICATE,
    // A second `import foreign js`. The subject is the name of its object.
    SEMANTIC_STATUS_FOREIGN_IMPORT_DUPLICATE,
    // A foreign procedure without an `import foreign js` before it. The subject is its name.
    SEMANTIC_STATUS_FOREIGN_IMPORT_MISSING,
    // A global variable without the `static` attribute. The subject is its name.
    SEMANTIC_STATUS_GLOBAL_NOT_STATIC,
    SEMANTIC_STATUS_IDENTIFIER_UNKNOWN,
    // A `break` that is not in a loop or a scope.
    SEMANTIC_STATUS_BREAK_OUTSIDE_SCOPE,
    // A `continue` that is not in the block of a loop.
    SEMANTIC_STATUS_JUMP_OUTSIDE_LOOP,
    // A statement with the label of a statement around it.
    SEMANTIC_STATUS_LABEL_DUPLICATE,
    // A `continue` with the label of a statement that is not a loop.
    SEMANTIC_STATUS_LABEL_NOT_LOOP,
    // A `break` or a `continue` with a label that no statement around it has.
    SEMANTIC_STATUS_LABEL_UNKNOWN,
    SEMANTIC_STATUS_OK,
    SEMANTIC_STATUS_PARAMETER_NAME_DUPLICATE,
    SEMANTIC_STATUS_PARAMETER_NAME_SHADOWS,
    SEMANTIC_STATUS_VARIABLE_NAME_DUPLICATE,
    SEMANTIC_STATUS_VARIABLE_NAME_SHADOWS,
} Semantic_Status;

/**
 * On `SEMANTIC_STATUS_OK`, `bindings` is the filled start of the binding buffer, as
 * `Semantic_Binding` elements, with one element per syntax tree node: the element at the index of a
 * node is what that node stands for, if it is an `AST_NODE_TYPE_IDENTIFIER`, or the procedure that
 * it calls, if it is an `AST_NODE_TYPE_CALL`, which is a binding of the kind
 * `SEMANTIC_BINDING_KIND_CONSTANT` whose target is the definition of the procedure. Any other node
 * has the kind `SEMANTIC_BINDING_KIND_NONE` for any other node. Otherwise `bindings.length` is zero
 * and `subject` is the name that the error is about, as a view into the source that the syntax tree
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
 * There are two scopes. The module scope has the name of every constant definition and of every
 * global variable, and a definition can be used anywhere in the module, also above where it is
 * defined. A global variable can be assigned to, like a variable, and keeps its value between the
 * calls of the procedures. The attributes of a global variable must be known and must not repeat,
 * and one of them must be `static`, because a global variable has static storage duration. The
 * scope of a procedure is inside the module scope and has the names of its parameters and of its
 * variables. A variable can be used only below its definition, and not in its own initial value.
 * The blocks of
 * an if statement are scopes inside the scope of the procedure: a variable that is defined in a
 * block can be used only in the rest of that block, including the blocks inside it, and not after
 * the block ends. The iterator of a for statement is a variable that is in scope in the block of
 * the statement only, not in its range, and it cannot be assigned to. A name is looked up in the
 * innermost scope that uses it first, and then in the scopes around it, up to the module scope.
 *
 * A constant can be a struct, such as `Person :: struct -> ...`. It has its name in the module
 * scope like any other constant, but it is a type and not a value, so it is not counted among the
 * constants that the `position` of a binding is the place of. The names of the fields of a struct
 * are not names in a scope, and must only differ from each other in the struct. The names of types
 * and of the fields of a literal or an access are not looked up here, because whether they stand
 * for something is for the target to check.
 *
 * Foreign procedures, which a `foreign js ->` block declares, are constants like the others and
 * are called the same way. They need an `import foreign js "name"` that says where they come from,
 * once and above all of them, because the object that it names is where the target looks for every
 * one of them.
 *
 * The name in a call, such as `f(1)`, must stand for a constant that is a procedure. It is looked
 * up among the definitions of the module only, because no other name can be the same as one of
 * them.
 *
 * A scope statement is a block of its own, and a `break` leaves the innermost loop or scope that it
 * is in, also when it is in the block of an if statement inside it, which is not one to leave. A
 * `break` with a label leaves the scope, loop or if statement with that label, which must be around
 * it. Labels are separate from the other names, but a statement cannot have the label of a
 * statement around it. A `continue` must be in the block of a for statement or a loop, and goes on
 * with the innermost one, also when scopes are in between, or with the loop that has its label. The
 * jump is bound to the statement that it leaves or goes on with, see
 * `SEMANTIC_BINDING_KIND_JUMP_TARGET`.
 *
 * An assignment, such as `x += 1`, uses the name that it assigns to like any other use of a name,
 * and the name must stand for a variable or a global variable: a parameter or a constant cannot be
 * assigned to. The same goes for the name that a field is assigned to through, as in `p.age = 1`.
 * Assigning through a pointer, as in `p^ = 1` or `p^.age = 1`, changes what `p` points to and not
 * `p`, so `p` can be a parameter. An operand of `%` that is a variable, or a field of one, marks
 * the variable as having its address taken, see `Semantic_Binding`.
 *
 * A name cannot be declared again where it is already visible, so a name has one meaning
 * everywhere it can be used, and there is no shadowing. Two definitions cannot have one name, two
 * parameters of a procedure cannot, and a parameter cannot have the name of a constant or of a
 * global variable. A variable cannot have the name of a constant, of a global variable, of a
 * parameter or of a variable that is in scope where it is
 * defined. Two blocks that are not inside each other can have a variable of one name each, because
 * neither is in scope in the other. A name that no scope has is an error as well.
 *
 * The analysis is about names only. It does not know what a type is, so whether the value that a
 * name stands for is of the right type is for the target to check.
 */
Semantic_Result semantic_analyze(Array_Slice nodes, Ast_Node_Index root,
                                 Array_Slice binding_buffer);

#endif
