// This file contains the implementation of the tokenizer, which converts a source string into
// a sequence of tokens.
//
// The tokenizer is designed to be simple and efficient, with a focus on ASCII characters and
// a limited set of token types. Character classes are ASCII only and spelled out instead of
// using `<ctype.h>`, whose results // depend on the locale, so that behavior does not change
// between machines.
#include <assert.h>
#include <stdbool.h>

#include <mialu/tokenizer.h>

static bool char_is_lower(char character) {
    return character >= 'a' && character <= 'z';
}

static bool char_is_digit(char character) {
    return character >= '0' && character <= '9';
}

static bool char_is_letter(char character) {
    return char_is_lower(character) || char_is_upper(character);
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

        // If the character is a letter or underscore, it starts an identifier:
        if (char_is_letter(character) || character == '_') {
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

            token_elements[result.tokens.length] = (Token){
                .token_type = TOKEN_TYPE_IDENTIFIER,
                .text = {
                    .bytes = source.bytes + offset,
                    .length = end - offset
                },
            };
            result.tokens.length += 1;
            offset = end;
        } else {
            result.status = TOKENIZE_STATUS_UNEXPECTED_CHARACTER;
            result.error_offset = offset;
            return result;
        }
    }

    assert(result.tokens.length <= token_buffer.length);
    assert(result.status == TOKENIZE_STATUS_OK);
    return result;
}
