#ifndef MIALU_H_PARSER
#define MIALU_H_PARSER

#include <stddef.h>

#include <mialu/array_slice.h>
#include <mialu/ast.h>
#include <mialu/str.h>

typedef enum {
    PARSE_STATUS_OK,
    PARSE_STATUS_NODE_LIMIT,
    /**
     * A `return` without a value in a procedure that has a return type.
     * `expected` is the return type, as the source writes it, which is a view of the source, and
     * `error_token_index` is the token after the `return`.
     */
    PARSE_STATUS_RETURN_VALUE_MISSING,
    PARSE_STATUS_UNEXPECTED_TOKEN,
} Parse_Status;

/**
 * On `PARSE_STATUS_OK`, `nodes` is the filled start of the node buffer, as `Ast_Node` elements,
 * and `root` is the index in it of the `AST_NODE_TYPE_MODULE` node. Otherwise `root` is
 * `AST_NODE_INDEX_NONE`, `error_token_index` is the index of the token where parsing stopped and,
 * for `PARSE_STATUS_UNEXPECTED_TOKEN`, `expected` describes what the parser wanted there. The
 * text of `expected` is a string literal and stays valid, and for
 * `PARSE_STATUS_RETURN_VALUE_MISSING` it is a view of the source.
 */
typedef struct {
    Array_Slice nodes;
    Str expected;
    size_t error_token_index;
    Ast_Node_Index root;
    Parse_Status status;
} Parse_Result;

/**
 * Builds a syntax tree from `tokens`, which holds `Token` elements and must be the tokens of a
 * successful `tokenize`, so the last one is `TOKEN_KIND_END`. The nodes are written to
 * `node_buffer`, which holds `Ast_Node` elements. Stops at the first token that does not fit the
 * grammar (see top of parser.h), since the parser has no error recovery yet.
 */
Parse_Result parse(Array_Slice tokens, Array_Slice node_buffer);

/**
 * Like `parse`, for a source file whose nodes go after the first `node_start` of `node_buffer`,
 * which another file has: the indexes of the nodes are those of the whole buffer, and the nodes of
 * the result are the whole buffer up to the last node that was made.
 */
Parse_Result parse_at(Array_Slice tokens, Array_Slice node_buffer, size_t node_start);

#endif // MIALU_H_PARSER
