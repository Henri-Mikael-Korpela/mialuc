#ifndef MIALU_TOKENIZER_H
#define MIALU_TOKENIZER_H

#include <stdint.h>

#include <mialu/array_slice.h>
#include <mialu/static_assert.h>
#include <mialu/str.h>

typedef enum {
    // Tokens consisting of a single character are represented by their ASCII value:
    TOKEN_TYPE_ASTERISK = '*',
    TOKEN_TYPE_COMMA = ',',
    TOKEN_TYPE_EQUALS = '=',
    TOKEN_TYPE_GREATER = '>',
    TOKEN_TYPE_LESS = '<',
    TOKEN_TYPE_MINUS = '-',
    TOKEN_TYPE_PARENTHESIS_LEFT = '(',
    TOKEN_TYPE_PARENTHESIS_RIGHT = ')',
    TOKEN_TYPE_PLUS = '+',
    TOKEN_TYPE_SLASH = '/',
    TOKEN_TYPE_TYPE_SEPARATOR = ':',

    // Tokens consisting of multiple characters are represented by a unique value, which starts
    // above the ASCII range so that it cannot be the value of a single-character token:
    TOKEN_TYPE_ARROW = 256,
    TOKEN_TYPE_ASTERISK_EQUALS,
    TOKEN_TYPE_EQUALS_EQUALS,
    TOKEN_TYPE_GREATER_EQUALS,
    TOKEN_TYPE_IDENTIFIER,
    TOKEN_TYPE_INDENTATION,
    TOKEN_TYPE_INTEGER_LITERAL,
    TOKEN_TYPE_KEYWORD_ELSE,
    TOKEN_TYPE_KEYWORD_FALSE,
    TOKEN_TYPE_KEYWORD_IF,
    TOKEN_TYPE_KEYWORD_PROC,
    TOKEN_TYPE_KEYWORD_RETURN,
    TOKEN_TYPE_KEYWORD_TRUE,
    TOKEN_TYPE_LESS_EQUALS,
    TOKEN_TYPE_MINUS_EQUALS,
    TOKEN_TYPE_NOT_EQUALS,
    TOKEN_TYPE_PLUS_EQUALS,
    TOKEN_TYPE_SLASH_EQUALS,
    TOKEN_TYPE_STRING_LITERAL,

    TOKEN_TYPE_END,
} Token_Type;

typedef struct {
    Token_Type token_type;
    // The byte offset of the token in the source, so that later stages can say where an error
    // is. It fits in the 4 bytes of padding after the enum, and sources are far below 4 GiB.
    uint32_t source_offset;
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
        struct {
            // The bytes between the quotes, as a view of the source text. They are valid UTF-8,
            // which the tokenizer checks. It is never NULL, also for the empty string, so that
            // its position in the source can be worked out.
            Str text;
        } string_literal;
    } payload;
} Token;

// The enum is 4 bytes in GCC and `source_offset` fills the 4 bytes after it. The largest payload
// is 16 bytes.
STATIC_ASSERT(sizeof(Token) == 24, token_size_is_24_bytes);

typedef enum {
    TOKENIZE_STATUS_OK,
    TOKENIZE_STATUS_INDENTATION_TAB,
    TOKENIZE_STATUS_INDENTATION_WIDTH_INVALID,
    TOKENIZE_STATUS_INTEGER_LITERAL_TOO_LARGE,
    // A backslash in a string. It is kept for escape sequences, which the language does not have
    // yet, so that strings written today keep their meaning when it gets them.
    TOKENIZE_STATUS_STRING_LITERAL_BACKSLASH,
    // The string has bytes that are not valid UTF-8. `error_offset` is where the first bad
    // character starts.
    TOKENIZE_STATUS_STRING_LITERAL_ENCODING_INVALID,
    // The string has no closing quote before the end of its line.
    TOKENIZE_STATUS_STRING_LITERAL_UNTERMINATED,
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
