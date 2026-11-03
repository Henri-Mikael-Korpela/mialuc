#ifndef MIALU_AST_H
#define MIALU_AST_H

#include <stdint.h>

#include <mialu/array_slice.h>
#include <mialu/static_assert.h>
#include <mialu/str.h>

// Alphabetical, because nothing depends on the order.
typedef enum {
    AST_NODE_TYPE_ADDITION,
    AST_NODE_TYPE_CONSTANT_DEFINITION,
    AST_NODE_TYPE_IDENTIFIER,
    AST_NODE_TYPE_INTEGER_LITERAL,
    AST_NODE_TYPE_MODULE,
    AST_NODE_TYPE_PARAMETER,
    AST_NODE_TYPE_PROCEDURE,
    AST_NODE_TYPE_RETURN_STATEMENT,
    AST_NODE_TYPE_STRING_LITERAL,
    AST_NODE_TYPE_TYPE_NAME,
    AST_NODE_TYPE_VARIABLE_DEFINITION,
} Ast_Node_Type;

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
 * - `AST_NODE_TYPE_CONSTANT_DEFINITION`: one child, the value that is bound to the name. It is
 *   an `AST_NODE_TYPE_PROCEDURE` or an `AST_NODE_TYPE_STRING_LITERAL`.
 * - `AST_NODE_TYPE_IDENTIFIER`: no children. It is a use of a name, such as a parameter.
 * - `AST_NODE_TYPE_INTEGER_LITERAL`: no children.
 * - `AST_NODE_TYPE_MODULE`: one child per top-level definition. It is the root of the tree.
 * - `AST_NODE_TYPE_PARAMETER`: one child, its type, which is a `AST_NODE_TYPE_TYPE_NAME`.
 * - `AST_NODE_TYPE_PROCEDURE`: first one child per parameter, then one child per statement of the
 *   body, which are variable definitions and return statements. The return type is in the
 *   payload.
 * - `AST_NODE_TYPE_RETURN_STATEMENT`: one child, the returned value.
 * - `AST_NODE_TYPE_STRING_LITERAL`: no children.
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
 * Prints the tree below `root`, one node per line, indented by depth. `nodes` holds `Ast_Node`
 * elements.
 */
void ast_print(Array_Slice nodes, Ast_Node_Index root);

#endif
