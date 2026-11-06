#ifndef MIALU_TOKENIZER_H
#define MIALU_TOKENIZER_H

#include <stdint.h>

#include <mialu/array_slice.h>
#include <mialu/static_assert.h>
#include <mialu/str.h>

typedef enum {
    // Tokens consisting of a single character are represented by their ASCII value:
    TOKEN_KIND_ASTERISK = '*',
    // The braces around the elements of an array literal, and the brackets around the length of an
    // array type and around the index of a subscript.
    TOKEN_KIND_BRACE_LEFT = '{',
    TOKEN_KIND_BRACE_RIGHT = '}',
    TOKEN_KIND_BRACKET_LEFT = '[',
    TOKEN_KIND_BRACKET_RIGHT = ']',
    // Written after a type to make it a pointer type, as in `^Int`, and after an expression to
    // follow the pointer that it is, as in `p^`.
    TOKEN_KIND_CARET = '^',
    TOKEN_KIND_COMMA = ',',
    // The dot of a field access, as in `p.age`. `..<` and `..<=` are the tokens of a range.
    TOKEN_KIND_DOT = '.',
    TOKEN_KIND_EQUALS = '=',
    TOKEN_KIND_GREATER = '>',
    // Starts the attribute list of a definition, such as `#(static)`.
    TOKEN_KIND_HASH = '#',
    TOKEN_KIND_LESS = '<',
    TOKEN_KIND_MINUS = '-',
    TOKEN_KIND_PARENTHESIS_LEFT = '(',
    TOKEN_KIND_PARENTHESIS_RIGHT = ')',
    // Written before an expression to take its address, as in `%x`.
    TOKEN_KIND_PERCENT = '%',
    TOKEN_KIND_PLUS = '+',
    TOKEN_KIND_SLASH = '/',
    TOKEN_KIND_TYPE_SEPARATOR = ':',

    // Tokens consisting of multiple characters are represented by a unique value, which starts
    // above the ASCII range so that it cannot be the value of a single-character token:
    TOKEN_KIND_ARROW = 256,
    TOKEN_KIND_ASTERISK_EQUALS,
    // `...`, which stands where the body of a procedure would be, for a procedure that is defined
    // elsewhere.
    TOKEN_KIND_ELLIPSIS,
    TOKEN_KIND_EQUALS_EQUALS,
    TOKEN_KIND_GREATER_EQUALS,
    TOKEN_KIND_IDENTIFIER,
    TOKEN_KIND_INDENTATION,
    TOKEN_KIND_INTEGER_LITERAL,
    TOKEN_KIND_KEYWORD_AND,
    TOKEN_KIND_KEYWORD_BREAK,
    TOKEN_KIND_KEYWORD_CASE,
    TOKEN_KIND_KEYWORD_CONST,
    TOKEN_KIND_KEYWORD_CONTINUE,
    TOKEN_KIND_KEYWORD_ELSE,
    TOKEN_KIND_KEYWORD_ENUM,
    TOKEN_KIND_KEYWORD_FALSE,
    TOKEN_KIND_KEYWORD_FOR,
    TOKEN_KIND_KEYWORD_FOREIGN,
    TOKEN_KIND_KEYWORD_IF,
    TOKEN_KIND_KEYWORD_IMPORT,
    TOKEN_KIND_KEYWORD_IN,
    TOKEN_KIND_KEYWORD_MATCH,
    TOKEN_KIND_KEYWORD_NIL,
    TOKEN_KIND_KEYWORD_NOT,
    TOKEN_KIND_KEYWORD_OR,
    TOKEN_KIND_KEYWORD_PROC,
    TOKEN_KIND_KEYWORD_RETURN,
    TOKEN_KIND_KEYWORD_STRUCT,
    TOKEN_KIND_KEYWORD_TRUE,
    TOKEN_KIND_KEYWORD_TYPE_INFO_OF,
    TOKEN_KIND_KEYWORD_UNION,
    // `@` and a name with nothing in between, such as `@outer`. The name without the `@` is in the
    // `identifier` payload.
    TOKEN_KIND_LABEL,
    TOKEN_KIND_LESS_EQUALS,
    TOKEN_KIND_MINUS_EQUALS,
    TOKEN_KIND_NOT_EQUALS,
    TOKEN_KIND_PLUS_EQUALS,
    // `..<` and `..<=`, which are the ends of an exclusive and of an inclusive range.
    TOKEN_KIND_RANGE_EXCLUSIVE,
    TOKEN_KIND_RANGE_INCLUSIVE,
    TOKEN_KIND_SLASH_EQUALS,
    TOKEN_KIND_STRING_LITERAL,

    TOKEN_KIND_END,
} Token_Kind;

typedef struct {
    Token_Kind token_kind;
    // The byte offset of the token in the source, so that later stages can say where an error
    // is. It fits in the 4 bytes of padding after the enum, and sources are far below 4 GiB.
    uint32_t source_offset;
    // This union is named, because unnamed unions are C11 and the project uses C99.
    // Only the member that matches `token_kind` is meaningful.
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
    // An `@` that is not directly followed by a name, or whose name is a reserved word.
    TOKENIZE_STATUS_LABEL_INVALID,
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
 * as `Token` elements. The last token is always `TOKEN_KIND_END`, and it appears only there, so
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
 * error recovery yet. One element of `token_buffer` is reserved for the `TOKEN_KIND_END` token,
 * so
 * at most `token_buffer.length - 1` other tokens fit, and the buffer must hold at least one
 * element.
 */
Tokenize_Result tokenize(Str source, Array_Slice token_buffer);

#endif
