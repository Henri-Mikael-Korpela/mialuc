#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include <mialu/ast.h>

enum {
    // The deepest tree that can be printed. The printer keeps the path from the root in a fixed
    // array instead of recursing. The parser produces trees much shallower than this: blocks nest
    // up to `AST_BLOCK_DEPTH_MAX` deep, and each level is an if statement and a block.
    AST_PRINT_DEPTH_MAX = 256,
    // Spaces printed per level of depth.
    AST_PRINT_INDENT_SPACES = 2,
};

bool ast_node_type_is_assignment(Ast_Node_Type node_type) {
    switch (node_type) {
    case AST_NODE_TYPE_ADDITION_ASSIGNMENT:
    case AST_NODE_TYPE_DIVISION_ASSIGNMENT:
    case AST_NODE_TYPE_MULTIPLICATION_ASSIGNMENT:
    case AST_NODE_TYPE_SUBTRACTION_ASSIGNMENT:
        return true;
    case AST_NODE_TYPE_ADDITION:
    case AST_NODE_TYPE_AND:
    case AST_NODE_TYPE_BLOCK:
    case AST_NODE_TYPE_BOOL_LITERAL:
    case AST_NODE_TYPE_BREAK_STATEMENT:
    case AST_NODE_TYPE_CALL:
    case AST_NODE_TYPE_CALL_STATEMENT:
    case AST_NODE_TYPE_COMPARISON:
    case AST_NODE_TYPE_CONSTANT_DEFINITION:
    case AST_NODE_TYPE_CONTINUE_STATEMENT:
    case AST_NODE_TYPE_DIVISION:
    case AST_NODE_TYPE_FOR_STATEMENT:
    case AST_NODE_TYPE_IDENTIFIER:
    case AST_NODE_TYPE_IF_STATEMENT:
    case AST_NODE_TYPE_INTEGER_LITERAL:
    case AST_NODE_TYPE_LABEL:
    case AST_NODE_TYPE_LABELED_STATEMENT:
    case AST_NODE_TYPE_LOOP_STATEMENT:
    case AST_NODE_TYPE_MODULE:
    case AST_NODE_TYPE_MULTIPLICATION:
    case AST_NODE_TYPE_NOT:
    case AST_NODE_TYPE_OR:
    case AST_NODE_TYPE_PARAMETER:
    case AST_NODE_TYPE_PROCEDURE:
    case AST_NODE_TYPE_RANGE:
    case AST_NODE_TYPE_RETURN_STATEMENT:
    case AST_NODE_TYPE_SCOPE_STATEMENT:
    case AST_NODE_TYPE_STRING_LITERAL:
    case AST_NODE_TYPE_SUBTRACTION:
    case AST_NODE_TYPE_TYPE_NAME:
    case AST_NODE_TYPE_VARIABLE_DEFINITION:
        return false;
    }

    // Unreachable: every node type is handled above.
    assert(false);
    return false;
}

Ast_Node_Index ast_procedure_end(Ast_Node const *nodes, size_t node_count,
                                 Ast_Node_Index procedure) {
    assert(procedure < node_count);
    assert(node_count < AST_NODE_INDEX_NONE);
    assert(nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);

    // The definition that the procedure is the value of is made before it, so the next one is the
    // first node that is not part of the procedure.
    size_t end = procedure + 1;
    while (end < node_count && nodes[end].node_type != AST_NODE_TYPE_CONSTANT_DEFINITION) {
        end += 1;
    }

    return (Ast_Node_Index)end;
}

/**
 * Returns the symbol of `operator`, as it is written in a program.
 */
static char const *ast_comparison_operator_name(Ast_Comparison_Operator operator) {
    switch (operator) {
    case AST_COMPARISON_OPERATOR_EQUAL:
        return "==";
    case AST_COMPARISON_OPERATOR_GREATER:
        return ">";
    case AST_COMPARISON_OPERATOR_GREATER_EQUAL:
        return ">=";
    case AST_COMPARISON_OPERATOR_LESS:
        return "<";
    case AST_COMPARISON_OPERATOR_LESS_EQUAL:
        return "<=";
    case AST_COMPARISON_OPERATOR_NOT_EQUAL:
        return "not=";
    }

    // Unreachable: every operator is handled above.
    assert(false);
    return "";
}

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
        case AST_NODE_TYPE_ADDITION_ASSIGNMENT:
            printf("ADDITION_ASSIGNMENT\n");
            break;
        case AST_NODE_TYPE_AND:
            printf("AND\n");
            break;
        case AST_NODE_TYPE_BLOCK:
            printf("BLOCK\n");
            break;
        case AST_NODE_TYPE_BOOL_LITERAL:
            printf("BOOL_LITERAL %s\n", node.payload.bool_literal.value ? "true" : "false");
            break;
        case AST_NODE_TYPE_BREAK_STATEMENT:
            printf("BREAK_STATEMENT\n");
            break;
        case AST_NODE_TYPE_CALL:
            printf("CALL %.*s\n", (int)node.payload.call.name.length, node.payload.call.name.bytes);
            break;
        case AST_NODE_TYPE_CALL_STATEMENT:
            printf("CALL_STATEMENT\n");
            break;
        case AST_NODE_TYPE_COMPARISON:
            printf("COMPARISON %s\n",
                   ast_comparison_operator_name(node.payload.comparison.operator));
            break;
        case AST_NODE_TYPE_CONSTANT_DEFINITION:
            printf("CONSTANT_DEFINITION %.*s\n", (int)node.payload.constant_definition.name.length,
                   node.payload.constant_definition.name.bytes);
            break;
        case AST_NODE_TYPE_CONTINUE_STATEMENT:
            printf("CONTINUE_STATEMENT\n");
            break;
        case AST_NODE_TYPE_DIVISION:
            printf("DIVISION\n");
            break;
        case AST_NODE_TYPE_DIVISION_ASSIGNMENT:
            printf("DIVISION_ASSIGNMENT\n");
            break;
        case AST_NODE_TYPE_FOR_STATEMENT:
            printf("FOR_STATEMENT %.*s\n", (int)node.payload.for_statement.name.length,
                   node.payload.for_statement.name.bytes);
            break;
        case AST_NODE_TYPE_IDENTIFIER:
            printf("IDENTIFIER %.*s\n", (int)node.payload.identifier.name.length,
                   node.payload.identifier.name.bytes);
            break;
        case AST_NODE_TYPE_IF_STATEMENT:
            printf("IF_STATEMENT\n");
            break;
        case AST_NODE_TYPE_INTEGER_LITERAL:
            printf("INTEGER_LITERAL %" PRId64 "\n", node.payload.integer_literal.value);
            break;
        case AST_NODE_TYPE_LABEL:
            printf("LABEL %.*s\n", (int)node.payload.label.name.length,
                   node.payload.label.name.bytes);
            break;
        case AST_NODE_TYPE_LABELED_STATEMENT:
            printf("LABELED_STATEMENT %.*s\n", (int)node.payload.label.name.length,
                   node.payload.label.name.bytes);
            break;
        case AST_NODE_TYPE_LOOP_STATEMENT:
            printf("LOOP_STATEMENT\n");
            break;
        case AST_NODE_TYPE_MODULE:
            printf("MODULE\n");
            break;
        case AST_NODE_TYPE_MULTIPLICATION:
            printf("MULTIPLICATION\n");
            break;
        case AST_NODE_TYPE_MULTIPLICATION_ASSIGNMENT:
            printf("MULTIPLICATION_ASSIGNMENT\n");
            break;
        case AST_NODE_TYPE_NOT:
            printf("NOT\n");
            break;
        case AST_NODE_TYPE_OR:
            printf("OR\n");
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
        case AST_NODE_TYPE_RANGE:
            printf("RANGE %s\n", node.payload.range.is_inclusive ? "inclusive" : "exclusive");
            break;
        case AST_NODE_TYPE_RETURN_STATEMENT:
            printf("RETURN_STATEMENT\n");
            break;
        case AST_NODE_TYPE_SCOPE_STATEMENT:
            printf("SCOPE_STATEMENT\n");
            break;
        case AST_NODE_TYPE_STRING_LITERAL:
            printf("STRING_LITERAL \"%.*s\"\n", (int)node.payload.string_literal.text.length,
                   node.payload.string_literal.text.bytes);
            break;
        case AST_NODE_TYPE_SUBTRACTION:
            printf("SUBTRACTION\n");
            break;
        case AST_NODE_TYPE_SUBTRACTION_ASSIGNMENT:
            printf("SUBTRACTION_ASSIGNMENT\n");
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
