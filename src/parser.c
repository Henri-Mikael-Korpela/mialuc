#include <assert.h>
#include <stdbool.h>

#include <mialu/parser.h>
#include <mialu/tokenizer.h>

// The indentation level of a line that is not indented. Definitions at this level are the
// top-level ones, and their bodies are one level deeper.
static uint32_t const INDENTATION_LEVEL_TOP = 0;

/**
 * A block that is open while the lines of a procedure body are parsed: the node that the
 * statements of the block are appended to, which is the procedure for the body of the procedure,
 * and the indentation level of the lines of the block.
 */
typedef struct {
    Ast_Node_Index node;
    uint32_t level;
} Parse_Block;

// Alphabetical, because nothing depends on the order.
typedef enum {
    // The fields below a `case` of a union match. They become fields of the struct that the union
    // match is in.
    PARSE_STRUCT_FRAME_KIND_CASE,
    // The fields of a struct, and its union matches.
    PARSE_STRUCT_FRAME_KIND_STRUCT,
    // The cases below `union match kind ->`.
    PARSE_STRUCT_FRAME_KIND_UNION,
} Parse_Struct_Frame_Kind;

/**
 * A part of a struct definition that is open while its lines are parsed, which is a struct, a union
 * match or a case of one. The lines of a frame are indented to its `level`. `node` is the struct
 * that the fields are appended to: for a union match and for its cases that is the struct that the
 * union match is in, because the fields of the cases are fields of that struct. `discriminant` and
 * `member` are the name of the field that is matched and the member of the case, for the frames
 * that are inside a union match. `item_count` is how many fields (or cases, for a union match)
 * the frame has had, because each one needs at least one.
 */
typedef struct {
    Str discriminant;
    Str member;
    Ast_Node_Index node;
    size_t item_count;
    uint32_t level;
    Parse_Struct_Frame_Kind kind;
} Parse_Struct_Frame;

/**
 * The state shared by the parsing functions. A parsing function returns the index of the node it
 * made, or `AST_NODE_INDEX_NONE` after it recorded an error here, and its caller then returns
 * `AST_NODE_INDEX_NONE` too without recording anything more. So only the first error is kept.
 */
typedef struct {
    Token const *tokens;
    Ast_Node *nodes;
    Str expected;
    // How many array literals and struct literals are open, which are inside each other because
    // their elements and fields are expressions. It is bounded by `AST_BLOCK_DEPTH_MAX`, so that
    // the recursion that this needs cannot go on without end.
    size_t literal_depth;
    // Whether `type_info_of` was parsed, which makes the module get the types of the information,
    // see `parse_reflection_types_append`.
    bool has_type_info;
    size_t token_count;
    size_t token_index;
    size_t node_count;
    size_t node_count_max;
    size_t error_token_index;
    Parse_Status status;
} Parser;

static Ast_Node_Index parse_array_literal       (Parser *parser);
static Ast_Node_Index parse_assignment          (Parser *parser);
static bool           parse_attributes          (Parser *parser, Ast_Node_Index definition);
static void           parse_block_close         (Parser *parser, Ast_Node_Index block);
static Ast_Node_Index parse_block_open          (Parser *parser, Ast_Node_Index statement);
static Ast_Node_Index parse_branch              (Parser *parser, Ast_Node_Index statement);
static Ast_Node_Index parse_definition          (Parser *parser);
static Ast_Node_Index parse_enum_member_value   (Parser *parser);
static Ast_Node_Index parse_enum_type           (Parser *parser, uint32_t indentation_level);
static bool           parse_foreign_block       (Parser *parser, Ast_Node_Index module);
static Ast_Node_Index parse_foreign_import      (Parser *parser);
static bool           parse_foreign_target_expect(Parser *parser);
static Ast_Node_Index parse_else_branch         (Parser *parser, Ast_Node_Index parent);
static Ast_Node_Index parse_if_statement        (Parser *parser, Ast_Node_Index parent);
static Ast_Node_Index parse_bool_literal        (Parser *parser);
static Ast_Node_Index parse_operand             (Parser *parser);
static Ast_Node_Index parse_operand_value       (Parser *parser);
static Ast_Node_Index parse_call                (Parser *parser);
static Ast_Node_Index parse_call_statement      (Parser *parser);
static Ast_Node_Index parse_comparison          (Parser *parser);
static Ast_Node_Index parse_conjunction         (Parser *parser);
static Ast_Node_Index parse_disjunction         (Parser *parser);
static Ast_Node_Index parse_expression          (Parser *parser);
static Ast_Node_Index parse_negation            (Parser *parser);
static Ast_Node_Index parse_sum                 (Parser *parser);
static Ast_Node_Index parse_fail                (Parser *parser, Str expected);
static Ast_Node_Index parse_for_statement       (Parser *parser, Ast_Node_Index parent);
static bool           parse_for_statement_has_range(Parser const *parser);
static Ast_Node_Index parse_global_variable_definition(Parser *parser);
static Ast_Node_Index parse_jump_statement      (Parser *parser);
static Ast_Node_Index parse_scope_statement     (Parser *parser, Ast_Node_Index parent);
static Ast_Node_Index parse_identifier          (Parser *parser);
static Ast_Node_Index parse_integer_literal     (Parser *parser);
static bool           parse_line_end_expect     (Parser *parser);
static Ast_Node_Index parse_module              (Parser *parser);
static void           parse_node_append_child   (Parser *parser, Ast_Node_Index parent,
                                                 Ast_Node_Index child);
static Ast_Node_Index parse_node_make           (Parser *parser, Ast_Node_Type node_type);
static Ast_Node_Index parse_parameter           (Parser *parser);
static Ast_Node_Index parse_procedure           (Parser *parser, uint32_t indentation_level,
                                         bool is_foreign);
static bool           parse_procedure_body      (Parser *parser, Ast_Node_Index procedure,
                                                 uint32_t indentation_level);
static bool           parse_procedure_return_type(Parser *parser, Ast_Node_Index procedure);
static bool           parse_procedure_parameters(Parser *parser, Ast_Node_Index procedure);
static bool           parse_reflection_types_append(Parser *parser, Ast_Node_Index module);
static Ast_Node_Index parse_return_statement    (Parser *parser);
static Ast_Node_Index parse_statement           (Parser *parser);
static Ast_Node_Index parse_string_literal      (Parser *parser);
static void           parse_skip_indentation    (Parser *parser);
static Ast_Node_Index parse_struct_literal      (Parser *parser);
static Ast_Node_Index parse_struct_type         (Parser *parser, uint32_t indentation_level);
static Str            parse_struct_type_frame_expected(Parse_Struct_Frame_Kind kind);
static Parse_Struct_Frame parse_struct_type_union_line(Parser *parser,
                                                 Parse_Struct_Frame const *parent);
static bool           parse_struct_type_union_tag(Parser *parser, Ast_Node_Index field,
                                                 Parse_Struct_Frame const *frame);
static Ast_Node_Index parse_subscripts          (Parser *parser, Ast_Node_Index operand);
static Ast_Node_Index parse_term                (Parser *parser);
static Ast_Node_Index parse_type_info_of        (Parser *parser);
static void           parse_token_advance       (Parser *parser);
static Token const *  parse_token_current       (Parser const *parser);
static bool           parse_token_expect        (Parser *parser, Token_Kind token_kind,
                                                 Str expected);
static Ast_Node_Index parse_type                (Parser *parser, Str expected);
static Ast_Node_Index parse_type_name           (Parser *parser, Str expected,
                                                 bool is_procedure_allowed);
static Ast_Node_Index parse_procedure_type      (Parser *parser);
static Ast_Node_Index parse_variable_definition (Parser *parser);

Parse_Result parse(Array_Slice tokens, Array_Slice node_buffer) {
    assert(tokens.elements != NULL);
    assert(node_buffer.elements != NULL);
    assert(tokens.length >= 1);

    Token const *token_elements = tokens.elements;
    assert(token_elements[tokens.length - 1].token_kind == TOKEN_KIND_END);

    Parser parser = {
        .tokens = token_elements,
        .nodes = node_buffer.elements,
        .expected = STR(""),
        .literal_depth = 0,
        .has_type_info = false,
        .token_count = tokens.length,
        .token_index = 0,
        .node_count = 0,
        .node_count_max = node_buffer.length,
        .error_token_index = 0,
        .status = PARSE_STATUS_OK,
    };

    Ast_Node_Index root = parse_module(&parser);

    // The root exists exactly when parsing succeeded.
    assert((root == AST_NODE_INDEX_NONE) == (parser.status != PARSE_STATUS_OK));
    assert(parser.node_count <= node_buffer.length);
    return (Parse_Result){
        .nodes = array_slice_make(node_buffer.elements, parser.node_count),
        .expected = parser.expected,
        .error_token_index = parser.error_token_index,
        .root = root,
        .status = parser.status,
    };
}

static Ast_Node_Index parse_module(Parser *parser) {
    assert(parser->token_index == 0);
    assert(parser->status == PARSE_STATUS_OK);

    Ast_Node_Index module = parse_node_make(parser, AST_NODE_TYPE_MODULE);
    if (module == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    // Every definition consumes at least its indentation token, and the last token, `END`, is
    // never consumed. So there are fewer definitions than tokens, which bounds the loop.
    for (size_t i = 0; i < parser->token_count; i += 1) {
        if (parse_token_current(parser)->token_kind == TOKEN_KIND_END) {
            break;
        }

        // A `foreign` block declares several procedures at once, so it appends them itself. The
        // token after the indentation tells it from any other definition.
        if (parse_token_current(parser)->token_kind == TOKEN_KIND_INDENTATION &&
            parser->token_index + 1 < parser->token_count &&
            parser->tokens[parser->token_index + 1].token_kind == TOKEN_KIND_KEYWORD_FOREIGN) {
            if (parse_foreign_block(parser, module) == false) {
                return AST_NODE_INDEX_NONE;
            }
            continue;
        }

        Ast_Node_Index definition = parse_definition(parser);
        if (definition == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, module, definition);
    }

    assert(parse_token_current(parser)->token_kind == TOKEN_KIND_END);
    if (parser->has_type_info && parse_reflection_types_append(parser, module) == false) {
        return AST_NODE_INDEX_NONE;
    }
    return module;
}

/**
 * Appends the definitions of the types that `type_info_of` gives information in to `module`, as if
 * the program had them at its end:
 *
 *     Enum_Member_Type_Info :: struct ->
 *         name: Str
 *         value: Int
 *
 * They are made only when the program uses `type_info_of`, so that other programs have the same
 * tree as before. They are ordinary structs from here on, so the analysis and the target need no
 * special case for the elements of `members`. Their names and fields are views of string literals
 * and not of the source, so an error about them, which only a program that defines the same name
 * can cause, has no place in the source. Returns false after it recorded an error.
 */
static bool parse_reflection_types_append(Parser *parser, Ast_Node_Index module) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(module < parser->node_count);

    Ast_Node_Index definition = parse_node_make(parser, AST_NODE_TYPE_CONSTANT_DEFINITION);
    if (definition == AST_NODE_INDEX_NONE) {
        return false;
    }
    parser->nodes[definition].payload.constant_definition.name = STR("Enum_Member_Type_Info");
    parse_node_append_child(parser, module, definition);

    Ast_Node_Index struct_type = parse_node_make(parser, AST_NODE_TYPE_STRUCT_TYPE);
    if (struct_type == AST_NODE_INDEX_NONE) {
        return false;
    }
    parse_node_append_child(parser, definition, struct_type);

    Str const field_names[] = {STR("name"), STR("value")};
    Str const field_types[] = {STR("Str"), STR("Int")};
    for (size_t i = 0; i < 2; i += 1) {
        Ast_Node_Index field = parse_node_make(parser, AST_NODE_TYPE_FIELD);
        if (field == AST_NODE_INDEX_NONE) {
            return false;
        }
        parser->nodes[field].payload.field.name = field_names[i];
        parse_node_append_child(parser, struct_type, field);

        Ast_Node_Index type_name = parse_node_make(parser, AST_NODE_TYPE_TYPE_NAME);
        if (type_name == AST_NODE_INDEX_NONE) {
            return false;
        }
        parser->nodes[type_name].payload.type_name.name = field_types[i];
        parse_node_append_child(parser, field, type_name);
    }

    return true;
}

static Ast_Node_Index parse_definition(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->node_count <= parser->node_count_max);

    // Expect definitions to be at the top level.
    Token const *indentation = parse_token_current(parser);
    if (indentation->token_kind != TOKEN_KIND_INDENTATION) {
        return parse_fail(parser, STR("the start of a line"));
    }
    if (indentation->payload.indentation.level != INDENTATION_LEVEL_TOP) {
        return parse_fail(parser, STR("no indentation"));
    }
    parse_token_advance(parser);

    // `import foreign js "name"` is not a definition: it has no name of its own.
    if (parse_token_current(parser)->token_kind == TOKEN_KIND_KEYWORD_IMPORT) {
        return parse_foreign_import(parser);
    }

    // Expect a definition to start with an identifier.
    Token const *name = parse_token_current(parser);
    if (name->token_kind != TOKEN_KIND_IDENTIFIER) {
        return parse_fail(parser, STR("a name"));
    }

    // A constant has `::` after its name, which is two separator tokens. Anything else is a global
    // variable, which has an attribute list, a single `:` or both after its name. The last token is
    // `END`, which is neither, so a name at the end of the file is not a constant, and the global
    // variable it starts reports what it expected.
    bool is_constant = parser->token_index + 2 < parser->token_count &&
                       parser->tokens[parser->token_index + 1].token_kind ==
                           TOKEN_KIND_TYPE_SEPARATOR &&
                       parser->tokens[parser->token_index + 2].token_kind ==
                           TOKEN_KIND_TYPE_SEPARATOR;
    if (is_constant == false) {
        return parse_global_variable_definition(parser);
    }

    Ast_Node_Index definition = parse_node_make(parser, AST_NODE_TYPE_CONSTANT_DEFINITION);
    if (definition == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[definition].payload.constant_definition.name = name->payload.identifier.text;
    parse_token_advance(parser);

    // `::` is two separator tokens, one after the other.
    if (parse_token_expect(parser, TOKEN_KIND_TYPE_SEPARATOR, STR("'::'")) == false) {
        return AST_NODE_INDEX_NONE;
    }
    if (parse_token_expect(parser, TOKEN_KIND_TYPE_SEPARATOR, STR("'::'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    // The value is a string, a struct, an enum, the member of an enum, or a procedure, which is
    // what any other token here starts.
    Ast_Node_Index value = AST_NODE_INDEX_NONE;
    if (parse_token_current(parser)->token_kind == TOKEN_KIND_KEYWORD_STRUCT) {
        // Like a procedure, a struct ends with its body.
        value = parse_struct_type(parser, INDENTATION_LEVEL_TOP);
    } else if (parse_token_current(parser)->token_kind == TOKEN_KIND_KEYWORD_ENUM) {
        // An enum ends with its members.
        value = parse_enum_type(parser, INDENTATION_LEVEL_TOP);
    } else if (parse_token_current(parser)->token_kind == TOKEN_KIND_IDENTIFIER) {
        value = parse_enum_member_value(parser);
    } else if (parse_token_current(parser)->token_kind == TOKEN_KIND_STRING_LITERAL) {
        value = parse_string_literal(parser);
        // A procedure ends with its body, but a string is one line, so its end is checked here.
        if (value != AST_NODE_INDEX_NONE && parse_line_end_expect(parser) == false) {
            return AST_NODE_INDEX_NONE;
        }
    } else {
        value = parse_procedure(parser, INDENTATION_LEVEL_TOP, false);
    }
    if (value == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, definition, value);

    assert(parser->nodes[definition].first_child == value);
    return definition;
}

/**
 * Parses the enum `enum Int ->`, with the name of the type of the numbers of its members, and the
 * members, which are the lines below it, indented one level deeper than the line of the
 * definition, each one a name. The enum must have a member. The current token is `enum`, and
 * `indentation_level` is the level of the line it is on.
 */
static Ast_Node_Index parse_enum_type(Parser *parser, uint32_t indentation_level) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parse_token_current(parser)->token_kind == TOKEN_KIND_KEYWORD_ENUM);
    assert(indentation_level < UINT32_MAX);

    parse_token_advance(parser);
    Ast_Node_Index root = parse_node_make(parser, AST_NODE_TYPE_ENUM_TYPE);
    if (root == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    // The type of the numbers is always written, so that it is not a surprise what they are.
    Token const *value_type = parse_token_current(parser);
    if (value_type->token_kind != TOKEN_KIND_IDENTIFIER) {
        return parse_fail(parser, STR("the type of the members after 'enum', such as 'Int'"));
    }
    parser->nodes[root].payload.enum_type.value_type = value_type->payload.identifier.text;
    parse_token_advance(parser);
    if (parse_token_expect(parser, TOKEN_KIND_ARROW, STR("'->'")) == false ||
        parse_line_end_expect(parser) == false) {
        return AST_NODE_INDEX_NONE;
    }

    // Each member consumes at least its indentation token, so there are fewer members than
    // tokens, which bounds the loop. It stops at the first line that is not a member.
    for (size_t i = 0; i < parser->token_count; i += 1) {
        Token const *line_start = parse_token_current(parser);
        if (line_start->token_kind != TOKEN_KIND_INDENTATION ||
            line_start->payload.indentation.level <= INDENTATION_LEVEL_TOP) {
            break;
        }
        if (line_start->payload.indentation.level != indentation_level + 1) {
            return parse_fail(parser, STR("a member that is indented one level"));
        }
        parse_token_advance(parser);

        Token const *member_name = parse_token_current(parser);
        if (member_name->token_kind != TOKEN_KIND_IDENTIFIER) {
            return parse_fail(parser, STR("a member name"));
        }
        Ast_Node_Index member = parse_node_make(parser, AST_NODE_TYPE_ENUM_MEMBER);
        if (member == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parser->nodes[member].payload.enum_member.name = member_name->payload.identifier.text;
        parse_token_advance(parser);
        parse_node_append_child(parser, root, member);
        if (parse_line_end_expect(parser) == false) {
            return AST_NODE_INDEX_NONE;
        }
    }

    if (parser->nodes[root].first_child == AST_NODE_INDEX_NONE) {
        return parse_fail(parser, STR("a member"));
    }
    return root;
}

/**
 * Parses the value of a constant that is the member of an enum, `Color.RED`: the name of the enum,
 * a dot and the name of the member, and then the end of the line. The node is the access of the
 * field `RED`, whose child is the identifier `Color`, so it is the same tree that `Color.RED` has
 * in an expression, and the nodes are in the order of the source.
 */
static Ast_Node_Index parse_enum_member_value(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);

    Ast_Node_Index enum_name = parse_identifier(parser);
    if (enum_name == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    if (parse_token_expect(parser, TOKEN_KIND_DOT, STR("'.' and the member of an enum")) == false) {
        return AST_NODE_INDEX_NONE;
    }
    Token const *member_name = parse_token_current(parser);
    if (member_name->token_kind != TOKEN_KIND_IDENTIFIER) {
        return parse_fail(parser, STR("the name of a member"));
    }
    Ast_Node_Index access = parse_node_make(parser, AST_NODE_TYPE_FIELD_ACCESS);
    if (access == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[access].payload.field_access.name = member_name->payload.identifier.text;
    parse_token_advance(parser);
    parse_node_append_child(parser, access, enum_name);

    if (parse_line_end_expect(parser) == false) {
        return AST_NODE_INDEX_NONE;
    }
    return access;
}

/**
 * Parses `import foreign js "name"`. The current token is `import`. The node has the name between
 * the quotes in its payload. `js` is the only target, so `foreign` and `js` are checked here and
 * not stored.
 */
static Ast_Node_Index parse_foreign_import(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parse_token_current(parser)->token_kind == TOKEN_KIND_KEYWORD_IMPORT);

    Ast_Node_Index import = parse_node_make(parser, AST_NODE_TYPE_FOREIGN_IMPORT);
    if (import == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_token_advance(parser);

    if (parse_token_expect(parser, TOKEN_KIND_KEYWORD_FOREIGN, STR("'foreign'")) == false) {
        return AST_NODE_INDEX_NONE;
    }
    if (parse_foreign_target_expect(parser) == false) {
        return AST_NODE_INDEX_NONE;
    }

    Token const *module_name = parse_token_current(parser);
    if (module_name->token_kind != TOKEN_KIND_STRING_LITERAL) {
        return parse_fail(parser, STR("the name of the object to import from, in quotes"));
    }
    parser->nodes[import].payload.foreign_import.module_name =
        module_name->payload.string_literal.text;
    parse_token_advance(parser);

    if (parse_line_end_expect(parser) == false) {
        return AST_NODE_INDEX_NONE;
    }
    return import;
}

/**
 * Consumes the name of the target of a foreign import or block, which must be `js`, because
 * JavaScript is the only language that Mialu can import from. It is an ordinary name and not a
 * keyword, so that programs can have a variable called `js`. Returns false after it recorded an
 * error.
 */
static bool parse_foreign_target_expect(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);

    Token const *target = parse_token_current(parser);
    if (target->token_kind != TOKEN_KIND_IDENTIFIER ||
        str_equals(target->payload.identifier.text, STR("js")) == false) {
        // The false return value reports the failure, so the node index is not needed.
        (void)parse_fail(parser, STR("'js', the only foreign target"));
        return false;
    }

    parse_token_advance(parser);
    return true;
}

/**
 * Parses `foreign js ->` and the procedures below it, one on each line, indented one level:
 * `name :: proc(parameters) Type -> ...`. Each becomes a constant definition that is appended to
 * `module`, like any other, so the block has no node of its own. The current token is the
 * indentation of the `foreign` line. Returns false after it recorded an error.
 */
static bool parse_foreign_block(Parser *parser, Ast_Node_Index module) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parse_token_current(parser)->token_kind == TOKEN_KIND_INDENTATION);

    if (parse_token_current(parser)->payload.indentation.level != INDENTATION_LEVEL_TOP) {
        // The false return value reports the failure, so the node index is not needed.
        (void)parse_fail(parser, STR("no indentation"));
        return false;
    }
    parse_token_advance(parser);

    assert(parse_token_current(parser)->token_kind == TOKEN_KIND_KEYWORD_FOREIGN);
    parse_token_advance(parser);
    if (parse_foreign_target_expect(parser) == false) {
        return false;
    }
    if (parse_token_expect(parser, TOKEN_KIND_ARROW, STR("'->'")) == false) {
        return false;
    }
    if (parse_line_end_expect(parser) == false) {
        return false;
    }

    // Every procedure consumes at least its indentation token, so there are fewer of them than
    // tokens, which bounds the loop. It stops at the first line that is not indented.
    size_t procedure_count = 0;
    for (size_t i = 0; i < parser->token_count; i += 1) {
        Token const *line_start = parse_token_current(parser);
        if (line_start->token_kind != TOKEN_KIND_INDENTATION) {
            break;
        }
        if (line_start->payload.indentation.level == INDENTATION_LEVEL_TOP) {
            break;
        }
        if (line_start->payload.indentation.level != INDENTATION_LEVEL_TOP + 1) {
            // The false return value reports the failure, so the node index is not needed.
            (void)parse_fail(parser, STR("a line that is not indented deeper"));
            return false;
        }
        parse_token_advance(parser);

        Token const *name = parse_token_current(parser);
        if (name->token_kind != TOKEN_KIND_IDENTIFIER) {
            // The false return value reports the failure, so the node index is not needed.
            (void)parse_fail(parser, STR("the name of a foreign procedure"));
            return false;
        }
        Ast_Node_Index definition = parse_node_make(parser, AST_NODE_TYPE_CONSTANT_DEFINITION);
        if (definition == AST_NODE_INDEX_NONE) {
            return false;
        }
        parser->nodes[definition].payload.constant_definition.name = name->payload.identifier.text;
        parse_token_advance(parser);

        // `::` is two separator tokens, one after the other.
        if (parse_token_expect(parser, TOKEN_KIND_TYPE_SEPARATOR, STR("'::'")) == false) {
            return false;
        }
        if (parse_token_expect(parser, TOKEN_KIND_TYPE_SEPARATOR, STR("'::'")) == false) {
            return false;
        }

        Ast_Node_Index procedure = parse_procedure(parser, INDENTATION_LEVEL_TOP + 1, true);
        if (procedure == AST_NODE_INDEX_NONE) {
            return false;
        }
        parse_node_append_child(parser, definition, procedure);
        parse_node_append_child(parser, module, definition);
        procedure_count += 1;
    }

    if (procedure_count == 0) {
        // The false return value reports the failure, so the node index is not needed.
        (void)parse_fail(parser, STR("an indented foreign procedure"));
        return false;
    }
    return true;
}

/**
 * Parses `name #(attribute)... : Type = value` or `name #(attribute)... := value`, a global
 * variable. The attribute list may be left out. The current token is the name, which the caller
 * checked. The node for the variable has the name in its payload. Its children are the attributes,
 * the type when the definition has one, and then the value, which is a literal.
 */
static Ast_Node_Index parse_global_variable_definition(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parse_token_current(parser)->token_kind == TOKEN_KIND_IDENTIFIER);

    Token const *name = parse_token_current(parser);
    Ast_Node_Index definition = parse_node_make(parser, AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION);
    if (definition == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[definition].payload.global_variable_definition.name = name->payload.identifier.text;
    parse_token_advance(parser);

    if (parse_token_current(parser)->token_kind == TOKEN_KIND_HASH) {
        if (parse_attributes(parser, definition) == false) {
            return AST_NODE_INDEX_NONE;
        }
    }

    if (parse_token_expect(parser, TOKEN_KIND_TYPE_SEPARATOR, STR("':' or '#'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    // `:=` has no type, which is then deduced from the value. Otherwise the type comes before `=`.
    if (parse_token_current(parser)->token_kind != TOKEN_KIND_EQUALS) {
        Ast_Node_Index type = parse_type(parser, STR("a type name or '='"));
        if (type == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, definition, type);
    }
    if (parse_token_expect(parser, TOKEN_KIND_EQUALS, STR("'='")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    // The value is stored once, before any code runs, so it is a literal and not an expression.
    // Information about a type is known when the program is compiled, so it is allowed as well.
    Ast_Node_Index value = AST_NODE_INDEX_NONE;
    switch (parse_token_current(parser)->token_kind) {
    case TOKEN_KIND_INTEGER_LITERAL:
        value = parse_integer_literal(parser);
        break;
    case TOKEN_KIND_KEYWORD_FALSE:
    case TOKEN_KIND_KEYWORD_TRUE:
        value = parse_bool_literal(parser);
        break;
    case TOKEN_KIND_STRING_LITERAL:
        value = parse_string_literal(parser);
        break;
    case TOKEN_KIND_KEYWORD_TYPE_INFO_OF:
        value = parse_type_info_of(parser);
        break;
    default:
        return parse_fail(parser, STR("a number, a string, 'true', 'false' or 'type_info_of'"));
    }
    if (value == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, definition, value);

    if (parse_line_end_expect(parser) == false) {
        return AST_NODE_INDEX_NONE;
    }

    assert(parser->nodes[definition].last_child == value);
    return definition;
}

/**
 * Parses an attribute list, `#(first)(second)`, and appends one attribute node to `definition` for
 * each attribute in it. The current token is the `#`. There is at least one attribute, and each is
 * a name between parentheses. Returns false after it recorded an error.
 */
static bool parse_attributes(Parser *parser, Ast_Node_Index definition) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parse_token_current(parser)->token_kind == TOKEN_KIND_HASH);

    parse_token_advance(parser);

    // Every attribute consumes three tokens, so there are fewer attributes than tokens, which
    // bounds the loop.
    for (size_t i = 0; i < parser->token_count; i += 1) {
        if (parse_token_expect(parser, TOKEN_KIND_PARENTHESIS_LEFT, STR("'(' after '#'")) == false) {
            return false;
        }

        Token const *name = parse_token_current(parser);
        if (name->token_kind != TOKEN_KIND_IDENTIFIER) {
            // The false return value reports the failure, so the node index is not needed.
            (void)parse_fail(parser, STR("an attribute name"));
            return false;
        }
        Ast_Node_Index attribute = parse_node_make(parser, AST_NODE_TYPE_ATTRIBUTE);
        if (attribute == AST_NODE_INDEX_NONE) {
            return false;
        }
        parser->nodes[attribute].payload.attribute.name = name->payload.identifier.text;
        parse_token_advance(parser);
        parse_node_append_child(parser, definition, attribute);

        if (parse_token_expect(parser, TOKEN_KIND_PARENTHESIS_RIGHT, STR("')'")) == false) {
            return false;
        }
        if (parse_token_current(parser)->token_kind != TOKEN_KIND_PARENTHESIS_LEFT) {
            break;
        }
    }

    return true;
}

/**
 * Parses a procedure: `proc(parameters) Type ->` and its body. With `is_foreign` the body is the
 * `...` that stands for code which is not in the program, and nothing else is on the line, as in a
 * `foreign js ->` block. Without it the body is the indented statements below.
 */
static Ast_Node_Index parse_procedure(Parser *parser, uint32_t indentation_level,
                                      bool is_foreign) {
    assert(indentation_level < UINT32_MAX);
    assert(parser->status == PARSE_STATUS_OK);

    if (parse_token_expect(parser, TOKEN_KIND_KEYWORD_PROC, STR("'proc'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    Ast_Node_Index procedure = parse_node_make(parser, AST_NODE_TYPE_PROCEDURE);
    if (procedure == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    if (parse_token_expect(parser, TOKEN_KIND_PARENTHESIS_LEFT, STR("'('")) == false) {
        return AST_NODE_INDEX_NONE;
    }
    if (parse_procedure_parameters(parser, procedure) == false) {
        return AST_NODE_INDEX_NONE;
    }
    if (parse_token_expect(parser, TOKEN_KIND_PARENTHESIS_RIGHT, STR("')'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    if (parse_procedure_return_type(parser, procedure) == false) {
        return AST_NODE_INDEX_NONE;
    }

    if (parse_token_expect(parser, TOKEN_KIND_ARROW, STR("'->'")) == false) {
        return AST_NODE_INDEX_NONE;
    }
    if (is_foreign) {
        Ast_Node_Index body = parse_node_make(parser, AST_NODE_TYPE_FOREIGN_BODY);
        if (body == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        if (parse_token_expect(parser, TOKEN_KIND_ELLIPSIS, STR("'...'")) == false) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, procedure, body);
        if (parse_line_end_expect(parser) == false) {
            return AST_NODE_INDEX_NONE;
        }
        return procedure;
    }
    if (parse_procedure_body(parser, procedure, indentation_level) == false) {
        return AST_NODE_INDEX_NONE;
    }

    return procedure;
}

/**
 * Parses the return type of a procedure or of a procedure type, which is optional: a name, or the
 * carets of a pointer type and then a name, as in `Int` and `^Int`. The type is only the source
 * text for now, from its first token to its last, which is why the name is not a node of its own:
 * an array cannot be returned, so a return type is never more than carets before a name, and
 * neither is it a pointer to a procedure. The current token is the one after the `)`. Returns false
 * after it recorded an error.
 */
static bool parse_procedure_return_type(Parser *parser, Ast_Node_Index procedure) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(procedure < parser->node_count);
    assert(parser->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE ||
           parser->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE_TYPE);

    Token const *first = parse_token_current(parser);
    if (first->token_kind != TOKEN_KIND_IDENTIFIER && first->token_kind != TOKEN_KIND_CARET) {
        return true;
    }

    // Every caret consumes a token, so there are fewer carets than tokens, which bounds the loop.
    for (size_t i = 0; i < parser->token_count; i += 1) {
        if (parse_token_current(parser)->token_kind != TOKEN_KIND_CARET) {
            break;
        }
        parse_token_advance(parser);
    }

    Token const *name = parse_token_current(parser);
    if (name->token_kind != TOKEN_KIND_IDENTIFIER) {
        // The false return value reports the failure, so the node index is not needed.
        if (name->token_kind == TOKEN_KIND_KEYWORD_PROC) {
            (void)parse_fail(parser, STR("the name of the return type after '^', because a "
                                         "procedure does not return a pointer to a procedure"));
            return false;
        }
        (void)parse_fail(parser, STR("the name of the return type after '^'"));
        return false;
    }
    parse_token_advance(parser);

    // The tokens do not point at the source, but the name does, and its offset says where the
    // source starts. The text runs from the first token to the end of the name.
    Str name_text = name->payload.identifier.text;
    char const *source_bytes = name_text.bytes - name->source_offset;
    char const *type_bytes = source_bytes + first->source_offset;
    char const *type_end = name_text.bytes + name_text.length;
    assert(type_end > type_bytes);
    parser->nodes[procedure].payload.procedure.return_type =
        str_make(type_bytes, (size_t)(type_end - type_bytes));
    return true;
}

/**
 * Parses the parameters between the parentheses, which can be none, and appends them to
 * `procedure`. Returns false after it recorded an error.
 */
static bool parse_procedure_parameters(Parser *parser, Ast_Node_Index procedure) {
    assert(procedure < parser->node_count);
    assert(parser->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);

    if (parse_token_current(parser)->token_kind == TOKEN_KIND_PARENTHESIS_RIGHT) {
        return true;
    }

    // Every parameter consumes at least its name, so there are fewer parameters than tokens,
    // which bounds the loop. A comma means that another parameter has to follow.
    for (size_t i = 0; i < parser->token_count; i += 1) {
        Ast_Node_Index parameter = parse_parameter(parser);
        if (parameter == AST_NODE_INDEX_NONE) {
            return false;
        }
        parse_node_append_child(parser, procedure, parameter);

        if (parse_token_current(parser)->token_kind != TOKEN_KIND_COMMA) {
            break;
        }
        parse_token_advance(parser);
    }

    return true;
}

/**
 * Parses the statements of the body, which are the lines that are indented one level deeper than
 * the line of the definition, and appends them to `procedure`. A line `if condition ->` or
 * `else ->` is followed by a block, which is the lines that are indented one level deeper still,
 * and the statements of the block are appended to it. The blocks that are open are kept in an array
 * and not on the stack of the calls: a line that is indented less than the innermost block closes
 * it, and as many outer ones as it is indented less than. Returns false after it recorded an
 * error.
 */
static bool parse_procedure_body(Parser *parser, Ast_Node_Index procedure,
                                 uint32_t indentation_level) {
    assert(procedure < parser->node_count);
    assert(indentation_level < UINT32_MAX);

    // The first one is the body of the procedure, and the others are inside each other.
    Parse_Block blocks[AST_BLOCK_DEPTH_MAX + 1];
    blocks[0] = (Parse_Block){.node = procedure, .level = indentation_level + 1};
    size_t block_count = 1;
    // Whether the last line opened a block, which the next line must be the first line of.
    bool is_block_expected = false;

    // Each statement consumes at least its indentation token, so there are fewer statements than
    // tokens, which bounds the loop. It stops at the first line that is not part of the body.
    size_t statement_count = 0;
    for (size_t i = 0; i < parser->token_count; i += 1) {
        Token const *line_start = parse_token_current(parser);
        if (line_start->token_kind != TOKEN_KIND_INDENTATION) {
            break;
        }

        uint32_t level = line_start->payload.indentation.level;
        if (is_block_expected) {
            if (level != blocks[block_count - 1].level) {
                // The false return value reports the failure, so the node index is not needed.
                (void)parse_fail(parser, STR("an indented block"));
                return false;
            }
            is_block_expected = false;
        } else {
            // A line that is indented less than the block that is open ends it.
            while (level < blocks[block_count - 1].level && block_count > 1) {
                parse_block_close(parser, blocks[block_count - 1].node);
                block_count -= 1;
            }
            if (level < blocks[block_count - 1].level) {
                break;
            }
            if (level > blocks[block_count - 1].level) {
                // The false return value reports the failure, so the node index is not needed.
                (void)parse_fail(parser, STR("a line that is not indented deeper"));
                return false;
            }
        }
        parse_token_advance(parser);

        Ast_Node_Index parent = blocks[block_count - 1].node;
        Token_Kind next = parse_token_current(parser)->token_kind;
        if (next == TOKEN_KIND_KEYWORD_IF || next == TOKEN_KIND_KEYWORD_ELSE ||
            next == TOKEN_KIND_KEYWORD_FOR || next == TOKEN_KIND_ARROW ||
            next == TOKEN_KIND_LABEL) {
            Ast_Node_Index block = AST_NODE_INDEX_NONE;
            if (next == TOKEN_KIND_ARROW || next == TOKEN_KIND_LABEL) {
                block = parse_scope_statement(parser, parent);
            } else if (next == TOKEN_KIND_KEYWORD_IF) {
                block = parse_if_statement(parser, parent);
            } else if (next == TOKEN_KIND_KEYWORD_FOR) {
                block = parse_for_statement(parser, parent);
            } else {
                block = parse_else_branch(parser, parent);
            }
            if (block == AST_NODE_INDEX_NONE) {
                return false;
            }
            if (block_count == AST_BLOCK_DEPTH_MAX + 1) {
                // The false return value reports the failure, so the node index is not needed.
                (void)parse_fail(parser, STR("fewer nested blocks"));
                return false;
            }
            blocks[block_count] = (Parse_Block){.node = block, .level = level + 1};
            block_count += 1;
            is_block_expected = true;
        } else {
            Ast_Node_Index statement = parse_statement(parser);
            if (statement == AST_NODE_INDEX_NONE) {
                return false;
            }
            parse_node_append_child(parser, parent, statement);
        }
        statement_count += 1;
    }

    if (is_block_expected) {
        // The false return value reports the failure, so the node index is not needed.
        (void)parse_fail(parser, STR("an indented block"));
        return false;
    }
    if (statement_count == 0) {
        // The false return value reports the failure, so the node index is not needed.
        (void)parse_fail(parser, STR("an indented procedure body"));
        return false;
    }

    // The body ends the blocks that are still open, innermost first.
    for (size_t open = block_count; open > 1; open -= 1) {
        parse_block_close(parser, blocks[open - 1].node);
    }

    return true;
}

/**
 * Closes `block`: the nodes that were made since it was are the ones in it, so the next node that
 * is made is the end of it. The analysis and the targets tell what scope a variable is in by it.
 */
static void parse_block_close(Parser *parser, Ast_Node_Index block) {
    assert(block < parser->node_count);
    assert(parser->nodes[block].node_type == AST_NODE_TYPE_BLOCK);
    assert(parser->node_count < AST_NODE_INDEX_NONE);

    parser->nodes[block].payload.block.end = (Ast_Node_Index)parser->node_count;
}

/**
 * Parses the `->` that ends the header line of `statement`, and the end of the line, and appends a
 * new empty block to the statement. Returns the block, so that the caller can fill it with the
 * lines that follow.
 */
static Ast_Node_Index parse_block_open(Parser *parser, Ast_Node_Index statement) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(statement < parser->node_count);

    if (parse_token_expect(parser, TOKEN_KIND_ARROW, STR("'->'")) == false) {
        return AST_NODE_INDEX_NONE;
    }
    if (parse_line_end_expect(parser) == false) {
        return AST_NODE_INDEX_NONE;
    }

    Ast_Node_Index block = parse_node_make(parser, AST_NODE_TYPE_BLOCK);
    if (block == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, statement, block);
    return block;
}

/**
 * Parses the header line `if condition ->`, and appends the statement that it starts to `parent`.
 * The statement gets the condition and an empty block, which is returned so that the caller can
 * fill it with the lines that follow. The `else if` and `else` lines that follow are added to the
 * statement by `parse_else_branch`.
 */
static Ast_Node_Index parse_if_statement(Parser *parser, Ast_Node_Index parent) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parse_token_current(parser)->token_kind == TOKEN_KIND_KEYWORD_IF);

    parse_token_advance(parser);

    Ast_Node_Index statement = parse_node_make(parser, AST_NODE_TYPE_IF_STATEMENT);
    if (statement == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, parent, statement);

    return parse_branch(parser, statement);
}

/**
 * Parses the header line `for ->` of a loop without an end, or `for name in first..<end ->`, or
 * `first..<=end` for a range that includes its end, or `for name in elements ->` for the elements
 * of an array or an array slice, and appends the statement that it starts to `parent`. The
 * statement gets a range, or the expression of the elements, and an empty block, which is returned
 * so that the caller can fill it with the lines that follow. The nodes are made in the order of the
 * source, so the iterator is not in scope in the range, only in the block.
 */
static Ast_Node_Index parse_for_statement(Parser *parser, Ast_Node_Index parent) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parse_token_current(parser)->token_kind == TOKEN_KIND_KEYWORD_FOR);

    parse_token_advance(parser);

    // `for ->` has no iterator and no range: the block runs until it is left.
    if (parse_token_current(parser)->token_kind == TOKEN_KIND_ARROW) {
        Ast_Node_Index loop = parse_node_make(parser, AST_NODE_TYPE_LOOP_STATEMENT);
        if (loop == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, parent, loop);
        return parse_block_open(parser, loop);
    }

    Token const *iterator = parse_token_current(parser);
    if (iterator->token_kind != TOKEN_KIND_IDENTIFIER) {
        return parse_fail(parser, STR("the name of the iterator"));
    }

    Ast_Node_Index statement = parse_node_make(parser, AST_NODE_TYPE_FOR_STATEMENT);
    if (statement == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[statement].payload.for_statement.name = iterator->payload.identifier.text;
    parse_node_append_child(parser, parent, statement);
    parse_token_advance(parser);

    if (parse_token_expect(parser, TOKEN_KIND_KEYWORD_IN, STR("'in'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    // Without a range operator in the header, the expression is the elements to go through.
    if (parse_for_statement_has_range(parser) == false) {
        Ast_Node_Index elements = parse_expression(parser);
        if (elements == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, statement, elements);
        return parse_block_open(parser, statement);
    }

    Ast_Node_Index range = parse_node_make(parser, AST_NODE_TYPE_RANGE);
    if (range == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, statement, range);

    Ast_Node_Index first = parse_expression(parser);
    if (first == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, range, first);

    Token_Kind range_operator = parse_token_current(parser)->token_kind;
    if (range_operator != TOKEN_KIND_RANGE_EXCLUSIVE &&
        range_operator != TOKEN_KIND_RANGE_INCLUSIVE) {
        return parse_fail(parser, STR("'..<' or '..<='"));
    }
    parser->nodes[range].payload.range.is_inclusive = range_operator == TOKEN_KIND_RANGE_INCLUSIVE;
    parse_token_advance(parser);

    Ast_Node_Index end = parse_expression(parser);
    if (end == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, range, end);

    return parse_block_open(parser, statement);
}

/**
 * Returns whether the header line of a for statement has a range operator, `..<` or `..<=`, from
 * the current token to the `->` that ends the header. The two kinds of for statement start the
 * same way, and the range has to be made before the expression that is its first number, so it has
 * to be known before that expression is parsed.
 */
static bool parse_for_statement_has_range(Parser const *parser) {
    assert(parser->token_index < parser->token_count);

    // The last token is `END`, which ends the line as well, so the loop stops at it.
    for (size_t i = parser->token_index; i < parser->token_count; i += 1) {
        Token_Kind token_kind = parser->tokens[i].token_kind;
        if (token_kind == TOKEN_KIND_RANGE_EXCLUSIVE || token_kind == TOKEN_KIND_RANGE_INCLUSIVE) {
            return true;
        }
        if (token_kind == TOKEN_KIND_ARROW || token_kind == TOKEN_KIND_INDENTATION ||
            token_kind == TOKEN_KIND_END) {
            return false;
        }
    }

    // Unreachable: the last token is `END`.
    assert(false);
    return false;
}

/**
 * Parses the header line of a scope statement, `->` or `@label ->`, and appends the statement that
 * it starts to `parent`. The statement gets the label, if there is one, and an empty block, which
 * is returned so that the caller can fill it with the lines that follow. A label that is followed
 * by `if` or `for` instead of `->` is the label of that statement: it gets a labeled statement node
 * that holds the label and the statement, and the block returned is the block of the statement.
 */
static Ast_Node_Index parse_scope_statement(Parser *parser, Ast_Node_Index parent) {
    assert(parser->status == PARSE_STATUS_OK);

    Token const *label = parse_token_current(parser);
    assert(label->token_kind == TOKEN_KIND_ARROW || label->token_kind == TOKEN_KIND_LABEL);

    if (label->token_kind == TOKEN_KIND_LABEL) {
        // The label is not the last token, because the last one is `END`.
        assert(parser->token_index + 1 < parser->token_count);
        Token_Kind after_label = parser->tokens[parser->token_index + 1].token_kind;
        if (after_label == TOKEN_KIND_KEYWORD_IF || after_label == TOKEN_KIND_KEYWORD_FOR) {
            Ast_Node_Index labeled = parse_node_make(parser, AST_NODE_TYPE_LABELED_STATEMENT);
            if (labeled == AST_NODE_INDEX_NONE) {
                return AST_NODE_INDEX_NONE;
            }
            parser->nodes[labeled].payload.label.name = label->payload.identifier.text;
            parse_node_append_child(parser, parent, labeled);
            parse_token_advance(parser);
            if (after_label == TOKEN_KIND_KEYWORD_IF) {
                return parse_if_statement(parser, labeled);
            }
            return parse_for_statement(parser, labeled);
        }
    }

    Ast_Node_Index statement = parse_node_make(parser, AST_NODE_TYPE_SCOPE_STATEMENT);
    if (statement == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, parent, statement);

    if (label->token_kind == TOKEN_KIND_LABEL) {
        Ast_Node_Index label_node = parse_node_make(parser, AST_NODE_TYPE_LABEL);
        if (label_node == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parser->nodes[label_node].payload.label.name = label->payload.identifier.text;
        parse_node_append_child(parser, statement, label_node);
        parse_token_advance(parser);
    }

    return parse_block_open(parser, statement);
}

/**
 * Parses `break` or `continue` and the end of the line. A `break` may name the scope to leave
 * with the name of its label, without the `@`, which becomes its child. The node keeps the word so
 * that the analysis can point at it. Whether the jump is inside a loop or a scope, and whether the
 * label exists, is for the analysis.
 */
static Ast_Node_Index parse_jump_statement(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);

    Token const *keyword = parse_token_current(parser);
    assert(keyword->token_kind == TOKEN_KIND_KEYWORD_BREAK ||
           keyword->token_kind == TOKEN_KIND_KEYWORD_CONTINUE);

    Ast_Node_Type node_type = AST_NODE_TYPE_BREAK_STATEMENT;
    if (keyword->token_kind == TOKEN_KIND_KEYWORD_CONTINUE) {
        node_type = AST_NODE_TYPE_CONTINUE_STATEMENT;
    }
    Ast_Node_Index jump = parse_node_make(parser, node_type);
    if (jump == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[jump].payload.jump.keyword = keyword->payload.identifier.text;
    parse_token_advance(parser);

    Token const *name = parse_token_current(parser);
    if (name->token_kind == TOKEN_KIND_IDENTIFIER) {
        Ast_Node_Index label = parse_node_make(parser, AST_NODE_TYPE_LABEL);
        if (label == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parser->nodes[label].payload.label.name = name->payload.identifier.text;
        parse_node_append_child(parser, jump, label);
        parse_token_advance(parser);
    } else if (name->token_kind == TOKEN_KIND_LABEL) {
        // The prefix is only for declaring a label.
        return parse_fail(parser, STR("the name of a label without '@'"));
    }

    if (parse_line_end_expect(parser) == false) {
        return AST_NODE_INDEX_NONE;
    }
    return jump;
}

/**
 * Parses the header line that starts with `else`, which is `else ->` or `else if condition ->`, and
 * adds the branch to the if statement that is the last statement of `parent`. That statement must
 * not have an `else` already. Returns the empty block of the branch, like `parse_if_statement`.
 */
static Ast_Node_Index parse_else_branch(Parser *parser, Ast_Node_Index parent) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parse_token_current(parser)->token_kind == TOKEN_KIND_KEYWORD_ELSE);

    Ast_Node_Index statement = parser->nodes[parent].last_child;
    // An if statement with a label is inside a labeled statement.
    if (statement != AST_NODE_INDEX_NONE &&
        parser->nodes[statement].node_type == AST_NODE_TYPE_LABELED_STATEMENT) {
        statement = parser->nodes[statement].last_child;
    }
    bool is_open = statement != AST_NODE_INDEX_NONE &&
                   parser->nodes[statement].node_type == AST_NODE_TYPE_IF_STATEMENT &&
                   parser->nodes[statement].payload.if_statement.has_else == false;
    if (is_open == false) {
        return parse_fail(parser, STR("an 'if' or 'else if' before 'else'"));
    }
    parse_token_advance(parser);

    if (parse_token_current(parser)->token_kind == TOKEN_KIND_KEYWORD_IF) {
        parse_token_advance(parser);
        return parse_branch(parser, statement);
    }

    parser->nodes[statement].payload.if_statement.has_else = true;
    return parse_block_open(parser, statement);
}

/**
 * Parses `condition ->` and the end of the line, and appends the condition and a new empty block
 * to the if statement `statement`. Returns the block.
 */
static Ast_Node_Index parse_branch(Parser *parser, Ast_Node_Index statement) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->nodes[statement].node_type == AST_NODE_TYPE_IF_STATEMENT);

    Ast_Node_Index condition = parse_expression(parser);
    if (condition == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, statement, condition);

    return parse_block_open(parser, statement);
}

/**
 * Parses an expression, which is the value of a variable or of a `return`, as well as the
 * condition of an `if`. The operators from the loosest to the tightest are `or`, `and`, `not`,
 * a comparison, `+` and `-`, and `*` and `/`, and there is a function for each level.
 */
static Ast_Node_Index parse_expression(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    return parse_disjunction(parser);
}

/**
 * Parses conjunctions separated by `or`, the operator with the lowest precedence. It is left
 * associative, so `a or b or c` is `(a or b) or c`.
 */
static Ast_Node_Index parse_disjunction(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Ast_Node_Index left = parse_conjunction(parser);
    if (left == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    // Every operator consumes a token, so there are fewer operators than tokens, which bounds
    // the loop.
    for (size_t i = 0; i < parser->token_count; i += 1) {
        if (parse_token_current(parser)->token_kind != TOKEN_KIND_KEYWORD_OR) {
            break;
        }
        parse_token_advance(parser);

        Ast_Node_Index right = parse_conjunction(parser);
        if (right == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }

        Ast_Node_Index disjunction = parse_node_make(parser, AST_NODE_TYPE_OR);
        if (disjunction == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, disjunction, left);
        parse_node_append_child(parser, disjunction, right);
        left = disjunction;
    }

    return left;
}

/**
 * Parses negations separated by `and`, which binds tighter than `or`. It is left associative.
 */
static Ast_Node_Index parse_conjunction(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Ast_Node_Index left = parse_negation(parser);
    if (left == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    // Every operator consumes a token, so there are fewer operators than tokens, which bounds
    // the loop.
    for (size_t i = 0; i < parser->token_count; i += 1) {
        if (parse_token_current(parser)->token_kind != TOKEN_KIND_KEYWORD_AND) {
            break;
        }
        parse_token_advance(parser);

        Ast_Node_Index right = parse_negation(parser);
        if (right == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }

        Ast_Node_Index conjunction = parse_node_make(parser, AST_NODE_TYPE_AND);
        if (conjunction == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, conjunction, left);
        parse_node_append_child(parser, conjunction, right);
        left = conjunction;
    }

    return left;
}

/**
 * Parses a comparison with any number of `not` before it. `not` binds looser than a comparison
 * and tighter than `and`, so `not a == b` is `not (a == b)`. The `not` keywords are counted and
 * the nodes are made after the operand, so that the parser does not call itself.
 */
static Ast_Node_Index parse_negation(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    size_t negation_count = 0;
    // Every `not` consumes a token, so there are fewer of them than tokens, which bounds the loop.
    for (size_t i = 0; i < parser->token_count; i += 1) {
        if (parse_token_current(parser)->token_kind != TOKEN_KIND_KEYWORD_NOT) {
            break;
        }
        parse_token_advance(parser);
        negation_count += 1;
    }

    Ast_Node_Index operand = parse_comparison(parser);
    if (operand == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    for (size_t i = 0; i < negation_count; i += 1) {
        Ast_Node_Index negation = parse_node_make(parser, AST_NODE_TYPE_NOT);
        if (negation == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, negation, operand);
        operand = negation;
    }

    return operand;
}

/**
 * Parses a sum, optionally followed by a comparison operator and another sum, such as
 * `x + 1 > y`. A comparison does not chain, so in `a < b < c` the second operator is left for the
 * caller, which expects the end of the line or `->` there. The result of a comparison is a
 * `Bool`.
 */
static Ast_Node_Index parse_comparison(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Ast_Node_Index left = parse_sum(parser);
    if (left == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    Ast_Comparison_Operator operator = AST_COMPARISON_OPERATOR_EQUAL;
    Token_Kind operator_token = parse_token_current(parser)->token_kind;
    if (operator_token == TOKEN_KIND_GREATER) {
        operator = AST_COMPARISON_OPERATOR_GREATER;
    } else if (operator_token == TOKEN_KIND_GREATER_EQUALS) {
        operator = AST_COMPARISON_OPERATOR_GREATER_EQUAL;
    } else if (operator_token == TOKEN_KIND_LESS) {
        operator = AST_COMPARISON_OPERATOR_LESS;
    } else if (operator_token == TOKEN_KIND_LESS_EQUALS) {
        operator = AST_COMPARISON_OPERATOR_LESS_EQUAL;
    } else if (operator_token == TOKEN_KIND_NOT_EQUALS) {
        operator = AST_COMPARISON_OPERATOR_NOT_EQUAL;
    } else if (operator_token != TOKEN_KIND_EQUALS_EQUALS) {
        // Not a comparison, so the expression is the sum alone.
        return left;
    }
    parse_token_advance(parser);

    Ast_Node_Index right = parse_sum(parser);
    if (right == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    Ast_Node_Index comparison = parse_node_make(parser, AST_NODE_TYPE_COMPARISON);
    if (comparison == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[comparison].payload.comparison.operator = operator;
    parse_node_append_child(parser, comparison, left);
    parse_node_append_child(parser, comparison, right);

    return comparison;
}

/**
 * Parses `name: Type`. The node for the parameter has the name in its payload and one child, the
 * type, which is a type name or an array type.
 */
static Ast_Node_Index parse_parameter(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Token const *name = parse_token_current(parser);
    if (name->token_kind != TOKEN_KIND_IDENTIFIER) {
        return parse_fail(parser, STR("a parameter name"));
    }

    Ast_Node_Index parameter = parse_node_make(parser, AST_NODE_TYPE_PARAMETER);
    if (parameter == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[parameter].payload.parameter.name = name->payload.identifier.text;
    parse_token_advance(parser);

    if (parse_token_expect(parser, TOKEN_KIND_TYPE_SEPARATOR, STR("':'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    Ast_Node_Index type = parse_type(parser, STR("a type name"));
    if (type == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, parameter, type);

    assert(parser->nodes[parameter].first_child == type);
    return parameter;
}

/**
 * Parses one statement of a body: a `return`, or a statement that starts with a name, which is a
 * variable definition when a `:` follows the name and an assignment otherwise.
 */
static Ast_Node_Index parse_statement(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Token_Kind next = parse_token_current(parser)->token_kind;
    if (next == TOKEN_KIND_KEYWORD_RETURN) {
        return parse_return_statement(parser);
    }
    if (next == TOKEN_KIND_KEYWORD_BREAK || next == TOKEN_KIND_KEYWORD_CONTINUE) {
        return parse_jump_statement(parser);
    }
    if (next == TOKEN_KIND_IDENTIFIER) {
        // The name is not the last token, because the last one is `END`.
        assert(parser->token_index + 1 < parser->token_count);
        Token_Kind after_name = parser->tokens[parser->token_index + 1].token_kind;
        if (after_name == TOKEN_KIND_TYPE_SEPARATOR) {
            return parse_variable_definition(parser);
        }
        if (after_name == TOKEN_KIND_PARENTHESIS_LEFT) {
            return parse_call_statement(parser);
        }
        return parse_assignment(parser);
    }

    return parse_fail(parser, STR("a statement"));
}

/**
 * Parses `name += value`, or the same with another assignment operator, or `name = value`. The
 * node for the assignment has two children: the target and the value. The target is the name, as an
 * identifier, or an element of an array or a field of a struct, which has subscripts and field
 * names after the name. The node is made before the name, like the node of every statement, so
 * that the nodes of a statement come after it in the order of the source, which the analysis
 * relies on. A target that is followed by anything else than an assignment operator is the error
 * here.
 */
static Ast_Node_Index parse_assignment(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index + 1 < parser->token_count);

    // The kind of assignment depends on the operator after the target, which is only known once
    // the target is parsed, so the node gets its type below.
    Ast_Node_Index assignment = parse_node_make(parser, AST_NODE_TYPE_ADDITION_ASSIGNMENT);
    if (assignment == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    Ast_Node_Index target = parse_identifier(parser);
    if (target == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    target = parse_subscripts(parser, target);
    if (target == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, assignment, target);

    // The error for anything else is at the token after the target.
    Token_Kind operator = parse_token_current(parser)->token_kind;
    Ast_Node_Type node_type = AST_NODE_TYPE_ADDITION_ASSIGNMENT;
    if (operator == TOKEN_KIND_SLASH_EQUALS) {
        node_type = AST_NODE_TYPE_DIVISION_ASSIGNMENT;
    } else if (operator == TOKEN_KIND_ASTERISK_EQUALS) {
        node_type = AST_NODE_TYPE_MULTIPLICATION_ASSIGNMENT;
    } else if (operator == TOKEN_KIND_MINUS_EQUALS) {
        node_type = AST_NODE_TYPE_SUBTRACTION_ASSIGNMENT;
    } else if (operator == TOKEN_KIND_EQUALS) {
        node_type = AST_NODE_TYPE_ASSIGNMENT;
    } else if (operator != TOKEN_KIND_PLUS_EQUALS) {
        return parse_fail(parser, STR("':' or an assignment operator"));
    }
    parser->nodes[assignment].node_type = node_type;
    parse_token_advance(parser);

    Ast_Node_Index value = parse_expression(parser);
    if (value == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, assignment, value);

    if (parse_line_end_expect(parser) == false) {
        return AST_NODE_INDEX_NONE;
    }

    assert(parser->nodes[assignment].first_child == target);
    assert(parser->nodes[assignment].last_child == value);
    return assignment;
}

/**
 * Parses `name := value` or `name : Type = value`. The node for the variable has the name in its
 * payload. Its children are the type, when the definition has one, and then the value.
 */
static Ast_Node_Index parse_variable_definition(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Token const *name = parse_token_current(parser);
    if (name->token_kind != TOKEN_KIND_IDENTIFIER) {
        return parse_fail(parser, STR("a variable name"));
    }

    Ast_Node_Index definition = parse_node_make(parser, AST_NODE_TYPE_VARIABLE_DEFINITION);
    if (definition == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[definition].payload.variable_definition.name = name->payload.identifier.text;
    parse_token_advance(parser);

    if (parse_token_expect(parser, TOKEN_KIND_TYPE_SEPARATOR, STR("':'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    // `:=` has no type, which is then deduced from the value. Otherwise the type comes before `=`.
    bool has_type = false;
    if (parse_token_current(parser)->token_kind != TOKEN_KIND_EQUALS) {
        Ast_Node_Index type = parse_type(parser, STR("a type name or '='"));
        if (type == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, definition, type);
        has_type = true;
    }

    // A variable with a type can leave its value out, as in `p : Person`. It then gets the default
    // value of its type, so the type is the only child.
    Token_Kind after_type = parse_token_current(parser)->token_kind;
    if (has_type && (after_type == TOKEN_KIND_INDENTATION || after_type == TOKEN_KIND_END)) {
        return definition;
    }
    Str expected = STR("'='");
    if (has_type) {
        expected = STR("'=' or the end of the line");
    }
    if (parse_token_expect(parser, TOKEN_KIND_EQUALS, expected) == false) {
        return AST_NODE_INDEX_NONE;
    }

    Ast_Node_Index value = parse_expression(parser);
    if (value == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, definition, value);

    if (parse_line_end_expect(parser) == false) {
        return AST_NODE_INDEX_NONE;
    }

    assert(parser->nodes[definition].last_child == value);
    return definition;
}

static Ast_Node_Index parse_return_statement(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    if (parse_token_expect(parser, TOKEN_KIND_KEYWORD_RETURN, STR("'return'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    Ast_Node_Index statement = parse_node_make(parser, AST_NODE_TYPE_RETURN_STATEMENT);
    if (statement == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    Ast_Node_Index value = parse_expression(parser);
    if (value == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, statement, value);

    if (parse_line_end_expect(parser) == false) {
        return AST_NODE_INDEX_NONE;
    }

    assert(parser->nodes[statement].first_child == value);
    return statement;
}

/**
 * Checks that the line ends at the current token. There is no end-of-line token, so the next
 * line, or the end, must follow directly. Records an error and returns false when it does not.
 */
static bool parse_line_end_expect(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Token_Kind next = parse_token_current(parser)->token_kind;
    bool is_line_end = next == TOKEN_KIND_INDENTATION || next == TOKEN_KIND_END;
    if (is_line_end == false) {
        // The false return value reports the failure, so the node index is not needed.
        (void)parse_fail(parser, STR("the end of the line"));
        return false;
    }

    return true;
}

/**
 * Parses terms separated by `+` or `-`, which have the same precedence. They are left associative,
 * so `a - b + c` is `(a - b) + c`: each operator makes a node that takes the expression so far as
 * its left operand. The operands of these operators are terms, see `parse_term`, so the operators
 * of a term bind tighter: `a + b * c` is `a + (b * c)`. The parser does not call itself, because
 * there are no parentheses yet.
 */
static Ast_Node_Index parse_sum(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Ast_Node_Index left = parse_term(parser);
    if (left == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    // Every operator consumes a token, so there are fewer operators than tokens, which bounds
    // the loop.
    for (size_t i = 0; i < parser->token_count; i += 1) {
        Token_Kind operator = parse_token_current(parser)->token_kind;
        if (operator != TOKEN_KIND_PLUS && operator != TOKEN_KIND_MINUS) {
            break;
        }
        parse_token_advance(parser);

        Ast_Node_Index right = parse_term(parser);
        if (right == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }

        Ast_Node_Type node_type = AST_NODE_TYPE_ADDITION;
        if (operator == TOKEN_KIND_MINUS) {
            node_type = AST_NODE_TYPE_SUBTRACTION;
        }
        Ast_Node_Index arithmetic = parse_node_make(parser, node_type);
        if (arithmetic == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, arithmetic, left);
        parse_node_append_child(parser, arithmetic, right);
        left = arithmetic;
    }

    return left;
}

/**
 * Parses operands separated by `*` or `/`, which have the same precedence, a higher one than `+`
 * and `-`. Like them, they are left associative, so `a / b * c` is `(a / b) * c`.
 */
static Ast_Node_Index parse_term(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Ast_Node_Index left = parse_operand(parser);
    if (left == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    // Every operator consumes a token, so there are fewer operators than tokens, which bounds
    // the loop.
    for (size_t i = 0; i < parser->token_count; i += 1) {
        Token_Kind operator = parse_token_current(parser)->token_kind;
        if (operator != TOKEN_KIND_ASTERISK && operator != TOKEN_KIND_SLASH) {
            break;
        }
        parse_token_advance(parser);

        Ast_Node_Index right = parse_operand(parser);
        if (right == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }

        Ast_Node_Type node_type = AST_NODE_TYPE_MULTIPLICATION;
        if (operator == TOKEN_KIND_SLASH) {
            node_type = AST_NODE_TYPE_DIVISION;
        }
        Ast_Node_Index arithmetic = parse_node_make(parser, node_type);
        if (arithmetic == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, arithmetic, left);
        parse_node_append_child(parser, arithmetic, right);
        left = arithmetic;
    }

    return left;
}

/**
 * Parses a call, `name(argument, argument)`, where the arguments are expressions separated by
 * commas, and there may be none. The node of the call is made before the nodes of its arguments.
 * A call is an operand, so it binds tighter than any operator: `f(x) + 1` is `(f(x)) + 1`.
 */
static Ast_Node_Index parse_call(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index + 1 < parser->token_count);

    Token const *name = parse_token_current(parser);
    if (name->token_kind != TOKEN_KIND_IDENTIFIER) {
        return parse_fail(parser, STR("a name"));
    }

    Ast_Node_Index call = parse_node_make(parser, AST_NODE_TYPE_CALL);
    if (call == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[call].payload.call.name = name->payload.identifier.text;
    parse_token_advance(parser);

    if (parse_token_expect(parser, TOKEN_KIND_PARENTHESIS_LEFT, STR("'('")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    if (parse_token_current(parser)->token_kind != TOKEN_KIND_PARENTHESIS_RIGHT) {
        // Every argument consumes a token, so there are fewer arguments than tokens, which bounds
        // the loop.
        for (size_t i = 0; i < parser->token_count; i += 1) {
            Ast_Node_Index argument = parse_expression(parser);
            if (argument == AST_NODE_INDEX_NONE) {
                return AST_NODE_INDEX_NONE;
            }
            parse_node_append_child(parser, call, argument);

            if (parse_token_current(parser)->token_kind != TOKEN_KIND_COMMA) {
                break;
            }
            parse_token_advance(parser);
        }
    }

    if (parse_token_expect(parser, TOKEN_KIND_PARENTHESIS_RIGHT, STR("',' or ')'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    return call;
}

/**
 * Parses a line that is a call, such as `greet(name)`, whose value, if the procedure has one, is
 * not used. The node of the statement is made before the nodes of the call, like the node of every
 * statement.
 */
static Ast_Node_Index parse_call_statement(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);

    Ast_Node_Index statement = parse_node_make(parser, AST_NODE_TYPE_CALL_STATEMENT);
    if (statement == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    Ast_Node_Index call = parse_call(parser);
    if (call == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, statement, call);

    return statement;
}

/**
 * Parses an operand with any number of `%` before it, which take its address. `%` binds tighter
 * than every operator, but looser than what follows a name, so `%p.x` is the address of the field
 * `p.x` and `%a[2]` the address of the element `a[2]`. Like `not`, the `%` are counted and the
 * nodes are made after the operand, so that the parser does not call itself. Whether the operand
 * has an address, which `%%x` and `%f(x)` do not, is for the target to tell.
 */
static Ast_Node_Index parse_operand(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    size_t address_count = 0;
    // Every `%` consumes a token, so there are fewer of them than tokens, which bounds the loop.
    for (size_t i = 0; i < parser->token_count; i += 1) {
        if (parse_token_current(parser)->token_kind != TOKEN_KIND_PERCENT) {
            break;
        }
        parse_token_advance(parser);
        address_count += 1;
    }

    Ast_Node_Index operand = parse_operand_value(parser);
    if (operand == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    for (size_t i = 0; i < address_count; i += 1) {
        Ast_Node_Index address = parse_node_make(parser, AST_NODE_TYPE_ADDRESS_OF);
        if (address == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, address, operand);
        operand = address;
    }

    return operand;
}

/**
 * Parses a value: an integer literal, a string literal, `true` or `false`, the name of
 * something, such as a parameter, or a call of a procedure, or an array literal. The name and the
 * array literal can have subscripts after them.
 */
static Ast_Node_Index parse_operand_value(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Token_Kind next = parse_token_current(parser)->token_kind;
    if (next == TOKEN_KIND_INTEGER_LITERAL) {
        return parse_integer_literal(parser);
    }
    if (next == TOKEN_KIND_IDENTIFIER) {
        // The name is not the last token, because the last one is `END`.
        assert(parser->token_index + 1 < parser->token_count);
        if (parser->tokens[parser->token_index + 1].token_kind == TOKEN_KIND_PARENTHESIS_LEFT) {
            return parse_call(parser);
        }
        // A name directly followed by a brace starts a struct literal, such as `Person { ... }`.
        if (parser->tokens[parser->token_index + 1].token_kind == TOKEN_KIND_BRACE_LEFT) {
            Ast_Node_Index literal = parse_struct_literal(parser);
            if (literal == AST_NODE_INDEX_NONE) {
                return AST_NODE_INDEX_NONE;
            }
            return parse_subscripts(parser, literal);
        }
        Ast_Node_Index identifier = parse_identifier(parser);
        if (identifier == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        return parse_subscripts(parser, identifier);
    }
    if (next == TOKEN_KIND_BRACKET_LEFT) {
        Ast_Node_Index literal = parse_array_literal(parser);
        if (literal == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        return parse_subscripts(parser, literal);
    }
    if (next == TOKEN_KIND_STRING_LITERAL) {
        return parse_string_literal(parser);
    }
    if (next == TOKEN_KIND_KEYWORD_TRUE || next == TOKEN_KIND_KEYWORD_FALSE) {
        return parse_bool_literal(parser);
    }
    if (next == TOKEN_KIND_DOT) {
        // `.RED` is the member of an enum that the place of the literal knows.
        parse_token_advance(parser);
        Token const *member_name = parse_token_current(parser);
        if (member_name->token_kind != TOKEN_KIND_IDENTIFIER) {
            return parse_fail(parser, STR("the name of a member of an enum after '.'"));
        }
        Ast_Node_Index literal = parse_node_make(parser, AST_NODE_TYPE_ENUM_LITERAL);
        if (literal == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parser->nodes[literal].payload.enum_literal.name = member_name->payload.identifier.text;
        parse_token_advance(parser);
        return literal;
    }
    if (next == TOKEN_KIND_KEYWORD_TYPE_INFO_OF) {
        return parse_type_info_of(parser);
    }
    if (next == TOKEN_KIND_KEYWORD_NIL) {
        Ast_Node_Index nil = parse_node_make(parser, AST_NODE_TYPE_NIL_LITERAL);
        if (nil == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_token_advance(parser);
        return nil;
    }

    return parse_fail(parser, STR("an integer literal, a string literal, 'true', 'false', 'nil', "
                                  "a name, a call, an array literal, a struct literal, "
                                  "'type_info_of' or a member of an enum such as '.RED'"));
}

/**
 * Parses `type_info_of(Type)`, the information about a type, and the subscripts that follow it,
 * such as the `.members` of `type_info_of(Color).members`. The node has the type as its child.
 * Returns the node of the last subscript, or the node itself when there is none.
 */
static Ast_Node_Index parse_type_info_of(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parse_token_current(parser)->token_kind == TOKEN_KIND_KEYWORD_TYPE_INFO_OF);

    Ast_Node_Index info = parse_node_make(parser, AST_NODE_TYPE_TYPE_INFO_OF);
    if (info == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_token_advance(parser);
    parser->has_type_info = true;

    if (parse_token_expect(parser, TOKEN_KIND_PARENTHESIS_LEFT, STR("'('")) == false) {
        return AST_NODE_INDEX_NONE;
    }
    Ast_Node_Index type = parse_type_name(parser, STR("the name of a type"), false);
    if (type == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, info, type);
    if (parse_token_expect(parser, TOKEN_KIND_PARENTHESIS_RIGHT, STR("')'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    return parse_subscripts(parser, info);
}

/**
 * Parses the subscripts that follow `operand`, which is the node of a name or of a literal:
 * `[index]` is the element at an index, `[first:end]` is the array slice of the elements from the
 * first index up to, and not including, the end, `.name` is a field of a struct, and `^` is what a
 * pointer points to. They can follow each other, and each one takes everything before it as its
 * first child. Like the
 * operator of an arithmetic operation, the node of a subscript is made after the nodes of its
 * operands. Returns the node of the last subscript, or `operand` when there is none.
 */
static Ast_Node_Index parse_subscripts(Parser *parser, Ast_Node_Index operand) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(operand < parser->node_count);

    Ast_Node_Index base = operand;
    // Every subscript consumes a token, so there are fewer subscripts than tokens, which bounds
    // the loop.
    for (size_t i = 0; i < parser->token_count; i += 1) {
        Token_Kind next = parse_token_current(parser)->token_kind;
        if (next == TOKEN_KIND_CARET) {
            // `p^` follows the pointer `p`. It can follow itself, as in `pp^^`.
            Ast_Node_Index dereference = parse_node_make(parser, AST_NODE_TYPE_DEREFERENCE);
            if (dereference == AST_NODE_INDEX_NONE) {
                return AST_NODE_INDEX_NONE;
            }
            parse_token_advance(parser);
            parse_node_append_child(parser, dereference, base);
            base = dereference;
            continue;
        }
        if (next == TOKEN_KIND_DOT) {
            parse_token_advance(parser);
            Token const *field_name = parse_token_current(parser);
            if (field_name->token_kind != TOKEN_KIND_IDENTIFIER) {
                return parse_fail(parser, STR("a field name"));
            }
            Ast_Node_Index access = parse_node_make(parser, AST_NODE_TYPE_FIELD_ACCESS);
            if (access == AST_NODE_INDEX_NONE) {
                return AST_NODE_INDEX_NONE;
            }
            parser->nodes[access].payload.field_access.name = field_name->payload.identifier.text;
            parse_token_advance(parser);
            parse_node_append_child(parser, access, base);
            base = access;
            continue;
        }
        if (next != TOKEN_KIND_BRACKET_LEFT) {
            break;
        }
        parse_token_advance(parser);

        Ast_Node_Index first = parse_expression(parser);
        if (first == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }

        Ast_Node_Index end = AST_NODE_INDEX_NONE;
        if (parse_token_current(parser)->token_kind == TOKEN_KIND_TYPE_SEPARATOR) {
            parse_token_advance(parser);
            end = parse_expression(parser);
            if (end == AST_NODE_INDEX_NONE) {
                return AST_NODE_INDEX_NONE;
            }
            if (parse_token_expect(parser, TOKEN_KIND_BRACKET_RIGHT, STR("']'")) == false) {
                return AST_NODE_INDEX_NONE;
            }
        } else if (parse_token_expect(parser, TOKEN_KIND_BRACKET_RIGHT, STR("':' or ']'")) ==
                   false) {
            return AST_NODE_INDEX_NONE;
        }

        Ast_Node_Type node_type = AST_NODE_TYPE_SUBSCRIPT;
        if (end != AST_NODE_INDEX_NONE) {
            node_type = AST_NODE_TYPE_SLICE;
        }
        Ast_Node_Index subscript = parse_node_make(parser, node_type);
        if (subscript == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, subscript, base);
        parse_node_append_child(parser, subscript, first);
        if (end != AST_NODE_INDEX_NONE) {
            parse_node_append_child(parser, subscript, end);
        }
        base = subscript;
    }

    return base;
}

/**
 * Parses an array literal, `[const]Int { 1, 2, 3 }` or `[3]Int { 1, 2, 3 }`: the type of the array
 * and the elements between braces, which are expressions separated by commas, and there may be
 * none. The type must have a length or `const`, because an array slice has no elements of its own.
 * The node is made before the nodes of its type and elements, which are its children.
 */
static Ast_Node_Index parse_array_literal(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parse_token_current(parser)->token_kind == TOKEN_KIND_BRACKET_LEFT);

    // The bracket is not the last token, because the last one is `END`. The error is at the
    // closing bracket, where the length is missing.
    assert(parser->token_index + 1 < parser->token_count);
    if (parser->tokens[parser->token_index + 1].token_kind == TOKEN_KIND_BRACKET_RIGHT) {
        parse_token_advance(parser);
        return parse_fail(parser, STR("an array length or 'const'"));
    }

    if (parser->literal_depth >= AST_BLOCK_DEPTH_MAX) {
        return parse_fail(parser, STR("fewer nested literals"));
    }
    Ast_Node_Index literal = parse_node_make(parser, AST_NODE_TYPE_ARRAY_LITERAL);
    if (literal == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    Ast_Node_Index type = parse_type(parser, STR("an array type"));
    if (type == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, literal, type);

    if (parse_token_expect(parser, TOKEN_KIND_BRACE_LEFT, STR("'{'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    // The elements can be on lines of their own, and a comma can come after the last one. Every
    // element consumes a token, so there are fewer elements than tokens, which bounds the loop.
    parser->literal_depth += 1;
    for (size_t i = 0; i < parser->token_count; i += 1) {
        parse_skip_indentation(parser);
        if (parse_token_current(parser)->token_kind == TOKEN_KIND_BRACE_RIGHT) {
            break;
        }

        Ast_Node_Index element = parse_expression(parser);
        if (element == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, literal, element);

        parse_skip_indentation(parser);
        if (parse_token_current(parser)->token_kind != TOKEN_KIND_COMMA) {
            break;
        }
        parse_token_advance(parser);
    }
    parser->literal_depth -= 1;

    parse_skip_indentation(parser);
    if (parse_token_expect(parser, TOKEN_KIND_BRACE_RIGHT, STR("',' or '}'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    return literal;
}

/**
 * Skips the indentation tokens of the lines of a literal between braces, which can be spread over
 * several lines. The indentation of those lines is not part of the structure of the program.
 */
static void parse_skip_indentation(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);

    // Every indentation token is a token, so there are fewer of them than tokens.
    for (size_t i = 0; i < parser->token_count; i += 1) {
        if (parse_token_current(parser)->token_kind != TOKEN_KIND_INDENTATION) {
            break;
        }
        parse_token_advance(parser);
    }
}

/**
 * Parses a struct literal, `Person { age = 30, name = "Bob" }`: the name of the struct, and the
 * fields that are given a value between braces. A field is its name, `=` and an expression, or a
 * literal `{ ... }` without a name when the field is a struct of its own. The fields are separated
 * by commas, can be on lines of their own, and can be in any order. A comma can come after the
 * last one. The literal without a name is only parsed as the value of a field, because it gets its
 * type from there. The node is made before the nodes of the fields.
 */
static Ast_Node_Index parse_struct_literal(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);

    Token const *start = parse_token_current(parser);
    assert(start->token_kind == TOKEN_KIND_IDENTIFIER ||
           start->token_kind == TOKEN_KIND_BRACE_LEFT);

    if (parser->literal_depth >= AST_BLOCK_DEPTH_MAX) {
        return parse_fail(parser, STR("fewer nested literals"));
    }
    Ast_Node_Index literal = parse_node_make(parser, AST_NODE_TYPE_STRUCT_LITERAL);
    if (literal == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    if (start->token_kind == TOKEN_KIND_IDENTIFIER) {
        parser->nodes[literal].payload.struct_literal.name = start->payload.identifier.text;
        parse_token_advance(parser);
    }

    if (parse_token_expect(parser, TOKEN_KIND_BRACE_LEFT, STR("'{'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    // Every field consumes a token, so there are fewer fields than tokens, which bounds the loop.
    parser->literal_depth += 1;
    for (size_t i = 0; i < parser->token_count; i += 1) {
        parse_skip_indentation(parser);
        if (parse_token_current(parser)->token_kind == TOKEN_KIND_BRACE_RIGHT) {
            break;
        }

        Token const *field_name = parse_token_current(parser);
        if (field_name->token_kind != TOKEN_KIND_IDENTIFIER) {
            return parse_fail(parser, STR("a field name or '}'"));
        }
        Ast_Node_Index initializer = parse_node_make(parser, AST_NODE_TYPE_FIELD_INITIALIZER);
        if (initializer == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parser->nodes[initializer].payload.field_initializer.name =
            field_name->payload.identifier.text;
        parse_token_advance(parser);
        parse_node_append_child(parser, literal, initializer);

        if (parse_token_expect(parser, TOKEN_KIND_EQUALS, STR("'='")) == false) {
            return AST_NODE_INDEX_NONE;
        }
        Ast_Node_Index value = AST_NODE_INDEX_NONE;
        if (parse_token_current(parser)->token_kind == TOKEN_KIND_BRACE_LEFT) {
            value = parse_struct_literal(parser);
        } else {
            value = parse_expression(parser);
        }
        if (value == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, initializer, value);

        parse_skip_indentation(parser);
        if (parse_token_current(parser)->token_kind != TOKEN_KIND_COMMA) {
            break;
        }
        parse_token_advance(parser);
    }
    parser->literal_depth -= 1;

    parse_skip_indentation(parser);
    if (parse_token_expect(parser, TOKEN_KIND_BRACE_RIGHT, STR("',' or '}'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    return literal;
}

/**
 * Parses the definition of a struct after `::`: the word `struct`, `->`, and the fields, which are
 * the lines that are indented one level deeper than the line of the definition, each of them
 * `name: Type`. A field can be a struct of its own, written `name: struct ->`, whose fields are
 * the lines below it, indented one level deeper still. A line can also be a union match,
 * `union match kind ->`, with its cases one level deeper, each of them `case .MEMBER ->` and
 * the fields of the case one level deeper still. The fields of a case are fields of the struct
 * that the union match is in, and each one has an `AST_NODE_TYPE_UNION_MATCH` after its type. The
 * parts that are open are kept in an array, like the blocks of a procedure, and a line that is
 * indented less than the innermost one closes it, and as many outer ones as it is indented less
 * than. Every struct must have a field, every union match a case and every case a field. The
 * current token is `struct`, and `indentation_level` is the level of the line it is on.
 */
static Ast_Node_Index parse_struct_type(Parser *parser, uint32_t indentation_level) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parse_token_current(parser)->token_kind == TOKEN_KIND_KEYWORD_STRUCT);
    assert(indentation_level < UINT32_MAX);

    parse_token_advance(parser);
    Ast_Node_Index root = parse_node_make(parser, AST_NODE_TYPE_STRUCT_TYPE);
    if (root == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    if (parse_token_expect(parser, TOKEN_KIND_ARROW, STR("'->'")) == false ||
        parse_line_end_expect(parser) == false) {
        return AST_NODE_INDEX_NONE;
    }

    // The first one is the struct itself, and the others are inside each other.
    Parse_Struct_Frame frames[AST_BLOCK_DEPTH_MAX + 1];
    frames[0] = (Parse_Struct_Frame){
        .discriminant = STR_EMPTY,
        .member = STR_EMPTY,
        .node = root,
        .item_count = 0,
        .level = indentation_level + 1,
        .kind = PARSE_STRUCT_FRAME_KIND_STRUCT,
    };
    size_t frame_count = 1;

    // Each line consumes at least its indentation token, so there are fewer lines than tokens,
    // which bounds the loop. It stops at the first line that is not part of the struct.
    for (size_t i = 0; i < parser->token_count; i += 1) {
        Token const *line_start = parse_token_current(parser);
        if (line_start->token_kind != TOKEN_KIND_INDENTATION) {
            break;
        }

        uint32_t level = line_start->payload.indentation.level;
        while (level < frames[frame_count - 1].level && frame_count > 1) {
            if (frames[frame_count - 1].item_count == 0) {
                return parse_fail(parser,
                                  parse_struct_type_frame_expected(frames[frame_count - 1].kind));
            }
            frame_count -= 1;
        }
        if (level < frames[frame_count - 1].level) {
            break;
        }
        if (level > frames[frame_count - 1].level) {
            return parse_fail(parser, STR("a line that is not indented deeper"));
        }
        parse_token_advance(parser);

        Parse_Struct_Frame *frame = &frames[frame_count - 1];
        frame->item_count += 1;

        // A line of a union match is a case, and a line of a struct is a field or a union match.
        // The case and the union match open a part that is inside the one they are in.
        bool is_union = frame->kind == PARSE_STRUCT_FRAME_KIND_STRUCT &&
                        parse_token_current(parser)->token_kind == TOKEN_KIND_KEYWORD_UNION;
        if (frame->kind == PARSE_STRUCT_FRAME_KIND_UNION || is_union) {
            if (frame_count == AST_BLOCK_DEPTH_MAX + 1) {
                return parse_fail(parser, STR("fewer nested structs"));
            }
            Parse_Struct_Frame opened = parse_struct_type_union_line(parser, frame);
            if (opened.node == AST_NODE_INDEX_NONE) {
                return AST_NODE_INDEX_NONE;
            }
            frames[frame_count] = opened;
            frame_count += 1;
            continue;
        }

        Token const *field_name = parse_token_current(parser);
        if (field_name->token_kind != TOKEN_KIND_IDENTIFIER) {
            return parse_fail(parser, STR("a field name"));
        }
        Ast_Node_Index field = parse_node_make(parser, AST_NODE_TYPE_FIELD);
        if (field == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parser->nodes[field].payload.field.name = field_name->payload.identifier.text;
        parse_token_advance(parser);
        parse_node_append_child(parser, frame->node, field);

        if (parse_token_expect(parser, TOKEN_KIND_TYPE_SEPARATOR, STR("':'")) == false) {
            return AST_NODE_INDEX_NONE;
        }

        if (parse_token_current(parser)->token_kind == TOKEN_KIND_KEYWORD_STRUCT) {
            if (frame_count == AST_BLOCK_DEPTH_MAX + 1) {
                return parse_fail(parser, STR("fewer nested structs"));
            }
            parse_token_advance(parser);
            Ast_Node_Index nested = parse_node_make(parser, AST_NODE_TYPE_STRUCT_TYPE);
            if (nested == AST_NODE_INDEX_NONE) {
                return AST_NODE_INDEX_NONE;
            }
            parse_node_append_child(parser, field, nested);
            if (parse_struct_type_union_tag(parser, field, frame) == false) {
                return AST_NODE_INDEX_NONE;
            }
            if (parse_token_expect(parser, TOKEN_KIND_ARROW, STR("'->'")) == false ||
                parse_line_end_expect(parser) == false) {
                return AST_NODE_INDEX_NONE;
            }
            frames[frame_count] = (Parse_Struct_Frame){
                .discriminant = STR_EMPTY,
                .member = STR_EMPTY,
                .node = nested,
                .item_count = 0,
                .level = level + 1,
                .kind = PARSE_STRUCT_FRAME_KIND_STRUCT,
            };
            frame_count += 1;
        } else {
            Ast_Node_Index type = parse_type(parser, STR("a type name or 'struct'"));
            if (type == AST_NODE_INDEX_NONE) {
                return AST_NODE_INDEX_NONE;
            }
            parse_node_append_child(parser, field, type);
            if (parse_struct_type_union_tag(parser, field, frame) == false) {
                return AST_NODE_INDEX_NONE;
            }
            if (parse_line_end_expect(parser) == false) {
                return AST_NODE_INDEX_NONE;
            }
        }
    }

    // The parts that are still open end here, and each one needs a field or a case.
    for (size_t open = frame_count; open > 0; open -= 1) {
        if (frames[open - 1].item_count == 0) {
            return parse_fail(parser, parse_struct_type_frame_expected(frames[open - 1].kind));
        }
    }

    return root;
}

/**
 * Returns what is missing in a part of a struct that has nothing in it, for the error.
 */
static Str parse_struct_type_frame_expected(Parse_Struct_Frame_Kind kind) {
    switch (kind) {
    case PARSE_STRUCT_FRAME_KIND_CASE:
    case PARSE_STRUCT_FRAME_KIND_STRUCT:
        return STR("a field");
    case PARSE_STRUCT_FRAME_KIND_UNION:
        return STR("a 'case'");
    }

    // Unreachable: every kind is handled above.
    assert(false);
    return STR("a field");
}

/**
 * Parses the rest of a line that opens a part of a union match, after the indentation: either
 * `union match kind ->` in the struct `parent` (the current token is `union`), or
 * `case .MEMBER ->` in the union match `parent` (the current token is `case`). Returns the part
 * that it opens, one level deeper than `parent`, or a frame whose `node` is
 * `AST_NODE_INDEX_NONE` after it recorded an error. The line is consumed up to the line end, and
 * the indentation of the next line is not.
 */
static Parse_Struct_Frame parse_struct_type_union_line(Parser *parser,
                                                       Parse_Struct_Frame const *parent) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parent->kind != PARSE_STRUCT_FRAME_KIND_CASE);

    Parse_Struct_Frame failed = {
        .discriminant = STR_EMPTY,
        .member = STR_EMPTY,
        .node = AST_NODE_INDEX_NONE,
        .item_count = 0,
        .level = 0,
        .kind = PARSE_STRUCT_FRAME_KIND_UNION,
    };
    Parse_Struct_Frame opened = {
        .discriminant = parent->discriminant,
        .member = STR_EMPTY,
        .node = parent->node,
        .item_count = 0,
        .level = parent->level + 1,
        .kind = PARSE_STRUCT_FRAME_KIND_UNION,
    };

    if (parent->kind == PARSE_STRUCT_FRAME_KIND_STRUCT) {
        assert(parse_token_current(parser)->token_kind == TOKEN_KIND_KEYWORD_UNION);
        parse_token_advance(parser);
        if (parse_token_expect(parser, TOKEN_KIND_KEYWORD_MATCH, STR("'match'")) == false) {
            return failed;
        }
        Token const *discriminant = parse_token_current(parser);
        if (discriminant->token_kind != TOKEN_KIND_IDENTIFIER) {
            (void)parse_fail(parser, STR("the name of the field to match"));
            return failed;
        }
        opened.discriminant = discriminant->payload.identifier.text;
        parse_token_advance(parser);
    } else {
        if (parse_token_expect(parser, TOKEN_KIND_KEYWORD_CASE, STR("'case'")) == false ||
            parse_token_expect(parser, TOKEN_KIND_DOT, STR("'.' and the member of the enum")) ==
                false) {
            return failed;
        }
        Token const *member = parse_token_current(parser);
        if (member->token_kind != TOKEN_KIND_IDENTIFIER) {
            (void)parse_fail(parser, STR("the member of the enum"));
            return failed;
        }
        opened.member = member->payload.identifier.text;
        opened.kind = PARSE_STRUCT_FRAME_KIND_CASE;
        parse_token_advance(parser);
    }

    if (parse_token_expect(parser, TOKEN_KIND_ARROW, STR("'->'")) == false ||
        parse_line_end_expect(parser) == false) {
        return failed;
    }
    return opened;
}

/**
 * Appends the nodes that say that `field` is in a case of a union match, when `frame` is the part
 * for the fields of a case: an `AST_NODE_TYPE_UNION_MATCH` with the field that is matched, that
 * has the `AST_NODE_TYPE_UNION_CASE` with the member as its child. The type of the field must
 * be its child already, so that the node of the match comes after it. Does nothing for any other
 * frame. Returns false after it recorded an error.
 */
static bool parse_struct_type_union_tag(Parser *parser, Ast_Node_Index field,
                                        Parse_Struct_Frame const *frame) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->nodes[field].node_type == AST_NODE_TYPE_FIELD);

    if (frame->kind != PARSE_STRUCT_FRAME_KIND_CASE) {
        return true;
    }

    Ast_Node_Index match = parse_node_make(parser, AST_NODE_TYPE_UNION_MATCH);
    if (match == AST_NODE_INDEX_NONE) {
        return false;
    }
    parser->nodes[match].payload.union_match.discriminant = frame->discriminant;
    parse_node_append_child(parser, field, match);

    Ast_Node_Index union_case = parse_node_make(parser, AST_NODE_TYPE_UNION_CASE);
    if (union_case == AST_NODE_INDEX_NONE) {
        return false;
    }
    parser->nodes[union_case].payload.union_case.member = frame->member;
    parse_node_append_child(parser, match, union_case);

    return true;
}

/**
 * Parses a type: a name, such as `Int`, a pointer type, such as `^Int`, or an array type, which is
 * the type of the elements after brackets: `[3]Int` is an array of three, `[const]Int` is an array
 * whose length is the number of elements in the literal that it starts, and `[]Int` is an array
 * slice. The type of the elements is a name or a pointer type, and so is the type that a pointer
 * points to: `[]^Int` is an array slice of pointers, while a pointer to an array is not a type.
 * `expected` says what the caller wants, for the error that is recorded when the current token
 * starts no type.
 */
static Ast_Node_Index parse_type(Parser *parser, Str expected) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(expected.length > 0);

    Token const *start = parse_token_current(parser);
    if (start->token_kind != TOKEN_KIND_BRACKET_LEFT) {
        return parse_type_name(parser, expected, true);
    }

    Ast_Node_Index array_type = parse_node_make(parser, AST_NODE_TYPE_ARRAY_TYPE);
    if (array_type == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_token_advance(parser);

    Token const *length = parse_token_current(parser);
    Ast_Array_Length_Kind length_kind = AST_ARRAY_LENGTH_OMITTED;
    int64_t length_value = 0;
    if (length->token_kind == TOKEN_KIND_INTEGER_LITERAL) {
        length_kind = AST_ARRAY_LENGTH_FIXED;
        length_value = length->payload.integer_literal.value;
        parse_token_advance(parser);
    } else if (length->token_kind == TOKEN_KIND_KEYWORD_CONST) {
        length_kind = AST_ARRAY_LENGTH_INFERRED;
        parse_token_advance(parser);
    } else if (length->token_kind != TOKEN_KIND_BRACKET_RIGHT) {
        return parse_fail(parser, STR("an array length, 'const' or ']'"));
    }
    parser->nodes[array_type].payload.array_type.length = length_value;
    parser->nodes[array_type].payload.array_type.length_kind = length_kind;

    if (parse_token_expect(parser, TOKEN_KIND_BRACKET_RIGHT, STR("']'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    Ast_Node_Index element = parse_type_name(parser, STR("the type of the elements"), true);
    if (element == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, array_type, element);

    return array_type;
}

/**
 * Parses a type that is not an array: a name, such as `Int`, or the carets of a pointer type and
 * then a name, such as `^Int` or `^^Int`, or the carets and then a procedure type, such as
 * `^proc(Int) Int`. Each caret makes a node that has the rest of the type as its child, and the
 * nodes are made in the order of the source. `expected` is what the caller wants when the current
 * token starts no type; after a caret the name is what is missing. A procedure type is only
 * accepted with `is_procedure_allowed`, which the parameters of a procedure type do not have, so
 * the procedure types do not nest and the parser calls itself at most once for them. A procedure
 * type needs at least one caret, because what a variable holds is a pointer to a procedure and not
 * the procedure.
 */
static Ast_Node_Index parse_type_name(Parser *parser, Str expected, bool is_procedure_allowed) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(expected.length > 0);

    Ast_Node_Index outermost = AST_NODE_INDEX_NONE;
    Ast_Node_Index innermost = AST_NODE_INDEX_NONE;
    // Every caret consumes a token, so there are fewer carets than tokens, which bounds the loop.
    for (size_t i = 0; i < parser->token_count; i += 1) {
        if (parse_token_current(parser)->token_kind != TOKEN_KIND_CARET) {
            break;
        }

        Ast_Node_Index pointer = parse_node_make(parser, AST_NODE_TYPE_POINTER_TYPE);
        if (pointer == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_token_advance(parser);
        if (outermost == AST_NODE_INDEX_NONE) {
            outermost = pointer;
        } else {
            parse_node_append_child(parser, innermost, pointer);
        }
        innermost = pointer;
    }

    Token const *name = parse_token_current(parser);
    Ast_Node_Index type_name = AST_NODE_INDEX_NONE;
    if (name->token_kind == TOKEN_KIND_KEYWORD_PROC && outermost != AST_NODE_INDEX_NONE &&
        is_procedure_allowed) {
        type_name = parse_procedure_type(parser);
        if (type_name == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
    } else {
        if (name->token_kind == TOKEN_KIND_KEYWORD_PROC) {
            // The word starts a procedure type, which is not accepted here: either the caret is
            // missing, or the type is a parameter of a procedure type.
            if (outermost == AST_NODE_INDEX_NONE) {
                return parse_fail(parser, STR("'^' before 'proc', because what a variable holds is "
                                              "a pointer to a procedure"));
            }
            return parse_fail(parser, STR("a type name, because the parameter of a procedure type "
                                          "is not a pointer to a procedure"));
        }
        if (name->token_kind != TOKEN_KIND_IDENTIFIER) {
            if (outermost != AST_NODE_INDEX_NONE) {
                return parse_fail(parser, STR("a type name after '^'"));
            }
            return parse_fail(parser, expected);
        }
        type_name = parse_node_make(parser, AST_NODE_TYPE_TYPE_NAME);
        if (type_name == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parser->nodes[type_name].payload.type_name.name = name->payload.identifier.text;
        parse_token_advance(parser);
    }

    if (outermost == AST_NODE_INDEX_NONE) {
        return type_name;
    }
    parse_node_append_child(parser, innermost, type_name);
    return outermost;
}

/**
 * Parses a procedure type, which is what a pointer to a procedure points to: `proc(Int, Int) Int`,
 * the types of the parameters between parentheses, which can be none, and the return type, which is
 * optional. The parameters have no names. The node has the types of the parameters as its
 * children, and the return type in its payload, like the node of a procedure. A parameter is a
 * name or a pointer type, and never a procedure type or an array. Returns the node, or
 * `AST_NODE_INDEX_NONE` after it recorded an error.
 */
static Ast_Node_Index parse_procedure_type(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parse_token_current(parser)->token_kind == TOKEN_KIND_KEYWORD_PROC);

    Ast_Node_Index procedure_type = parse_node_make(parser, AST_NODE_TYPE_PROCEDURE_TYPE);
    if (procedure_type == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    // A keyword token has its text like a name does.
    Str keyword = parse_token_current(parser)->payload.identifier.text;
    parse_token_advance(parser);

    if (parse_token_expect(parser, TOKEN_KIND_PARENTHESIS_LEFT, STR("'('")) == false) {
        return AST_NODE_INDEX_NONE;
    }
    if (parse_token_current(parser)->token_kind != TOKEN_KIND_PARENTHESIS_RIGHT) {
        // Every parameter consumes at least a token, so there are fewer parameters than tokens,
        // which bounds the loop. A comma means that another parameter has to follow.
        for (size_t i = 0; i < parser->token_count; i += 1) {
            Ast_Node_Index parameter =
                parse_type_name(parser, STR("the type of a parameter"), false);
            if (parameter == AST_NODE_INDEX_NONE) {
                return AST_NODE_INDEX_NONE;
            }
            parse_node_append_child(parser, procedure_type, parameter);

            if (parse_token_current(parser)->token_kind != TOKEN_KIND_COMMA) {
                break;
            }
            parse_token_advance(parser);
        }
    }
    if (parse_token_expect(parser, TOKEN_KIND_PARENTHESIS_RIGHT, STR("',' or ')'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    if (parse_procedure_return_type(parser, procedure_type) == false) {
        return AST_NODE_INDEX_NONE;
    }
    // See `AST_NODE_TYPE_PROCEDURE_TYPE`: the keyword is what an error about the type points at.
    if (parser->nodes[procedure_type].payload.procedure.return_type.bytes == NULL) {
        parser->nodes[procedure_type].payload.procedure.return_type = keyword;
    }
    return procedure_type;
}

static Ast_Node_Index parse_bool_literal(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Token_Kind keyword = parse_token_current(parser)->token_kind;
    if (keyword != TOKEN_KIND_KEYWORD_TRUE && keyword != TOKEN_KIND_KEYWORD_FALSE) {
        return parse_fail(parser, STR("'true' or 'false'"));
    }

    Ast_Node_Index node = parse_node_make(parser, AST_NODE_TYPE_BOOL_LITERAL);
    if (node == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[node].payload.bool_literal.value = keyword == TOKEN_KIND_KEYWORD_TRUE;
    parse_token_advance(parser);

    assert(parser->nodes[node].first_child == AST_NODE_INDEX_NONE);
    return node;
}

static Ast_Node_Index parse_string_literal(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Token const *literal = parse_token_current(parser);
    if (literal->token_kind != TOKEN_KIND_STRING_LITERAL) {
        return parse_fail(parser, STR("a string literal"));
    }

    Ast_Node_Index node = parse_node_make(parser, AST_NODE_TYPE_STRING_LITERAL);
    if (node == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[node].payload.string_literal.text = literal->payload.string_literal.text;
    parse_token_advance(parser);

    assert(parser->nodes[node].first_child == AST_NODE_INDEX_NONE);
    return node;
}

static Ast_Node_Index parse_integer_literal(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Token const *literal = parse_token_current(parser);
    if (literal->token_kind != TOKEN_KIND_INTEGER_LITERAL) {
        return parse_fail(parser, STR("an integer literal"));
    }

    Ast_Node_Index node = parse_node_make(parser, AST_NODE_TYPE_INTEGER_LITERAL);
    if (node == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[node].payload.integer_literal.value = literal->payload.integer_literal.value;
    parse_token_advance(parser);

    assert(parser->nodes[node].first_child == AST_NODE_INDEX_NONE);
    return node;
}

static Ast_Node_Index parse_identifier(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Token const *identifier = parse_token_current(parser);
    if (identifier->token_kind != TOKEN_KIND_IDENTIFIER) {
        return parse_fail(parser, STR("a name"));
    }

    Ast_Node_Index node = parse_node_make(parser, AST_NODE_TYPE_IDENTIFIER);
    if (node == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[node].payload.identifier.name = identifier->payload.identifier.text;
    parse_token_advance(parser);

    assert(parser->nodes[node].first_child == AST_NODE_INDEX_NONE);
    return node;
}

static Token const *parse_token_current(Parser const *parser) {
    assert(parser->token_index < parser->token_count);
    assert(parser->tokens != NULL);

    return &parser->tokens[parser->token_index];
}

static void parse_token_advance(Parser *parser) {
    // The `END` token is never consumed, so there is always a current token.
    assert(parse_token_current(parser)->token_kind != TOKEN_KIND_END);

    parser->token_index += 1;

    assert(parser->token_index < parser->token_count);
}

/**
 * Consumes the current token if it has type `token_kind`. Otherwise records an error that says
 * `expected` and returns false.
 */
static bool parse_token_expect(Parser *parser, Token_Kind token_kind, Str expected) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(expected.length > 0);

    if (parse_token_current(parser)->token_kind == token_kind) {
        parse_token_advance(parser);
        return true;
    }

    // The false return value reports the failure to the caller, so the node index that
    // `parse_fail` returns for convenience is not needed.
    (void)parse_fail(parser, expected);
    return false;
}

static Ast_Node_Index parse_node_make(Parser *parser, Ast_Node_Type node_type) {
    assert(parser->node_count <= parser->node_count_max);
    assert(parser->status == PARSE_STATUS_OK);

    if (parser->node_count == parser->node_count_max) {
        parser->status = PARSE_STATUS_NODE_LIMIT;
        parser->error_token_index = parser->token_index;
        return AST_NODE_INDEX_NONE;
    }

    // Node indexes are 32 bits, and a node needs a token, which is bounded by the same limit.
    assert(parser->node_count < AST_NODE_INDEX_NONE);
    Ast_Node_Index index = (Ast_Node_Index)parser->node_count;
    parser->nodes[index] = (Ast_Node){
        .node_type = node_type,
        .first_child = AST_NODE_INDEX_NONE,
        .last_child = AST_NODE_INDEX_NONE,
        .next_sibling = AST_NODE_INDEX_NONE,
    };
    parser->node_count += 1;

    return index;
}

static void parse_node_append_child(Parser *parser, Ast_Node_Index parent,
                                    Ast_Node_Index child) {
    assert(parent < parser->node_count);
    assert(child < parser->node_count);
    assert(parent != child);

    Ast_Node *parent_node = &parser->nodes[parent];
    if (parent_node->first_child == AST_NODE_INDEX_NONE) {
        parent_node->first_child = child;
    } else {
        parser->nodes[parent_node->last_child].next_sibling = child;
    }
    parent_node->last_child = child;

    assert(parent_node->last_child == child);
    assert(parser->nodes[child].next_sibling == AST_NODE_INDEX_NONE);
}

static Ast_Node_Index parse_fail(Parser *parser, Str expected) {
    // Only the first error is kept, and callers return right after it.
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    parser->status = PARSE_STATUS_UNEXPECTED_TOKEN;
    parser->expected = expected;
    parser->error_token_index = parser->token_index;

    return AST_NODE_INDEX_NONE;
}
