#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <mialu/semantic.h>

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

/**
 * A loop, a scope or a labeled if statement that a `break` can leave: the node of the statement,
 * the index after the last node of its last block, and its label, which has `bytes` NULL when it
 * has none. An if statement is only here when it has a label, and a `break` without a label
 * passes it.
 */
typedef struct {
    Ast_Node_Index node;
    Ast_Node_Index end;
    Str label;
    bool is_loop;
    bool is_if;
} Semantic_Jump_Target;

static bool             semantic_attributes_check   (Semantic_Analyzer *analyzer,
                                                     Ast_Node_Index definition);
static bool             semantic_call_resolve       (Semantic_Analyzer *analyzer,
                                                     Ast_Node_Index procedure,
                                                     Ast_Node_Index call);
static Semantic_Binding semantic_definition_find    (Semantic_Analyzer const *analyzer, Str name);
static bool             semantic_definitions_check  (Semantic_Analyzer *analyzer);
static bool             semantic_fields_check       (Semantic_Analyzer *analyzer);
static bool             semantic_fail               (Semantic_Analyzer *analyzer,
                                                     Semantic_Status status, Str subject);
static bool             semantic_identifier_resolve (Semantic_Analyzer *analyzer,
                                                     Ast_Node_Index procedure,
                                                     Ast_Node_Index statement,
                                                     Ast_Node_Index identifier);
static bool             semantic_jump_target_is_statement(Semantic_Analyzer const *analyzer,
                                                     Ast_Node_Index procedure,
                                                     Ast_Node_Index statement);
static bool             semantic_jump_target_push   (Semantic_Analyzer *analyzer,
                                                     Semantic_Jump_Target *targets,
                                                     size_t *target_count,
                                                     Ast_Node_Index statement);
static bool             semantic_jump_resolve       (Semantic_Analyzer *analyzer,
                                                     Semantic_Jump_Target const *targets,
                                                     size_t target_count, Ast_Node_Index jump);
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
                                                     Ast_Node_Index before,
                                                     Ast_Node_Index excluded, Str name);

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
    if (is_ok) {
        is_ok = semantic_fields_check(&analyzer);
    }

    Ast_Node_Index definition = node_elements[root].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < nodes.length && definition != AST_NODE_INDEX_NONE && is_ok; i += 1) {
        if (node_elements[definition].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION) {
            Ast_Node_Index value = node_elements[definition].first_child;
            if (node_elements[value].node_type == AST_NODE_TYPE_PROCEDURE) {
                is_ok = semantic_procedure_resolve(&analyzer, definition);
            }
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
 * Checks that no two definitions of the module have one name, and that the attributes of the global
 * variables are fine. Returns false after it recorded an error.
 */
static bool semantic_definitions_check(Semantic_Analyzer *analyzer) {
    assert(analyzer->nodes[analyzer->module].node_type == AST_NODE_TYPE_MODULE);

    Ast_Node_Index definition = analyzer->nodes[analyzer->module].first_child;
    for (size_t i = 0; i < analyzer->node_count && definition != AST_NODE_INDEX_NONE; i += 1) {
        Str name = ast_definition_name(&analyzer->nodes[definition]);

        // The search finds the first definition of the name, which is this one unless an earlier
        // definition has the name as well.
        Semantic_Binding first = semantic_definition_find(analyzer, name);
        assert(first.kind == SEMANTIC_BINDING_KIND_CONSTANT ||
               first.kind == SEMANTIC_BINDING_KIND_GLOBAL);
        if (first.target != definition) {
            return semantic_fail(analyzer, SEMANTIC_STATUS_DEFINITION_NAME_DUPLICATE, name);
        }

        if (analyzer->nodes[definition].node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION &&
            semantic_attributes_check(analyzer, definition) == false) {
            return false;
        }

        definition = analyzer->nodes[definition].next_sibling;
    }

    return true;
}

/**
 * Checks that no two fields of a struct have the same name, in every struct of the module, also
 * the ones that are the type of a field. Returns false after it recorded an error.
 */
static bool semantic_fields_check(Semantic_Analyzer *analyzer) {
    for (size_t i = 0; i < analyzer->node_count; i += 1) {
        if (analyzer->nodes[i].node_type != AST_NODE_TYPE_STRUCT_TYPE) {
            continue;
        }

        Ast_Node_Index field = analyzer->nodes[i].first_child;
        // Every field is a distinct node, so there cannot be more than there are nodes.
        for (size_t j = 0; j < analyzer->node_count && field != AST_NODE_INDEX_NONE; j += 1) {
            Str name = analyzer->nodes[field].payload.field.name;
            Ast_Node_Index other = analyzer->nodes[field].next_sibling;
            for (size_t k = 0; k < analyzer->node_count && other != AST_NODE_INDEX_NONE; k += 1) {
                if (str_equals(analyzer->nodes[other].payload.field.name, name)) {
                    return semantic_fail(analyzer, SEMANTIC_STATUS_FIELD_NAME_DUPLICATE,
                                         analyzer->nodes[other].payload.field.name);
                }
                other = analyzer->nodes[other].next_sibling;
            }
            field = analyzer->nodes[field].next_sibling;
        }
    }

    return true;
}

/**
 * Checks the attributes of the global variable `definition`: the language has the attribute
 * `static` only, an attribute cannot be given twice, and a global variable must be `static`,
 * because only variables with static storage duration are supported. The attributes are the first
 * children of the definition. Returns false after it recorded an error.
 */
static bool semantic_attributes_check(Semantic_Analyzer *analyzer, Ast_Node_Index definition) {
    assert(definition < analyzer->node_count);
    assert(analyzer->nodes[definition].node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION);

    bool is_static = false;
    Ast_Node_Index attribute = analyzer->nodes[definition].first_child;
    // Every attribute is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < analyzer->node_count && attribute != AST_NODE_INDEX_NONE; i += 1) {
        Ast_Node const *attribute_node = &analyzer->nodes[attribute];
        if (attribute_node->node_type != AST_NODE_TYPE_ATTRIBUTE) {
            break;
        }

        Str name = attribute_node->payload.attribute.name;
        if (str_equals(name, STR("static")) == false) {
            return semantic_fail(analyzer, SEMANTIC_STATUS_ATTRIBUTE_UNKNOWN, name);
        }
        if (is_static) {
            return semantic_fail(analyzer, SEMANTIC_STATUS_ATTRIBUTE_DUPLICATE, name);
        }
        is_static = true;

        attribute = attribute_node->next_sibling;
    }

    if (is_static == false) {
        return semantic_fail(analyzer, SEMANTIC_STATUS_GLOBAL_NOT_STATIC,
                             analyzer->nodes[definition].payload.global_variable_definition.name);
    }
    return true;
}

/**
 * Resolves the names in the body of the procedure that `definition` binds to its name, and checks
 * its parameters and variables. The nodes of the procedure, including the ones in its blocks, are
 * in the order of the source, so one pass over them meets everything in the order it is written:
 * the parameters, and then the statements, each followed by the nodes of its expressions. The
 * statement that an expression belongs to is the last statement that the pass met. Returns false
 * after it recorded an error.
 */
static bool semantic_procedure_resolve(Semantic_Analyzer *analyzer, Ast_Node_Index definition) {
    assert(definition < analyzer->node_count);
    assert(analyzer->nodes[definition].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION);

    Ast_Node_Index procedure = analyzer->nodes[definition].first_child;
    assert(analyzer->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);

    Ast_Node_Index end = ast_procedure_end(analyzer->nodes, analyzer->node_count, procedure);
    Ast_Node_Index statement = AST_NODE_INDEX_NONE;
    // The loops and the scopes that the pass is in, the innermost last. A `break` leaves one of
    // them, and a `continue` goes on with the innermost loop.
    Semantic_Jump_Target targets[AST_BLOCK_DEPTH_MAX] = {0};
    size_t target_count = 0;
    for (Ast_Node_Index current = procedure + 1; current < end; current += 1) {
        Ast_Node_Type node_type = analyzer->nodes[current].node_type;

        while (target_count > 0 && targets[target_count - 1].end <= current) {
            target_count -= 1;
        }
        if (semantic_jump_target_is_statement(analyzer, procedure, current)) {
            if (semantic_jump_target_push(analyzer, targets, &target_count, current) == false) {
                return false;
            }
        }
        if (node_type == AST_NODE_TYPE_BREAK_STATEMENT ||
            node_type == AST_NODE_TYPE_CONTINUE_STATEMENT) {
            if (semantic_jump_resolve(analyzer, targets, target_count, current) == false) {
                return false;
            }
        }

        if (node_type == AST_NODE_TYPE_PARAMETER) {
            // The parameters come first, so by the time that a statement is reached all of them
            // are known.
            if (semantic_parameter_check(analyzer, procedure, current) == false) {
                return false;
            }
        } else if (node_type == AST_NODE_TYPE_VARIABLE_DEFINITION ||
                   node_type == AST_NODE_TYPE_FOR_STATEMENT) {
            statement = current;
            if (semantic_variable_check(analyzer, procedure, current) == false) {
                return false;
            }
        } else if (node_type == AST_NODE_TYPE_IDENTIFIER) {
            // The nodes of an expression come after the node of their statement, and the other
            // kinds of node that are met in between are not statements.
            if (semantic_identifier_resolve(analyzer, procedure, statement, current) == false) {
                return false;
            }
        } else if (node_type == AST_NODE_TYPE_CALL) {
            if (semantic_call_resolve(analyzer, procedure, current) == false) {
                return false;
            }
        } else if (node_type == AST_NODE_TYPE_RETURN_STATEMENT ||
                   node_type == AST_NODE_TYPE_CALL_STATEMENT ||
                   node_type == AST_NODE_TYPE_IF_STATEMENT ||
                   ast_node_type_is_assignment(node_type)) {
            statement = current;
        }
    }

    return true;
}

/**
 * Returns whether the node `statement` of `procedure` is a statement that a `break` can leave: a
 * for statement, a loop statement or a scope statement, or an if statement that has a label. The
 * label of an if, for or loop statement is on the labeled statement right before it.
 */
static bool semantic_jump_target_is_statement(Semantic_Analyzer const *analyzer,
                                              Ast_Node_Index procedure, Ast_Node_Index statement) {
    assert(statement < analyzer->node_count);

    Ast_Node_Type node_type = analyzer->nodes[statement].node_type;
    if (node_type == AST_NODE_TYPE_IF_STATEMENT) {
        return statement > procedure + 1 &&
               analyzer->nodes[statement - 1].node_type == AST_NODE_TYPE_LABELED_STATEMENT;
    }
    return node_type == AST_NODE_TYPE_FOR_STATEMENT || node_type == AST_NODE_TYPE_LOOP_STATEMENT ||
           node_type == AST_NODE_TYPE_SCOPE_STATEMENT;
}

/**
 * Adds `statement`, for which `semantic_jump_target_is_statement` is true, to the loops and scopes
 * `targets` that the pass is in, which has `*target_count` of them. A label cannot be the label of
 * a statement that is around it. Returns false after it recorded an error.
 */
static bool semantic_jump_target_push(Semantic_Analyzer *analyzer, Semantic_Jump_Target *targets,
                                      size_t *target_count, Ast_Node_Index statement) {
    assert(*target_count < AST_BLOCK_DEPTH_MAX);

    Ast_Node const *node = &analyzer->nodes[statement];
    Str label = STR_EMPTY;
    if (node->node_type == AST_NODE_TYPE_SCOPE_STATEMENT &&
        analyzer->nodes[node->first_child].node_type == AST_NODE_TYPE_LABEL) {
        label = analyzer->nodes[node->first_child].payload.label.name;
    } else if (analyzer->nodes[statement - 1].node_type == AST_NODE_TYPE_LABELED_STATEMENT) {
        label = analyzer->nodes[statement - 1].payload.label.name;
    }
    if (label.bytes != NULL) {
        for (size_t i = 0; i < *target_count; i += 1) {
            if (targets[i].label.bytes != NULL && str_equals(targets[i].label, label)) {
                return semantic_fail(analyzer, SEMANTIC_STATUS_LABEL_DUPLICATE, label);
            }
        }
    }

    // The block is the last child, and a statement is inside a block of its own, so there is room.
    Ast_Node_Index block = node->last_child;
    targets[*target_count] = (Semantic_Jump_Target){
        .node = statement,
        .end = analyzer->nodes[block].payload.block.end,
        .label = label,
        .is_loop = node->node_type == AST_NODE_TYPE_FOR_STATEMENT ||
                   node->node_type == AST_NODE_TYPE_LOOP_STATEMENT,
        .is_if = node->node_type == AST_NODE_TYPE_IF_STATEMENT,
    };
    *target_count += 1;
    return true;
}

/**
 * Binds the `break` or `continue` statement `jump` to the statement that it leaves or continues,
 * out of `targets`, which are the loops and scopes that it is in, the innermost last. A `break`
 * without a label leaves the innermost one, a `break` with a label leaves the scope with that
 * label, and a `continue` goes on with the innermost loop, also when scopes are inside it, or with
 * the loop that has the label. Returns
 * false after it recorded an error.
 */
static bool semantic_jump_resolve(Semantic_Analyzer *analyzer, Semantic_Jump_Target const *targets,
                                  size_t target_count, Ast_Node_Index jump) {
    assert(jump < analyzer->node_count);
    assert(target_count <= AST_BLOCK_DEPTH_MAX);

    Ast_Node const *jump_node = &analyzer->nodes[jump];
    bool is_break = jump_node->node_type == AST_NODE_TYPE_BREAK_STATEMENT;
    assert(is_break || jump_node->node_type == AST_NODE_TYPE_CONTINUE_STATEMENT);

    Ast_Node_Index found = AST_NODE_INDEX_NONE;
    if (jump_node->first_child != AST_NODE_INDEX_NONE) {
        Str label = analyzer->nodes[jump_node->first_child].payload.label.name;
        for (size_t i = target_count; i > 0 && found == AST_NODE_INDEX_NONE; i -= 1) {
            Semantic_Jump_Target const *target = &targets[i - 1];
            if (target->label.bytes != NULL && str_equals(target->label, label)) {
                found = target->node;
            }
        }
        if (found == AST_NODE_INDEX_NONE) {
            return semantic_fail(analyzer, SEMANTIC_STATUS_LABEL_UNKNOWN, label);
        }
        // A `continue` goes on with a loop, so a scope or an if statement is no use to it.
        if (is_break == false && analyzer->nodes[found].node_type != AST_NODE_TYPE_FOR_STATEMENT &&
            analyzer->nodes[found].node_type != AST_NODE_TYPE_LOOP_STATEMENT) {
            return semantic_fail(analyzer, SEMANTIC_STATUS_LABEL_NOT_LOOP, label);
        }
    } else {
        for (size_t i = target_count; i > 0 && found == AST_NODE_INDEX_NONE; i -= 1) {
            Semantic_Jump_Target const *target = &targets[i - 1];
            if (target->is_if == false && (is_break || target->is_loop)) {
                found = target->node;
            }
        }
        if (found == AST_NODE_INDEX_NONE && is_break) {
            return semantic_fail(analyzer, SEMANTIC_STATUS_BREAK_OUTSIDE_SCOPE,
                                 jump_node->payload.jump.keyword);
        }
        if (found == AST_NODE_INDEX_NONE) {
            return semantic_fail(analyzer, SEMANTIC_STATUS_JUMP_OUTSIDE_LOOP,
                                 jump_node->payload.jump.keyword);
        }
    }

    analyzer->bindings[jump] = (Semantic_Binding){
        .target = found,
        .position = 0,
        .kind = SEMANTIC_BINDING_KIND_JUMP_TARGET,
    };
    return true;
}

/**
 * Checks that the name of `parameter` is new: no earlier parameter of the procedure has it, and no
 * constant or global variable has it. A parameter with such a name would hide it in the body, and
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

    Semantic_Binding definition = semantic_definition_find(analyzer, name);
    if (definition.kind != SEMANTIC_BINDING_KIND_NONE) {
        return semantic_fail(analyzer, SEMANTIC_STATUS_PARAMETER_NAME_SHADOWS, name);
    }

    return true;
}

/**
 * Checks that the name of `variable`, a variable definition or the iterator of a for statement, is
 * new: no constant, global variable, parameter or earlier variable of the procedure has it. Like a
 * parameter, a
 * variable with such a name would hide it. Returns false after it recorded an error.
 */
static bool semantic_variable_check(Semantic_Analyzer *analyzer, Ast_Node_Index procedure,
                                    Ast_Node_Index variable) {
    assert(variable < analyzer->node_count);
    assert(analyzer->nodes[variable].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION ||
           analyzer->nodes[variable].node_type == AST_NODE_TYPE_FOR_STATEMENT);

    Str name = analyzer->nodes[variable].payload.variable_definition.name;
    if (analyzer->nodes[variable].node_type == AST_NODE_TYPE_FOR_STATEMENT) {
        name = analyzer->nodes[variable].payload.for_statement.name;
    }

    Semantic_Binding earlier = semantic_variable_find(analyzer, procedure, variable,
                                                     AST_NODE_INDEX_NONE, name);
    if (earlier.kind != SEMANTIC_BINDING_KIND_NONE) {
        return semantic_fail(analyzer, SEMANTIC_STATUS_VARIABLE_NAME_DUPLICATE, name);
    }

    Semantic_Binding parameter = semantic_parameter_find(analyzer, procedure, name);
    Semantic_Binding definition = semantic_definition_find(analyzer, name);
    if (parameter.kind != SEMANTIC_BINDING_KIND_NONE ||
        definition.kind != SEMANTIC_BINDING_KIND_NONE) {
        return semantic_fail(analyzer, SEMANTIC_STATUS_VARIABLE_NAME_SHADOWS, name);
    }

    return true;
}

/**
 * Looks up the name of `identifier`, which belongs to `statement` of `procedure`, and records what
 * it stands for. The variables that are in scope, which are the ones defined above and in the
 * blocks that the use is in, and the parameters are searched before the constants, as the scope of
 * the procedure is the inner one. Because a name cannot be declared twice, the order does not
 * change the result. The name that an assignment assigns to must stand for a variable. Returns
 * false after it recorded an error.
 *
 * The lookup is made as of the identifier itself, so that the condition of an `else if` does not
 * see the variables of the blocks above it. The initial value of a variable is part of its
 * definition, so the variable is not in scope there, which is why the variable of `statement` is
 * left out. When `statement` is a variable that was defined in a block that has ended, it is out
 * of scope anyway.
 */
static bool semantic_identifier_resolve(Semantic_Analyzer *analyzer, Ast_Node_Index procedure,
                                        Ast_Node_Index statement, Ast_Node_Index identifier) {
    assert(identifier < analyzer->node_count);
    assert(analyzer->nodes[identifier].node_type == AST_NODE_TYPE_IDENTIFIER);

    Str name = analyzer->nodes[identifier].payload.identifier.name;

    Ast_Node_Index excluded = AST_NODE_INDEX_NONE;
    if (statement != AST_NODE_INDEX_NONE &&
        analyzer->nodes[statement].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION) {
        excluded = statement;
    }

    Semantic_Binding binding = semantic_variable_find(analyzer, procedure, identifier, excluded,
                                                      name);
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

    // The first child of an assignment is the name that is assigned to, or the field of a struct
    // that is, which is found through the name of the variable that has it.
    bool is_target = false;
    if (statement != AST_NODE_INDEX_NONE &&
        ast_node_type_is_assignment(analyzer->nodes[statement].node_type)) {
        Ast_Node_Index target = analyzer->nodes[statement].first_child;
        // Each step goes down one level, so there are fewer steps than nodes.
        for (size_t i = 0; i < analyzer->node_count &&
                           analyzer->nodes[target].node_type == AST_NODE_TYPE_FIELD_ACCESS;
             i += 1) {
            target = analyzer->nodes[target].first_child;
        }
        is_target = target == identifier;
    }
    if (is_target && binding.kind != SEMANTIC_BINDING_KIND_VARIABLE &&
        binding.kind != SEMANTIC_BINDING_KIND_GLOBAL) {
        return semantic_fail(analyzer, SEMANTIC_STATUS_ASSIGNMENT_TARGET_NOT_VARIABLE, name);
    }
    if (is_target && analyzer->nodes[binding.target].node_type == AST_NODE_TYPE_FOR_STATEMENT) {
        return semantic_fail(analyzer, SEMANTIC_STATUS_ASSIGNMENT_TARGET_ITERATOR, name);
    }

    return true;
}

/**
 * Binds the call `call` to the definition of the procedure that it calls. The name must be the
 * name of a constant whose value is a procedure. When it is the name of a parameter or a variable
 * or of a constant that is a string, the error says that it cannot be called, and when it is the
 * name of nothing, that it is unknown. Returns false after it recorded an error.
 */
static bool semantic_call_resolve(Semantic_Analyzer *analyzer, Ast_Node_Index procedure,
                                  Ast_Node_Index call) {
    assert(call < analyzer->node_count);
    assert(analyzer->nodes[call].node_type == AST_NODE_TYPE_CALL);

    Str name = analyzer->nodes[call].payload.call.name;

    Semantic_Binding binding = semantic_definition_find(analyzer, name);
    if (binding.kind == SEMANTIC_BINDING_KIND_NONE) {
        Semantic_Binding local = semantic_variable_find(analyzer, procedure, call,
                                                        AST_NODE_INDEX_NONE, name);
        if (local.kind == SEMANTIC_BINDING_KIND_NONE) {
            local = semantic_parameter_find(analyzer, procedure, name);
        }
        if (local.kind == SEMANTIC_BINDING_KIND_NONE) {
            return semantic_fail(analyzer, SEMANTIC_STATUS_IDENTIFIER_UNKNOWN, name);
        }
        return semantic_fail(analyzer, SEMANTIC_STATUS_CALL_TARGET_NOT_PROCEDURE, name);
    }

    // A global variable is not a procedure, and its first child is not a value.
    if (binding.kind != SEMANTIC_BINDING_KIND_CONSTANT) {
        return semantic_fail(analyzer, SEMANTIC_STATUS_CALL_TARGET_NOT_PROCEDURE, name);
    }
    Ast_Node_Index value = analyzer->nodes[binding.target].first_child;
    if (analyzer->nodes[value].node_type != AST_NODE_TYPE_PROCEDURE) {
        return semantic_fail(analyzer, SEMANTIC_STATUS_CALL_TARGET_NOT_PROCEDURE, name);
    }

    analyzer->bindings[call] = binding;
    return true;
}

/**
 * Returns the first definition of the module that is called `name`, which is a constant or a global
 * variable, or a binding of the kind `SEMANTIC_BINDING_KIND_NONE` when there is none. The position
 * of the binding counts the definitions of the same kind above it, so the constants and the global
 * variables are numbered apart.
 */
static Semantic_Binding semantic_definition_find(Semantic_Analyzer const *analyzer, Str name) {
    assert(name.bytes != NULL);

    uint32_t constant_count = 0;
    uint32_t global_count = 0;
    Ast_Node_Index definition = analyzer->nodes[analyzer->module].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < analyzer->node_count && definition != AST_NODE_INDEX_NONE; i += 1) {
        bool is_global =
            analyzer->nodes[definition].node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION;
        if (str_equals(ast_definition_name(&analyzer->nodes[definition]), name)) {
            if (is_global) {
                return (Semantic_Binding){
                    .target = definition,
                    .position = global_count,
                    .kind = SEMANTIC_BINDING_KIND_GLOBAL,
                };
            }
            return (Semantic_Binding){
                .target = definition,
                .position = constant_count,
                .kind = SEMANTIC_BINDING_KIND_CONSTANT,
            };
        }

        if (is_global) {
            global_count += 1;
        } else if (analyzer->nodes[analyzer->nodes[definition].first_child].node_type !=
                   AST_NODE_TYPE_STRUCT_TYPE) {
            // A struct is a type and becomes no function, so it takes no place among the
            // constants.
            constant_count += 1;
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
 * Returns the variable of `procedure` that is called `name` and is in scope at the node `before`,
 * or a binding of the kind `SEMANTIC_BINDING_KIND_NONE` when there is none. A variable is in scope
 * from its definition to the end of the block that the definition is in, or of the procedure. The
 * parser makes the nodes in the order of the source, so a variable is defined above `before` when
 * its index is lower, and a block ends at the index that it has in its payload.
 *
 * The variable `excluded`, which can be `AST_NODE_INDEX_NONE`, is never found.
 *
 * The position of the binding is the number of variables of the procedure above the variable,
 * also the ones in blocks that have ended, because each variable has a place of its own.
 *
 * The pass over the nodes keeps the ends of the blocks that it is in, in an array.
 */
static Semantic_Binding semantic_variable_find(Semantic_Analyzer const *analyzer,
                                               Ast_Node_Index procedure, Ast_Node_Index before,
                                               Ast_Node_Index excluded, Str name) {
    assert(procedure < analyzer->node_count);
    assert(analyzer->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);
    assert(before < analyzer->node_count);
    assert(before > procedure);
    assert(name.bytes != NULL);

    // The ends of the blocks that the pass is in, the innermost last.
    Ast_Node_Index block_ends[AST_BLOCK_DEPTH_MAX] = {0};
    size_t block_count = 0;

    Semantic_Binding found = {
        .target = AST_NODE_INDEX_NONE,
        .position = 0,
        .kind = SEMANTIC_BINDING_KIND_NONE,
    };
    uint32_t position = 0;
    for (Ast_Node_Index current = procedure + 1; current < before; current += 1) {
        while (block_count > 0 && block_ends[block_count - 1] <= current) {
            block_count -= 1;
        }

        Ast_Node const *node = &analyzer->nodes[current];
        if (node->node_type == AST_NODE_TYPE_BLOCK) {
            assert(block_count < AST_BLOCK_DEPTH_MAX);
            block_ends[block_count] = node->payload.block.end;
            block_count += 1;
        } else if (node->node_type == AST_NODE_TYPE_FOR_STATEMENT) {
            // The iterator is in scope in the block of the statement, which is its last child and
            // comes after the range.
            Ast_Node_Index block = node->last_child;
            bool is_in_scope = block <= before && before < analyzer->nodes[block].payload.block.end;
            if (is_in_scope && str_equals(node->payload.for_statement.name, name)) {
                found = (Semantic_Binding){
                    .target = current,
                    .position = position,
                    .kind = SEMANTIC_BINDING_KIND_VARIABLE,
                };
            }
            position += 1;
        } else if (node->node_type == AST_NODE_TYPE_VARIABLE_DEFINITION) {
            // The variable is in scope at `before` when the block that it is in goes on after it.
            bool is_in_scope = block_count == 0 || block_ends[block_count - 1] > before;
            if (is_in_scope && current != excluded &&
                str_equals(node->payload.variable_definition.name, name)) {
                found = (Semantic_Binding){
                    .target = current,
                    .position = position,
                    .kind = SEMANTIC_BINDING_KIND_VARIABLE,
                };
            }
            position += 1;
        }
    }

    return found;
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
