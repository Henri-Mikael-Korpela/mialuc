#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include <mialu/ast.h>

enum {
    // The deepest tree that can be printed. The printer keeps the path from the root in a fixed
    // array instead of recursing. The parser produces trees much shallower than this.
    AST_PRINT_DEPTH_MAX = 64,
    // Spaces printed per level of depth.
    AST_PRINT_INDENT_SPACES = 2,
};

void ast_print(Array_Slice nodes, Ast_Node_Index root) {
    assert(nodes.elements != NULL);
    assert(root < nodes.length);

    Ast_Node const *node_elements = nodes.elements;

    // The nodes on the path from the root to the current node, excluding the current node. Its
    // size is the depth of the current node.
    Ast_Node_Index path[AST_PRINT_DEPTH_MAX] = {0};
    size_t path_count = 0;

    Ast_Node_Index current = root;
    // Every node is visited once, so `nodes.length` steps are enough.
    for (size_t step = 0; step < nodes.length; step += 1) {
        assert(current < nodes.length);
        Ast_Node node = node_elements[current];

        // The casts are needed because `%*s` and `%.*s` take an `int` width.
        printf("%*s", (int)(path_count * AST_PRINT_INDENT_SPACES), "");
        switch (node.node_type) {
        case AST_NODE_TYPE_ADDITION:
            printf("ADDITION\n");
            break;
        case AST_NODE_TYPE_CONSTANT_DEFINITION:
            printf("CONSTANT_DEFINITION %.*s\n", (int)node.payload.constant_definition.name.length,
                   node.payload.constant_definition.name.bytes);
            break;
        case AST_NODE_TYPE_IDENTIFIER:
            printf("IDENTIFIER %.*s\n", (int)node.payload.identifier.name.length,
                   node.payload.identifier.name.bytes);
            break;
        case AST_NODE_TYPE_INTEGER_LITERAL:
            printf("INTEGER_LITERAL %" PRId64 "\n", node.payload.integer_literal.value);
            break;
        case AST_NODE_TYPE_MODULE:
            printf("MODULE\n");
            break;
        case AST_NODE_TYPE_MULTIPLICATION:
            printf("MULTIPLICATION\n");
            break;
        case AST_NODE_TYPE_PARAMETER:
            printf("PARAMETER %.*s\n", (int)node.payload.parameter.name.length,
                   node.payload.parameter.name.bytes);
            break;
        case AST_NODE_TYPE_PROCEDURE:
            if (node.payload.procedure.return_type.bytes == NULL) {
                printf("PROCEDURE\n");
            } else {
                printf("PROCEDURE returns %.*s\n", (int)node.payload.procedure.return_type.length,
                       node.payload.procedure.return_type.bytes);
            }
            break;
        case AST_NODE_TYPE_RETURN_STATEMENT:
            printf("RETURN_STATEMENT\n");
            break;
        case AST_NODE_TYPE_STRING_LITERAL:
            printf("STRING_LITERAL \"%.*s\"\n", (int)node.payload.string_literal.text.length,
                   node.payload.string_literal.text.bytes);
            break;
        case AST_NODE_TYPE_SUBTRACTION:
            printf("SUBTRACTION\n");
            break;
        case AST_NODE_TYPE_TYPE_NAME:
            printf("TYPE_NAME %.*s\n", (int)node.payload.type_name.name.length,
                   node.payload.type_name.name.bytes);
            break;
        case AST_NODE_TYPE_VARIABLE_DEFINITION:
            printf("VARIABLE_DEFINITION %.*s\n", (int)node.payload.variable_definition.name.length,
                   node.payload.variable_definition.name.bytes);
            break;
        }

        // Depth first: go down to the first child if there is one.
        if (node.first_child != AST_NODE_INDEX_NONE) {
            assert(path_count < AST_PRINT_DEPTH_MAX);
            path[path_count] = current;
            path_count += 1;
            current = node.first_child;
            continue;
        }

        // Otherwise go to the next sibling, climbing up while there is none. The step budget above
        // bounds this loop too, because every climb pops a node that was pushed by a step.
        while (node_elements[current].next_sibling == AST_NODE_INDEX_NONE && path_count > 0) {
            path_count -= 1;
            current = path[path_count];
        }
        if (node_elements[current].next_sibling == AST_NODE_INDEX_NONE) {
            break;
        }
        current = node_elements[current].next_sibling;
    }
}
