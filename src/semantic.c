#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <mialu/semantic.h>

enum {
    // The deepest expression that can be walked. The walk keeps the path from the root of the
    // expression in a fixed array instead of recursing. Left associative operators nest to the
    // left, so `a + b + c + ...` is as deep as it has operators. A longer chain is rejected.
    SEMANTIC_EXPRESSION_DEPTH_MAX = 1024,
};

/**
 * The state shared by the functions of the analysis. A function that can fail returns false after
 * it recorded the error here, and its caller then returns false too without recording anything
 * more. So only the first error is kept.
 */
typedef struct {
    Ast_Node const *nodes;
    Semantic_Binding *bindings;
    Str subject;
    size_t node_count;
    Ast_Node_Index module;
    Semantic_Status status;
} Semantic_Analyzer;

static Semantic_Binding semantic_definition_find    (Semantic_Analyzer const *analyzer, Str name);
static bool             semantic_assignment_resolve (Semantic_Analyzer *analyzer,
                                                     Ast_Node_Index definition,
                                                     Ast_Node_Index assignment);
static bool             semantic_definitions_check  (Semantic_Analyzer *analyzer);
static bool             semantic_expression_resolve (Semantic_Analyzer *analyzer,
                                                     Ast_Node_Index definition,
                                                     Ast_Node_Index statement,
                                                     Ast_Node_Index expression);
static bool             semantic_fail               (Semantic_Analyzer *analyzer,
                                                     Semantic_Status status, Str subject);
static bool             semantic_identifier_resolve (Semantic_Analyzer *analyzer,
                                                     Ast_Node_Index procedure,
                                                     Ast_Node_Index statement,
                                                     Ast_Node_Index identifier);
static bool             semantic_parameter_check    (Semantic_Analyzer *analyzer,
                                                     Ast_Node_Index procedure,
                                                     Ast_Node_Index parameter);
static Semantic_Binding semantic_parameter_find     (Semantic_Analyzer const *analyzer,
                                                     Ast_Node_Index procedure, Str name);
static bool             semantic_procedure_resolve  (Semantic_Analyzer *analyzer,
                                                     Ast_Node_Index definition);
static bool             semantic_variable_check     (Semantic_Analyzer *analyzer,
                                                     Ast_Node_Index procedure,
                                                     Ast_Node_Index variable);
static Semantic_Binding semantic_variable_find      (Semantic_Analyzer const *analyzer,
                                                     Ast_Node_Index procedure,
                                                     Ast_Node_Index before, Str name);

Semantic_Result semantic_analyze(Array_Slice nodes, Ast_Node_Index root,
                                 Array_Slice binding_buffer) {
    assert(nodes.elements != NULL);
    assert(binding_buffer.elements != NULL);
    assert(binding_buffer.length >= nodes.length);
    assert(root < nodes.length);

    Ast_Node const *node_elements = nodes.elements;
    assert(node_elements[root].node_type == AST_NODE_TYPE_MODULE);

    // Every node starts as not being a use of a name, so that the table can be read at any index.
    Semantic_Binding *binding_elements = binding_buffer.elements;
    for (size_t i = 0; i < nodes.length; i += 1) {
        binding_elements[i] = (Semantic_Binding){
            .target = AST_NODE_INDEX_NONE,
            .position = 0,
            .kind = SEMANTIC_BINDING_KIND_NONE,
        };
    }

    Semantic_Analyzer analyzer = {
        .nodes = node_elements,
        .bindings = binding_elements,
        .subject = STR_EMPTY,
        .node_count = nodes.length,
        .module = root,
        .status = SEMANTIC_STATUS_OK,
    };

    // The definitions are checked before any name is looked up, so that a procedure can use a
    // constant that is defined below it, and so that a lookup finds the only definition of a name.
    bool is_ok = semantic_definitions_check(&analyzer);

    Ast_Node_Index definition = node_elements[root].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < nodes.length && definition != AST_NODE_INDEX_NONE && is_ok; i += 1) {
        Ast_Node_Index value = node_elements[definition].first_child;
        if (node_elements[value].node_type == AST_NODE_TYPE_PROCEDURE) {
            is_ok = semantic_procedure_resolve(&analyzer, definition);
        }
        definition = node_elements[definition].next_sibling;
    }

    // Nothing is handed out when there was an error, because the table is only partly filled.
    size_t length = 0;
    if (is_ok) {
        length = nodes.length;
    }

    assert(is_ok == (analyzer.status == SEMANTIC_STATUS_OK));
    return (Semantic_Result){
        .bindings = array_slice_make(binding_elements, length),
        .subject = analyzer.subject,
        .status = analyzer.status,
    };
}

/**
 * Checks that no two definitions of the module have one name. Returns false after it recorded an
 * error.
 */
static bool semantic_definitions_check(Semantic_Analyzer *analyzer) {
    assert(analyzer->nodes[analyzer->module].node_type == AST_NODE_TYPE_MODULE);

    Ast_Node_Index definition = analyzer->nodes[analyzer->module].first_child;
    for (size_t i = 0; i < analyzer->node_count && definition != AST_NODE_INDEX_NONE; i += 1) {
        Str name = analyzer->nodes[definition].payload.constant_definition.name;

        // The search finds the first definition of the name, which is this one unless an earlier
        // definition has the name as well.
        Semantic_Binding first = semantic_definition_find(analyzer, name);
        assert(first.kind == SEMANTIC_BINDING_KIND_CONSTANT);
        if (first.target != definition) {
            return semantic_fail(analyzer, SEMANTIC_STATUS_DEFINITION_NAME_DUPLICATE, name);
        }

        definition = analyzer->nodes[definition].next_sibling;
    }

    return true;
}

/**
 * Resolves the names in the body of the procedure that `definition` binds to its name, and checks
 * its parameters. Returns false after it recorded an error.
 */
static bool semantic_procedure_resolve(Semantic_Analyzer *analyzer, Ast_Node_Index definition) {
    assert(definition < analyzer->node_count);
    assert(analyzer->nodes[definition].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION);

    Ast_Node_Index procedure = analyzer->nodes[definition].first_child;
    assert(analyzer->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);

    // The parameters come first, so by the time that a statement is reached all of them are known.
    Ast_Node_Index child = analyzer->nodes[procedure].first_child;
    for (size_t i = 0; i < analyzer->node_count && child != AST_NODE_INDEX_NONE; i += 1) {
        Ast_Node const *child_node = &analyzer->nodes[child];

        if (child_node->node_type == AST_NODE_TYPE_PARAMETER) {
            if (semantic_parameter_check(analyzer, procedure, child) == false) {
                return false;
            }
        } else if (child_node->node_type == AST_NODE_TYPE_VARIABLE_DEFINITION) {
            // The value is the last child, after the type if there is one. It is resolved before
            // the variable is declared, so that the variable is not visible in its own value.
            Ast_Node_Index value = child_node->last_child;
            if (semantic_expression_resolve(analyzer, definition, child, value) == false) {
                return false;
            }
            if (semantic_variable_check(analyzer, procedure, child) == false) {
                return false;
            }
        } else if (ast_node_type_is_assignment(child_node->node_type)) {
            if (semantic_assignment_resolve(analyzer, definition, child) == false) {
                return false;
            }
        } else {
            assert(child_node->node_type == AST_NODE_TYPE_RETURN_STATEMENT);
            if (semantic_expression_resolve(analyzer, definition, child,
                                            child_node->first_child) == false) {
                return false;
            }
        }

        child = child_node->next_sibling;
    }

    return true;
}

/**
 * Resolves the names of `assignment`, a statement of the procedure that `definition` binds to its
 * name. Its first child is the name that is assigned to and the second is the value, which are
 * both resolved like any other use of a name: a variable that is assigned to must be defined above
 * the assignment, and the value may use it. The name must stand for a variable. Returns false after
 * it recorded an error.
 */
static bool semantic_assignment_resolve(Semantic_Analyzer *analyzer, Ast_Node_Index definition,
                                        Ast_Node_Index assignment) {
    assert(assignment < analyzer->node_count);
    assert(ast_node_type_is_assignment(analyzer->nodes[assignment].node_type));

    if (semantic_expression_resolve(analyzer, definition, assignment, assignment) == false) {
        return false;
    }

    Ast_Node_Index target = analyzer->nodes[assignment].first_child;
    assert(analyzer->nodes[target].node_type == AST_NODE_TYPE_IDENTIFIER);
    if (analyzer->bindings[target].kind != SEMANTIC_BINDING_KIND_VARIABLE) {
        Str name = analyzer->nodes[target].payload.identifier.name;
        return semantic_fail(analyzer, SEMANTIC_STATUS_ASSIGNMENT_TARGET_NOT_VARIABLE, name);
    }

    return true;
}

/**
 * Checks that the name of `parameter` is new: no earlier parameter of the procedure has it, and no
 * constant has it. A parameter with the name of a constant would hide the constant in the body, and
 * the same name would mean two things in one program. Returns false after it recorded an error.
 */
static bool semantic_parameter_check(Semantic_Analyzer *analyzer, Ast_Node_Index procedure,
                                     Ast_Node_Index parameter) {
    assert(parameter < analyzer->node_count);
    assert(analyzer->nodes[parameter].node_type == AST_NODE_TYPE_PARAMETER);

    Str name = analyzer->nodes[parameter].payload.parameter.name;

    // The search finds the first parameter of the name, which is this one unless an earlier
    // parameter has the name as well.
    Semantic_Binding first = semantic_parameter_find(analyzer, procedure, name);
    assert(first.kind == SEMANTIC_BINDING_KIND_PARAMETER);
    if (first.target != parameter) {
        return semantic_fail(analyzer, SEMANTIC_STATUS_PARAMETER_NAME_DUPLICATE, name);
    }

    Semantic_Binding constant = semantic_definition_find(analyzer, name);
    if (constant.kind != SEMANTIC_BINDING_KIND_NONE) {
        return semantic_fail(analyzer, SEMANTIC_STATUS_PARAMETER_NAME_SHADOWS, name);
    }

    return true;
}

/**
 * Checks that the name of `variable` is new: no constant, parameter or earlier variable of the
 * procedure has it. Like a parameter, a variable with such a name would hide it. Returns false
 * after it recorded an error.
 */
static bool semantic_variable_check(Semantic_Analyzer *analyzer, Ast_Node_Index procedure,
                                    Ast_Node_Index variable) {
    assert(variable < analyzer->node_count);
    assert(analyzer->nodes[variable].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION);

    Str name = analyzer->nodes[variable].payload.variable_definition.name;

    Semantic_Binding earlier = semantic_variable_find(analyzer, procedure, variable, name);
    if (earlier.kind != SEMANTIC_BINDING_KIND_NONE) {
        return semantic_fail(analyzer, SEMANTIC_STATUS_VARIABLE_NAME_DUPLICATE, name);
    }

    Semantic_Binding parameter = semantic_parameter_find(analyzer, procedure, name);
    Semantic_Binding constant = semantic_definition_find(analyzer, name);
    if (parameter.kind != SEMANTIC_BINDING_KIND_NONE ||
        constant.kind != SEMANTIC_BINDING_KIND_NONE) {
        return semantic_fail(analyzer, SEMANTIC_STATUS_VARIABLE_NAME_SHADOWS, name);
    }

    return true;
}

/**
 * Resolves every name in the expression below `expression`, which is in `statement` of the
 * procedure that `definition` binds to its name. The variables that `statement` can use are the
 * ones defined above it. The walk keeps the path from the root of the expression in an
 * array instead of calling itself. Returns false after it recorded an error.
 */
static bool semantic_expression_resolve(Semantic_Analyzer *analyzer, Ast_Node_Index definition,
                                        Ast_Node_Index statement, Ast_Node_Index expression) {
    assert(expression < analyzer->node_count);
    assert(definition < analyzer->node_count);
    assert(statement < analyzer->node_count);

    Ast_Node_Index procedure = analyzer->nodes[definition].first_child;

    // The nodes on the path from the root to the current node, excluding the current node.
    Ast_Node_Index path[SEMANTIC_EXPRESSION_DEPTH_MAX] = {0};
    size_t path_count = 0;

    Ast_Node_Index current = expression;
    // False once the children of `current` have been walked.
    bool is_descending = true;
    // Every node is entered once and left once, so twice the number of nodes steps are enough.
    for (size_t step = 0; step < analyzer->node_count * 2; step += 1) {
        Ast_Node const *node = &analyzer->nodes[current];

        if (is_descending) {
            if (node->first_child != AST_NODE_INDEX_NONE) {
                if (path_count == SEMANTIC_EXPRESSION_DEPTH_MAX) {
                    // The tree has no source positions, so the definition is the closest place.
                    Str name = analyzer->nodes[definition].payload.constant_definition.name;
                    return semantic_fail(analyzer, SEMANTIC_STATUS_EXPRESSION_TOO_DEEP, name);
                }
                path[path_count] = current;
                path_count += 1;
                current = node->first_child;
                continue;
            }
        }

        if (node->node_type == AST_NODE_TYPE_IDENTIFIER) {
            if (semantic_identifier_resolve(analyzer, procedure, statement, current) == false) {
                return false;
            }
        }

        if (path_count == 0) {
            assert(current == expression);
            return true;
        }

        // Go on with the next sibling, or finish the parent when there is none.
        if (node->next_sibling != AST_NODE_INDEX_NONE) {
            current = node->next_sibling;
            is_descending = true;
        } else {
            path_count -= 1;
            current = path[path_count];
            is_descending = false;
        }
    }

    // Unreachable: the step budget covers every node twice.
    assert(false);
    return false;
}

/**
 * Looks up the name of `identifier`, used in `statement` of `procedure`, and records what it
 * stands for. The variables defined above the statement and the parameters are searched before the
 * constants, as the scope of the procedure is the inner one. Because a name cannot be declared
 * twice, the order does not change the result. Returns false after it recorded an error.
 */
static bool semantic_identifier_resolve(Semantic_Analyzer *analyzer, Ast_Node_Index procedure,
                                        Ast_Node_Index statement, Ast_Node_Index identifier) {
    assert(identifier < analyzer->node_count);
    assert(analyzer->nodes[identifier].node_type == AST_NODE_TYPE_IDENTIFIER);

    Str name = analyzer->nodes[identifier].payload.identifier.name;

    Semantic_Binding binding = semantic_variable_find(analyzer, procedure, statement, name);
    if (binding.kind == SEMANTIC_BINDING_KIND_NONE) {
        binding = semantic_parameter_find(analyzer, procedure, name);
    }
    if (binding.kind == SEMANTIC_BINDING_KIND_NONE) {
        binding = semantic_definition_find(analyzer, name);
    }
    if (binding.kind == SEMANTIC_BINDING_KIND_NONE) {
        return semantic_fail(analyzer, SEMANTIC_STATUS_IDENTIFIER_UNKNOWN, name);
    }

    analyzer->bindings[identifier] = binding;
    return true;
}

/**
 * Returns the first definition of the module that is called `name`, or a binding of the kind
 * `SEMANTIC_BINDING_KIND_NONE` when there is none.
 */
static Semantic_Binding semantic_definition_find(Semantic_Analyzer const *analyzer, Str name) {
    assert(name.bytes != NULL);

    Ast_Node_Index definition = analyzer->nodes[analyzer->module].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < analyzer->node_count && definition != AST_NODE_INDEX_NONE; i += 1) {
        if (str_equals(analyzer->nodes[definition].payload.constant_definition.name, name)) {
            assert(i <= UINT32_MAX);
            return (Semantic_Binding){
                .target = definition,
                .position = (uint32_t)i,
                .kind = SEMANTIC_BINDING_KIND_CONSTANT,
            };
        }
        definition = analyzer->nodes[definition].next_sibling;
    }

    return (Semantic_Binding){
        .target = AST_NODE_INDEX_NONE,
        .position = 0,
        .kind = SEMANTIC_BINDING_KIND_NONE,
    };
}

/**
 * Returns the first parameter of `procedure` that is called `name`, or a binding of the kind
 * `SEMANTIC_BINDING_KIND_NONE` when there is none. The parameters are the first children of the
 * procedure, before the statements of the body.
 */
static Semantic_Binding semantic_parameter_find(Semantic_Analyzer const *analyzer,
                                                Ast_Node_Index procedure, Str name) {
    assert(procedure < analyzer->node_count);
    assert(analyzer->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);
    assert(name.bytes != NULL);

    Ast_Node_Index parameter = analyzer->nodes[procedure].first_child;
    // Every parameter is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < analyzer->node_count && parameter != AST_NODE_INDEX_NONE; i += 1) {
        Ast_Node const *parameter_node = &analyzer->nodes[parameter];
        if (parameter_node->node_type != AST_NODE_TYPE_PARAMETER) {
            break;
        }

        if (str_equals(parameter_node->payload.parameter.name, name)) {
            assert(i <= UINT32_MAX);
            return (Semantic_Binding){
                .target = parameter,
                .position = (uint32_t)i,
                .kind = SEMANTIC_BINDING_KIND_PARAMETER,
            };
        }
        parameter = parameter_node->next_sibling;
    }

    return (Semantic_Binding){
        .target = AST_NODE_INDEX_NONE,
        .position = 0,
        .kind = SEMANTIC_BINDING_KIND_NONE,
    };
}

/**
 * Returns the first variable of `procedure` that is called `name` and is defined above the
 * statement `before`, or a binding of the kind `SEMANTIC_BINDING_KIND_NONE` when there is none.
 * The statements are the children of the procedure after its parameters, and the parser makes the
 * nodes in the order of the source, so a statement is above `before` when its index is lower.
 */
static Semantic_Binding semantic_variable_find(Semantic_Analyzer const *analyzer,
                                               Ast_Node_Index procedure, Ast_Node_Index before,
                                               Str name) {
    assert(procedure < analyzer->node_count);
    assert(analyzer->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);
    assert(before < analyzer->node_count);
    assert(name.bytes != NULL);

    uint32_t position = 0;
    Ast_Node_Index statement = analyzer->nodes[procedure].first_child;
    // Every statement is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < analyzer->node_count && statement != AST_NODE_INDEX_NONE; i += 1) {
        if (statement >= before) {
            break;
        }

        Ast_Node const *statement_node = &analyzer->nodes[statement];
        if (statement_node->node_type == AST_NODE_TYPE_VARIABLE_DEFINITION) {
            if (str_equals(statement_node->payload.variable_definition.name, name)) {
                return (Semantic_Binding){
                    .target = statement,
                    .position = position,
                    .kind = SEMANTIC_BINDING_KIND_VARIABLE,
                };
            }
            position += 1;
        }
        statement = statement_node->next_sibling;
    }

    return (Semantic_Binding){
        .target = AST_NODE_INDEX_NONE,
        .position = 0,
        .kind = SEMANTIC_BINDING_KIND_NONE,
    };
}

/**
 * Records the first error and returns false, so that a caller can report failure in one line.
 */
static bool semantic_fail(Semantic_Analyzer *analyzer, Semantic_Status status, Str subject) {
    // Only the first error is kept, and callers return right after it.
    assert(analyzer->status == SEMANTIC_STATUS_OK);
    assert(status != SEMANTIC_STATUS_OK);

    analyzer->status = status;
    analyzer->subject = subject;
    return false;
}
