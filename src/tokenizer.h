#ifndef MIALU_TOKENIZER_H
#define MIALU_TOKENIZER_H

#include <mialu/array_slice.h>
#include <mialu/static_assert.h>
#include <mialu/str.h>

typedef enum {
    TOKEN_TYPE_TYPE_SEPARATOR = ':',
    TOKEN_TYPE_IDENTIFIER,
} Token_Type;

typedef struct {
    Token_Type token_type;
    struct {
        Str text;
    } identifier;
} Token;

// The enum is 4 bytes in GCC, so 4 bytes of padding follow it.
STATIC_ASSERT(sizeof(Token) == 24, token_size_is_24_bytes);

typedef enum {
    TOKENIZE_STATUS_OK,
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
