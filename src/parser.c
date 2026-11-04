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
static bool           parse_attributes          (Parser *parser, Ast_Node_Index definition);
static void           parse_block_close         (Parser *parser, Ast_Node_Index block);
static Ast_Node_Index parse_block_open          (Parser *parser, Ast_Node_Index statement);
static Ast_Node_Index parse_branch              (Parser *parser, Ast_Node_Index statement);
static Ast_Node_Index parse_definition          (Parser *parser);
static Ast_Node_Index parse_else_branch         (Parser *parser, Ast_Node_Index parent);
static Ast_Node_Index parse_if_statement        (Parser *parser, Ast_Node_Index parent);
static Ast_Node_Index parse_bool_literal        (Parser *parser);
static Ast_Node_Index parse_operand             (Parser *parser);
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

        Ast_Node_Index definition = parse_definition(parser);
        if (definition == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, module, definition);
    }

    assert(parse_token_current(parser)->token_type == TOKEN_TYPE_END);
    return module;
}

static Ast_Node_Index parse_definition(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->node_count <= parser->node_count_max);

    // Expect definitions to be at the top level.
    Token const *indentation = parse_token_current(parser);
    if (indentation->token_type != TOKEN_TYPE_INDENTATION) {
        return parse_fail(parser, STR("the start of a line"));
    }
    if (indentation->payload.indentation.level != INDENTATION_LEVEL_TOP) {
        return parse_fail(parser, STR("no indentation"));
    }
    parse_token_advance(parser);

    // Expect a definition to start with an identifier.
    Token const *name = parse_token_current(parser);
    if (name->token_type != TOKEN_TYPE_IDENTIFIER) {
        return parse_fail(parser, STR("a name"));
    }

    // A constant has `::` after its name, which is two separator tokens. Anything else is a global
    // variable, which has an attribute list, a single `:` or both after its name. The last token is
    // `END`, which is neither, so a name at the end of the file is not a constant, and the global
    // variable it starts reports what it expected.
    bool is_constant = parser->token_index + 2 < parser->token_count &&
                       parser->tokens[parser->token_index + 1].token_type ==
                           TOKEN_TYPE_TYPE_SEPARATOR &&
                       parser->tokens[parser->token_index + 2].token_type ==
                           TOKEN_TYPE_TYPE_SEPARATOR;
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

/**
 * Parses `name #(attribute)... : Type = value` or `name #(attribute)... := value`, a global
 * variable. The attribute list may be left out. The current token is the name, which the caller
 * checked. The node for the variable has the name in its payload. Its children are the attributes,
 * the type when the definition has one, and then the value, which is a literal.
 */
static Ast_Node_Index parse_global_variable_definition(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parse_token_current(parser)->token_type == TOKEN_TYPE_IDENTIFIER);

    Token const *name = parse_token_current(parser);
    Ast_Node_Index definition = parse_node_make(parser, AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION);
    if (definition == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[definition].payload.global_variable_definition.name = name->payload.identifier.text;
    parse_token_advance(parser);

    if (parse_token_current(parser)->token_type == TOKEN_TYPE_HASH) {
        if (parse_attributes(parser, definition) == false) {
            return AST_NODE_INDEX_NONE;
        }
    }

    if (parse_token_expect(parser, TOKEN_TYPE_TYPE_SEPARATOR, STR("':' or '#'")) == false) {
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

    // The value is stored once, before any code runs, so it is a literal and not an expression.
    Ast_Node_Index value = AST_NODE_INDEX_NONE;
    switch (parse_token_current(parser)->token_type) {
    case TOKEN_TYPE_INTEGER_LITERAL:
        value = parse_integer_literal(parser);
        break;
    case TOKEN_TYPE_KEYWORD_FALSE:
    case TOKEN_TYPE_KEYWORD_TRUE:
        value = parse_bool_literal(parser);
        break;
    case TOKEN_TYPE_STRING_LITERAL:
        value = parse_string_literal(parser);
        break;
    default:
        return parse_fail(parser, STR("a number, a string, 'true' or 'false'"));
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
    assert(parse_token_current(parser)->token_type == TOKEN_TYPE_HASH);

    parse_token_advance(parser);

    // Every attribute consumes three tokens, so there are fewer attributes than tokens, which
    // bounds the loop.
    for (size_t i = 0; i < parser->token_count; i += 1) {
        if (parse_token_expect(parser, TOKEN_TYPE_PARENTHESIS_LEFT, STR("'(' after '#'")) == false) {
            return false;
        }

        Token const *name = parse_token_current(parser);
        if (name->token_type != TOKEN_TYPE_IDENTIFIER) {
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

        if (parse_token_expect(parser, TOKEN_TYPE_PARENTHESIS_RIGHT, STR("')'")) == false) {
            return false;
        }
        if (parse_token_current(parser)->token_type != TOKEN_TYPE_PARENTHESIS_LEFT) {
            break;
        }
    }

    return true;
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
        if (line_start->token_type != TOKEN_TYPE_INDENTATION) {
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
        Token_Type next = parse_token_current(parser)->token_type;
        if (next == TOKEN_TYPE_KEYWORD_IF || next == TOKEN_TYPE_KEYWORD_ELSE ||
            next == TOKEN_TYPE_KEYWORD_FOR || next == TOKEN_TYPE_ARROW ||
            next == TOKEN_TYPE_LABEL) {
            Ast_Node_Index block = AST_NODE_INDEX_NONE;
            if (next == TOKEN_TYPE_ARROW || next == TOKEN_TYPE_LABEL) {
                block = parse_scope_statement(parser, parent);
            } else if (next == TOKEN_TYPE_KEYWORD_IF) {
                block = parse_if_statement(parser, parent);
            } else if (next == TOKEN_TYPE_KEYWORD_FOR) {
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

    if (parse_token_expect(parser, TOKEN_TYPE_ARROW, STR("'->'")) == false) {
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
    assert(parse_token_current(parser)->token_type == TOKEN_TYPE_KEYWORD_IF);

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
 * `first..<=end` for a range that includes its end, and appends the statement that it starts to
 * `parent`. The statement gets a range and an empty block, which is returned so that the caller can
 * fill it with the lines that follow. The nodes are made in the order of the source, so the
 * iterator is not in scope in the range, only in the block.
 */
static Ast_Node_Index parse_for_statement(Parser *parser, Ast_Node_Index parent) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parse_token_current(parser)->token_type == TOKEN_TYPE_KEYWORD_FOR);

    parse_token_advance(parser);

    // `for ->` has no iterator and no range: the block runs until it is left.
    if (parse_token_current(parser)->token_type == TOKEN_TYPE_ARROW) {
        Ast_Node_Index loop = parse_node_make(parser, AST_NODE_TYPE_LOOP_STATEMENT);
        if (loop == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parse_node_append_child(parser, parent, loop);
        return parse_block_open(parser, loop);
    }

    Token const *iterator = parse_token_current(parser);
    if (iterator->token_type != TOKEN_TYPE_IDENTIFIER) {
        return parse_fail(parser, STR("the name of the iterator"));
    }

    Ast_Node_Index statement = parse_node_make(parser, AST_NODE_TYPE_FOR_STATEMENT);
    if (statement == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[statement].payload.for_statement.name = iterator->payload.identifier.text;
    parse_node_append_child(parser, parent, statement);
    parse_token_advance(parser);

    if (parse_token_expect(parser, TOKEN_TYPE_KEYWORD_IN, STR("'in'")) == false) {
        return AST_NODE_INDEX_NONE;
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

    Token_Type range_operator = parse_token_current(parser)->token_type;
    if (range_operator != TOKEN_TYPE_RANGE_EXCLUSIVE &&
        range_operator != TOKEN_TYPE_RANGE_INCLUSIVE) {
        return parse_fail(parser, STR("'..<' or '..<='"));
    }
    parser->nodes[range].payload.range.is_inclusive = range_operator == TOKEN_TYPE_RANGE_INCLUSIVE;
    parse_token_advance(parser);

    Ast_Node_Index end = parse_expression(parser);
    if (end == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, range, end);

    return parse_block_open(parser, statement);
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
    assert(label->token_type == TOKEN_TYPE_ARROW || label->token_type == TOKEN_TYPE_LABEL);

    if (label->token_type == TOKEN_TYPE_LABEL) {
        // The label is not the last token, because the last one is `END`.
        assert(parser->token_index + 1 < parser->token_count);
        Token_Type after_label = parser->tokens[parser->token_index + 1].token_type;
        if (after_label == TOKEN_TYPE_KEYWORD_IF || after_label == TOKEN_TYPE_KEYWORD_FOR) {
            Ast_Node_Index labeled = parse_node_make(parser, AST_NODE_TYPE_LABELED_STATEMENT);
            if (labeled == AST_NODE_INDEX_NONE) {
                return AST_NODE_INDEX_NONE;
            }
            parser->nodes[labeled].payload.label.name = label->payload.identifier.text;
            parse_node_append_child(parser, parent, labeled);
            parse_token_advance(parser);
            if (after_label == TOKEN_TYPE_KEYWORD_IF) {
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

    if (label->token_type == TOKEN_TYPE_LABEL) {
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
    assert(keyword->token_type == TOKEN_TYPE_KEYWORD_BREAK ||
           keyword->token_type == TOKEN_TYPE_KEYWORD_CONTINUE);

    Ast_Node_Type node_type = AST_NODE_TYPE_BREAK_STATEMENT;
    if (keyword->token_type == TOKEN_TYPE_KEYWORD_CONTINUE) {
        node_type = AST_NODE_TYPE_CONTINUE_STATEMENT;
    }
    Ast_Node_Index jump = parse_node_make(parser, node_type);
    if (jump == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[jump].payload.jump.keyword = keyword->payload.identifier.text;
    parse_token_advance(parser);

    Token const *name = parse_token_current(parser);
    if (name->token_type == TOKEN_TYPE_IDENTIFIER) {
        Ast_Node_Index label = parse_node_make(parser, AST_NODE_TYPE_LABEL);
        if (label == AST_NODE_INDEX_NONE) {
            return AST_NODE_INDEX_NONE;
        }
        parser->nodes[label].payload.label.name = name->payload.identifier.text;
        parse_node_append_child(parser, jump, label);
        parse_token_advance(parser);
    } else if (name->token_type == TOKEN_TYPE_LABEL) {
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
    assert(parse_token_current(parser)->token_type == TOKEN_TYPE_KEYWORD_ELSE);

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

    if (parse_token_current(parser)->token_type == TOKEN_TYPE_KEYWORD_IF) {
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
        if (parse_token_current(parser)->token_type != TOKEN_TYPE_KEYWORD_OR) {
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
        if (parse_token_current(parser)->token_type != TOKEN_TYPE_KEYWORD_AND) {
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
        if (parse_token_current(parser)->token_type != TOKEN_TYPE_KEYWORD_NOT) {
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
    Token_Type operator_token = parse_token_current(parser)->token_type;
    if (operator_token == TOKEN_TYPE_GREATER) {
        operator = AST_COMPARISON_OPERATOR_GREATER;
    } else if (operator_token == TOKEN_TYPE_GREATER_EQUALS) {
        operator = AST_COMPARISON_OPERATOR_GREATER_EQUAL;
    } else if (operator_token == TOKEN_TYPE_LESS) {
        operator = AST_COMPARISON_OPERATOR_LESS;
    } else if (operator_token == TOKEN_TYPE_LESS_EQUALS) {
        operator = AST_COMPARISON_OPERATOR_LESS_EQUAL;
    } else if (operator_token == TOKEN_TYPE_NOT_EQUALS) {
        operator = AST_COMPARISON_OPERATOR_NOT_EQUAL;
    } else if (operator_token != TOKEN_TYPE_EQUALS_EQUALS) {
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
    if (next == TOKEN_TYPE_KEYWORD_BREAK || next == TOKEN_TYPE_KEYWORD_CONTINUE) {
        return parse_jump_statement(parser);
    }
    if (next == TOKEN_TYPE_IDENTIFIER) {
        // The name is not the last token, because the last one is `END`.
        assert(parser->token_index + 1 < parser->token_count);
        Token_Type after_name = parser->tokens[parser->token_index + 1].token_type;
        if (after_name == TOKEN_TYPE_TYPE_SEPARATOR) {
            return parse_variable_definition(parser);
        }
        if (after_name == TOKEN_TYPE_PARENTHESIS_LEFT) {
            return parse_call_statement(parser);
        }
        return parse_assignment(parser);
    }

    return parse_fail(parser, STR("a statement"));
}

/**
 * Parses `name += value`, or the same with another assignment operator. The node for the
 * assignment has two children: the name, as an identifier, and the value. The node is made before
 * the name, like the node of every statement, so that the nodes of a statement come after it in
 * the order of the source, which the analysis relies on. A name that is followed by anything else
 * than `:` or an assignment operator is the error here.
 */
static Ast_Node_Index parse_assignment(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index + 1 < parser->token_count);

    // The operator is the token after the name, which is not the last one, because the last is
    // `END`.
    Token_Type operator = parser->tokens[parser->token_index + 1].token_type;
    Ast_Node_Type node_type = AST_NODE_TYPE_ADDITION_ASSIGNMENT;
    if (operator == TOKEN_TYPE_SLASH_EQUALS) {
        node_type = AST_NODE_TYPE_DIVISION_ASSIGNMENT;
    } else if (operator == TOKEN_TYPE_ASTERISK_EQUALS) {
        node_type = AST_NODE_TYPE_MULTIPLICATION_ASSIGNMENT;
    } else if (operator == TOKEN_TYPE_MINUS_EQUALS) {
        node_type = AST_NODE_TYPE_SUBTRACTION_ASSIGNMENT;
    } else if (operator != TOKEN_TYPE_PLUS_EQUALS) {
        // The error is at the token after the name.
        parse_token_advance(parser);
        return parse_fail(parser, STR("':' or an assignment operator"));
    }

    Ast_Node_Index assignment = parse_node_make(parser, node_type);
    if (assignment == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    Ast_Node_Index target = parse_identifier(parser);
    if (target == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parse_node_append_child(parser, assignment, target);
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
 * Parses a call, `name(argument, argument)`, where the arguments are expressions separated by
 * commas, and there may be none. The node of the call is made before the nodes of its arguments.
 * A call is an operand, so it binds tighter than any operator: `f(x) + 1` is `(f(x)) + 1`.
 */
static Ast_Node_Index parse_call(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index + 1 < parser->token_count);

    Token const *name = parse_token_current(parser);
    if (name->token_type != TOKEN_TYPE_IDENTIFIER) {
        return parse_fail(parser, STR("a name"));
    }

    Ast_Node_Index call = parse_node_make(parser, AST_NODE_TYPE_CALL);
    if (call == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[call].payload.call.name = name->payload.identifier.text;
    parse_token_advance(parser);

    if (parse_token_expect(parser, TOKEN_TYPE_PARENTHESIS_LEFT, STR("'('")) == false) {
        return AST_NODE_INDEX_NONE;
    }

    if (parse_token_current(parser)->token_type != TOKEN_TYPE_PARENTHESIS_RIGHT) {
        // Every argument consumes a token, so there are fewer arguments than tokens, which bounds
        // the loop.
        for (size_t i = 0; i < parser->token_count; i += 1) {
            Ast_Node_Index argument = parse_expression(parser);
            if (argument == AST_NODE_INDEX_NONE) {
                return AST_NODE_INDEX_NONE;
            }
            parse_node_append_child(parser, call, argument);

            if (parse_token_current(parser)->token_type != TOKEN_TYPE_COMMA) {
                break;
            }
            parse_token_advance(parser);
        }
    }

    if (parse_token_expect(parser, TOKEN_TYPE_PARENTHESIS_RIGHT, STR("',' or ')'")) == false) {
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
 * Parses a value: an integer literal, a string literal, `true` or `false`, the name of
 * something, such as a parameter, or a call of a procedure.
 */
static Ast_Node_Index parse_operand(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Token_Type next = parse_token_current(parser)->token_type;
    if (next == TOKEN_TYPE_INTEGER_LITERAL) {
        return parse_integer_literal(parser);
    }
    if (next == TOKEN_TYPE_IDENTIFIER) {
        // The name is not the last token, because the last one is `END`.
        assert(parser->token_index + 1 < parser->token_count);
        if (parser->tokens[parser->token_index + 1].token_type == TOKEN_TYPE_PARENTHESIS_LEFT) {
            return parse_call(parser);
        }
        return parse_identifier(parser);
    }
    if (next == TOKEN_TYPE_STRING_LITERAL) {
        return parse_string_literal(parser);
    }
    if (next == TOKEN_TYPE_KEYWORD_TRUE || next == TOKEN_TYPE_KEYWORD_FALSE) {
        return parse_bool_literal(parser);
    }

    return parse_fail(parser, STR("an integer literal, a string literal, 'true', 'false', a name "
                                  "or a call"));
}

static Ast_Node_Index parse_bool_literal(Parser *parser) {
    assert(parser->status == PARSE_STATUS_OK);
    assert(parser->token_index < parser->token_count);

    Token_Type keyword = parse_token_current(parser)->token_type;
    if (keyword != TOKEN_TYPE_KEYWORD_TRUE && keyword != TOKEN_TYPE_KEYWORD_FALSE) {
        return parse_fail(parser, STR("'true' or 'false'"));
    }

    Ast_Node_Index node = parse_node_make(parser, AST_NODE_TYPE_BOOL_LITERAL);
    if (node == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    parser->nodes[node].payload.bool_literal.value = keyword == TOKEN_TYPE_KEYWORD_TRUE;
    parse_token_advance(parser);

    assert(parser->nodes[node].first_child == AST_NODE_INDEX_NONE);
    return node;
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
