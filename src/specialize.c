#include <assert.h>
#include <iso646.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <mialu/constant.h>
#include <mialu/specialize.h>

enum {
    // The most rounds, see `specialize`. A program needs as many as its polymorphic procedures call
    // each other deep, plus one, so this is far more than one needs, and a procedure that calls
    // itself with a larger type each time, which would need rounds without end, stops at it. Such a
    // type grows by three tokens in a round, so it stays within `SPECIALIZE_TYPE_TOKEN_MAX`.
    SPECIALIZE_ROUND_MAX = 32,
    // The most copies of polymorphic definitions, which is as many procedures as a target builds,
    // see `X64_CONSTANT_COUNT_MAX`.
    SPECIALIZE_REQUEST_MAX = 1024,
    // The most type parameters of one procedure or struct, see `AST_STRUCT_TYPE_PARAMETER_COUNT_MAX`.
    SPECIALIZE_TYPE_PARAMETER_MAX = AST_STRUCT_TYPE_PARAMETER_COUNT_MAX,
    // The longest text of a type that is worked out, which is far longer than a type is written.
    SPECIALIZE_TYPE_TEXT_SIZE_BYTES = 512,
    // The most tokens of the text of a type, see `Specialize_Type_Tokens`.
    SPECIALIZE_TYPE_TOKEN_MAX = 128,
    // How deep the type of an expression is followed through the names and the expressions that it
    // is made of, which is the depth that a constant expression is allowed to have.
    SPECIALIZE_TYPE_DEPTH_MAX = 64,
};

// The bit of a mark that says that a node was replaced by the nodes of a type, see
// `specialize_copy_range`. The generations of the marks are below it.
static uint32_t const SPECIALIZE_MARK_REPLACED = 0x80000000u;

/**
 * A token of the text of a type: a word, which is a name, a name of a package with a dot, a name
 * with `$` before it or a number, and `kind` is `w` for it, or one of the characters `^`, `[`, `]`,
 * `(`, `)` and `,`, which is its kind. The text is a view of the text of the type.
 */
typedef struct {
    Str text;
    char kind;
} Specialize_Type_Token;

/**
 * The tokens of the text of a type, which is not valid when it has a character that no type has,
 * or more tokens than there is room for.
 */
typedef struct {
    Specialize_Type_Token tokens[SPECIALIZE_TYPE_TOKEN_MAX];
    size_t count;
    bool is_valid;
} Specialize_Type_Tokens;

/**
 * Text that is made in place, which is not valid once it does not fit.
 */
typedef struct {
    char bytes[SPECIALIZE_TYPE_TEXT_SIZE_BYTES];
    size_t length;
    bool is_overflowed;
} Specialize_Type_Text;

/**
 * A copy of a polymorphic definition that the program needs: the origin of the definition, see
 * `Specialize_Result`, the types that its type parameters are, as one text of canonical types
 * separated by `, `, in the order that the type parameters are declared, and the name of the copy,
 * which is the name of the definition and the types between parentheses. Both texts are in the
 * texts of the specialization. A copy is made once, in the round that first needs it.
 */
typedef struct {
    Str arguments;
    Str name;
    Ast_Node_Index generic;
    bool is_emitted;
} Specialize_Request;

/**
 * The type parameters of a polymorphic definition and what a copy of it makes of them: the names,
 * without `$`, and the types that the copy gives them, which are views of the texts of the
 * specialization, and the name of the copy.
 */
typedef struct {
    Str names[SPECIALIZE_TYPE_PARAMETER_MAX];
    Str arguments[SPECIALIZE_TYPE_PARAMETER_MAX];
    Str name;
    size_t count;
} Specialize_Substitution;

// What came of deducing the types of a call. Alphabetical, because nothing depends on the order.
typedef enum {
    SPECIALIZE_DEDUCTION_DONE,
    SPECIALIZE_DEDUCTION_FAILED,
    // The type of an argument is not known yet, because it is the result of a call that is not
    // specialized yet, so the call is looked at again in the next round.
    SPECIALIZE_DEDUCTION_PENDING,
} Specialize_Deduction;

/**
 * The state shared by the functions of the specialization. The tree of a round is the one that is
 * read, with its analysis and the origins of its nodes, and the next tree is the one that is made,
 * see `specialize_copy_tree`. A function that can fail returns false after it recorded the error
 * here, and its caller returns false too.
 */
typedef struct {
    Ast_Node const *nodes;
    Semantic_Binding const *bindings;
    Constant_Value const *constants;
    // The origins of the nodes of the tree of the round, or NULL when it is the tree that was
    // given, whose nodes are their own origins.
    Ast_Node_Index const *origins;
    size_t node_count;
    Ast_Node_Index root;
    size_t imported_end;
    Ast_Node *target;
    Ast_Node_Index *target_origins;
    size_t target_count;
    size_t target_max;
    // For each node of the tree of the round, where its copy is in the next tree, and the size of
    // the next tree after the node is copied, see `specialize_copy_range`.
    Ast_Node_Index *positions;
    Ast_Node_Index *ends;
    // For each node of the tree of the round, the generation of the copy that leaves it out, or
    // that replaces it, with `SPECIALIZE_MARK_REPLACED`.
    uint32_t *marks;
    // For each node of the tree of the round, the text that its copy has instead of its name or its
    // type, or a `Str` whose `bytes` is NULL.
    Str *rewrites;
    Specialize_Texts *texts;
    uint32_t generation;
    uint32_t round_generation;
    Specialize_Request requests[SPECIALIZE_REQUEST_MAX];
    size_t request_count;
    // What the round found: the nodes whose copies are not the same, and the calls whose types are
    // not known yet, with the first of them and its type parameter that is not known, for the error
    // when they stay so.
    size_t change_count;
    size_t pending_count;
    Ast_Node_Index pending_call;
    Str pending_parameter;
    Str subject;
    Str detail;
    Str other;
    Str target_text;
    int64_t number;
    Specialize_Status status;
} Specializer;

static bool             specialize_call_scan           (Specializer *specializer,
                                                        Ast_Node_Index call,
                                                        Ast_Node_Index definition);
static Specialize_Deduction specialize_call_deduce     (Specializer *specializer,
                                                        Ast_Node_Index call,
                                                        Ast_Node_Index procedure,
                                                        Specialize_Substitution *deduced,
                                                        bool *is_explicit);
static Specialize_Type_Text specialize_canonical       (Specialize_Type_Tokens const *tokens,
                                                        size_t start, size_t end);
static bool             specialize_copy_range          (Specializer *specializer,
                                                        Ast_Node_Index start, Ast_Node_Index end,
                                                        Specialize_Substitution const *substitution,
                                                        Ast_Node_Index *copy);
static bool             specialize_copy_tree           (Specializer *specializer, bool is_final);
static Ast_Node_Index   specialize_definition_end      (Specializer const *specializer,
                                                        Ast_Node_Index definition);
static Ast_Node_Index   specialize_definition_find     (Specializer const *specializer, Str name);
static bool             specialize_definition_type     (Specializer const *specializer,
                                                        Ast_Node_Index target, size_t depth,
                                                        Specialize_Type_Text *text);
static bool             specialize_expression_type     (Specializer const *specializer,
                                                        Ast_Node_Index expression, size_t depth,
                                                        Specialize_Type_Text *text);
static bool             specialize_fail                (Specializer *specializer,
                                                        Specialize_Status status, Str subject);
static Ast_Node_Index   specialize_origin              (Specializer const *specializer,
                                                        Ast_Node_Index node);
static bool             specialize_parameters_collect  (Specializer *specializer,
                                                        Ast_Node_Index definition,
                                                        Specialize_Substitution *substitution);
static bool             specialize_request             (Specializer *specializer,
                                                        Ast_Node_Index generic, Str arguments,
                                                        Str source, Str *name);
static bool             specialize_round_scan          (Specializer *specializer);
static Str              specialize_store               (Specializer *specializer, Str text,
                                                        Str source);
static Str              specialize_substitute          (Specializer *specializer, Str text,
                                                        Specialize_Substitution const *substitution,
                                                        bool *is_failed);
static bool             specialize_has_parameter       (Str text,
                                                        Specialize_Substitution const *substitution);
static void             specialize_text_append         (Specialize_Type_Text *text, Str part);
static Str              specialize_text_str            (Specialize_Type_Text const *text);
static bool             specialize_type_match          (Specializer *specializer, Str pattern,
                                                        Str actual, Specialize_Substitution *deduced,
                                                        bool is_untyped, Ast_Node_Index call,
                                                        bool *is_matched);
static bool             specialize_type_nodes_emit     (Specializer *specializer, Str text,
                                                        uint32_t source_offset,
                                                        Ast_Node_Index *root);
static bool             specialize_type_node_text      (Ast_Node const *nodes, Ast_Node_Index type,
                                                        Specialize_Type_Text *text);
static bool             specialize_type_text_process   (Specializer *specializer, Str text,
                                                        Str *canonical);
static Specialize_Type_Tokens specialize_type_tokenize (Str text);
static size_t           specialize_type_extent         (Specialize_Type_Tokens const *tokens,
                                                        size_t start);

Specialize_Result specialize(Array_Slice nodes, Ast_Node_Index root, size_t imported_end,
                             Semantic_Result analyzed, Specialize_Buffers buffers,
                             Specialize_Texts *texts) {
    assert(nodes.elements != NULL);
    assert(root < nodes.length);
    assert(analyzed.status == SEMANTIC_STATUS_OK);
    assert(analyzed.bindings.length == nodes.length);
    assert(texts != NULL);
    assert(buffers.nodes.length == buffers.spare_nodes.length);
    assert(buffers.origins.length >= buffers.nodes.length);
    assert(buffers.spare_origins.length >= buffers.nodes.length);
    assert(buffers.positions.length > nodes.length);
    assert(buffers.ends.length > nodes.length);
    assert(buffers.marks.length > nodes.length);
    assert(buffers.rewrites.length >= nodes.length);

    Specializer specializer_state;
    Specializer *specializer = &specializer_state;
    *specializer = (Specializer){
        .nodes = nodes.elements,
        .bindings = analyzed.bindings.elements,
        .constants = analyzed.constants.elements,
        .origins = NULL,
        .node_count = nodes.length,
        .root = root,
        .imported_end = imported_end,
        .target = NULL,
        .target_origins = NULL,
        .target_count = 0,
        .target_max = buffers.nodes.length,
        .positions = buffers.positions.elements,
        .ends = buffers.ends.elements,
        .marks = buffers.marks.elements,
        .rewrites = buffers.rewrites.elements,
        .texts = texts,
        .generation = 1,
        .round_generation = 1,
        .request_count = 0,
        .change_count = 0,
        .pending_count = 0,
        .pending_call = AST_NODE_INDEX_NONE,
        .pending_parameter = STR_EMPTY,
        .subject = STR_EMPTY,
        .detail = STR_EMPTY,
        .other = STR_EMPTY,
        .target_text = STR_EMPTY,
        .number = 0,
        .status = SPECIALIZE_STATUS_OK,
    };
    for (size_t i = 0; i < buffers.marks.length; i += 1) {
        ((uint32_t *)buffers.marks.elements)[i] = 0;
    }
    for (size_t i = 0; i < buffers.rewrites.length; i += 1) {
        ((Str *)buffers.rewrites.elements)[i] = STR_EMPTY;
    }

    Specialize_Result result = {
        .nodes = nodes,
        .origins = buffers.origins,
        .semantic = analyzed,
        .subject = STR_EMPTY,
        .detail = STR_EMPTY,
        .other = STR_EMPTY,
        .target = STR_EMPTY,
        .number = 0,
        .root = root,
        .imported_end = imported_end,
        .status = SPECIALIZE_STATUS_OK,
    };

    // The trees of the rounds alternate between the two buffers, and the first round reads the
    // tree that was given.
    Array_Slice node_buffers[2] = {buffers.nodes, buffers.spare_nodes};
    Array_Slice origin_buffers[2] = {buffers.origins, buffers.spare_origins};
    size_t next = 0;
    bool is_original = true;
    bool is_ok = true;
    size_t round = 0;
    for (round = 0; round < SPECIALIZE_ROUND_MAX && is_ok; round += 1) {
        specializer->change_count = 0;
        specializer->pending_count = 0;
        specializer->pending_call = AST_NODE_INDEX_NONE;
        specializer->generation += 1;
        specializer->round_generation = specializer->generation;
        is_ok = specialize_round_scan(specializer);
        if (not is_ok) {
            break;
        }
        if (specializer->change_count == 0) {
            if (specializer->pending_count > 0) {
                specializer->detail = specializer->pending_parameter;
                is_ok = specialize_fail(
                    specializer, SPECIALIZE_STATUS_TYPE_PARAMETER_UNDEDUCED,
                    specializer->nodes[specializer->pending_call].payload.call.name);
            }
            break;
        }

        specializer->target = node_buffers[next].elements;
        specializer->target_origins = origin_buffers[next].elements;
        is_ok = specialize_copy_tree(specializer, false);
        if (not is_ok) {
            break;
        }
        Semantic_Result semantic = semantic_analyze(
            array_slice_make(specializer->target, specializer->target_count),
            specializer->root, specializer->imported_end, buffers.bindings, buffers.constants,
            buffers.text);
        if (semantic.status != SEMANTIC_STATUS_OK) {
            result.semantic = semantic;
            result.status = SPECIALIZE_STATUS_SEMANTIC;
            return result;
        }
        specializer->nodes = specializer->target;
        specializer->origins = specializer->target_origins;
        specializer->node_count = specializer->target_count;
        specializer->bindings = semantic.bindings.elements;
        specializer->constants = semantic.constants.elements;
        is_original = false;
        next = 1 - next;
    }
    if (is_ok && round == SPECIALIZE_ROUND_MAX) {
        // The copy that was requested last is the one that goes on growing, and its name is told
        // at the place that needed it.
        assert(specializer->request_count > 0);
        specializer->number = SPECIALIZE_ROUND_MAX;
        is_ok = specialize_fail(specializer, SPECIALIZE_STATUS_LIMIT,
                                specializer->requests[specializer->request_count - 1].name);
    }
    if (not is_ok) {
        result.subject = specializer->subject;
        result.detail = specializer->detail;
        result.other = specializer->other;
        result.target = specializer->target_text;
        result.number = specializer->number;
        result.status = specializer->status;
        return result;
    }

    // A program without polymorphic definitions, and without types to make canonical, is the tree
    // that was given, whose nodes are their own origins.
    if (is_original) {
        Ast_Node_Index *origins = buffers.origins.elements;
        for (size_t i = 0; i < nodes.length; i += 1) {
            origins[i] = (Ast_Node_Index)i;
        }
        result.origins = array_slice_make(buffers.origins.elements, nodes.length);
        return result;
    }

    // The last tree has the polymorphic definitions, which nothing uses any more, and the targets
    // are given a tree without them.
    specializer->target = node_buffers[next].elements;
    specializer->target_origins = origin_buffers[next].elements;
    specializer->generation += 1;
    specializer->round_generation = specializer->generation;
    if (not specialize_copy_tree(specializer, true)) {
        result.subject = specializer->subject;
        result.number = specializer->number;
        result.status = specializer->status;
        return result;
    }
    Array_Slice final_nodes = array_slice_make(specializer->target, specializer->target_count);
    Semantic_Result semantic =
        semantic_analyze(final_nodes, specializer->root, specializer->imported_end,
                         buffers.bindings, buffers.constants, buffers.text);
    result.nodes = final_nodes;
    result.origins = array_slice_make(specializer->target_origins, specializer->target_count);
    result.semantic = semantic;
    result.root = specializer->root;
    result.imported_end = specializer->imported_end;
    if (semantic.status != SEMANTIC_STATUS_OK) {
        result.status = SPECIALIZE_STATUS_SEMANTIC;
    }
    return result;
}

Str specialize_source_of(Specialize_Texts const *texts, Str subject) {
    if (texts == NULL) {
        return subject;
    }
    // A text can be made of another text that was written, such as the name of a copy of a
    // procedure that a copy calls, which is followed back to the source. A text is only made of the
    // ones that were written before it, so there are no more steps than texts.
    for (size_t step = 0; step <= texts->entry_count; step += 1) {
        if (subject.bytes == NULL || subject.bytes < texts->bytes ||
            subject.bytes >= texts->bytes + texts->length) {
            return subject;
        }
        Str source = STR_EMPTY;
        for (size_t i = 0; i < texts->entry_count; i += 1) {
            Specialize_Text const *entry = &texts->entries[i];
            if (subject.bytes >= entry->text.bytes &&
                subject.bytes < entry->text.bytes + entry->text.length) {
                source = entry->source;
            }
        }
        if (source.bytes == NULL) {
            return subject;
        }
        subject = source;
    }
    return subject;
}

/**
 * Looks at every definition of the tree of the round that is not polymorphic, and notes what the
 * copy of it in the next tree has that is not the same: the calls of polymorphic procedures, whose
 * types are deduced, and which call the copy of the procedure for those types, and the types that
 * name a polymorphic struct with type arguments, which are made canonical, so that the same types
 * are one text, which is the name of the copy of the struct. Each copy that is needed is requested.
 * A polymorphic definition is only checked, because it is no code: the copies of it are. Returns
 * false after it recorded an error.
 */
static bool specialize_round_scan(Specializer *specializer) {
    Ast_Node const *nodes = specializer->nodes;
    Ast_Node_Index definition = nodes[specializer->root].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t step = 0; step < specializer->node_count && definition != AST_NODE_INDEX_NONE;
         step += 1) {
        Ast_Node_Index end = specialize_definition_end(specializer, definition);
        if (ast_definition_is_polymorphic(nodes, definition)) {
            Specialize_Substitution parameters = {0};
            if (not specialize_parameters_collect(specializer, definition, &parameters)) {
                return false;
            }
            definition = nodes[definition].next_sibling;
            continue;
        }

        for (Ast_Node_Index node = definition; node < end; node += 1) {
            Ast_Node const *current = &nodes[node];
            Str text = STR_EMPTY;
            switch (current->node_type) {
            case AST_NODE_TYPE_CALL:
                if (not specialize_call_scan(specializer, node, definition)) {
                    return false;
                }
                break;
            case AST_NODE_TYPE_IDENTIFIER: {
                Semantic_Binding binding = specializer->bindings[node];
                if (binding.kind == SEMANTIC_BINDING_KIND_CONSTANT &&
                    ast_definition_is_polymorphic(nodes, binding.target)) {
                    return specialize_fail(specializer, SPECIALIZE_STATUS_PROCEDURE_NOT_CALLED,
                                           current->payload.identifier.name);
                }
                break;
            }
            case AST_NODE_TYPE_TYPE_NAME:
                text = current->payload.type_name.name;
                break;
            case AST_NODE_TYPE_STRUCT_LITERAL:
                text = current->payload.struct_literal.name;
                break;
            case AST_NODE_TYPE_CAST:
                text = current->payload.cast.type_name;
                break;
            case AST_NODE_TYPE_PROCEDURE:
            case AST_NODE_TYPE_PROCEDURE_TYPE:
                text = current->payload.procedure.return_type;
                break;
            default:
                break;
            }
            if (text.bytes == NULL) {
                continue;
            }
            // A type is made canonical, and the copies of the structs that it names are requested.
            // The children of a type name are its type arguments, which its text has, so the copy
            // has none.
            Str canonical = STR_EMPTY;
            if (not specialize_type_text_process(specializer, text, &canonical)) {
                return false;
            }
            bool has_arguments = current->node_type == AST_NODE_TYPE_TYPE_NAME &&
                                 current->first_child != AST_NODE_INDEX_NONE;
            if (canonical.bytes != text.bytes || has_arguments) {
                specializer->rewrites[node] = canonical;
                specializer->change_count += 1;
            }
            if (has_arguments) {
                for (Ast_Node_Index child = current->first_child; child != AST_NODE_INDEX_NONE;
                     child = nodes[child].next_sibling) {
                    // A type is its node and the nodes after it up to its last descendant, which
                    // is the last child of the last child, and so on.
                    Ast_Node_Index last = child;
                    for (size_t depth = 0; depth < specializer->node_count &&
                                           nodes[last].last_child != AST_NODE_INDEX_NONE;
                         depth += 1) {
                        last = nodes[last].last_child;
                    }
                    for (Ast_Node_Index dropped = child; dropped <= last; dropped += 1) {
                        specializer->marks[dropped] = specializer->round_generation;
                    }
                }
            }
        }
        definition = nodes[definition].next_sibling;
    }
    return true;
}

/**
 * Looks at `call` of `definition`, which is not polymorphic: a call of a polymorphic procedure has
 * its types deduced, the copy for them is requested, and the call is noted to call it, without the
 * arguments that are types when it gives them. A call whose types are not known yet is counted, to
 * be looked at in the next round. Returns false after it recorded an error.
 */
static bool specialize_call_scan(Specializer *specializer, Ast_Node_Index call,
                                 Ast_Node_Index definition) {
    (void)definition;
    Ast_Node const *nodes = specializer->nodes;
    Semantic_Binding binding = specializer->bindings[call];
    if (binding.kind != SEMANTIC_BINDING_KIND_CONSTANT ||
        not ast_definition_is_polymorphic(nodes, binding.target)) {
        return true;
    }

    Specialize_Substitution deduced = {0};
    bool is_explicit = false;
    Specialize_Deduction deduction =
        specialize_call_deduce(specializer, call, binding.target, &deduced, &is_explicit);
    if (deduction == SPECIALIZE_DEDUCTION_FAILED) {
        return false;
    }
    if (deduction == SPECIALIZE_DEDUCTION_PENDING) {
        if (specializer->pending_count == 0) {
            specializer->pending_call = call;
        }
        specializer->pending_count += 1;
        return true;
    }

    // The types as one text, in the order of the type parameters.
    Specialize_Type_Text arguments = {.length = 0, .is_overflowed = false};
    for (size_t i = 0; i < deduced.count; i += 1) {
        if (i > 0) {
            specialize_text_append(&arguments, STR(", "));
        }
        specialize_text_append(&arguments, deduced.arguments[i]);
    }
    if (arguments.is_overflowed) {
        specializer->number = SPECIALIZE_TYPE_TEXT_SIZE_BYTES;
        return specialize_fail(specializer, SPECIALIZE_STATUS_LIMIT, nodes[call].payload.call.name);
    }
    Str name = STR_EMPTY;
    if (not specialize_request(specializer, binding.target, specialize_text_str(&arguments),
                               nodes[call].payload.call.name, &name)) {
        return false;
    }
    Str stored = specialize_store(specializer, name, nodes[call].payload.call.name);
    if (stored.bytes == NULL) {
        return false;
    }
    specializer->rewrites[call] = stored;
    specializer->change_count += 1;

    // The arguments that are types are left out of the copy, because the copy of the procedure has
    // no type parameters. They are types, whose nodes are each one after the other.
    if (is_explicit) {
        Ast_Node_Index procedure = nodes[binding.target].first_child;
        Ast_Node_Index parameter = nodes[procedure].first_child;
        for (Ast_Node_Index argument = nodes[call].first_child; argument != AST_NODE_INDEX_NONE;
             argument = nodes[argument].next_sibling) {
            if (ast_parameter_is_type(nodes, parameter)) {
                Ast_Node_Index last = argument;
                for (size_t depth = 0; depth < specializer->node_count &&
                                       nodes[last].last_child != AST_NODE_INDEX_NONE;
                     depth += 1) {
                    last = nodes[last].last_child;
                }
                for (Ast_Node_Index dropped = argument; dropped <= last; dropped += 1) {
                    specializer->marks[dropped] = specializer->round_generation;
                }
            }
            parameter = nodes[parameter].next_sibling;
        }
    }
    return true;
}

/**
 * Collects the type parameters of the polymorphic definition `definition` in `substitution`, in the
 * order that they are declared: the parameters `$T: type_id`, and each name with `$` before it in
 * the type of a parameter, of a procedure, and the type parameters of a struct. Returns false after
 * it recorded an error, which is a type parameter that is declared twice, or more of them than
 * there is room for.
 */
static bool specialize_parameters_collect(Specializer *specializer, Ast_Node_Index definition,
                                          Specialize_Substitution *substitution) {
    Ast_Node const *nodes = specializer->nodes;
    assert(ast_definition_is_polymorphic(nodes, definition));

    substitution->count = 0;
    Ast_Node_Index value = nodes[definition].first_child;
    Ast_Node_Index parameter = nodes[value].first_child;
    if (nodes[value].node_type == AST_NODE_TYPE_STRUCT_TYPE) {
        parameter = nodes[value].next_sibling;
    }
    for (; parameter != AST_NODE_INDEX_NONE &&
           nodes[parameter].node_type == AST_NODE_TYPE_PARAMETER;
         parameter = nodes[parameter].next_sibling) {
        // The names that the parameter declares: its own, or the ones with `$` in its type.
        Str declared[SPECIALIZE_TYPE_PARAMETER_MAX];
        size_t declared_count = 0;
        if (ast_parameter_is_type(nodes, parameter)) {
            declared[0] = nodes[parameter].payload.parameter.name;
            declared_count = 1;
        } else {
            // The type is carets and brackets down to a name, whose text has the names with `$`,
            // also in its type arguments, and which views the source, so the names do too.
            Ast_Node_Index name_node = nodes[parameter].first_child;
            for (size_t depth = 0;
                 depth < AST_BLOCK_DEPTH_MAX && name_node != AST_NODE_INDEX_NONE &&
                 (nodes[name_node].node_type == AST_NODE_TYPE_POINTER_TYPE ||
                  nodes[name_node].node_type == AST_NODE_TYPE_ARRAY_TYPE);
                 depth += 1) {
                name_node = nodes[name_node].first_child;
            }
            Specialize_Type_Tokens tokens = {.count = 0, .is_valid = false};
            if (name_node != AST_NODE_INDEX_NONE &&
                nodes[name_node].node_type == AST_NODE_TYPE_TYPE_NAME) {
                tokens = specialize_type_tokenize(nodes[name_node].payload.type_name.name);
            }
            for (size_t i = 0; tokens.is_valid && i < tokens.count; i += 1) {
                Str word = tokens.tokens[i].text;
                if (tokens.tokens[i].kind != 'w' || word.bytes[0] != '$') {
                    continue;
                }
                if (declared_count == SPECIALIZE_TYPE_PARAMETER_MAX) {
                    specializer->number = SPECIALIZE_TYPE_PARAMETER_MAX;
                    return specialize_fail(specializer, SPECIALIZE_STATUS_LIMIT, word);
                }
                declared[declared_count] = str_make(word.bytes + 1, word.length - 1);
                declared_count += 1;
            }
        }
        for (size_t i = 0; i < declared_count; i += 1) {
            for (size_t j = 0; j < substitution->count; j += 1) {
                if (str_equals(substitution->names[j], declared[i])) {
                    return specialize_fail(specializer,
                                           SPECIALIZE_STATUS_TYPE_PARAMETER_DUPLICATE,
                                           declared[i]);
                }
            }
            if (substitution->count == SPECIALIZE_TYPE_PARAMETER_MAX) {
                specializer->number = SPECIALIZE_TYPE_PARAMETER_MAX;
                return specialize_fail(specializer, SPECIALIZE_STATUS_LIMIT, declared[i]);
            }
            substitution->names[substitution->count] = declared[i];
            substitution->arguments[substitution->count] = STR_EMPTY;
            substitution->count += 1;
        }
    }
    return true;
}

/**
 * Deduces the types that `call` gives the type parameters of the polymorphic procedure that
 * `definition` defines, in `deduced`, which gets the names of the type parameters and their types,
 * as canonical texts. A call that gives every parameter, the ones that are types too, has
 * `*is_explicit` set, and those types are its arguments. The types of the other arguments are
 * matched with the types of their parameters, first the ones whose type is known, and then the
 * untyped constants, which only say the type of a type parameter that nothing else said, because
 * they can become several. Returns what came of it: an error is recorded when it failed.
 */
static Specialize_Deduction specialize_call_deduce(Specializer *specializer, Ast_Node_Index call,
                                                   Ast_Node_Index definition,
                                                   Specialize_Substitution *deduced,
                                                   bool *is_explicit) {
    Ast_Node const *nodes = specializer->nodes;
    Str name = nodes[call].payload.call.name;
    Ast_Node_Index procedure = nodes[definition].first_child;
    assert(nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);

    if (not specialize_parameters_collect(specializer, definition, deduced)) {
        return SPECIALIZE_DEDUCTION_FAILED;
    }

    size_t parameter_count = 0;
    size_t value_count = 0;
    for (Ast_Node_Index parameter = nodes[procedure].first_child;
         parameter != AST_NODE_INDEX_NONE &&
         nodes[parameter].node_type == AST_NODE_TYPE_PARAMETER;
         parameter = nodes[parameter].next_sibling) {
        parameter_count += 1;
        if (not ast_parameter_is_type(nodes, parameter)) {
            value_count += 1;
        }
    }
    size_t argument_count = 0;
    for (Ast_Node_Index argument = nodes[call].first_child; argument != AST_NODE_INDEX_NONE;
         argument = nodes[argument].next_sibling) {
        argument_count += 1;
    }
    *is_explicit = parameter_count != value_count && argument_count == parameter_count;
    if (not *is_explicit && argument_count != value_count) {
        (void)specialize_fail(specializer, SPECIALIZE_STATUS_ARGUMENT_COUNT_MISMATCH, name);
        return SPECIALIZE_DEDUCTION_FAILED;
    }

    // The two passes: the arguments whose type is known, and the untyped constants.
    bool is_pending = false;
    for (int pass = 0; pass < 2; pass += 1) {
        bool is_untyped_pass = pass == 1;
        Ast_Node_Index parameter = nodes[procedure].first_child;
        Ast_Node_Index argument = nodes[call].first_child;
        for (size_t i = 0; i < parameter_count; i += 1) {
            assert(parameter != AST_NODE_INDEX_NONE);
            bool is_type = ast_parameter_is_type(nodes, parameter);
            if (is_type && not *is_explicit) {
                parameter = nodes[parameter].next_sibling;
                continue;
            }
            assert(argument != AST_NODE_INDEX_NONE);
            Constant_Value constant = specializer->constants[argument];
            bool is_untyped = constant.status == CONSTANT_STATUS_OK &&
                              (constant.type == CONSTANT_TYPE_UNTYPED_BOOL ||
                               constant.type == CONSTANT_TYPE_UNTYPED_FLOAT ||
                               constant.type == CONSTANT_TYPE_UNTYPED_INTEGER ||
                               constant.type == CONSTANT_TYPE_UNTYPED_STRING);
            if (is_type && not is_untyped_pass) {
                // The type that is given is the argument itself.
                Specialize_Type_Text given = {.length = 0, .is_overflowed = false};
                if (not specialize_type_node_text(nodes, argument, &given)) {
                    (void)specialize_fail(specializer, SPECIALIZE_STATUS_ARGUMENT_TYPE_MISMATCH,
                                          name);
                    return SPECIALIZE_DEDUCTION_FAILED;
                }
                bool is_matched = false;
                Str pattern = nodes[parameter].payload.parameter.name;
                if (not specialize_type_match(specializer, pattern, specialize_text_str(&given),
                                              deduced, false, call, &is_matched)) {
                    return SPECIALIZE_DEDUCTION_FAILED;
                }
                assert(is_matched);
            } else if (not is_type && is_untyped == is_untyped_pass) {
                // Only the type of a parameter that has a type parameter in it says anything about
                // the types, and the type of any other argument is checked by the target, which
                // also knows what an untyped constant becomes there.
                Specialize_Type_Text pattern = {.length = 0, .is_overflowed = false};
                bool has_pattern = specialize_type_node_text(nodes, nodes[parameter].first_child,
                                                             &pattern) &&
                                   specialize_has_parameter(specialize_text_str(&pattern),
                                                            deduced);
                Specialize_Type_Text actual = {.length = 0, .is_overflowed = false};
                if (has_pattern && not specialize_expression_type(specializer, argument, 0,
                                                                  &actual)) {
                    is_pending = true;
                } else if (has_pattern) {
                    bool is_matched = false;
                    if (not specialize_type_match(specializer, specialize_text_str(&pattern),
                                                  specialize_text_str(&actual), deduced,
                                                  is_untyped, call, &is_matched)) {
                        return SPECIALIZE_DEDUCTION_FAILED;
                    }
                    if (not is_matched) {
                        (void)specialize_fail(specializer,
                                              SPECIALIZE_STATUS_ARGUMENT_TYPE_MISMATCH, name);
                        return SPECIALIZE_DEDUCTION_FAILED;
                    }
                }
            }
            argument = nodes[argument].next_sibling;
            parameter = nodes[parameter].next_sibling;
        }
    }

    for (size_t i = 0; i < deduced->count; i += 1) {
        if (deduced->arguments[i].bytes == NULL) {
            if (is_pending) {
                if (specializer->pending_count == 0) {
                    specializer->pending_parameter = deduced->names[i];
                }
                return SPECIALIZE_DEDUCTION_PENDING;
            }
            specializer->detail = deduced->names[i];
            (void)specialize_fail(specializer, SPECIALIZE_STATUS_TYPE_PARAMETER_UNDEDUCED, name);
            return SPECIALIZE_DEDUCTION_FAILED;
        }
    }
    return SPECIALIZE_DEDUCTION_DONE;
}

/**
 * Matches the type `actual` with `pattern`, the type of a parameter, in which a name that is a type
 * parameter of `deduced`, with or without `$`, stands for any one type, and gives the type
 * parameter that type in `deduced`. Everything else must be the same. `*is_matched` says whether
 * the types match. A type parameter that has a type already must be given the same, or the call
 * gives it two, which is an error, except for `is_untyped`, an untyped constant, which only gives a
 * type to a type parameter that has none. Returns false after it recorded an error.
 */
static bool specialize_type_match(Specializer *specializer, Str pattern, Str actual,
                                  Specialize_Substitution *deduced, bool is_untyped,
                                  Ast_Node_Index call, bool *is_matched) {
    Specialize_Type_Tokens const pattern_tokens = specialize_type_tokenize(pattern);
    Specialize_Type_Tokens const actual_tokens = specialize_type_tokenize(actual);
    *is_matched = false;
    if (not pattern_tokens.is_valid || not actual_tokens.is_valid) {
        return true;
    }

    size_t j = 0;
    for (size_t i = 0; i < pattern_tokens.count; i += 1) {
        Specialize_Type_Token token = pattern_tokens.tokens[i];
        size_t parameter = deduced->count;
        if (token.kind == 'w') {
            Str word = token.text;
            if (word.bytes[0] == '$') {
                word = str_make(word.bytes + 1, word.length - 1);
            }
            for (size_t k = 0; k < deduced->count; k += 1) {
                if (str_equals(deduced->names[k], word)) {
                    parameter = k;
                }
            }
        }
        if (parameter < deduced->count) {
            // The type parameter is as much of the actual type as one type is.
            size_t end = specialize_type_extent(&actual_tokens, j);
            if (end == 0) {
                return true;
            }
            Specialize_Type_Text canonical = specialize_canonical(&actual_tokens, j, end);
            Str given = specialize_text_str(&canonical);
            Str *bound = &deduced->arguments[parameter];
            if (bound->bytes == NULL) {
                *bound = specialize_store(specializer, given, STR_EMPTY);
                if (bound->bytes == NULL) {
                    return false;
                }
            } else if (not str_equals(*bound, given) && not is_untyped) {
                specializer->detail = deduced->names[parameter];
                specializer->other = *bound;
                specializer->target_text = specialize_store(specializer, given, STR_EMPTY);
                return specialize_fail(specializer, SPECIALIZE_STATUS_TYPE_PARAMETER_CONFLICT,
                                       specializer->nodes[call].payload.call.name);
            }
            j = end;
            continue;
        }
        // An array, and a dynamic array, is given where an array slice is wanted, as a slice of all
        // of its elements, so `[]` matches brackets with a length between them.
        bool is_slice_of_array = token.kind == '[' && i + 1 < pattern_tokens.count &&
                                 pattern_tokens.tokens[i + 1].kind == ']' &&
                                 j + 2 < actual_tokens.count &&
                                 actual_tokens.tokens[j].kind == '[' &&
                                 actual_tokens.tokens[j + 1].kind == 'w' &&
                                 actual_tokens.tokens[j + 2].kind == ']';
        if (is_slice_of_array) {
            i += 1;
            j += 3;
            continue;
        }
        if (j >= actual_tokens.count || actual_tokens.tokens[j].kind != token.kind ||
            (token.kind == 'w' && not str_equals(actual_tokens.tokens[j].text, token.text))) {
            return true;
        }
        j += 1;
    }
    *is_matched = j == actual_tokens.count;
    return true;
}

/**
 * Returns whether the text of a type, `text`, has a word that is a type parameter of
 * `substitution`, with or without `$`.
 */
static bool specialize_has_parameter(Str text, Specialize_Substitution const *substitution) {
    Specialize_Type_Tokens const tokens = specialize_type_tokenize(text);
    for (size_t i = 0; tokens.is_valid && i < tokens.count; i += 1) {
        Str word = tokens.tokens[i].text;
        if (tokens.tokens[i].kind != 'w') {
            continue;
        }
        if (word.bytes[0] == '$') {
            word = str_make(word.bytes + 1, word.length - 1);
        }
        for (size_t k = 0; k < substitution->count; k += 1) {
            if (str_equals(substitution->names[k], word)) {
                return true;
            }
        }
    }
    return false;
}

/**
 * Returns the index after the last token of the type that starts at the token `start` of
 * `tokens`: the carets and brackets before it, its name, and the type arguments after the name
 * with their parentheses. Returns 0 when no type starts there.
 */
static size_t specialize_type_extent(Specialize_Type_Tokens const *tokens, size_t start) {
    size_t index = start;
    // Every step consumes a token, so there are fewer steps than tokens.
    for (size_t step = 0; step < SPECIALIZE_TYPE_TOKEN_MAX && index < tokens->count; step += 1) {
        char kind = tokens->tokens[index].kind;
        if (kind == '^') {
            index += 1;
        } else if (kind == '[') {
            index += 1;
            if (index < tokens->count && tokens->tokens[index].kind == 'w') {
                index += 1;
            }
            if (index >= tokens->count || tokens->tokens[index].kind != ']') {
                return 0;
            }
            index += 1;
        } else {
            break;
        }
    }
    if (index >= tokens->count || tokens->tokens[index].kind != 'w') {
        return 0;
    }
    index += 1;
    if (index < tokens->count && tokens->tokens[index].kind == '(') {
        size_t depth = 0;
        for (; index < tokens->count; index += 1) {
            if (tokens->tokens[index].kind == '(') {
                depth += 1;
            } else if (tokens->tokens[index].kind == ')') {
                depth -= 1;
                if (depth == 0) {
                    return index + 1;
                }
            }
        }
        return 0;
    }
    return index;
}

/**
 * Makes the text of a type, `text`, canonical, so that two texts of one type are the same text:
 * no spaces, except one after each comma. Every word that is followed by type arguments must be a
 * polymorphic struct with as many type parameters, and the copy of it for the canonical types is
 * requested, and every other word must not be a polymorphic struct, which has no type without its
 * type arguments. A name with `$` before it declares a type parameter, which is only done in the
 * type of a parameter of a procedure, and a polymorphic definition is not read here. Sets
 * `*canonical` to the canonical text, which is `text` when it is canonical already, and otherwise a
 * text of the specialization. Returns false after it recorded an error.
 */
static bool specialize_type_text_process(Specializer *specializer, Str text, Str *canonical) {
    Specialize_Type_Tokens const tokens = specialize_type_tokenize(text);
    *canonical = text;
    // A text that is no type, such as the word `proc` of a procedure type that returns nothing,
    // is left to what reads it.
    if (not tokens.is_valid) {
        return true;
    }
    // Only a text that has type arguments is made canonical, so that a program whose types have
    // none is the tree that it is, with every text a view of its source.
    bool has_instantiation = false;
    for (size_t i = 0; i < tokens.count; i += 1) {
        Specialize_Type_Token token = tokens.tokens[i];
        if (token.kind != 'w') {
            continue;
        }
        if (token.text.bytes[0] == '$') {
            return specialize_fail(specializer, SPECIALIZE_STATUS_TYPE_PARAMETER_MISPLACED,
                                   token.text);
        }
        bool has_arguments = i + 1 < tokens.count && tokens.tokens[i + 1].kind == '(';
        has_instantiation = has_instantiation || has_arguments;
        Ast_Node_Index definition = specialize_definition_find(specializer, token.text);
        bool is_polymorphic_struct =
            definition != AST_NODE_INDEX_NONE &&
            ast_definition_is_polymorphic(specializer->nodes, definition) &&
            specializer->nodes[specializer->nodes[definition].first_child].node_type ==
                AST_NODE_TYPE_STRUCT_TYPE;
        if (not has_arguments && not is_polymorphic_struct) {
            continue;
        }
        // The text from the name to the `)` of its arguments is the subject of an error about it.
        size_t end = specialize_type_extent(&tokens, i);
        Str subject = token.text;
        if (end > i) {
            Str last = tokens.tokens[end - 1].text;
            subject = str_make(token.text.bytes,
                               (size_t)(last.bytes + last.length - token.text.bytes));
        }
        if (has_arguments && not is_polymorphic_struct) {
            return specialize_fail(specializer, SPECIALIZE_STATUS_STRUCT_NOT_POLYMORPHIC,
                                   subject);
        }
        Specialize_Substitution parameters = {0};
        if (not specialize_parameters_collect(specializer, definition, &parameters)) {
            return false;
        }
        // The arguments are the types between the parentheses, which are counted at the depth of
        // the parentheses of the name.
        size_t argument_count = 0;
        if (has_arguments && end > i + 2) {
            argument_count = 1;
            size_t depth = 0;
            for (size_t k = i + 1; k < end; k += 1) {
                char kind = tokens.tokens[k].kind;
                if (kind == '(') {
                    depth += 1;
                } else if (kind == ')') {
                    depth -= 1;
                } else if (kind == ',' && depth == 1) {
                    argument_count += 1;
                }
            }
        }
        if (argument_count != parameters.count) {
            specializer->number = (int64_t)parameters.count;
            return specialize_fail(specializer,
                                   SPECIALIZE_STATUS_STRUCT_TYPE_ARGUMENT_COUNT_MISMATCH, subject);
        }
        // The arguments are between the parentheses after the name, up to the last token.
        Specialize_Type_Text arguments = specialize_canonical(&tokens, i + 2, end - 1);
        if (arguments.is_overflowed) {
            specializer->number = SPECIALIZE_TYPE_TEXT_SIZE_BYTES;
            return specialize_fail(specializer, SPECIALIZE_STATUS_LIMIT, subject);
        }
        Str name = STR_EMPTY;
        if (not specialize_request(specializer, definition, specialize_text_str(&arguments),
                                   subject, &name)) {
            return false;
        }
    }

    if (not has_instantiation) {
        return true;
    }
    Specialize_Type_Text text_canonical = specialize_canonical(&tokens, 0, tokens.count);
    if (text_canonical.is_overflowed) {
        specializer->number = SPECIALIZE_TYPE_TEXT_SIZE_BYTES;
        return specialize_fail(specializer, SPECIALIZE_STATUS_LIMIT, text);
    }
    Str made = specialize_text_str(&text_canonical);
    if (not str_equals(made, text)) {
        // It is stored with the text that it is made of, which an error about it points at.
        *canonical = specialize_store(specializer, made, text);
        if (canonical->bytes == NULL) {
            return false;
        }
    }
    return true;
}

/**
 * Requests the copy of the polymorphic definition `generic` of the tree of the round for the types
 * `arguments`, which is stored with a new request, and sets `*name` to the name of the copy. A copy that
 * was requested before, also in an earlier round, is the same copy. `source` is what an error about
 * it points at. Returns false after it recorded an error.
 */
static bool specialize_request(Specializer *specializer, Ast_Node_Index generic, Str arguments,
                               Str source, Str *name) {
    Ast_Node_Index origin = specialize_origin(specializer, generic);
    for (size_t i = 0; i < specializer->request_count; i += 1) {
        Specialize_Request const *request = &specializer->requests[i];
        if (request->generic == origin && str_equals(request->arguments, arguments)) {
            *name = request->name;
            return true;
        }
    }
    Str generic_name = ast_definition_name(&specializer->nodes[generic]);
    if (specializer->request_count == SPECIALIZE_REQUEST_MAX) {
        specializer->number = SPECIALIZE_REQUEST_MAX;
        return specialize_fail(specializer, SPECIALIZE_STATUS_LIMIT, generic_name);
    }
    Specialize_Type_Text text = {.length = 0, .is_overflowed = false};
    specialize_text_append(&text, generic_name);
    specialize_text_append(&text, STR("("));
    specialize_text_append(&text, arguments);
    specialize_text_append(&text, STR(")"));
    if (text.is_overflowed) {
        specializer->number = SPECIALIZE_TYPE_TEXT_SIZE_BYTES;
        return specialize_fail(specializer, SPECIALIZE_STATUS_LIMIT, generic_name);
    }
    Str stored = specialize_store(specializer, specialize_text_str(&text), source);
    if (stored.bytes == NULL) {
        return false;
    }
    // The types are the part of the name between the parentheses, which is stored.
    Str stored_arguments = str_make(stored.bytes + generic_name.length + 1, arguments.length);
    specializer->requests[specializer->request_count] = (Specialize_Request){
        .arguments = stored_arguments,
        .name = stored,
        .generic = origin,
        .is_emitted = false,
    };
    specializer->request_count += 1;
    specializer->change_count += 1;
    *name = stored;
    return true;
}

/**
 * Makes the next tree of the tree of the round: the definitions in the order of the module, each
 * copied with what the round noted, see `specialize_round_scan`, and after each polymorphic
 * definition, the copies of it that were requested and are not in the tree yet. The module is
 * between the definitions of the imported files and those of the program, as it is in every tree,
 * so that the first node of the program is the module. With `is_final` the polymorphic definitions
 * are left out instead, because nothing uses them. Returns false after it recorded an error.
 */
static bool specialize_copy_tree(Specializer *specializer, bool is_final) {
    Ast_Node const *nodes = specializer->nodes;
    specializer->target_count = 0;

    Ast_Node_Index definitions[2] = {AST_NODE_INDEX_NONE, AST_NODE_INDEX_NONE};
    Ast_Node_Index target_root = AST_NODE_INDEX_NONE;
    Ast_Node_Index definition = nodes[specializer->root].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes, and the
    // module is copied once, at the first definition of the program or after the last one.
    for (size_t step = 0; step <= specializer->node_count; step += 1) {
        if (target_root == AST_NODE_INDEX_NONE &&
            (definition == AST_NODE_INDEX_NONE || definition >= specializer->imported_end)) {
            if (not specialize_copy_range(specializer, specializer->root, specializer->root + 1,
                                          NULL, &target_root)) {
                return false;
            }
            specializer->target[target_root].first_child = AST_NODE_INDEX_NONE;
            specializer->target[target_root].last_child = AST_NODE_INDEX_NONE;
        }
        if (definition == AST_NODE_INDEX_NONE) {
            break;
        }

        bool is_polymorphic = ast_definition_is_polymorphic(nodes, definition);
        Ast_Node_Index end = specialize_definition_end(specializer, definition);
        size_t copy_count = 0;
        Ast_Node_Index copies[SPECIALIZE_REQUEST_MAX + 1];
        if (not (is_final && is_polymorphic)) {
            if (not specialize_copy_range(specializer, definition, end, NULL,
                                          &copies[copy_count])) {
                return false;
            }
            copy_count += 1;
        }
        if (is_polymorphic && not is_final) {
            Ast_Node_Index origin = specialize_origin(specializer, definition);
            for (size_t i = 0; i < specializer->request_count; i += 1) {
                Specialize_Request *request = &specializer->requests[i];
                if (request->generic != origin || request->is_emitted) {
                    continue;
                }
                Specialize_Substitution substitution = {0};
                if (not specialize_parameters_collect(specializer, definition, &substitution)) {
                    return false;
                }
                // The types are the parts of the text of the arguments between the commas that
                // are not in parentheses, which are canonical, so they have one space after each.
                Specialize_Type_Tokens const tokens = specialize_type_tokenize(request->arguments);
                assert(tokens.is_valid);
                size_t argument = 0;
                size_t start = 0;
                // Every type consumes a token and the comma after it, so there are fewer types
                // than tokens.
                for (size_t step = 0; step < tokens.count && start < tokens.count &&
                                      argument < substitution.count;
                     step += 1) {
                    size_t extent = specialize_type_extent(&tokens, start);
                    assert(extent > start);
                    Str first = tokens.tokens[start].text;
                    Str last = tokens.tokens[extent - 1].text;
                    substitution.arguments[argument] = str_make(
                        first.bytes, (size_t)(last.bytes + last.length - first.bytes));
                    argument += 1;
                    start = extent + 1;
                }
                assert(argument == substitution.count);
                substitution.name = request->name;
                if (not specialize_copy_range(specializer, definition, end, &substitution,
                                              &copies[copy_count])) {
                    return false;
                }
                copy_count += 1;
                request->is_emitted = true;
            }
        }
        // The copies are linked as the children of the module, in the order that they are made.
        for (size_t i = 0; i < copy_count; i += 1) {
            if (definitions[0] == AST_NODE_INDEX_NONE) {
                definitions[0] = copies[i];
            } else {
                specializer->target[definitions[1]].next_sibling = copies[i];
            }
            definitions[1] = copies[i];
        }
        definition = nodes[definition].next_sibling;
    }

    assert(target_root != AST_NODE_INDEX_NONE);
    specializer->target[target_root].first_child = definitions[0];
    specializer->target[target_root].last_child = definitions[1];
    specializer->root = target_root;
    specializer->imported_end = target_root;
    return true;
}

/**
 * Copies the nodes from `start` up to `end` of the tree of the round, which are a definition or the
 * module, to the end of the next tree, in the same order, and sets `*copy` to the copy of the
 * first. The nodes that are marked as left out are not copied, see `Specializer.marks`, and a node
 * that has a text of the round has it in its copy instead, see `Specializer.rewrites`. The links of
 * the copies are made again, to the copies of the nodes that they were links to, and past the nodes
 * that are left out, and the end of a block is the end of the copy of its last node. The link of
 * the first node to the node after it is not, because the caller links the definitions.
 *
 * With `substitution` the copy is the copy of a polymorphic definition: it has the name of the copy,
 * no type parameters, and every type parameter in it is the type that the copy gives it: a type
 * name that is one is replaced by the nodes of the type, and a text that has one has the type in
 * its place, see `specialize_substitute`. Its nodes have no origin, since none of the tree that was
 * given is them. Returns false after it recorded an error.
 */
static bool specialize_copy_range(Specializer *specializer, Ast_Node_Index start,
                                  Ast_Node_Index end, Specialize_Substitution const *substitution,
                                  Ast_Node_Index *copy) {
    Ast_Node const *nodes = specializer->nodes;
    assert(start < end);
    assert(end <= specializer->node_count);

    uint32_t generation = specializer->round_generation;
    if (substitution != NULL) {
        specializer->generation += 1;
        generation = specializer->generation;
        // The type parameters are left out, and so are the type arguments of the type names, whose
        // texts have them. Each is a parameter or a type, whose nodes are after it up to its last
        // descendant.
        for (Ast_Node_Index node = start; node < end; node += 1) {
            bool is_type_parameter = nodes[node].node_type == AST_NODE_TYPE_PARAMETER &&
                                     ast_parameter_is_type(nodes, node);
            bool has_arguments = nodes[node].node_type == AST_NODE_TYPE_TYPE_NAME &&
                                 nodes[node].first_child != AST_NODE_INDEX_NONE;
            if (not is_type_parameter && not has_arguments) {
                continue;
            }
            Ast_Node_Index first = node;
            if (has_arguments) {
                first = nodes[node].first_child;
            }
            Ast_Node_Index last = node;
            for (size_t depth = 0; depth < specializer->node_count &&
                                   nodes[last].last_child != AST_NODE_INDEX_NONE;
                 depth += 1) {
                last = nodes[last].last_child;
            }
            for (Ast_Node_Index dropped = first; dropped <= last; dropped += 1) {
                specializer->marks[dropped] = generation;
            }
        }
    }

    for (Ast_Node_Index node = start; node < end; node += 1) {
        // A node that is left out has no copy, and its text of the round, if it has one, must not
        // be taken by a node of the next tree that has its index.
        if (specializer->marks[node] == generation) {
            specializer->positions[node] = AST_NODE_INDEX_NONE;
            specializer->ends[node] = (Ast_Node_Index)specializer->target_count;
            specializer->rewrites[node] = STR_EMPTY;
            continue;
        }
        Ast_Node const *source = &nodes[node];

        // A type name that is a type parameter, `T` or `$T`, is replaced by the nodes of its type.
        if (substitution != NULL && source->node_type == AST_NODE_TYPE_TYPE_NAME) {
            Str name = source->payload.type_name.name;
            if (name.length > 0 && name.bytes[0] == '$') {
                name = str_make(name.bytes + 1, name.length - 1);
            }
            size_t parameter = substitution->count;
            for (size_t i = 0; i < substitution->count; i += 1) {
                if (str_equals(substitution->names[i], name)) {
                    parameter = i;
                }
            }
            if (parameter < substitution->count) {
                Ast_Node_Index root = AST_NODE_INDEX_NONE;
                if (not specialize_type_nodes_emit(specializer, substitution->arguments[parameter],
                                                   source->source_offset, &root)) {
                    return false;
                }
                specializer->positions[node] = root;
                specializer->ends[node] = (Ast_Node_Index)specializer->target_count;
                specializer->marks[node] = generation | SPECIALIZE_MARK_REPLACED;
                continue;
            }
        }

        if (specializer->target_count == specializer->target_max) {
            specializer->number = (int64_t)specializer->target_max;
            return specialize_fail(specializer, SPECIALIZE_STATUS_LIMIT,
                                   ast_definition_name(&nodes[start]));
        }
        Ast_Node_Index target = (Ast_Node_Index)specializer->target_count;
        specializer->target[target] = *source;
        specializer->target_origins[target] = AST_NODE_INDEX_NONE;
        if (substitution == NULL) {
            specializer->target_origins[target] = specialize_origin(specializer, node);
        }
        specializer->target_count += 1;
        specializer->positions[node] = target;
        specializer->ends[node] = (Ast_Node_Index)specializer->target_count;
        Ast_Node *copied = &specializer->target[target];

        // The texts of the round, or of the substitution.
        Str rewrite = specializer->rewrites[node];
        if (substitution == NULL && rewrite.bytes != NULL) {
            specializer->rewrites[node] = STR_EMPTY;
        } else {
            rewrite = STR_EMPTY;
        }
        bool is_failed = false;
        switch (source->node_type) {
        case AST_NODE_TYPE_CALL:
            if (rewrite.bytes != NULL) {
                copied->payload.call.name = rewrite;
            }
            break;
        case AST_NODE_TYPE_CAST:
            if (substitution != NULL) {
                rewrite = specialize_substitute(specializer, source->payload.cast.type_name,
                                                substitution, &is_failed);
            }
            if (rewrite.bytes != NULL) {
                copied->payload.cast.type_name = rewrite;
            }
            break;
        case AST_NODE_TYPE_CONSTANT_DEFINITION:
            if (substitution != NULL && node == start) {
                copied->payload.constant_definition.name = substitution->name;
            }
            break;
        case AST_NODE_TYPE_PROCEDURE:
        case AST_NODE_TYPE_PROCEDURE_TYPE:
            if (substitution != NULL) {
                rewrite = specialize_substitute(specializer, source->payload.procedure.return_type,
                                                substitution, &is_failed);
            }
            if (rewrite.bytes != NULL) {
                copied->payload.procedure.return_type = rewrite;
            }
            break;
        case AST_NODE_TYPE_STRUCT_LITERAL:
            if (substitution != NULL) {
                rewrite = specialize_substitute(specializer, source->payload.struct_literal.name,
                                                substitution, &is_failed);
            }
            if (rewrite.bytes != NULL) {
                copied->payload.struct_literal.name = rewrite;
            }
            break;
        case AST_NODE_TYPE_TYPE_NAME:
            if (substitution != NULL) {
                rewrite = specialize_substitute(specializer, source->payload.type_name.name,
                                                substitution, &is_failed);
            }
            if (rewrite.bytes != NULL) {
                copied->payload.type_name.name = rewrite;
            }
            break;
        default:
            break;
        }
        if (is_failed) {
            return false;
        }
    }

    // The links, past the nodes that are left out. A node that is replaced by the nodes of a type
    // only needs the link to its next sibling, which is the one of its type.
    for (Ast_Node_Index node = start; node < end; node += 1) {
        uint32_t mark = specializer->marks[node];
        if (mark == generation) {
            continue;
        }
        Ast_Node const *source = &nodes[node];
        Ast_Node_Index next = source->next_sibling;
        while (next != AST_NODE_INDEX_NONE && specializer->marks[next] == generation) {
            next = nodes[next].next_sibling;
        }
        Ast_Node_Index target = specializer->positions[node];
        if (next == AST_NODE_INDEX_NONE || node == start) {
            specializer->target[target].next_sibling = AST_NODE_INDEX_NONE;
        } else {
            specializer->target[target].next_sibling = specializer->positions[next];
        }
        if (mark == (generation | SPECIALIZE_MARK_REPLACED)) {
            continue;
        }
        Ast_Node_Index first = AST_NODE_INDEX_NONE;
        Ast_Node_Index last = AST_NODE_INDEX_NONE;
        for (Ast_Node_Index child = source->first_child; child != AST_NODE_INDEX_NONE;
             child = nodes[child].next_sibling) {
            if (specializer->marks[child] == generation) {
                continue;
            }
            if (first == AST_NODE_INDEX_NONE) {
                first = specializer->positions[child];
            }
            last = specializer->positions[child];
        }
        specializer->target[target].first_child = first;
        specializer->target[target].last_child = last;
        if (source->node_type == AST_NODE_TYPE_BLOCK) {
            Ast_Node_Index block_end = source->payload.block.end;
            assert(block_end > node);
            assert(block_end <= end);
            specializer->target[target].payload.block.end = specializer->ends[block_end - 1];
        }
    }

    *copy = specializer->positions[start];
    return true;
}

/**
 * Makes the nodes of the type `text`, canonical and of the texts of the specialization, at the end
 * of the next tree, as the parser makes the nodes of a type: a pointer type for each caret and an
 * array type for each pair of brackets, each with the rest as its child, and a type name at the
 * end, whose name is the rest of the text, with its type arguments, which it has no nodes for. Each
 * node has `source_offset`, the place of the type parameter that the type replaces. Sets `*root` to
 * the first node. Returns false after it recorded an error.
 */
static bool specialize_type_nodes_emit(Specializer *specializer, Str text, uint32_t source_offset,
                                       Ast_Node_Index *root) {
    Specialize_Type_Tokens const tokens = specialize_type_tokenize(text);
    assert(tokens.is_valid);
    assert(tokens.count > 0);

    *root = AST_NODE_INDEX_NONE;
    Ast_Node_Index parent = AST_NODE_INDEX_NONE;
    size_t index = 0;
    // Every node consumes at least a token, so there are fewer nodes than tokens.
    for (size_t step = 0; step <= tokens.count && index < tokens.count; step += 1) {
        if (specializer->target_count == specializer->target_max) {
            specializer->number = (int64_t)specializer->target_max;
            return specialize_fail(specializer, SPECIALIZE_STATUS_LIMIT, text);
        }
        Ast_Node_Index node = (Ast_Node_Index)specializer->target_count;
        Ast_Node *made = &specializer->target[node];
        *made = (Ast_Node){
            .node_type = AST_NODE_TYPE_TYPE_NAME,
            .first_child = AST_NODE_INDEX_NONE,
            .last_child = AST_NODE_INDEX_NONE,
            .next_sibling = AST_NODE_INDEX_NONE,
            .source_offset = source_offset,
            .result_count = 0,
            .result_index = 0,
        };
        specializer->target_origins[node] = AST_NODE_INDEX_NONE;
        specializer->target_count += 1;

        bool is_last = false;
        char kind = tokens.tokens[index].kind;
        if (kind == '^') {
            made->node_type = AST_NODE_TYPE_POINTER_TYPE;
            index += 1;
        } else if (kind == '[') {
            made->node_type = AST_NODE_TYPE_ARRAY_TYPE;
            made->payload.array_type.length = 0;
            made->payload.array_type.length_kind = AST_ARRAY_LENGTH_OMITTED;
            index += 1;
            if (tokens.tokens[index].kind == 'w') {
                Str length = tokens.tokens[index].text;
                if (str_equals(length, STR("dynamic"))) {
                    made->payload.array_type.length_kind = AST_ARRAY_LENGTH_DYNAMIC;
                } else if (str_equals(length, STR("const"))) {
                    made->payload.array_type.length_kind = AST_ARRAY_LENGTH_INFERRED;
                } else {
                    // A length is the digits of a number that the analysis worked out, which
                    // fits in an `Int`.
                    int64_t value = 0;
                    for (size_t i = 0; i < length.length && value < INT64_MAX / 10; i += 1) {
                        value = value * 10 + (length.bytes[i] - '0');
                    }
                    made->payload.array_type.length = value;
                    made->payload.array_type.length_kind = AST_ARRAY_LENGTH_FIXED;
                }
                index += 1;
            }
            index += 1;
        } else {
            // The name and everything after it, which is its type arguments.
            Str name = tokens.tokens[index].text;
            made->payload.type_name.name =
                str_make(name.bytes, (size_t)(text.bytes + text.length - name.bytes));
            is_last = true;
        }
        if (parent == AST_NODE_INDEX_NONE) {
            *root = node;
        } else {
            specializer->target[parent].first_child = node;
            specializer->target[parent].last_child = node;
        }
        parent = node;
        if (is_last) {
            break;
        }
    }
    assert(*root != AST_NODE_INDEX_NONE);
    return true;
}

/**
 * Returns `text`, the text of a type, with each word that is a type parameter of `substitution`,
 * with or without `$`, replaced by its type, as a canonical text of the specialization, or a `Str`
 * whose `bytes` is NULL when it has no type parameter, so that the text stays a view of the source.
 * Sets `*is_failed` after it recorded an error.
 */
static Str specialize_substitute(Specializer *specializer, Str text,
                                 Specialize_Substitution const *substitution, bool *is_failed) {
    *is_failed = false;
    if (text.bytes == NULL) {
        return STR_EMPTY;
    }
    Specialize_Type_Tokens const tokens = specialize_type_tokenize(text);
    if (not tokens.is_valid) {
        return STR_EMPTY;
    }
    bool has_parameter = false;
    Specialize_Type_Text made = {.length = 0, .is_overflowed = false};
    for (size_t i = 0; i < tokens.count; i += 1) {
        Specialize_Type_Token token = tokens.tokens[i];
        Str part = token.text;
        if (token.kind == 'w') {
            Str word = token.text;
            if (word.bytes[0] == '$') {
                word = str_make(word.bytes + 1, word.length - 1);
            }
            for (size_t k = 0; k < substitution->count; k += 1) {
                if (str_equals(substitution->names[k], word)) {
                    part = substitution->arguments[k];
                    has_parameter = true;
                }
            }
        }
        specialize_text_append(&made, part);
        if (token.kind == ',') {
            specialize_text_append(&made, STR(" "));
        }
    }
    if (not has_parameter) {
        return STR_EMPTY;
    }
    if (made.is_overflowed) {
        specializer->number = SPECIALIZE_TYPE_TEXT_SIZE_BYTES;
        *is_failed = true;
        (void)specialize_fail(specializer, SPECIALIZE_STATUS_LIMIT, text);
        return STR_EMPTY;
    }
    Str stored = specialize_store(specializer, specialize_text_str(&made), text);
    *is_failed = stored.bytes == NULL;
    return stored;
}

/**
 * Writes the type of the value of `expression` of the tree of the round to `text`, as the text that
 * the type is written with. Returns false when the type is not known, which is the case for an
 * expression whose type depends on what it is given, such as `nil`, and for the call of a
 * polymorphic procedure, until it is specialized. The type follows the names and the expressions
 * that it is made of, `depth` of them so far, which is bounded.
 */
static bool specialize_expression_type(Specializer const *specializer, Ast_Node_Index expression,
                                       size_t depth, Specialize_Type_Text *text) {
    Ast_Node const *nodes = specializer->nodes;
    assert(expression < specializer->node_count);

    if (depth >= SPECIALIZE_TYPE_DEPTH_MAX) {
        return false;
    }
    Ast_Node const *node = &nodes[expression];
    Constant_Value constant = specializer->constants[expression];
    if (constant.status == CONSTANT_STATUS_OK) {
        char const *type = NULL;
        switch (constant_type_default(constant.type)) {
        case CONSTANT_TYPE_BOOL:
            type = "Bool";
            break;
        case CONSTANT_TYPE_BYTE:
            type = "Byte";
            break;
        case CONSTANT_TYPE_F32:
            type = "F32";
            break;
        case CONSTANT_TYPE_F64:
            type = "F64";
            break;
        case CONSTANT_TYPE_I32:
            type = "I32";
            break;
        case CONSTANT_TYPE_I64:
            type = "I64";
            break;
        case CONSTANT_TYPE_INT:
            type = "Int";
            break;
        case CONSTANT_TYPE_RUNE:
            type = "Rune";
            break;
        case CONSTANT_TYPE_STR:
            type = "Str";
            break;
        case CONSTANT_TYPE_U32:
            type = "U32";
            break;
        case CONSTANT_TYPE_U64:
            type = "U64";
            break;
        case CONSTANT_TYPE_UNTYPED_BOOL:
        case CONSTANT_TYPE_UNTYPED_FLOAT:
        case CONSTANT_TYPE_UNTYPED_INTEGER:
        case CONSTANT_TYPE_UNTYPED_STRING:
            break;
        }
        if (type == NULL) {
            return false;
        }
        specialize_text_append(text, str_make(type, strlen(type)));
        return not text->is_overflowed;
    }

    switch (node->node_type) {
    case AST_NODE_TYPE_ADDITION:
    case AST_NODE_TYPE_DIVISION:
    case AST_NODE_TYPE_MULTIPLICATION:
    case AST_NODE_TYPE_SUBTRACTION: {
        // The operands have one type, which an untyped constant takes from the other.
        Ast_Node_Index left = node->first_child;
        Ast_Node_Index right = nodes[left].next_sibling;
        Ast_Node_Index typed = left;
        if (specializer->constants[left].status == CONSTANT_STATUS_OK) {
            typed = right;
        }
        return specialize_expression_type(specializer, typed, depth + 1, text);
    }
    case AST_NODE_TYPE_AND:
    case AST_NODE_TYPE_COMPARISON:
    case AST_NODE_TYPE_NOT:
    case AST_NODE_TYPE_OR:
        specialize_text_append(text, STR("Bool"));
        return not text->is_overflowed;
    case AST_NODE_TYPE_CAST:
        specialize_text_append(text, node->payload.cast.type_name);
        return not text->is_overflowed;
    case AST_NODE_TYPE_STRUCT_LITERAL:
        if (node->payload.struct_literal.name.bytes == NULL) {
            return false;
        }
        specialize_text_append(text, node->payload.struct_literal.name);
        return not text->is_overflowed;
    case AST_NODE_TYPE_ARRAY_LITERAL: {
        Ast_Node_Index type = node->first_child;
        if (nodes[type].payload.array_type.length_kind != AST_ARRAY_LENGTH_INFERRED) {
            return specialize_type_node_text(nodes, type, text);
        }
        // `[const]` is as long as the literal has elements.
        size_t count = 0;
        for (Ast_Node_Index element = nodes[type].next_sibling; element != AST_NODE_INDEX_NONE;
             element = nodes[element].next_sibling) {
            count += 1;
        }
        char digits[24] = {0};
        size_t length = 0;
        for (size_t value = count; length == 0 || value > 0; value /= 10) {
            digits[sizeof(digits) - 1 - length] = (char)('0' + value % 10);
            length += 1;
        }
        specialize_text_append(text, STR("["));
        specialize_text_append(text, str_make(digits + sizeof(digits) - length, length));
        specialize_text_append(text, STR("]"));
        return specialize_type_node_text(nodes, nodes[type].first_child, text);
    }
    case AST_NODE_TYPE_ADDRESS_OF: {
        Specialize_Type_Text operand = {.length = 0, .is_overflowed = false};
        if (not specialize_expression_type(specializer, node->first_child, depth + 1, &operand) ||
            operand.bytes[0] == '[') {
            return false;
        }
        specialize_text_append(text, STR("^"));
        specialize_text_append(text, specialize_text_str(&operand));
        return not text->is_overflowed;
    }
    case AST_NODE_TYPE_DEREFERENCE:
    case AST_NODE_TYPE_FIELD_ACCESS:
    case AST_NODE_TYPE_SLICE:
    case AST_NODE_TYPE_SUBSCRIPT: {
        // The member of an enum, such as `Color.RED`, has the enum as its type.
        Ast_Node_Index base = node->first_child;
        if (node->node_type == AST_NODE_TYPE_FIELD_ACCESS &&
            nodes[base].node_type == AST_NODE_TYPE_IDENTIFIER &&
            specializer->bindings[base].kind == SEMANTIC_BINDING_KIND_CONSTANT &&
            nodes[nodes[specializer->bindings[base].target].first_child].node_type ==
                AST_NODE_TYPE_ENUM_TYPE) {
            specialize_text_append(text,
                                   ast_definition_name(&nodes[specializer->bindings[base].target]));
            return not text->is_overflowed;
        }
        Specialize_Type_Text base_type = {.length = 0, .is_overflowed = false};
        if (not specialize_expression_type(specializer, base, depth + 1, &base_type)) {
            return false;
        }
        Str type = specialize_text_str(&base_type);
        if (node->node_type == AST_NODE_TYPE_DEREFERENCE) {
            if (type.length < 2 || type.bytes[0] != '^') {
                return false;
            }
            specialize_text_append(text, str_make(type.bytes + 1, type.length - 1));
            return not text->is_overflowed;
        }
        if (node->node_type != AST_NODE_TYPE_FIELD_ACCESS) {
            // A string is subscripted by the character, and sliced into a string.
            if (str_equals(type, STR("Str"))) {
                if (node->node_type == AST_NODE_TYPE_SUBSCRIPT) {
                    specialize_text_append(text, STR("Rune"));
                } else {
                    specialize_text_append(text, STR("Str"));
                }
                return not text->is_overflowed;
            }
            size_t close = 0;
            while (close < type.length && type.bytes[close] != ']') {
                close += 1;
            }
            if (type.bytes[0] != '[' || close == type.length) {
                return false;
            }
            Str element = str_make(type.bytes + close + 1, type.length - close - 1);
            if (node->node_type == AST_NODE_TYPE_SLICE) {
                specialize_text_append(text, STR("[]"));
            }
            specialize_text_append(text, element);
            return not text->is_overflowed;
        }
        // A field of a struct, which may be behind a pointer, because a field is read through one.
        if (type.length > 0 && type.bytes[0] == '^') {
            type = str_make(type.bytes + 1, type.length - 1);
        }
        Ast_Node_Index definition = specialize_definition_find(specializer, type);
        if (definition == AST_NODE_INDEX_NONE ||
            nodes[nodes[definition].first_child].node_type != AST_NODE_TYPE_STRUCT_TYPE ||
            ast_definition_is_polymorphic(nodes, definition)) {
            return false;
        }
        Str field_name = node->payload.field_access.name;
        for (Ast_Node_Index field = nodes[nodes[definition].first_child].first_child;
             field != AST_NODE_INDEX_NONE; field = nodes[field].next_sibling) {
            if (str_equals(nodes[field].payload.field.name, field_name)) {
                return specialize_type_node_text(nodes, nodes[field].first_child, text);
            }
        }
        return false;
    }
    case AST_NODE_TYPE_IDENTIFIER: {
        Semantic_Binding binding = specializer->bindings[expression];
        if (binding.kind == SEMANTIC_BINDING_KIND_NONE ||
            binding.kind == SEMANTIC_BINDING_KIND_JUMP_TARGET) {
            return false;
        }
        return specialize_definition_type(specializer, binding.target, depth + 1, text);
    }
    case AST_NODE_TYPE_CALL: {
        Semantic_Binding binding = specializer->bindings[expression];
        Str name = node->payload.call.name;
        if (binding.kind == SEMANTIC_BINDING_KIND_NONE) {
            // The built-in procedures that give one value.
            char const *type = NULL;
            if (str_equals(name, STR("length_of"))) {
                type = "Int";
            } else if (str_equals(name, STR("int_to_byte"))) {
                type = "Byte";
            } else if (str_equals(name, STR("int_to_i32"))) {
                type = "I32";
            } else if (str_equals(name, STR("str_to_c_str"))) {
                type = "C_Str";
            } else if (str_equals(name, STR("make")) && node->first_child != AST_NODE_INDEX_NONE) {
                return specialize_type_node_text(nodes, node->first_child, text);
            }
            if (type == NULL) {
                return false;
            }
            specialize_text_append(text, str_make(type, strlen(type)));
            return not text->is_overflowed;
        }
        if (binding.kind != SEMANTIC_BINDING_KIND_CONSTANT) {
            return false;
        }
        Ast_Node_Index procedure = nodes[binding.target].first_child;
        if (nodes[procedure].node_type != AST_NODE_TYPE_PROCEDURE ||
            ast_definition_is_polymorphic(nodes, binding.target) ||
            ast_procedure_return_count(&nodes[procedure]) != 1) {
            return false;
        }
        specialize_text_append(text, ast_procedure_return_type_at(&nodes[procedure], 0));
        return not text->is_overflowed;
    }
    default:
        break;
    }
    return false;
}

/**
 * Writes the type of what `target` declares, a definition, a parameter or the iterator of a for
 * statement, to `text`, as `specialize_expression_type` does for an expression, which it and this
 * call each other for, `depth` deep.
 */
static bool specialize_definition_type(Specializer const *specializer, Ast_Node_Index target,
                                       size_t depth, Specialize_Type_Text *text) {
    Ast_Node const *nodes = specializer->nodes;
    Ast_Node const *node = &nodes[target];
    if (depth >= SPECIALIZE_TYPE_DEPTH_MAX) {
        return false;
    }

    switch (node->node_type) {
    case AST_NODE_TYPE_PARAMETER:
        return specialize_type_node_text(nodes, node->first_child, text);
    case AST_NODE_TYPE_FOR_STATEMENT: {
        // The iterator of a range is an `Int`, and the one of a sequence is an element of it.
        Ast_Node_Index sequence = node->first_child;
        if (nodes[sequence].node_type == AST_NODE_TYPE_RANGE) {
            specialize_text_append(text, STR("Int"));
            return not text->is_overflowed;
        }
        Specialize_Type_Text sequence_type = {.length = 0, .is_overflowed = false};
        if (not specialize_expression_type(specializer, sequence, depth + 1, &sequence_type)) {
            return false;
        }
        Str type = specialize_text_str(&sequence_type);
        if (str_equals(type, STR("Str"))) {
            specialize_text_append(text, STR("Rune"));
            return not text->is_overflowed;
        }
        size_t close = 0;
        while (close < type.length && type.bytes[close] != ']') {
            close += 1;
        }
        if (type.bytes[0] != '[' || close == type.length) {
            return false;
        }
        specialize_text_append(text, str_make(type.bytes + close + 1, type.length - close - 1));
        return not text->is_overflowed;
    }
    case AST_NODE_TYPE_CONSTANT_DEFINITION: {
        Ast_Node_Index value = node->first_child;
        Ast_Node_Type value_type = nodes[value].node_type;
        if (value_type == AST_NODE_TYPE_PROCEDURE || value_type == AST_NODE_TYPE_STRUCT_TYPE ||
            value_type == AST_NODE_TYPE_ENUM_TYPE) {
            return false;
        }
        // A typed constant has its type after its value.
        if (nodes[value].next_sibling != AST_NODE_INDEX_NONE) {
            return specialize_type_node_text(nodes, nodes[value].next_sibling, text);
        }
        return specialize_expression_type(specializer, value, depth + 1, text);
    }
    case AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION:
    case AST_NODE_TYPE_VARIABLE_DEFINITION: {
        Ast_Node_Index value = node->first_child;
        // The attributes of a global variable come before its type.
        while (value != AST_NODE_INDEX_NONE &&
               nodes[value].node_type == AST_NODE_TYPE_ATTRIBUTE) {
            value = nodes[value].next_sibling;
        }
        if (value == AST_NODE_INDEX_NONE) {
            // A name of the values of a call after the first, whose type is the one that the
            // procedure returns at its place.
            if (node->node_type != AST_NODE_TYPE_VARIABLE_DEFINITION || node->result_index == 0) {
                return false;
            }
            Ast_Node_Index call = nodes[ast_variable_leader(nodes, target)].first_child;
            Semantic_Binding binding = specializer->bindings[call];
            if (nodes[call].node_type != AST_NODE_TYPE_CALL ||
                binding.kind != SEMANTIC_BINDING_KIND_CONSTANT ||
                ast_definition_is_polymorphic(nodes, binding.target)) {
                return false;
            }
            Ast_Node const *procedure = &nodes[nodes[binding.target].first_child];
            if (procedure->node_type != AST_NODE_TYPE_PROCEDURE ||
                ast_procedure_return_count(procedure) <= node->result_index) {
                return false;
            }
            specialize_text_append(text, ast_procedure_return_type_at(procedure,
                                                                      node->result_index));
            return not text->is_overflowed;
        }
        Ast_Node_Type value_type = nodes[value].node_type;
        bool is_type = value_type == AST_NODE_TYPE_TYPE_NAME ||
                       value_type == AST_NODE_TYPE_POINTER_TYPE ||
                       value_type == AST_NODE_TYPE_ARRAY_TYPE ||
                       value_type == AST_NODE_TYPE_PROCEDURE_TYPE;
        if (is_type) {
            // `[const]` is the length of the literal that the variable is given.
            Ast_Node_Index literal = nodes[value].next_sibling;
            if (value_type == AST_NODE_TYPE_ARRAY_TYPE &&
                nodes[value].payload.array_type.length_kind == AST_ARRAY_LENGTH_INFERRED &&
                literal != AST_NODE_INDEX_NONE) {
                return specialize_expression_type(specializer, literal, depth + 1, text);
            }
            return specialize_type_node_text(nodes, value, text);
        }
        // The first of several names of the values of a call has the first type.
        if (node->node_type == AST_NODE_TYPE_VARIABLE_DEFINITION && node->result_count > 1) {
            Semantic_Binding binding = specializer->bindings[value];
            if (nodes[value].node_type != AST_NODE_TYPE_CALL ||
                binding.kind != SEMANTIC_BINDING_KIND_CONSTANT ||
                ast_definition_is_polymorphic(nodes, binding.target)) {
                return false;
            }
            Ast_Node const *procedure = &nodes[nodes[binding.target].first_child];
            if (procedure->node_type != AST_NODE_TYPE_PROCEDURE ||
                ast_procedure_return_count(procedure) < 1) {
                return false;
            }
            specialize_text_append(text, ast_procedure_return_type_at(procedure, 0));
            return not text->is_overflowed;
        }
        return specialize_expression_type(specializer, value, depth + 1, text);
    }
    default:
        break;
    }
    return false;
}

/**
 * Writes the text of the type node `type` to `text`, as it is written: the carets and the brackets
 * of its pointer and array types, and the name at the end, with its type arguments. Returns false
 * for a node that is not a type, or a type that does not fit.
 */
static bool specialize_type_node_text(Ast_Node const *nodes, Ast_Node_Index type,
                                      Specialize_Type_Text *text) {
    // Each step goes down one level, and a type is no deeper than the blocks are.
    for (size_t depth = 0; depth < AST_BLOCK_DEPTH_MAX && type != AST_NODE_INDEX_NONE;
         depth += 1) {
        Ast_Node const *node = &nodes[type];
        if (node->node_type == AST_NODE_TYPE_TYPE_NAME) {
            specialize_text_append(text, node->payload.type_name.name);
            return not text->is_overflowed;
        }
        if (node->node_type == AST_NODE_TYPE_POINTER_TYPE) {
            specialize_text_append(text, STR("^"));
        } else if (node->node_type == AST_NODE_TYPE_ARRAY_TYPE) {
            switch (node->payload.array_type.length_kind) {
            case AST_ARRAY_LENGTH_DYNAMIC:
                specialize_text_append(text, STR("[dynamic]"));
                break;
            case AST_ARRAY_LENGTH_INFERRED:
                specialize_text_append(text, STR("[const]"));
                break;
            case AST_ARRAY_LENGTH_OMITTED:
                specialize_text_append(text, STR("[]"));
                break;
            case AST_ARRAY_LENGTH_CONSTANT:
            case AST_ARRAY_LENGTH_FIXED: {
                // The analysis made a constant length a number, see `AST_ARRAY_LENGTH_CONSTANT`.
                char digits[24] = {0};
                size_t length = 0;
                uint64_t value = (uint64_t)node->payload.array_type.length;
                for (; length == 0 || value > 0; value /= 10) {
                    digits[sizeof(digits) - 1 - length] = (char)('0' + value % 10);
                    length += 1;
                }
                specialize_text_append(text, STR("["));
                specialize_text_append(text, str_make(digits + sizeof(digits) - length, length));
                specialize_text_append(text, STR("]"));
                break;
            }
            }
        } else {
            return false;
        }
        type = node->first_child;
    }
    return false;
}

/**
 * Splits `text`, the text of a type, into its tokens, see `Specialize_Type_Token`. Spaces and tabs
 * between them are left out.
 */
static Specialize_Type_Tokens specialize_type_tokenize(Str text) {
    Specialize_Type_Tokens tokens = {.count = 0, .is_valid = text.length > 0};
    size_t i = 0;
    // Every step consumes a character, so there are fewer steps than characters.
    while (i < text.length && tokens.is_valid) {
        char character = text.bytes[i];
        if (character == ' ' || character == '\t') {
            i += 1;
            continue;
        }
        if (tokens.count == SPECIALIZE_TYPE_TOKEN_MAX) {
            tokens.is_valid = false;
            break;
        }
        bool is_word_byte = (character >= 'a' && character <= 'z') ||
                            (character >= 'A' && character <= 'Z') ||
                            (character >= '0' && character <= '9') || character == '_' ||
                            character == '.' || character == '$';
        if (is_word_byte) {
            size_t start = i;
            while (i < text.length &&
                   ((text.bytes[i] >= 'a' && text.bytes[i] <= 'z') ||
                    (text.bytes[i] >= 'A' && text.bytes[i] <= 'Z') ||
                    (text.bytes[i] >= '0' && text.bytes[i] <= '9') || text.bytes[i] == '_' ||
                    text.bytes[i] == '.' || text.bytes[i] == '$')) {
                i += 1;
            }
            tokens.tokens[tokens.count] = (Specialize_Type_Token){
                .text = str_make(text.bytes + start, i - start),
                .kind = 'w',
            };
            tokens.count += 1;
            continue;
        }
        if (character != '^' && character != '[' && character != ']' && character != '(' &&
            character != ')' && character != ',') {
            tokens.is_valid = false;
            break;
        }
        tokens.tokens[tokens.count] = (Specialize_Type_Token){
            .text = str_make(text.bytes + i, 1),
            .kind = character,
        };
        tokens.count += 1;
        i += 1;
    }
    return tokens;
}

/**
 * Returns the canonical text of the tokens from `start` up to `end`, see
 * `specialize_type_text_process`.
 */
static Specialize_Type_Text specialize_canonical(Specialize_Type_Tokens const *tokens,
                                                 size_t start, size_t end) {
    Specialize_Type_Text text = {.length = 0, .is_overflowed = false};
    for (size_t i = start; i < end && i < tokens->count; i += 1) {
        specialize_text_append(&text, tokens->tokens[i].text);
        if (tokens->tokens[i].kind == ',') {
            specialize_text_append(&text, STR(" "));
        }
    }
    return text;
}

/**
 * Returns the definition of the tree of the round that is called `name`, or `AST_NODE_INDEX_NONE`.
 */
static Ast_Node_Index specialize_definition_find(Specializer const *specializer, Str name) {
    Ast_Node const *nodes = specializer->nodes;
    Ast_Node_Index definition = nodes[specializer->root].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < specializer->node_count && definition != AST_NODE_INDEX_NONE;
         i += 1) {
        if (nodes[definition].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION &&
            str_equals(nodes[definition].payload.constant_definition.name, name)) {
            return definition;
        }
        definition = nodes[definition].next_sibling;
    }
    return AST_NODE_INDEX_NONE;
}

/**
 * Returns the index after the last node of `definition`, a child of the module of the tree of the
 * round, which is the next node that is a child of a module, or a module, or the end of the tree.
 * The nodes of a definition are made after it and before the next one.
 */
static Ast_Node_Index specialize_definition_end(Specializer const *specializer,
                                                Ast_Node_Index definition) {
    Ast_Node const *nodes = specializer->nodes;
    Ast_Node_Index end = definition + 1;
    while (end < specializer->node_count) {
        Ast_Node_Type node_type = nodes[end].node_type;
        if (node_type == AST_NODE_TYPE_CONSTANT_DEFINITION ||
            node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION ||
            node_type == AST_NODE_TYPE_FOREIGN_IMPORT || node_type == AST_NODE_TYPE_IMPORT ||
            node_type == AST_NODE_TYPE_MODULE) {
            break;
        }
        end += 1;
    }
    return end;
}

/**
 * Returns the origin of `node` of the tree of the round, see `Specialize_Result`.
 */
static Ast_Node_Index specialize_origin(Specializer const *specializer, Ast_Node_Index node) {
    if (specializer->origins == NULL) {
        return node;
    }
    return specializer->origins[node];
}

/**
 * Writes `text` to the texts of the specialization, with `source`, the text of a source that it is
 * made of, or an empty `Str`, and returns it as a view of them. Returns a `Str` whose `bytes` is
 * NULL after it recorded the error that they are full.
 */
static Str specialize_store(Specializer *specializer, Str text, Str source) {
    Specialize_Texts *texts = specializer->texts;
    if (text.length > texts->size_bytes - texts->length ||
        texts->entry_count == texts->entry_count_max) {
        specializer->number = (int64_t)texts->size_bytes;
        (void)specialize_fail(specializer, SPECIALIZE_STATUS_LIMIT, source);
        return STR_EMPTY;
    }
    char *place = texts->bytes + texts->length;
    memcpy(place, text.bytes, text.length);
    texts->length += text.length;
    Str stored = str_make(place, text.length);
    texts->entries[texts->entry_count] = (Specialize_Text){
        .text = stored,
        .source = source,
    };
    texts->entry_count += 1;
    return stored;
}

/**
 * Appends `part` to `text`, which is not valid any more when it does not fit.
 */
static void specialize_text_append(Specialize_Type_Text *text, Str part) {
    if (text->is_overflowed || part.length > sizeof(text->bytes) - text->length) {
        text->is_overflowed = true;
        return;
    }
    memcpy(text->bytes + text->length, part.bytes, part.length);
    text->length += part.length;
}

/**
 * Returns `text` as a view of its bytes, which is good as long as it is.
 */
static Str specialize_text_str(Specialize_Type_Text const *text) {
    return str_make(text->bytes, text->length);
}

/**
 * Records the first error and returns false, so that a caller can report failure in one line.
 */
static bool specialize_fail(Specializer *specializer, Specialize_Status status, Str subject) {
    assert(status != SPECIALIZE_STATUS_OK);

    if (specializer->status == SPECIALIZE_STATUS_OK) {
        specializer->status = status;
        specializer->subject = subject;
    }
    return false;
}
