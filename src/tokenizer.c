// This file contains the implementation of the tokenizer, which converts a source string into
// a sequence of tokens.
//
// The tokenizer is designed to be simple and efficient, with a focus on ASCII characters and
// a limited set of token types. Character classes are ASCII only and spelled out instead of
// using `<ctype.h>`, whose results // depend on the locale, so that behavior does not change
// between machines.
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

#include <mialu/tokenizer.h>

static bool char_is_digit     (char character);
static bool char_is_letter    (char character);
static bool char_is_lower     (char character);
static bool char_is_upper     (char character);
static bool char_is_whitespace(char character);

static bool char_is_digit(char character) {
    return character >= '0' && character <= '9';
}

static bool char_is_letter(char character) {
    return char_is_lower(character) || char_is_upper(character);
}

static bool char_is_lower(char character) {
    return character >= 'a' && character <= 'z';
}

static bool char_is_upper(char character) {
    return character >= 'A' && character <= 'Z';
}

static bool char_is_whitespace(char character) {
    return (
        character == ' ' ||
        character == '\t' ||
        character == '\r' ||
        character == '\n'
    );
}

Tokenize_Result tokenize(Str source, Array_Slice token_buffer) {
    assert(token_buffer.elements != NULL);
    assert(source.bytes != NULL || source.length == 0);

    Token *token_elements = token_buffer.elements;
    Tokenize_Result result = {
        .tokens = {
            .elements = token_buffer.elements,
            .length = 0
        },
        .error_offset = 0,
        .status = TOKENIZE_STATUS_OK,
    };

    // Each iteration consumes at least one byte, so the loop ends after `source.length` steps.
    size_t offset = 0;
    while (offset < source.length) {
        char character = source.bytes[offset];

        if (char_is_whitespace(character)) {
            // Ignore whitespace.
            offset += 1;
            continue;
        }
        // If the token buffer is full, the tokenizer cannot continue, so it returns an error.
        else if (result.tokens.length == token_buffer.length) {
            result.status = TOKENIZE_STATUS_TOKEN_LIMIT;
            result.error_offset = offset;
            return result;
        }
        // Only decimal format is supported for integer literals.
        else if (char_is_digit(character)) {
            int64_t value = 0;
            bool is_too_large = false;

            // Bounded by `source.length`, since each iteration consumes one digit.
            size_t end = offset;
            while (end < source.length && char_is_digit(source.bytes[end])) {
                int64_t digit = source.bytes[end] - '0';

                // Checked before multiplying, because signed overflow is undefined behavior.
                if (value > (INT64_MAX - digit) / 10) {
                    is_too_large = true;
                    break;
                }
                value = value * 10 + digit;
                end += 1;
            }

            if (is_too_large) {
                result.status = TOKENIZE_STATUS_INTEGER_LITERAL_TOO_LARGE;
                result.error_offset = offset;
                return result;
            }

            // A letter or underscore right after the digits would otherwise be silently split into
            // a separate identifier, but identifiers cannot begin with a digit.
            if (end < source.length && (char_is_letter(source.bytes[end]) ||
                                        source.bytes[end] == '_')) {
                result.status = TOKENIZE_STATUS_UNEXPECTED_CHARACTER;
                result.error_offset = end;
                return result;
            }

            token_elements[result.tokens.length] = (Token){
                .token_type = TOKEN_TYPE_INTEGER_LITERAL,
                .integer_literal = {
                    .value = value,
                }
            };
            result.tokens.length += 1;
            offset = end;
        }
        // If the character is a letter or underscore, it starts an identifier:
        else if (char_is_letter(character) || character == '_') {
            // The start character is already known to be valid, so scanning continues after it.
            // An identifier part is a start character or a digit, which is how identifiers cannot
            // begin with a digit but may contain one.
            size_t end = offset + 1;
            while (end < source.length) {
                char part = source.bytes[end];

                if (char_is_letter(part) || char_is_digit(part) || part == '_') {
                    end += 1;
                } else {
                    break;
                }
            }

            assert(end > offset);
            assert(end <= source.length);

            Str text = str_make(source.bytes + offset, end - offset);

            // Keywords are scanned like identifiers and told apart afterwards, so that a longer
            // identifier such as `returned` is not split into a keyword and a remainder.
            Token_Type token_type = TOKEN_TYPE_IDENTIFIER;
            if (str_equals(text, STR("return"))) {
                token_type = TOKEN_TYPE_KEYWORD_RETURN;
            }

            token_elements[result.tokens.length] = (Token){
                .token_type = token_type,
                .identifier = {
                    .text = text,
                }
            };
            result.tokens.length += 1;
            offset = end;
        }
        else if (character == (char)TOKEN_TYPE_PARENTHESIS_LEFT) {
            token_elements[result.tokens.length] = (Token){
                .token_type = TOKEN_TYPE_PARENTHESIS_LEFT,
            };
            result.tokens.length += 1;
            offset += 1;
        }
        else if (character == (char)TOKEN_TYPE_PARENTHESIS_RIGHT) {
            token_elements[result.tokens.length] = (Token){
                .token_type = TOKEN_TYPE_PARENTHESIS_RIGHT,
            };
            result.tokens.length += 1;
            offset += 1;
        }
        else if (character == (char)TOKEN_TYPE_TYPE_SEPARATOR) {
            token_elements[result.tokens.length] = (Token){
                .token_type = TOKEN_TYPE_TYPE_SEPARATOR,
            };
            result.tokens.length += 1;
            offset += 1;
        }
        // If the two subsequent characters form a `->` token, it is recognized as a single token.
        else if (
            character == '-' &&
            offset + 1 < source.length &&
            source.bytes[offset + 1] == '>'
        ) {
            token_elements[result.tokens.length] = (Token){
                .token_type = TOKEN_TYPE_ARROW,
            };
            result.tokens.length += 1;
            offset += 2;
        }
        // Unrecognized characters are an error, since the language has no error recovery yet.
        else {
            result.status = TOKENIZE_STATUS_UNEXPECTED_CHARACTER;
            result.error_offset = offset;
            return result;
        }
    }

    assert(result.tokens.length <= token_buffer.length);
    assert(result.status == TOKENIZE_STATUS_OK);
    return result;
}
