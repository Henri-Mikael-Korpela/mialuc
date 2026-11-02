#ifndef MIALU_TOKENIZER_H
#define MIALU_TOKENIZER_H

#include <stdint.h>

#include <mialu/array_slice.h>
#include <mialu/static_assert.h>
#include <mialu/str.h>

typedef enum {
    // Tokens consisting of a single character are represented by their ASCII value:
    TOKEN_TYPE_PARENTHESIS_LEFT = '(',
    TOKEN_TYPE_PARENTHESIS_RIGHT = ')',
    TOKEN_TYPE_TYPE_SEPARATOR = ':',

    // Tokens consisting of multiple characters are represented by a unique value:
    TOKEN_TYPE_ARROW,
    TOKEN_TYPE_IDENTIFIER,
    TOKEN_TYPE_INTEGER_LITERAL,
    TOKEN_TYPE_KEYWORD_RETURN,
} Token_Type;

typedef struct {
    Token_Type token_type;
    union {
        struct {
            Str text;
        } identifier;
        struct {
            // Signed so that it can later hold the result of negating a literal.
            // Literals that do not fit are rejected by the tokenizer.
            int64_t value;
        } integer_literal;
    };
} Token;

// The enum is 4 bytes in GCC, so 4 bytes of padding follow it. A union of the payloads would save
// 8 bytes, but unnamed unions are not C99.
STATIC_ASSERT(sizeof(Token) == 32, token_size_is_32_bytes);

typedef enum {
    TOKENIZE_STATUS_OK,
    TOKENIZE_STATUS_INTEGER_LITERAL_TOO_LARGE,
    TOKENIZE_STATUS_TOKEN_LIMIT,
    TOKENIZE_STATUS_UNEXPECTED_CHARACTER,
} Tokenize_Status;

/**
 * On `TOKENIZE_STATUS_OK` only `tokens` is meaningful: it is the filled start of the token buffer,
 * as `Token` elements. Otherwise `error_offset` is the byte offset in the source where tokenizing
 * stopped.
 */
typedef struct {
    Array_Slice tokens;
    size_t error_offset;
    Tokenize_Status status;
} Tokenize_Result;

/**
 * Splits `source` into tokens written to `token_buffer`, which holds `Token` elements. Skips
 * whitespace. Stops at the first character that cannot start a token, since the language has no
 * error recovery yet.
 */
Tokenize_Result tokenize(Str source, Array_Slice token_buffer);

#endif
