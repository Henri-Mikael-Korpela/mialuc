#include <assert.h>
#include <iso646.h>
#include <stdbool.h>
#include <stdint.h>

#include <mialu/constant.h>
#include <mialu/semantic.h>

/**
 * The state shared by the functions of the analysis. A function that can fail returns false after
 * it recorded the error here, and its caller then returns false too without recording anything
 * more. So only the first error is kept.
 */
typedef struct {
    // Not constant, because the text of a `++` is written to the string literal after it.
    Ast_Node *nodes;
    Semantic_Binding *bindings;
    // What is known about each node when the program is compiled, one entry per node, see
    // `semantic_constants_check`.
    Constant_Value *constants;
    // Where the text of the strings that `++` makes is written, see `Constant_Text`.
    Constant_Text text;
    Str subject;
    size_t node_count;
    // The nodes below this one are of imported files, whose parameters and variables are not
    // compared with the definitions of the program: a name of an imported file stands for its own,
    // see `packages_load`, so it cannot hide one of the program.
    size_t imported_end;
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

static void             semantic_address_of_resolve (Semantic_Analyzer *analyzer,
                                                     Ast_Node_Index address_of);
static bool             semantic_array_lengths_resolve(Semantic_Analyzer *analyzer);
static bool             semantic_attributes_check   (Semantic_Analyzer *analyzer,
                                                     Ast_Node_Index node, Str known,
                                                     Semantic_Status unknown_status,
                                                     bool *has_known);
static bool             semantic_call_resolve       (Semantic_Analyzer *analyzer,
                                                     Ast_Node_Index procedure,
                                                     Ast_Node_Index statement,
                                                     Ast_Node_Index call);
static bool             semantic_call_type_arguments_check(Semantic_Analyzer *analyzer,
                                                     Ast_Node_Index call,
                                                     Ast_Node_Index procedure);
static bool             semantic_constant_fail      (Semantic_Analyzer *analyzer,
                                                     Constant_Value value,
                                                     Ast_Node_Index definition);
static Str              semantic_constant_subject   (Semantic_Analyzer const *analyzer,
                                                     Ast_Node_Index node,
                                                     Ast_Node_Index definition);
static bool             semantic_constants_check    (Semantic_Analyzer *analyzer);
static bool             semantic_defer_check        (Semantic_Analyzer *analyzer,
                                                     Ast_Node_Index defer);
static Ast_Node_Index   semantic_defer_end          (Semantic_Analyzer const *analyzer,
                                                     Ast_Node_Index defer);
static Semantic_Binding semantic_definition_find    (Semantic_Analyzer const *analyzer, Str name);
static bool             semantic_definitions_check  (Semantic_Analyzer *analyzer);
static Ast_Node_Index   semantic_definition_end     (Semantic_Analyzer const *analyzer,
                                                     Ast_Node_Index definition);
static bool             semantic_definition_names_resolve(Semantic_Analyzer *analyzer,
                                                     Ast_Node_Index definition);
static bool             semantic_enum_members_check (Semantic_Analyzer *analyzer);
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

Semantic_Result semantic_analyze(Array_Slice nodes, Ast_Node_Index root, size_t imported_end,
                                 Array_Slice binding_buffer, Array_Slice constant_buffer,
                                 Array_Slice text_buffer) {
    assert(nodes.elements != NULL);
    assert(binding_buffer.elements != NULL);
    assert(binding_buffer.length >= nodes.length);
    assert(constant_buffer.elements != NULL);
    assert(constant_buffer.length >= nodes.length);
    assert(text_buffer.elements != NULL);
    assert(root < nodes.length);

    Ast_Node *node_elements = nodes.elements;
    assert(node_elements[root].node_type == AST_NODE_TYPE_MODULE);

    // Every node starts as not being a use of a name, so that the table can be read at any index.
    Semantic_Binding *binding_elements = binding_buffer.elements;
    for (size_t i = 0; i < nodes.length; i += 1) {
        binding_elements[i] = (Semantic_Binding){
            .target = AST_NODE_INDEX_NONE,
            .position = 0,
            .kind = SEMANTIC_BINDING_KIND_NONE,
            .is_address_taken = false,
        };
    }

    // Nothing is known about any node until it is evaluated, see `constant_evaluate`.
    Constant_Value *constant_elements = constant_buffer.elements;
    for (size_t i = 0; i < nodes.length; i += 1) {
        constant_elements[i] = (Constant_Value){
            .integer = 0,
            .string = AST_NODE_INDEX_NONE,
            .error = AST_NODE_INDEX_NONE,
            .type = CONSTANT_TYPE_UNTYPED_INTEGER,
            .status = CONSTANT_STATUS_UNKNOWN,
        };
    }

    Semantic_Analyzer analyzer = {
        .nodes = node_elements,
        .bindings = binding_elements,
        .constants = constant_elements,
        .text = {.bytes = text_buffer.elements, .size_bytes = text_buffer.length, .length = 0},
        .subject = STR_EMPTY,
        .node_count = nodes.length,
        .imported_end = imported_end,
        .module = root,
        .status = SEMANTIC_STATUS_OK,
    };

    // The definitions are checked before any name is looked up, so that a procedure can use a
    // constant that is defined below it, and so that a lookup finds the only definition of a name.
    bool is_ok = semantic_definitions_check(&analyzer);
    if (is_ok) {
        is_ok = semantic_fields_check(&analyzer);
    }
    if (is_ok) {
        is_ok = semantic_enum_members_check(&analyzer);
    }

    Ast_Node_Index definition = node_elements[root].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < nodes.length && definition != AST_NODE_INDEX_NONE && is_ok; i += 1) {
        if (node_elements[definition].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION) {
            Ast_Node_Index value = node_elements[definition].first_child;
            if (node_elements[value].node_type == AST_NODE_TYPE_PROCEDURE) {
                is_ok = semantic_procedure_resolve(&analyzer, definition);
            } else if (node_elements[value].node_type != AST_NODE_TYPE_ENUM_TYPE) {
                // A struct has the names in the lengths of the arrays that its fields are.
                is_ok = semantic_definition_names_resolve(&analyzer, definition);
            }
        } else if (node_elements[definition].node_type ==
                   AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION) {
            is_ok = semantic_definition_names_resolve(&analyzer, definition);
        }
        definition = node_elements[definition].next_sibling;
    }

    // The values are worked out once every name is resolved, because a constant can use one that
    // is defined below it.
    if (is_ok) {
        is_ok = semantic_constants_check(&analyzer);
    }
    if (is_ok) {
        is_ok = semantic_array_lengths_resolve(&analyzer);
    }

    // Nothing is handed out when there was an error, because the tables are only partly filled.
    size_t length = 0;
    if (is_ok) {
        length = nodes.length;
    }

    assert(is_ok == (analyzer.status == SEMANTIC_STATUS_OK));
    return (Semantic_Result){
        .bindings = array_slice_make(binding_elements, length),
        .constants = array_slice_make(constant_elements, length),
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
    // Whether the import of each foreign target was seen, indexed by `Ast_Foreign_Target`, which
    // has two targets.
    bool has_foreign_import[2] = {false, false};
    for (size_t i = 0; i < analyzer->node_count && definition != AST_NODE_INDEX_NONE; i += 1) {
        // An `import foreign` has no name. There is one at most for each target, and it must come
        // before the foreign procedures that it is for.
        if (analyzer->nodes[definition].node_type == AST_NODE_TYPE_FOREIGN_IMPORT) {
            Ast_Foreign_Target target = analyzer->nodes[definition].payload.foreign_import.target;
            // A library of the system has a name of its own, which the parser resolved, so there
            // is no more to check about it here.
            if (target == AST_FOREIGN_TARGET_SYSTEM) {
                definition = analyzer->nodes[definition].next_sibling;
                continue;
            }
            assert((size_t)target < 2);

            if (has_foreign_import[target]) {
                return semantic_fail(analyzer, SEMANTIC_STATUS_FOREIGN_IMPORT_DUPLICATE,
                                     ast_foreign_import_name(&analyzer->nodes[definition]));
            }
            has_foreign_import[target] = true;
            definition = analyzer->nodes[definition].next_sibling;
            continue;
        }

        Str name = ast_definition_name(&analyzer->nodes[definition]);
        if (analyzer->nodes[definition].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION) {
            Ast_Node_Index value = analyzer->nodes[definition].first_child;
            if (analyzer->nodes[value].node_type == AST_NODE_TYPE_PROCEDURE &&
                ast_procedure_is_foreign(analyzer->nodes, value) &&
                ast_procedure_foreign_target(analyzer->nodes, value) != AST_FOREIGN_TARGET_SYSTEM &&
                not has_foreign_import[ast_procedure_foreign_target(analyzer->nodes, value)]) {
                return semantic_fail(analyzer, SEMANTIC_STATUS_FOREIGN_IMPORT_MISSING, name);
            }
        }

        // The search finds the first definition of the name, which is this one unless an earlier
        // definition has the name as well.
        Semantic_Binding first = semantic_definition_find(analyzer, name);
        assert(first.kind == SEMANTIC_BINDING_KIND_CONSTANT ||
               first.kind == SEMANTIC_BINDING_KIND_GLOBAL);
        if (first.target != definition) {
            return semantic_fail(analyzer, SEMANTIC_STATUS_DEFINITION_NAME_DUPLICATE, name);
        }

        // A global variable must be `static`, because only variables with static storage duration
        // are supported.
        if (analyzer->nodes[definition].node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION) {
            bool is_static = false;
            if (not semantic_attributes_check(analyzer, definition, STR("static"),
                                          SEMANTIC_STATUS_ATTRIBUTE_UNKNOWN, &is_static)) {
                return false;
            }
            if (not is_static) {
                return semantic_fail(analyzer, SEMANTIC_STATUS_GLOBAL_NOT_STATIC, name);
            }
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
 * Checks that no two members of an enum have the same name, in every enum of the module. Returns
 * false after it recorded an error.
 */
static bool semantic_enum_members_check(Semantic_Analyzer *analyzer) {
    for (size_t i = 0; i < analyzer->node_count; i += 1) {
        if (analyzer->nodes[i].node_type != AST_NODE_TYPE_ENUM_TYPE) {
            continue;
        }

        Ast_Node_Index member = analyzer->nodes[i].first_child;
        // Every member is a distinct node, so there cannot be more than there are nodes.
        for (size_t j = 0; j < analyzer->node_count && member != AST_NODE_INDEX_NONE; j += 1) {
            Str name = analyzer->nodes[member].payload.enum_member.name;
            Ast_Node_Index other = analyzer->nodes[member].next_sibling;
            for (size_t k = 0; k < analyzer->node_count && other != AST_NODE_INDEX_NONE; k += 1) {
                if (str_equals(analyzer->nodes[other].payload.enum_member.name, name)) {
                    return semantic_fail(analyzer, SEMANTIC_STATUS_ENUM_MEMBER_DUPLICATE,
                                         analyzer->nodes[other].payload.enum_member.name);
                }
                other = analyzer->nodes[other].next_sibling;
            }
            member = analyzer->nodes[member].next_sibling;
        }
    }

    return true;
}

/**
 * Resolves the names in the value of the definition `definition`, a constant whose value is not a
 * procedure, a struct or an enum, or a global variable. The value is in the module scope, so a name
 * in it is a constant or a global variable. Whether it is known when the program is compiled is
 * checked later, see `semantic_constants_check`, because that depends on what the names stand for.
 * Returns false after it recorded an error.
 */
static bool semantic_definition_names_resolve(Semantic_Analyzer *analyzer,
                                              Ast_Node_Index definition) {
    assert(analyzer->nodes[definition].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION ||
           analyzer->nodes[definition].node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION);

    Ast_Node_Index end = semantic_definition_end(analyzer, definition);
    for (Ast_Node_Index current = definition + 1; current < end; current += 1) {
        if (analyzer->nodes[current].node_type != AST_NODE_TYPE_IDENTIFIER) {
            continue;
        }
        Str name = analyzer->nodes[current].payload.identifier.name;
        Semantic_Binding binding = semantic_definition_find(analyzer, name);
        if (binding.kind == SEMANTIC_BINDING_KIND_NONE) {
            return semantic_fail(analyzer, SEMANTIC_STATUS_IDENTIFIER_UNKNOWN, name);
        }
        analyzer->bindings[current] = binding;
    }
    return true;
}

/**
 * Returns the index after the last node of `definition`, which is a child of the module. Its nodes
 * are made after it and before the next one, so that is the next node that is a child of the
 * module, or the end of the tree.
 */
static Ast_Node_Index semantic_definition_end(Semantic_Analyzer const *analyzer,
                                              Ast_Node_Index definition) {
    assert(definition < analyzer->node_count);

    Ast_Node_Index end = definition + 1;
    while (end < analyzer->node_count &&
           analyzer->nodes[end].node_type != AST_NODE_TYPE_CONSTANT_DEFINITION &&
           analyzer->nodes[end].node_type != AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION &&
           analyzer->nodes[end].node_type != AST_NODE_TYPE_FOREIGN_IMPORT) {
        end += 1;
    }
    return end;
}

/**
 * Works out the length of each array type that has a constant expression between its brackets, which
 * is its second child, and gives the type that length as if it had been written as a number, so
 * that no target has to know the other. The expression is evaluated here, and not only where the
 * expressions of a procedure are, because the type of a field of a struct is not in a procedure.
 * The length is an integer that is not below zero. Returns false after it recorded an error.
 */
static bool semantic_array_lengths_resolve(Semantic_Analyzer *analyzer) {
    for (Ast_Node_Index current = 0; current < analyzer->node_count; current += 1) {
        Ast_Node *node = &analyzer->nodes[current];
        if (node->node_type != AST_NODE_TYPE_ARRAY_TYPE ||
            node->payload.array_type.length_kind != AST_ARRAY_LENGTH_CONSTANT) {
            continue;
        }

        Ast_Node_Index expression = analyzer->nodes[node->first_child].next_sibling;
        assert(expression != AST_NODE_INDEX_NONE);
        // The definition that the type is part of, which a message about a constant names.
        Ast_Node_Index definition = analyzer->module;
        for (Ast_Node_Index child = analyzer->nodes[analyzer->module].first_child;
             child != AST_NODE_INDEX_NONE && child <= current;
             child = analyzer->nodes[child].next_sibling) {
            definition = child;
        }
        Constant_Value value = constant_evaluate(analyzer->nodes, analyzer->node_count,
                                                 analyzer->bindings, analyzer->constants,
                                                 &analyzer->text, expression);
        Str subject = semantic_constant_subject(analyzer, expression, definition);
        if (value.status == CONSTANT_STATUS_NOT_CONSTANT) {
            return semantic_fail(analyzer, SEMANTIC_STATUS_ARRAY_LENGTH_NOT_CONSTANT, subject);
        }
        if (value.status != CONSTANT_STATUS_OK) {
            return semantic_constant_fail(analyzer, value, definition);
        }
        bool is_integer = value.type == CONSTANT_TYPE_UNTYPED_INTEGER ||
                          value.type == CONSTANT_TYPE_INT || value.type == CONSTANT_TYPE_I32 ||
                          value.type == CONSTANT_TYPE_I64 || value.type == CONSTANT_TYPE_U32 ||
                          value.type == CONSTANT_TYPE_U64 || value.type == CONSTANT_TYPE_BYTE;
        if (not is_integer || value.integer < 0) {
            return semantic_fail(analyzer, SEMANTIC_STATUS_ARRAY_LENGTH_INVALID, subject);
        }
        node->payload.array_type.length = value.integer;
        node->payload.array_type.length_kind = AST_ARRAY_LENGTH_FIXED;
    }
    return true;
}

/**
 * Works out what is known about the expressions of the program when it is compiled, and fills the
 * table of constants with it, see `constant_evaluate`. The value of a constant that is not a
 * procedure, a struct, an enum or the member of an enum must be a constant, and so must the initial
 * value of a global variable, unless it is the information about a type, which the target checks.
 * Every expression of every procedure is evaluated too, so that an error in a part that is known,
 * such as `1 / 0`, is found also where the expression as a whole is not, and so that a target
 * finds the entry of every expression filled. Returns false after it recorded the first error, in
 * the order of the source.
 */
static bool semantic_constants_check(Semantic_Analyzer *analyzer) {
    Ast_Node const *nodes = analyzer->nodes;
    Ast_Node_Index definition = nodes[analyzer->module].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < analyzer->node_count && definition != AST_NODE_INDEX_NONE; i += 1) {
        Ast_Node_Type definition_type = nodes[definition].node_type;
        if (definition_type == AST_NODE_TYPE_CONSTANT_DEFINITION &&
            constant_definition_is_value(nodes, definition)) {
            Constant_Value value = constant_evaluate(analyzer->nodes, analyzer->node_count,
                                                     analyzer->bindings, analyzer->constants,
                                                     &analyzer->text, definition);
            if (value.status != CONSTANT_STATUS_OK) {
                return semantic_constant_fail(analyzer, value, definition);
            }
        } else if (definition_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION) {
            // The information about a type is known when the program is compiled as well, but it
            // is no value of the kinds that a constant has, and the target checks it.
            Ast_Node_Index value_node = nodes[definition].last_child;
            bool is_type_info =
                nodes[value_node].node_type == AST_NODE_TYPE_TYPE_INFO_OF ||
                (nodes[value_node].node_type == AST_NODE_TYPE_FIELD_ACCESS &&
                 nodes[nodes[value_node].first_child].node_type == AST_NODE_TYPE_TYPE_INFO_OF);
            if (not is_type_info) {
                Constant_Value value = constant_evaluate(analyzer->nodes, analyzer->node_count,
                                                         analyzer->bindings, analyzer->constants,
                                                         &analyzer->text, value_node);
                if (value.status != CONSTANT_STATUS_OK) {
                    return semantic_constant_fail(analyzer, value, definition);
                }
            }
        } else if (definition_type == AST_NODE_TYPE_CONSTANT_DEFINITION &&
                   nodes[nodes[definition].first_child].node_type == AST_NODE_TYPE_PROCEDURE) {
            Ast_Node_Index procedure = nodes[definition].first_child;
            Ast_Node_Index end = ast_procedure_end(nodes, analyzer->node_count, procedure);
            for (Ast_Node_Index current = procedure + 1; current < end; current += 1) {
                Ast_Node_Type node_type = nodes[current].node_type;
                // The built-in `int_to_str` is the one call that is a constant.
                bool is_int_to_str = node_type == AST_NODE_TYPE_CALL &&
                                     analyzer->bindings[current].kind ==
                                         SEMANTIC_BINDING_KIND_NONE &&
                                     str_equals(nodes[current].payload.call.name,
                                                STR("int_to_str"));
                // The expressions that can be constants. The others are statements, or expressions
                // that never are, such as a call, which a target finds out from the entry that
                // stays unknown.
                bool is_candidate = node_type == AST_NODE_TYPE_ADDITION ||
                                    node_type == AST_NODE_TYPE_AND ||
                                    node_type == AST_NODE_TYPE_BOOL_LITERAL ||
                                    node_type == AST_NODE_TYPE_COMPARISON ||
                                    is_int_to_str ||
                                    node_type == AST_NODE_TYPE_CONCATENATION ||
                                    node_type == AST_NODE_TYPE_DIVISION ||
                                    node_type == AST_NODE_TYPE_FLOAT_LITERAL ||
                                    node_type == AST_NODE_TYPE_IDENTIFIER ||
                                    node_type == AST_NODE_TYPE_INTEGER_LITERAL ||
                                    node_type == AST_NODE_TYPE_MULTIPLICATION ||
                                    node_type == AST_NODE_TYPE_NOT ||
                                    node_type == AST_NODE_TYPE_OR ||
                                    node_type == AST_NODE_TYPE_RUNE_LITERAL ||
                                    node_type == AST_NODE_TYPE_STRING_LITERAL ||
                                    node_type == AST_NODE_TYPE_SUBTRACTION;
                if (not is_candidate) {
                    continue;
                }
                // A node that an operator above it evaluated already is not worked out again.
                Constant_Value value = constant_evaluate(analyzer->nodes, analyzer->node_count,
                                                         analyzer->bindings, analyzer->constants,
                                                         &analyzer->text, current);
                // Most expressions of a procedure are not constants, which is fine there, but the
                // text of a `++` is made when the program is compiled, so it has to be one.
                if (value.status == CONSTANT_STATUS_NOT_CONSTANT &&
                    node_type == AST_NODE_TYPE_CONCATENATION) {
                    return semantic_fail(
                        analyzer, SEMANTIC_STATUS_CONCATENATION_NOT_CONSTANT,
                        semantic_constant_subject(analyzer, current, definition));
                }
                if (value.status == CONSTANT_STATUS_NOT_CONSTANT && is_int_to_str) {
                    return semantic_fail(
                        analyzer, SEMANTIC_STATUS_INT_TO_STR_NOT_CONSTANT,
                        semantic_constant_subject(analyzer, current, definition));
                }
                if (value.status != CONSTANT_STATUS_OK &&
                    value.status != CONSTANT_STATUS_NOT_CONSTANT) {
                    return semantic_constant_fail(analyzer, value, definition);
                }
            }
        }
        definition = nodes[definition].next_sibling;
    }
    return true;
}

/**
 * Records the error of `value`, which is not `CONSTANT_STATUS_OK`, found in `definition`, the
 * constant, global variable or procedure that it is in. Returns false, like every function that
 * records an error.
 */
static bool semantic_constant_fail(Semantic_Analyzer *analyzer, Constant_Value value,
                                   Ast_Node_Index definition) {
    assert(value.status != CONSTANT_STATUS_OK);
    assert(definition < analyzer->node_count);

    Str subject = semantic_constant_subject(analyzer, value.error, definition);
    switch (value.status) {
    case CONSTANT_STATUS_CYCLE:
        return semantic_fail(analyzer, SEMANTIC_STATUS_CONSTANT_CYCLE, subject);
    case CONSTANT_STATUS_DEPTH_EXCEEDED:
        return semantic_fail(analyzer, SEMANTIC_STATUS_CONSTANT_TOO_DEEP, subject);
    case CONSTANT_STATUS_DIVISION_BY_ZERO:
        return semantic_fail(analyzer, SEMANTIC_STATUS_CONSTANT_DIVISION_BY_ZERO, subject);
    case CONSTANT_STATUS_NOT_CONSTANT:
        // Only the value of a definition has to be a constant, and its name is the subject.
        if (analyzer->nodes[definition].node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION) {
            return semantic_fail(analyzer, SEMANTIC_STATUS_GLOBAL_VALUE_NOT_CONSTANT, subject);
        }
        return semantic_fail(analyzer, SEMANTIC_STATUS_CONSTANT_NOT_CONSTANT, subject);
    case CONSTANT_STATUS_OPERAND_TYPE_INVALID:
        return semantic_fail(analyzer, SEMANTIC_STATUS_CONSTANT_OPERAND_TYPE_INVALID, subject);
    case CONSTANT_STATUS_OVERFLOW:
        return semantic_fail(analyzer, SEMANTIC_STATUS_CONSTANT_OVERFLOW, subject);
    case CONSTANT_STATUS_TEXT_TOO_LONG:
        return semantic_fail(analyzer, SEMANTIC_STATUS_CONSTANT_TEXT_TOO_LONG, subject);
    case CONSTANT_STATUS_TYPE_INVALID:
        return semantic_fail(analyzer, SEMANTIC_STATUS_CONSTANT_TYPE_INVALID, subject);
    case CONSTANT_STATUS_TYPE_MISMATCH:
        return semantic_fail(analyzer, SEMANTIC_STATUS_CONSTANT_TYPE_MISMATCH, subject);
    case CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE:
        return semantic_fail(analyzer, SEMANTIC_STATUS_CONSTANT_VALUE_NOT_REPRESENTABLE, subject);
    case CONSTANT_STATUS_EVALUATING:
    case CONSTANT_STATUS_OK:
    case CONSTANT_STATUS_UNKNOWN:
        // A finished evaluation has none of these, and the second is no error.
        assert(false);
        break;
    }
    return semantic_fail(analyzer, SEMANTIC_STATUS_CONSTANT_NOT_CONSTANT, subject);
}

/**
 * Returns the text in the source that an error about `node` points at: the name of a definition,
 * or the first name or string in an expression, which is its leftmost leaf. An expression that
 * starts with a number or with `true` or `false` has no such text, and neither has a node of
 * `AST_NODE_INDEX_NONE`, so the name of `definition`, which the error is in, is the closest place
 * that the tree knows.
 */
static Str semantic_constant_subject(Semantic_Analyzer const *analyzer, Ast_Node_Index node,
                                     Ast_Node_Index definition) {
    assert(definition < analyzer->node_count);

    Ast_Node_Index current = node;
    // Each step goes to a child, so there cannot be more than there are nodes.
    for (size_t i = 0; i < analyzer->node_count && current != AST_NODE_INDEX_NONE; i += 1) {
        Ast_Node const *current_node = &analyzer->nodes[current];
        if (current_node->node_type == AST_NODE_TYPE_IDENTIFIER) {
            return current_node->payload.identifier.name;
        }
        if (current_node->node_type == AST_NODE_TYPE_STRING_LITERAL) {
            return current_node->payload.string_literal.text;
        }
        if (current_node->node_type == AST_NODE_TYPE_CONSTANT_DEFINITION ||
            current_node->node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION) {
            return ast_definition_name(current_node);
        }
        current = current_node->first_child;
    }
    return ast_definition_name(&analyzer->nodes[definition]);
}

/**
 * Checks the attributes of `node`, a global variable or a match statement, which are its first
 * children. Each kind of node has one attribute, `known`, which `#(static)` and `#(partial)` give,
 * and any other is an error of `unknown_status`, because the message says which one there is. An
 * attribute cannot be given twice either. Returns false after it recorded an error, and sets
 * `*has_known` to whether the attribute is given.
 */
static bool semantic_attributes_check(Semantic_Analyzer *analyzer, Ast_Node_Index node,
                                      Str known, Semantic_Status unknown_status,
                                      bool *has_known) {
    assert(node < analyzer->node_count);
    assert(known.bytes != NULL);

    *has_known = false;
    Ast_Node_Index attribute = analyzer->nodes[node].first_child;
    // Every attribute is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < analyzer->node_count && attribute != AST_NODE_INDEX_NONE; i += 1) {
        Ast_Node const *attribute_node = &analyzer->nodes[attribute];
        if (attribute_node->node_type != AST_NODE_TYPE_ATTRIBUTE) {
            break;
        }

        Str name = attribute_node->payload.attribute.name;
        if (not str_equals(name, known)) {
            return semantic_fail(analyzer, unknown_status, name);
        }
        if (*has_known) {
            return semantic_fail(analyzer, SEMANTIC_STATUS_ATTRIBUTE_DUPLICATE, name);
        }
        *has_known = true;

        attribute = attribute_node->next_sibling;
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
    // The defer statement that the pass is in, while it is in its deferred statement, and the index
    // after the last node of that statement. One is enough, because a `defer` cannot be in another.
    Ast_Node_Index defer = AST_NODE_INDEX_NONE;
    Ast_Node_Index defer_end = 0;
    for (Ast_Node_Index current = procedure + 1; current < end; current += 1) {
        Ast_Node_Type node_type = analyzer->nodes[current].node_type;

        while (target_count > 0 && targets[target_count - 1].end <= current) {
            target_count -= 1;
        }
        if (defer != AST_NODE_INDEX_NONE && defer_end <= current) {
            defer = AST_NODE_INDEX_NONE;
        }
        if (node_type == AST_NODE_TYPE_DEFER_STATEMENT) {
            if (defer != AST_NODE_INDEX_NONE) {
                return semantic_fail(analyzer, SEMANTIC_STATUS_DEFER_NESTED,
                                     analyzer->nodes[current].payload.jump.keyword);
            }
            if (not semantic_defer_check(analyzer, current)) {
                return false;
            }
            defer = current;
            defer_end = semantic_defer_end(analyzer, current);
        }
        if (node_type == AST_NODE_TYPE_RETURN_STATEMENT && defer != AST_NODE_INDEX_NONE) {
            return semantic_fail(analyzer, SEMANTIC_STATUS_RETURN_IN_DEFER,
                                 analyzer->nodes[current].payload.jump.keyword);
        }
        if (semantic_jump_target_is_statement(analyzer, procedure, current)) {
            if (not semantic_jump_target_push(analyzer, targets, &target_count, current)) {
                return false;
            }
        }
        if (node_type == AST_NODE_TYPE_BREAK_STATEMENT ||
            node_type == AST_NODE_TYPE_CONTINUE_STATEMENT) {
            if (not semantic_jump_resolve(analyzer, targets, target_count, current)) {
                return false;
            }
            // The statements are in the order of the source and the target is around the jump, so
            // a target that comes before the `defer` is around the `defer` as well.
            if (defer != AST_NODE_INDEX_NONE && analyzer->bindings[current].target < defer) {
                return semantic_fail(analyzer, SEMANTIC_STATUS_JUMP_OUT_OF_DEFER,
                                     analyzer->nodes[current].payload.jump.keyword);
            }
        }

        if (node_type == AST_NODE_TYPE_PARAMETER) {
            // The parameters come first, so by the time that a statement is reached all of them
            // are known.
            if (not semantic_parameter_check(analyzer, procedure, current)) {
                return false;
            }
        } else if (node_type == AST_NODE_TYPE_VARIABLE_DEFINITION ||
                   node_type == AST_NODE_TYPE_FOR_STATEMENT) {
            statement = current;
            if (not semantic_variable_check(analyzer, procedure, current)) {
                return false;
            }
        } else if (node_type == AST_NODE_TYPE_IDENTIFIER) {
            // The nodes of an expression come after the node of their statement, and the other
            // kinds of node that are met in between are not statements.
            if (not semantic_identifier_resolve(analyzer, procedure, statement, current)) {
                return false;
            }
        } else if (node_type == AST_NODE_TYPE_CALL) {
            if (not semantic_call_resolve(analyzer, procedure, statement, current)) {
                return false;
            }
        } else if (node_type == AST_NODE_TYPE_ADDRESS_OF) {
            // The node comes after its operand, so the names in it are resolved already.
            semantic_address_of_resolve(analyzer, current);
        } else if (node_type == AST_NODE_TYPE_MATCH_STATEMENT) {
            // Whether the match is partial is for the target, which knows the type of the subject.
            statement = current;
            bool is_partial = false;
            if (not semantic_attributes_check(analyzer, current, STR("partial"),
                                          SEMANTIC_STATUS_MATCH_ATTRIBUTE_UNKNOWN,
                                          &is_partial)) {
                return false;
            }
        } else if (node_type == AST_NODE_TYPE_RETURN_STATEMENT ||
                   node_type == AST_NODE_TYPE_CALL_STATEMENT ||
                   node_type == AST_NODE_TYPE_IF_STATEMENT ||
                   node_type == AST_NODE_TYPE_LOOP_STATEMENT ||
                   node_type == AST_NODE_TYPE_MATCH_CASE ||
                   ast_node_type_is_assignment(node_type)) {
            // The condition of a loop belongs to the loop, so the variable defined before it is in
            // scope there.
            statement = current;
        }
    }

    return true;
}

/**
 * Marks the variable that the operand of `address_of`, an `AST_NODE_TYPE_ADDRESS_OF`, is a part of
 * as having its address taken. The operand is a name or a field of one, such as `x` or `x.a.b`,
 * and the name is followed down through the fields. Any other operand is an element of an array,
 * what a pointer points to or no place at all, none of which is a variable of the procedure, so
 * there is nothing to mark; whether such an operand has an address is for the target to tell. A
 * name that is not a variable, such as a parameter, is not marked either, for the same reason.
 */
static void semantic_address_of_resolve(Semantic_Analyzer *analyzer, Ast_Node_Index address_of) {
    assert(address_of < analyzer->node_count);
    assert(analyzer->nodes[address_of].node_type == AST_NODE_TYPE_ADDRESS_OF);

    Ast_Node_Index operand = analyzer->nodes[address_of].first_child;
    // Each step goes down one level, so there are fewer steps than nodes.
    for (size_t i = 0; i < analyzer->node_count &&
                       analyzer->nodes[operand].node_type == AST_NODE_TYPE_FIELD_ACCESS;
         i += 1) {
        operand = analyzer->nodes[operand].first_child;
    }
    if (analyzer->nodes[operand].node_type != AST_NODE_TYPE_IDENTIFIER) {
        return;
    }

    Semantic_Binding binding = analyzer->bindings[operand];
    if (binding.kind == SEMANTIC_BINDING_KIND_VARIABLE) {
        analyzer->bindings[binding.target].is_address_taken = true;
    }
}

/**
 * Checks that the statement of the defer statement `defer` is one that can be deferred: not a
 * variable definition, which would be out of scope as soon as it was made, and not a `return`, a
 * `break` or a `continue`, which would leave the block while it is being left. Returns false after
 * it recorded an error.
 */
static bool semantic_defer_check(Semantic_Analyzer *analyzer, Ast_Node_Index defer) {
    assert(defer < analyzer->node_count);
    assert(analyzer->nodes[defer].node_type == AST_NODE_TYPE_DEFER_STATEMENT);

    Ast_Node_Index statement = analyzer->nodes[defer].first_child;
    assert(statement == defer + 1);

    Ast_Node_Type node_type = analyzer->nodes[statement].node_type;
    if (node_type == AST_NODE_TYPE_VARIABLE_DEFINITION ||
        node_type == AST_NODE_TYPE_RETURN_STATEMENT ||
        node_type == AST_NODE_TYPE_BREAK_STATEMENT ||
        node_type == AST_NODE_TYPE_CONTINUE_STATEMENT) {
        return semantic_fail(analyzer, SEMANTIC_STATUS_DEFER_STATEMENT_INVALID,
                             analyzer->nodes[defer].payload.jump.keyword);
    }
    // The parser makes no other statements.
    assert(ast_node_type_is_assignment(node_type) || node_type == AST_NODE_TYPE_CALL_STATEMENT ||
           node_type == AST_NODE_TYPE_IF_STATEMENT || node_type == AST_NODE_TYPE_FOR_STATEMENT ||
           node_type == AST_NODE_TYPE_LOOP_STATEMENT ||
           node_type == AST_NODE_TYPE_MATCH_STATEMENT ||
           node_type == AST_NODE_TYPE_SCOPE_STATEMENT ||
           node_type == AST_NODE_TYPE_LABELED_STATEMENT);
    return true;
}

/**
 * Returns the index after the last node of the statement of `defer`, a defer statement that
 * `semantic_defer_check` accepted, as far as statements are concerned: a statement with a block
 * ends where its last block does, and an assignment or a call ends right after its own node,
 * because it holds no statements, only expressions, which are no matter to the analysis of a
 * `defer`.
 */
static Ast_Node_Index semantic_defer_end(Semantic_Analyzer const *analyzer, Ast_Node_Index defer) {
    assert(defer < analyzer->node_count);
    assert(analyzer->nodes[defer].node_type == AST_NODE_TYPE_DEFER_STATEMENT);

    Ast_Node_Index statement = analyzer->nodes[defer].first_child;
    if (analyzer->nodes[statement].node_type == AST_NODE_TYPE_LABELED_STATEMENT) {
        statement = analyzer->nodes[statement].first_child;
    }

    Ast_Node_Type node_type = analyzer->nodes[statement].node_type;
    if (node_type == AST_NODE_TYPE_IF_STATEMENT || node_type == AST_NODE_TYPE_FOR_STATEMENT ||
        node_type == AST_NODE_TYPE_LOOP_STATEMENT || node_type == AST_NODE_TYPE_SCOPE_STATEMENT) {
        // The last child of each of them is a block, also of an if statement with an `else`.
        Ast_Node_Index block = analyzer->nodes[statement].last_child;
        assert(analyzer->nodes[block].node_type == AST_NODE_TYPE_BLOCK);
        assert(analyzer->nodes[block].payload.block.end > block);
        return analyzer->nodes[block].payload.block.end;
    }
    if (node_type == AST_NODE_TYPE_MATCH_STATEMENT) {
        // The parser made sure that the last case has a block, which is where the match ends.
        Ast_Node_Index block = ast_match_case_block(analyzer->nodes,
                                                    analyzer->nodes[statement].last_child);
        assert(block != AST_NODE_INDEX_NONE);
        return analyzer->nodes[block].payload.block.end;
    }
    return statement + 1;
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
        if (not is_break && analyzer->nodes[found].node_type != AST_NODE_TYPE_FOR_STATEMENT &&
            analyzer->nodes[found].node_type != AST_NODE_TYPE_LOOP_STATEMENT) {
            return semantic_fail(analyzer, SEMANTIC_STATUS_LABEL_NOT_LOOP, label);
        }
    } else {
        for (size_t i = target_count; i > 0 && found == AST_NODE_INDEX_NONE; i -= 1) {
            Semantic_Jump_Target const *target = &targets[i - 1];
            if (not target->is_if && (is_break || target->is_loop)) {
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
    if (parameter >= analyzer->imported_end && definition.kind != SEMANTIC_BINDING_KIND_NONE) {
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

    // `_` is where a value that is not wanted goes, so any number of them can be in one procedure.
    if (str_equals(name, STR("_"))) {
        return true;
    }

    Semantic_Binding earlier = semantic_variable_find(analyzer, procedure, variable,
                                                     AST_NODE_INDEX_NONE, name);
    if (earlier.kind != SEMANTIC_BINDING_KIND_NONE) {
        return semantic_fail(analyzer, SEMANTIC_STATUS_VARIABLE_NAME_DUPLICATE, name);
    }

    Semantic_Binding parameter = semantic_parameter_find(analyzer, procedure, name);
    Semantic_Binding definition = semantic_definition_find(analyzer, name);
    if (parameter.kind != SEMANTIC_BINDING_KIND_NONE ||
        (variable >= analyzer->imported_end && definition.kind != SEMANTIC_BINDING_KIND_NONE)) {
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
        // A field of a name can be a field of what the name points to, which is not the name that
        // is assigned to, and only the types tell. So only a name that is assigned to as a whole is
        // checked here, and the target checks the fields.
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
 * Binds the call `call`, which belongs to `statement` of `procedure`, to what it calls. The name is
 * the name of a constant whose value is a procedure, which is the call of that procedure, or the
 * name of a variable, a parameter or a global variable, which is a call through the pointer to a
 * procedure that it holds. Whether it holds one is for the target to check, because it depends on
 * the type. A constant that is a string or a struct cannot be called, and a name of nothing is
 * unknown. The variable that `statement` defines is not in scope in its own value, as for any other
 * use of a name. Returns false after it recorded an error.
 */
static bool semantic_call_resolve(Semantic_Analyzer *analyzer, Ast_Node_Index procedure,
                                  Ast_Node_Index statement, Ast_Node_Index call) {
    assert(call < analyzer->node_count);
    assert(analyzer->nodes[call].node_type == AST_NODE_TYPE_CALL);

    Str name = analyzer->nodes[call].payload.call.name;

    Ast_Node_Index excluded = AST_NODE_INDEX_NONE;
    if (statement != AST_NODE_INDEX_NONE &&
        analyzer->nodes[statement].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION) {
        excluded = statement;
    }

    // A name has one meaning, so the order of the lookups does not change the result.
    Semantic_Binding binding = semantic_variable_find(analyzer, procedure, call, excluded, name);
    if (binding.kind == SEMANTIC_BINDING_KIND_NONE) {
        binding = semantic_parameter_find(analyzer, procedure, name);
    }
    if (binding.kind == SEMANTIC_BINDING_KIND_NONE) {
        binding = semantic_definition_find(analyzer, name);
    }
    if (binding.kind == SEMANTIC_BINDING_KIND_NONE) {
        // A built-in procedure is what is called when the program has no name of its own, and the
        // call keeps the kind `SEMANTIC_BINDING_KIND_NONE` for the target to recognize by name.
        if (semantic_is_built_in_procedure(name)) {
            return semantic_call_type_arguments_check(analyzer, call, AST_NODE_INDEX_NONE);
        }
        return semantic_fail(analyzer, SEMANTIC_STATUS_IDENTIFIER_UNKNOWN, name);
    }

    if (binding.kind != SEMANTIC_BINDING_KIND_CONSTANT) {
        analyzer->bindings[call] = binding;
        return semantic_call_type_arguments_check(analyzer, call, AST_NODE_INDEX_NONE);
    }
    Ast_Node_Index value = analyzer->nodes[binding.target].first_child;
    if (analyzer->nodes[value].node_type != AST_NODE_TYPE_PROCEDURE) {
        return semantic_fail(analyzer, SEMANTIC_STATUS_CALL_TARGET_NOT_PROCEDURE, name);
    }

    analyzer->bindings[call] = binding;
    return semantic_call_type_arguments_check(analyzer, call, value);
}

/**
 * Checks the arguments of `call` that are types, which `procedure` takes, the
 * `AST_NODE_TYPE_PROCEDURE` that the call is bound to, or `AST_NODE_INDEX_NONE` for a call of a
 * built-in procedure or through a pointer. A type is an argument only of a type parameter that is
 * given as a type, `$T: type_id`, when the call gives every parameter, the type parameters too, see
 * `ast_parameter_is_type`, and of the built-in `make`, whose first argument is a type, see
 * `AST_NODE_TYPE_CALL`. A type that is a name, such as `Int` or `Point`, is parsed as a name, so it
 * becomes the `AST_NODE_TYPE_TYPE_NAME` that it is here, before the names of the arguments are
 * looked up, which they are after the call because their nodes come after it. Any other type that
 * is an argument is an error, and so is an argument of a type parameter that is not a type.
 * Returns false after it recorded an error.
 */
static bool semantic_call_type_arguments_check(Semantic_Analyzer *analyzer, Ast_Node_Index call,
                                               Ast_Node_Index procedure) {
    assert(analyzer->nodes[call].node_type == AST_NODE_TYPE_CALL);

    Str name = analyzer->nodes[call].payload.call.name;
    size_t argument_count = 0;
    for (Ast_Node_Index argument = analyzer->nodes[call].first_child;
         argument != AST_NODE_INDEX_NONE; argument = analyzer->nodes[argument].next_sibling) {
        argument_count += 1;
    }
    size_t parameter_count = 0;
    bool has_type_parameter = false;
    if (procedure != AST_NODE_INDEX_NONE) {
        for (Ast_Node_Index parameter = analyzer->nodes[procedure].first_child;
             parameter != AST_NODE_INDEX_NONE &&
             analyzer->nodes[parameter].node_type == AST_NODE_TYPE_PARAMETER;
             parameter = analyzer->nodes[parameter].next_sibling) {
            parameter_count += 1;
            has_type_parameter = has_type_parameter || ast_parameter_is_type(analyzer->nodes,
                                                                             parameter);
        }
    }
    bool is_explicit = has_type_parameter && argument_count == parameter_count;
    bool is_make = procedure == AST_NODE_INDEX_NONE &&
                   analyzer->bindings[call].kind == SEMANTIC_BINDING_KIND_NONE &&
                   str_equals(name, STR("make"));

    Ast_Node_Index parameter = AST_NODE_INDEX_NONE;
    if (is_explicit) {
        parameter = analyzer->nodes[procedure].first_child;
    }
    size_t index = 0;
    for (Ast_Node_Index argument = analyzer->nodes[call].first_child;
         argument != AST_NODE_INDEX_NONE; argument = analyzer->nodes[argument].next_sibling) {
        Ast_Node *node = &analyzer->nodes[argument];
        bool is_type = node->node_type == AST_NODE_TYPE_TYPE_NAME ||
                       node->node_type == AST_NODE_TYPE_ARRAY_TYPE ||
                       node->node_type == AST_NODE_TYPE_POINTER_TYPE;
        bool is_type_wanted = (is_make && index == 0) ||
                              (parameter != AST_NODE_INDEX_NONE &&
                               ast_parameter_is_type(analyzer->nodes, parameter));
        if (is_type_wanted && node->node_type == AST_NODE_TYPE_IDENTIFIER) {
            // The payloads of both are the name alone, see `AST_NODE_TYPE_TYPE_NAME`.
            Str type_name = node->payload.identifier.name;
            node->node_type = AST_NODE_TYPE_TYPE_NAME;
            node->payload.type_name.name = type_name;
            is_type = true;
        }
        if (is_type_wanted && not is_type) {
            return semantic_fail(analyzer, SEMANTIC_STATUS_TYPE_ARGUMENT_INVALID, name);
        }
        if (not is_type_wanted && is_type) {
            return semantic_fail(analyzer, SEMANTIC_STATUS_TYPE_ARGUMENT_MISPLACED, name);
        }
        if (parameter != AST_NODE_INDEX_NONE) {
            parameter = analyzer->nodes[parameter].next_sibling;
        }
        index += 1;
    }
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
        if (analyzer->nodes[definition].node_type == AST_NODE_TYPE_FOREIGN_IMPORT) {
            definition = analyzer->nodes[definition].next_sibling;
            continue;
        }

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
                       AST_NODE_TYPE_STRUCT_TYPE &&
                   analyzer->nodes[analyzer->nodes[definition].first_child].node_type !=
                       AST_NODE_TYPE_ENUM_TYPE) {
            // A struct and an enum are types and become no function, so they take no place among
            // the constants.
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
            if (is_in_scope && current != excluded && not str_equals(name, STR("_")) &&
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

bool semantic_is_built_in_procedure(Str name) {
    assert(name.bytes != NULL);

    return str_equals(name, STR("append")) || str_equals(name, STR("destroy")) ||
           str_equals(name, STR("int_to_byte")) || str_equals(name, STR("length_of")) ||
           str_equals(name, STR("make")) ||
           str_equals(name, STR("str_to_c_str")) || str_equals(name, STR("try_str_to_int")) ||
           str_equals(name, STR("utf8_decode")) || str_equals(name, STR("int_to_i32")) ||
           str_equals(name, STR("int_to_str"));
}

