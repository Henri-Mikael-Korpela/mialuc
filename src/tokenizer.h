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
    TOKEN_TYPE_INDENTATION,
    TOKEN_TYPE_INTEGER_LITERAL,
    TOKEN_TYPE_KEYWORD_RETURN,

    TOKEN_TYPE_END,
} Token_Type;

typedef struct {
    Token_Type token_type;
    // This union is named, because unnamed unions are C11 and the project uses C99.
    // Only the member that matches `token_type` is meaningful.
    union {
        struct {
            // The number of indentation levels, where one level is four spaces. Zero for a line
            // that is not indented.
            uint32_t level;
        } indentation;
        struct {
            Str text;
        } identifier;
        struct {
            // Signed so that it can later hold the result of negating a literal.
            // Literals that do not fit are rejected by the tokenizer.
            int64_t value;
        } integer_literal;
    } payload;
} Token;

// The enum is 4 bytes in GCC, so 4 bytes of padding follow it, and the largest payload is 16 bytes.
STATIC_ASSERT(sizeof(Token) == 24, token_size_is_24_bytes);

typedef enum {
    TOKENIZE_STATUS_OK,
    TOKENIZE_STATUS_INDENTATION_TAB,
    TOKENIZE_STATUS_INDENTATION_WIDTH_INVALID,
    TOKENIZE_STATUS_INTEGER_LITERAL_TOO_LARGE,
    TOKENIZE_STATUS_TOKEN_LIMIT,
    TOKENIZE_STATUS_UNEXPECTED_CHARACTER,
} Tokenize_Status;

/**
 * On `TOKENIZE_STATUS_OK` only `tokens` is meaningful: it is the filled start of the token buffer,
 * as `Token` elements. The last token is always `TOKEN_TYPE_END`, and it appears only there, so
 * `tokens` is never empty. Otherwise `error_offset` is the byte offset in the source where
 * tokenizing stopped.
 */
typedef struct {
    Array_Slice tokens;
    size_t error_offset;
    Tokenize_Status status;
} Tokenize_Result;

/**
 * Splits `source` into tokens written to `token_buffer`, which holds `Token` elements. Skips
 * whitespace. Stops at the first character that cannot start a token, since the language has no
 * error recovery yet. One element of `token_buffer` is reserved for the `TOKEN_TYPE_END` token,
 * so
 * at most `token_buffer.length - 1` other tokens fit, and the buffer must hold at least one
 * element.
 */
Tokenize_Result tokenize(Str source, Array_Slice token_buffer);

#endif
