#ifndef MIALU_AST_H
#define MIALU_AST_H

#include <stdbool.h>
#include <stdint.h>

#include <mialu/array_slice.h>
#include <mialu/static_assert.h>
#include <mialu/str.h>

// Alphabetical, because nothing depends on the order.
typedef enum {
    AST_NODE_TYPE_ADDITION,
    AST_NODE_TYPE_ADDITION_ASSIGNMENT,
    AST_NODE_TYPE_BLOCK,
    AST_NODE_TYPE_BOOL_LITERAL,
    AST_NODE_TYPE_COMPARISON,
    AST_NODE_TYPE_CONSTANT_DEFINITION,
    AST_NODE_TYPE_DIVISION,
    AST_NODE_TYPE_DIVISION_ASSIGNMENT,
    AST_NODE_TYPE_IDENTIFIER,
    AST_NODE_TYPE_IF_STATEMENT,
    AST_NODE_TYPE_INTEGER_LITERAL,
    AST_NODE_TYPE_MODULE,
    AST_NODE_TYPE_MULTIPLICATION,
    AST_NODE_TYPE_MULTIPLICATION_ASSIGNMENT,
    AST_NODE_TYPE_PARAMETER,
    AST_NODE_TYPE_PROCEDURE,
    AST_NODE_TYPE_RETURN_STATEMENT,
    AST_NODE_TYPE_STRING_LITERAL,
    AST_NODE_TYPE_SUBTRACTION,
    AST_NODE_TYPE_SUBTRACTION_ASSIGNMENT,
    AST_NODE_TYPE_TYPE_NAME,
    AST_NODE_TYPE_VARIABLE_DEFINITION,
} Ast_Node_Type;

// Alphabetical, because nothing depends on the order.
typedef enum {
    AST_COMPARISON_OPERATOR_EQUAL,
    AST_COMPARISON_OPERATOR_GREATER,
    AST_COMPARISON_OPERATOR_GREATER_EQUAL,
    AST_COMPARISON_OPERATOR_LESS,
    AST_COMPARISON_OPERATOR_LESS_EQUAL,
    AST_COMPARISON_OPERATOR_NOT_EQUAL,
} Ast_Comparison_Operator;

enum {
    // The most blocks that can be inside each other in a procedure, not counting the body of the
    // procedure. The parser and the analysis keep the blocks that are open in a fixed array
    // instead of recursing, and a deeper nesting is rejected by the parser.
    AST_BLOCK_DEPTH_MAX = 32,
};

/**
 * The position of a node in the node buffer. Nodes refer to each other by index instead of by
 * pointer, so the buffer can be allocated once and the indexes stay valid. An index is 32 bits,
 * which is far more nodes than a source file can have.
 */
typedef uint32_t Ast_Node_Index;

// Means "no node". It is not zero, because zero is the index of the first node.
static Ast_Node_Index const AST_NODE_INDEX_NONE = UINT32_MAX;

/**
 * One node of the syntax tree. The children of a node form a linked list: `first_child` is the
 * head, `next_sibling` of each child points at the following one, and `last_child` makes appending
 * a child a constant-time operation. Which children a node has depends on `node_type`:
 *
 * - `AST_NODE_TYPE_ADDITION`: two children, the left and the right operand, in that order.
 * - `AST_NODE_TYPE_ADDITION_ASSIGNMENT`: two children, the `AST_NODE_TYPE_IDENTIFIER` of the
 *   variable that gets the sum, and the value that is added to it, in that order. It is a
 *   statement of a procedure body.
 * - `AST_NODE_TYPE_BLOCK`: one child per statement. It is the body of a branch of an
 *   `AST_NODE_TYPE_IF_STATEMENT`, and it has its own scope: a variable defined in it cannot be
 *   used below it. Its payload has the end of the block, see below.
 * - `AST_NODE_TYPE_BOOL_LITERAL`: no children. It is `true` or `false`, which is in the payload.
 * - `AST_NODE_TYPE_COMPARISON`: two children, the left and the right operand, in that order. It is
 *   an expression of type `Bool` with the lowest precedence of the operators, so its operands are
 *   not comparisons. The operator is in the payload.
 * - `AST_NODE_TYPE_CONSTANT_DEFINITION`: one child, the value that is bound to the name. It is
 *   an `AST_NODE_TYPE_PROCEDURE` or an `AST_NODE_TYPE_STRING_LITERAL`.
 * - `AST_NODE_TYPE_DIVISION`: two children, the dividend and the divisor, in that order.
 * - `AST_NODE_TYPE_DIVISION_ASSIGNMENT`: two children, the `AST_NODE_TYPE_IDENTIFIER` of the
 *   variable that is divided and gets the quotient, and the divisor, in that order. It is a
 *   statement of a procedure body.
 * - `AST_NODE_TYPE_IDENTIFIER`: no children. It is a use of a name, such as a parameter.
 * - `AST_NODE_TYPE_IF_STATEMENT`: a chain of `if`, any number of `else if` and an optional
 *   `else`, as the children in the order of the source: a condition, which is any expression
 *   (the target requires it to be a `Bool`), and the `AST_NODE_TYPE_BLOCK` that runs when it holds, for the `if`
 *   and for every `else if`, and then, when `has_else` is set in the payload, the last child is the
 *   `AST_NODE_TYPE_BLOCK` of the `else`.
 * - `AST_NODE_TYPE_INTEGER_LITERAL`: no children.
 * - `AST_NODE_TYPE_MODULE`: one child per top-level definition. It is the root of the tree.
 * - `AST_NODE_TYPE_MULTIPLICATION`: two children, the left and the right operand, in that order.
 * - `AST_NODE_TYPE_MULTIPLICATION_ASSIGNMENT`: two children, the `AST_NODE_TYPE_IDENTIFIER` of the
 *   variable that gets the product, and the factor that it is multiplied by, in that order. It
 *   is a statement of a procedure body.
 * - `AST_NODE_TYPE_PARAMETER`: one child, its type, which is a `AST_NODE_TYPE_TYPE_NAME`.
 * - `AST_NODE_TYPE_PROCEDURE`: first one child per parameter, then one child per statement of the
 *   body, which are variable definitions, assignments, if statements and return statements. The
 *   return type is in the payload.
 * - `AST_NODE_TYPE_RETURN_STATEMENT`: one child, the returned value.
 * - `AST_NODE_TYPE_STRING_LITERAL`: no children.
 * - `AST_NODE_TYPE_SUBTRACTION`: two children, the minuend and the subtrahend, in that order.
 * - `AST_NODE_TYPE_SUBTRACTION_ASSIGNMENT`: two children, the `AST_NODE_TYPE_IDENTIFIER` of the
 *   variable that gets the difference, and the value that is subtracted from it, in that order.
 *   It is a statement of a procedure body.
 * - `AST_NODE_TYPE_TYPE_NAME`: no children.
 * - `AST_NODE_TYPE_VARIABLE_DEFINITION`: the initial value, preceded by the type, an
 *   `AST_NODE_TYPE_TYPE_NAME`, when the definition gives one. Without it the type is deduced from
 *   the value, so the value is the only child. The name is in the payload.
 */
typedef struct {
    Ast_Node_Type node_type;
    Ast_Node_Index first_child;
    Ast_Node_Index last_child;
    Ast_Node_Index next_sibling;
    // This union is named, because unnamed unions are C11 and the project uses C99.
    // Only the member that matches `node_type` is meaningful.
    union {
        struct {
            // Views the source text, so it is valid only as long as the source is.
            Str name;
        } constant_definition;
        struct {
            // Views the source text, like the other names.
            Str name;
        } variable_definition;
        struct {
            // The index after the last node that is in the block. The nodes of a block are the
            // ones from its own index up to this, because the parser makes the nodes in the order
            // of the source.
            Ast_Node_Index end;
        } block;
        struct {
            bool value;
        } bool_literal;
        struct {
            Ast_Comparison_Operator operator;
        } comparison;
        struct {
            bool has_else;
        } if_statement;
        struct {
            // The name of the return type, as a view of the source text. Empty, with `bytes` NULL,
            // when the procedure does not declare one. Types are only names for now, so a name is
            // enough; when types get more structure, this should become a child node.
            Str return_type;
        } procedure;
        struct {
            // Views the source text, like the other names.
            Str name;
        } identifier;
        struct {
            int64_t value;
        } integer_literal;
        struct {
            Str name;
        } parameter;
        struct {
            // The bytes between the quotes, as a view of the source text, valid UTF-8, and never
            // with `bytes` NULL.
            Str text;
        } string_literal;
        struct {
            Str name;
        } type_name;
    } payload;
} Ast_Node;

// Four 4-byte fields, followed by the 16-byte payload.
STATIC_ASSERT(sizeof(Ast_Node) == 32, ast_node_size_is_32_bytes);

/**
 * Returns whether nodes of `node_type` are assignments: statements that change the value of a
 * variable, such as `x += 1`. They all have the same shape, the variable and then the value, see
 * the children of each type above.
 */
bool ast_node_type_is_assignment(Ast_Node_Type node_type);

/**
 * Returns the index after the last node of the procedure at index `procedure`, which is the index
 * of the next constant definition, or `node_count` for the last one. The nodes of a procedure,
 * including the ones of its nested blocks, are the ones after its own, because the parser makes
 * the nodes in the order of the source.
 */
Ast_Node_Index ast_procedure_end(Ast_Node const *nodes, size_t node_count,
                                 Ast_Node_Index procedure);

/**
 * Prints the tree below `root`, one node per line, indented by depth. `nodes` holds `Ast_Node`
 * elements.
 */
void ast_print(Array_Slice nodes, Ast_Node_Index root);

#endif
