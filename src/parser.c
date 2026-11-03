#include <assert.h>
#include <stdbool.h>

#include <mialu/parser.h>
#include <mialu/tokenizer.h>

// The indentation level of a line that is not indented. Definitions at this level are the
// top-level ones, and their bodies are one level deeper.
static uint32_t const INDENTATION_LEVEL_TOP = 0;

/**
 * The state shared by the parsing functions. A parsing function returns the index of the node it
 * made, or `AST_NODE_INDEX_NONE` after it recorded an error here, and its caller then returns
 * `AST_NODE_INDEX_NONE` too without recording anything more. So only the first error is kept.
 */
typedef struct {
    Token const *tokens;
    Ast_Node *nodes;
    Str expected;
    size_t token_count;
    size_t token_index;
    size_t node_count;
    size_t node_count_max;
    size_t error_token_index;
    Parse_Status status;
} Parser;

static Ast_Node_Index parse_assignment          (Parser *parser);
static Ast_Node_Index parse_constant_definition (Parser *parser);
static Ast_Node_Index parse_operand             (Parser *parser);
static Ast_Node_Index parse_expression          (Parser *parser);
static Ast_Node_Index parse_fail                (Parser *parser, Str expected);
static Ast_Node_Index parse_identifier          (Parser *parser);
static Ast_Node_Index parse_integer_literal     (Parser *parser);
static bool           parse_line_end_expect     (Parser *parser);
static Ast_Node_Index parse_module              (Parser *parser);
static void           parse_node_append_child   (Parser *parser, Ast_Node_Index parent,
                                                 Ast_Node_Index child);
static Ast_Node_Index parse_node_make           (Parser *parser, Ast_Node_Type node_type);
static Ast_Node_Index parse_parameter           (Parser *parser);
static Ast_Node_Index parse_procedure           (Parser *parser, uint32_t indentation_level);
static bool           parse_procedure_body      (Parser *parser, Ast_Node_Index procedure,
                                                 uint32_t indentation_level);
static bool           parse_procedure_parameters(Parser *parser, Ast_Node_Index procedure);
static Ast_Node_Index parse_return_statement    (Parser *parser);
static Ast_Node_Index parse_statement           (Parser *parser);
static Ast_Node_Index parse_string_literal      (Parser *parser);
static Ast_Node_Index parse_term                (Parser *parser);
static void           parse_token_advance       (Parser *parser);
static Token const *  parse_token_current       (Parser const *parser);
static bool           parse_token_expect        (Parser *parser, Token_Type token_type,
                                                 Str expected);
static Ast_Node_Index parse_variable_definition (Parser *parser);

Parse_Result parse(Array_Slice tokens, Array_Slice node_buffer) {
    assert(tokens.elements != NULL);
    assert(node_buffer.elements != NULL);
    assert(tokens.length >= 1);

    Token const *token_elements = tokens.elements;
    assert(token_elements[tokens.length - 1].token_type == TOKEN_TYPE_END);

    Parser parser = {
        .tokens = token_elements,
        .nodes = node_buffer.elements,
        .expected = STR(""),
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
        if (parse_token_current(parser)->token_type == TOKEN_TYPE_END) {
            break;
        }

        Ast_Node_Index definition = parse_constant_definition(parser);
        if (definition == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, module, definition);
    }

    assert(parse_token_current(parser)->token_type == TOKEN_TYPE_END);
    return module;
}

static Ast_Node_Index parse_constant_definition(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->node_count <= parser->node_count_max);

    // Expect constant definitions to be at the top level.
    Token const *indentation = parse_token_current(parser);
    if (indentation->token_type != TOKEN_TYPE_INDENTATION) {
        return parse_fail(parser, STR("the start of a line"));
    }
    if (indentation->payload.indentation.level != INDENTATION_LEVEL_TOP) {
        return parse_fail(parser, STR("no indentation"));
    }
    parse_token_advance(parser);

    // Expect constant definition to start with an identifier.
    Token const *name = parse_token_current(parser);
    if (name->token_type != TOKEN_TYPE_IDENTIFIER) {
        return parse_fail(parser, STR("a name"));
    }

    Ast_Node_Index definition = parse_node_make(parser, AST_NODE_TYPE_CONSTANT_DEFINITION);
    if (definition == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[definition].payload.constant_definition.name = name->payload.identifier.text;
    parse_token_advance(parser);

    // `::` is two separator tokens, one after the other.
    if (parse_token_expect(parser, TOKEN_TYPE_TYPE_SEPARATOR, STR("'::'")) == false) {
        return AST_NODE_INDEX_NONE;
    }
    if (parse_token_expect(parser, TOKEN_TYPE_TYPE_SEPARATOR, STR("'::'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    // The value is a string, or a procedure, which is what any other token here starts.
    Ast_Node_Index value = AST_NODE_INDEX_NONE;
    if (parse_token_current(parser)->token_type == TOKEN_TYPE_STRING_LITERAL) {
        value = parse_string_literal(parser);
        // A procedure ends with its body, but a string is one line, so its end is checked here.
        if (value != AST_NODE_INDEX_NONE && parse_line_end_expect(parser) == false) {
            return AST_NODE_INDEX_NONE;
        }
    } else {
        value = parse_procedure(parser, INDENTATION_LEVEL_TOP);
    }
    if (value == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, definition, value);

    assert(parser->nodes[definition].first_child == value);
    return definition;
}

static Ast_Node_Index parse_procedure(Parser *parser, uint32_t indentation_level) {
    assert(indentation_level < UINT32_MAX);
    assert(parser->status == PARSE_STATUS_OK);

    if (parse_token_expect(parser, TOKEN_TYPE_KEYWORD_PROC, STR("'proc'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    Ast_Node_Index procedure = parse_node_make(parser, AST_NODE_TYPE_PROCEDURE);
    if (procedure == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    if (parse_token_expect(parser, TOKEN_TYPE_PARENTHESIS_LEFT, STR("'('")) == false) {
        return AST_NODE_INDEX_NONE;
    }
    if (parse_procedure_parameters(parser, procedure) == false) {
        return AST_NODE_INDEX_NONE;
    }
    if (parse_token_expect(parser, TOKEN_TYPE_PARENTHESIS_RIGHT, STR("')'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    // The return type is optional. It is only a name for now, so an identifier here is it.
    Token const *type = parse_token_current(parser);
    if (type->token_type == TOKEN_TYPE_IDENTIFIER) {
        parser->nodes[procedure].payload.procedure.return_type = type->payload.identifier.text;
        parse_token_advance(parser);
    }

    if (parse_token_expect(parser, TOKEN_TYPE_ARROW, STR("'->'")) == false) {
        return AST_NODE_INDEX_NONE;
    }
    if (parse_procedure_body(parser, procedure, indentation_level) == false) {
        return AST_NODE_INDEX_NONE;
    }

    return procedure;
}

/**
 * Parses the parameters between the parentheses, which can be none, and appends them to
 * `procedure`. Returns false after it recorded an error.
 */
static bool parse_procedure_parameters(Parser *parser, Ast_Node_Index procedure) {
    assert(procedure < parser->node_count);
    assert(parser->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);

    if (parse_token_current(parser)->token_type == TOKEN_TYPE_PARENTHESIS_RIGHT) {
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

        if (parse_token_current(parser)->token_type != TOKEN_TYPE_COMMA) {
            break;
        }
        parse_token_advance(parser);
    }

    return true;
}

/**
 * Parses the statements of the body, which are the lines that are indented one level deeper than
 * the line of the definition, and appends them to `procedure`. Returns false after it recorded an
 * error.
 */
static bool parse_procedure_body(Parser *parser, Ast_Node_Index procedure,
                                 uint32_t indentation_level) {
    assert(procedure < parser->node_count);
    assert(indentation_level < UINT32_MAX);

    // Each statement consumes at least its indentation token, so there are fewer statements than
    // tokens, which bounds the loop. It stops at the first line that is not part of the body.
    uint32_t const body_level = indentation_level + 1;
    size_t statement_count = 0;
    for (size_t i = 0; i < parser->token_count; i += 1) {
        Token const *line_start = parse_token_current(parser);
        if (line_start->token_type != TOKEN_TYPE_INDENTATION) {
            break;
        }

        uint32_t level = line_start->payload.indentation.level;
        if (level < body_level) {
            break;
        }
        if (level > body_level) {
            // The false return value reports the failure, so the node index is not needed.
            (void)parse_fail(parser, STR("a line that is not indented deeper"));
            return false;
        }
        parse_token_advance(parser);

        Ast_Node_Index statement = parse_statement(parser);
        if (statement == AST_NODE_INDEX_NONE) {
            return false;
        }
        parse_node_append_child(parser, procedure, statement);
        statement_count += 1;
    }

    if (statement_count == 0) {
        // The false return value reports the failure, so the node index is not needed.
        (void)parse_fail(parser, STR("an indented procedure body"));
        return false;
    }

    return true;
}

/**
 * Parses `name: Type`. The node for the parameter has the name in its payload and one child, the
 * type.
 */
static Ast_Node_Index parse_parameter(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Token const *name = parse_token_current(parser);
    if (name->token_type != TOKEN_TYPE_IDENTIFIER) {
        return parse_fail(parser, STR("a parameter name"));
    }

    Ast_Node_Index parameter = parse_node_make(parser, AST_NODE_TYPE_PARAMETER);
    if (parameter == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[parameter].payload.parameter.name = name->payload.identifier.text;
    parse_token_advance(parser);

    if (parse_token_expect(parser, TOKEN_TYPE_TYPE_SEPARATOR, STR("':'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    Token const *type = parse_token_current(parser);
    if (type->token_type != TOKEN_TYPE_IDENTIFIER) {
        return parse_fail(parser, STR("a type name"));
    }

    Ast_Node_Index type_name = parse_node_make(parser, AST_NODE_TYPE_TYPE_NAME);
    if (type_name == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[type_name].payload.type_name.name = type->payload.identifier.text;
    parse_token_advance(parser);
    parse_node_append_child(parser, parameter, type_name);

    assert(parser->nodes[parameter].first_child == type_name);
    return parameter;
}

/**
 * Parses one statement of a body: a `return`, or a statement that starts with a name, which is a
 * variable definition when a `:` follows the name and an assignment otherwise.
 */
static Ast_Node_Index parse_statement(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Token_Type next = parse_token_current(parser)->token_type;
    if (next == TOKEN_TYPE_KEYWORD_RETURN) {
        return parse_return_statement(parser);
    }
    if (next == TOKEN_TYPE_IDENTIFIER) {
        // The name is not the last token, because the last one is `END`.
        assert(parser->token_index + 1 < parser->token_count);
        if (parser->tokens[parser->token_index + 1].token_type == TOKEN_TYPE_TYPE_SEPARATOR) {
            return parse_variable_definition(parser);
        }
        return parse_assignment(parser);
    }

    return parse_fail(parser, STR("a statement"));
}

/**
 * Parses `name += value`, or the same with another assignment operator. The node for the assignment has two children: the name, as an
 * identifier, and the value. A name that is followed by anything else than `:` or an assignment
 * operator is the error here.
 */
static Ast_Node_Index parse_assignment(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index + 1 < parser->token_count);

    Ast_Node_Index target = parse_identifier(parser);
    if (target == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    Token_Type operator = parse_token_current(parser)->token_type;
    Ast_Node_Type node_type = AST_NODE_TYPE_ADDITION_ASSIGNMENT;
    if (operator == TOKEN_TYPE_SLASH_EQUALS) {
        node_type = AST_NODE_TYPE_DIVISION_ASSIGNMENT;
    } else if (operator == TOKEN_TYPE_ASTERISK_EQUALS) {
        node_type = AST_NODE_TYPE_MULTIPLICATION_ASSIGNMENT;
    } else if (operator == TOKEN_TYPE_MINUS_EQUALS) {
        node_type = AST_NODE_TYPE_SUBTRACTION_ASSIGNMENT;
    } else if (operator != TOKEN_TYPE_PLUS_EQUALS) {
        return parse_fail(parser, STR("':' or an assignment operator"));
    }
    parse_token_advance(parser);

    Ast_Node_Index assignment = parse_node_make(parser, node_type);
    if (assignment == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, assignment, target);

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
    if (name->token_type != TOKEN_TYPE_IDENTIFIER) {
        return parse_fail(parser, STR("a variable name"));
    }

    Ast_Node_Index definition = parse_node_make(parser, AST_NODE_TYPE_VARIABLE_DEFINITION);
    if (definition == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[definition].payload.variable_definition.name = name->payload.identifier.text;
    parse_token_advance(parser);

    if (parse_token_expect(parser, TOKEN_TYPE_TYPE_SEPARATOR, STR("':'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    // `:=` has no type, which is then deduced from the value. Otherwise the type comes before `=`.
    if (parse_token_current(parser)->token_type != TOKEN_TYPE_EQUALS) {
        Token const *type = parse_token_current(parser);
        if (type->token_type != TOKEN_TYPE_IDENTIFIER) {
            return parse_fail(parser, STR("a type name or '='"));
        }

        Ast_Node_Index type_name = parse_node_make(parser, AST_NODE_TYPE_TYPE_NAME);
        if (type_name == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parser->nodes[type_name].payload.type_name.name = type->payload.identifier.text;
        parse_token_advance(parser);
        parse_node_append_child(parser, definition, type_name);
    }
    if (parse_token_expect(parser, TOKEN_TYPE_EQUALS, STR("'='")) == false) {
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

    if (parse_token_expect(parser, TOKEN_TYPE_KEYWORD_RETURN, STR("'return'")) == false) {
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

    Token_Type next = parse_token_current(parser)->token_type;
    bool is_line_end = next == TOKEN_TYPE_INDENTATION || next == TOKEN_TYPE_END;
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
static Ast_Node_Index parse_expression(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Ast_Node_Index left = parse_term(parser);
    if (left == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    // Every operator consumes a token, so there are fewer operators than tokens, which bounds
    // the loop.
    for (size_t i = 0; i < parser->token_count; i += 1) {
        Token_Type operator = parse_token_current(parser)->token_type;
        if (operator != TOKEN_TYPE_PLUS && operator != TOKEN_TYPE_MINUS) {
            break;
        }
        parse_token_advance(parser);

        Ast_Node_Index right = parse_term(parser);
        if (right == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }

        Ast_Node_Type node_type = AST_NODE_TYPE_ADDITION;
        if (operator == TOKEN_TYPE_MINUS) {
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
        Token_Type operator = parse_token_current(parser)->token_type;
        if (operator != TOKEN_TYPE_ASTERISK && operator != TOKEN_TYPE_SLASH) {
            break;
        }
        parse_token_advance(parser);

        Ast_Node_Index right = parse_operand(parser);
        if (right == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }

        Ast_Node_Type node_type = AST_NODE_TYPE_MULTIPLICATION;
        if (operator == TOKEN_TYPE_SLASH) {
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
 * Parses a value: an integer literal, a string literal, or the name of something, such as a
 * parameter.
 */
static Ast_Node_Index parse_operand(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Token_Type next = parse_token_current(parser)->token_type;
    if (next == TOKEN_TYPE_INTEGER_LITERAL) {
        return parse_integer_literal(parser);
    }
    if (next == TOKEN_TYPE_IDENTIFIER) {
        return parse_identifier(parser);
    }
    if (next == TOKEN_TYPE_STRING_LITERAL) {
        return parse_string_literal(parser);
    }

    return parse_fail(parser, STR("an integer literal, a string literal or a name"));
}

static Ast_Node_Index parse_string_literal(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Token const *literal = parse_token_current(parser);
    if (literal->token_type != TOKEN_TYPE_STRING_LITERAL) {
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
    if (literal->token_type != TOKEN_TYPE_INTEGER_LITERAL) {
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
    if (identifier->token_type != TOKEN_TYPE_IDENTIFIER) {
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
    assert(parse_token_current(parser)->token_type != TOKEN_TYPE_END);

    parser->token_index += 1;

    assert(parser->token_index < parser->token_count);
}

/**
 * Consumes the current token if it has type `token_type`. Otherwise records an error that says
 * `expected` and returns false.
 */
static bool parse_token_expect(Parser *parser, Token_Type token_type, Str expected) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(expected.length > 0);

    if (parse_token_current(parser)->token_type == token_type) {
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
