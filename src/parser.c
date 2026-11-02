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

static uint32_t      parse_module             (Parser *parser);
static uint32_t      parse_constant_definition(Parser *parser);
static uint32_t      parse_function           (Parser *parser, uint32_t indentation_level);
static uint32_t      parse_return_statement   (Parser *parser);
static uint32_t      parse_integer_literal    (Parser *parser);
static Token const * parse_token_current      (Parser const *parser);
static void          parse_token_advance      (Parser *parser);
static bool          parse_token_expect       (Parser *parser, Token_Type token_type, Str expected);
static uint32_t      parse_node_make          (Parser *parser, Ast_Node_Type node_type);
static void          parse_node_append_child  (Parser *parser, uint32_t parent, uint32_t child);
static uint32_t      parse_fail               (Parser *parser, Str expected);

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

    uint32_t root = parse_module(&parser);

    // The root exists exactly when parsing succeeded.
    assert((root == AST_NODE_INDEX_NONE) == (parser.status != PARSE_STATUS_OK));
    assert(parser.node_count <= node_buffer.length);
    return (Parse_Result){
        .nodes = {.elements = node_buffer.elements, .length = parser.node_count},
        .expected = parser.expected,
        .error_token_index = parser.error_token_index,
        .root = root,
        .status = parser.status,
    };
}

static uint32_t parse_module(Parser *parser) {
    assert(parser->token_index == 0);
    assert(parser->status == PARSE_STATUS_OK);

    uint32_t module = parse_node_make(parser, AST_NODE_TYPE_MODULE);
    if (module == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    // Every definition consumes at least its indentation token, and the last token, `END`, is
    // never consumed. So there are fewer definitions than tokens, which bounds the loop.
    for (size_t i = 0; i < parser->token_count; i += 1) {
        if (parse_token_current(parser)->token_type == TOKEN_TYPE_END) {
            break;
        }

        uint32_t definition = parse_constant_definition(parser);
        if (definition == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, module, definition);
    }

    assert(parse_token_current(parser)->token_type == TOKEN_TYPE_END);
    return module;
}

static uint32_t parse_constant_definition(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->node_count <= parser->node_count_max);

    Token const *indentation = parse_token_current(parser);
    if (indentation->token_type != TOKEN_TYPE_INDENTATION) {
        return parse_fail(parser, STR("the start of a line"));
    }
    if (indentation->payload.indentation.level != INDENTATION_LEVEL_TOP) {
        return parse_fail(parser, STR("no indentation"));
    }
    parse_token_advance(parser);

    Token const *name = parse_token_current(parser);
    if (name->token_type != TOKEN_TYPE_IDENTIFIER) {
        return parse_fail(parser, STR("a name"));
    }

    uint32_t definition = parse_node_make(parser, AST_NODE_TYPE_CONSTANT_DEFINITION);
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

    uint32_t function = parse_function(parser, INDENTATION_LEVEL_TOP);
    if (function == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, definition, function);

    assert(parser->nodes[definition].first_child == function);
    return definition;
}

static uint32_t parse_function(Parser *parser, uint32_t indentation_level) {
    assert(indentation_level < UINT32_MAX);
    assert(parser->status == PARSE_STATUS_OK);

    // `proc` is not a keyword token yet, so it is recognized by its text.
    Token const *keyword = parse_token_current(parser);
    if (keyword->token_type != TOKEN_TYPE_IDENTIFIER) {
        return parse_fail(parser, STR("'proc'"));
    }
    if (str_equals(keyword->payload.identifier.text, STR("proc")) == false) {
        return parse_fail(parser, STR("'proc'"));
    }
    parse_token_advance(parser);

    if (parse_token_expect(parser, TOKEN_TYPE_PARENTHESIS_LEFT, STR("'('")) == false) {
        return AST_NODE_INDEX_NONE;
    }
    if (parse_token_expect(parser, TOKEN_TYPE_PARENTHESIS_RIGHT, STR("')'")) == false) {
        return AST_NODE_INDEX_NONE;
    }
    if (parse_token_expect(parser, TOKEN_TYPE_ARROW, STR("'->'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    uint32_t function = parse_node_make(parser, AST_NODE_TYPE_FUNCTION);
    if (function == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    // The body is the lines that are indented one level deeper than the line of the definition.
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
            return parse_fail(parser, STR("a line that is not indented deeper"));
        }
        parse_token_advance(parser);

        uint32_t statement = parse_return_statement(parser);
        if (statement == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, function, statement);
        statement_count += 1;
    }

    if (statement_count == 0) {
        return parse_fail(parser, STR("an indented function body"));
    }

    return function;
}

static uint32_t parse_return_statement(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    if (parse_token_expect(parser, TOKEN_TYPE_KEYWORD_RETURN, STR("'return'")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    uint32_t statement = parse_node_make(parser, AST_NODE_TYPE_RETURN_STATEMENT);
    if (statement == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    uint32_t value = parse_integer_literal(parser);
    if (value == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, statement, value);

    // There is no end-of-line token, so the next line, or the end, must follow directly.
    Token_Type next = parse_token_current(parser)->token_type;
    bool is_line_end = next == TOKEN_TYPE_INDENTATION || next == TOKEN_TYPE_END;
    if (is_line_end == false) {
        return parse_fail(parser, STR("the end of the line"));
    }

    assert(parser->nodes[statement].first_child == value);
    return statement;
}

static uint32_t parse_integer_literal(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Token const *literal = parse_token_current(parser);
    if (literal->token_type != TOKEN_TYPE_INTEGER_LITERAL) {
        return parse_fail(parser, STR("an integer literal"));
    }

    uint32_t node = parse_node_make(parser, AST_NODE_TYPE_INTEGER_LITERAL);
    if (node == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[node].payload.integer_literal.value = literal->payload.integer_literal.value;
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

static uint32_t parse_node_make(Parser *parser, Ast_Node_Type node_type) {
    assert(parser->node_count <= parser->node_count_max);
    assert(parser->status == PARSE_STATUS_OK);

    if (parser->node_count == parser->node_count_max) {
        parser->status = PARSE_STATUS_NODE_LIMIT;
        parser->error_token_index = parser->token_index;
        return AST_NODE_INDEX_NONE;
    }

    // Node indexes are 32 bits, and a node needs a token, which is bounded by the same limit.
    assert(parser->node_count < AST_NODE_INDEX_NONE);
    uint32_t index = (uint32_t)parser->node_count;
    parser->nodes[index] = (Ast_Node){
        .node_type = node_type,
        .first_child = AST_NODE_INDEX_NONE,
        .last_child = AST_NODE_INDEX_NONE,
        .next_sibling = AST_NODE_INDEX_NONE,
    };
    parser->node_count += 1;

    return index;
}

static void parse_node_append_child(Parser *parser, uint32_t parent, uint32_t child) {
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

static uint32_t parse_fail(Parser *parser, Str expected) {
    // Only the first error is kept, and callers return right after it.
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    parser->status = PARSE_STATUS_UNEXPECTED_TOKEN;
    parser->expected = expected;
    parser->error_token_index = parser->token_index;

    return AST_NODE_INDEX_NONE;
}
