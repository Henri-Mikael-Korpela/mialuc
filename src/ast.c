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

void ast_print(Array_Slice nodes, uint32_t root) {
    assert(nodes.elements != NULL);
    assert(root < nodes.length);

    Ast_Node const *node_elements = nodes.elements;

    // The nodes on the path from the root to the current node, excluding the current node. Its
    // size is the depth of the current node.
    uint32_t path[AST_PRINT_DEPTH_MAX] = {0};
    size_t path_count = 0;

    uint32_t current = root;
    // Every node is visited once, so `nodes.length` steps are enough.
    for (size_t step = 0; step < nodes.length; step += 1) {
        assert(current < nodes.length);
        Ast_Node node = node_elements[current];

        // The casts are needed because `%*s` and `%.*s` take an `int` width.
        printf("%*s", (int)(path_count * AST_PRINT_INDENT_SPACES), "");
        switch (node.node_type) {
        case AST_NODE_TYPE_CONSTANT_DEFINITION:
            printf("CONSTANT_DEFINITION %.*s\n", (int)node.payload.constant_definition.name.length,
                   node.payload.constant_definition.name.bytes);
            break;
        case AST_NODE_TYPE_FUNCTION:
            if (node.payload.function.return_type.bytes == NULL) {
                printf("FUNCTION\n");
            } else {
                printf("FUNCTION returns %.*s\n", (int)node.payload.function.return_type.length,
                       node.payload.function.return_type.bytes);
            }
            break;
        case AST_NODE_TYPE_INTEGER_LITERAL:
            printf("INTEGER_LITERAL %" PRId64 "\n", node.payload.integer_literal.value);
            break;
        case AST_NODE_TYPE_MODULE:
            printf("MODULE\n");
            break;
        case AST_NODE_TYPE_RETURN_STATEMENT:
            printf("RETURN_STATEMENT\n");
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
