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

enum {
    // Indentation must be a whole number of levels, and one level is always this many spaces.
    INDENTATION_SPACES_PER_LEVEL = 4,
};

static bool   char_is_digit     (char character);
static bool   char_is_letter    (char character);
static bool   char_is_lower     (char character);
static bool   char_is_upper     (char character);
static bool   char_is_whitespace(char character);
static size_t utf8_sequence_size(Str text, size_t offset);
static Token_Type tokenizer_keyword_type(Str text);
static bool   tokenizer_operator_types(char character, Token_Type *plain_type,
                                       Token_Type *equals_type);

/**
 * Returns the number of bytes of the UTF-8 encoded character that starts at `offset` in `text`,
 * or zero when the bytes there are not a valid encoding. The rules are the strict ones of
 * RFC 3629: a character is encoded in the fewest bytes that can hold it, the surrogates U+D800 to
 * U+DFFF have no encoding, and nothing is above U+10FFFF. They are checked so that a `Str`, which
 * is promised to be UTF-8, never holds bytes that other software would reject or read differently.
 * Written as the ranges of the second byte, because that is where the rules differ.
 */
static size_t utf8_sequence_size(Str text, size_t offset) {
    assert(offset < text.length);

    // The sequence is a lead byte, and then this many continuation bytes, which are 0x80 to 0xBF.
    uint8_t lead = (uint8_t)text.bytes[offset];
    size_t continuation_count = 0;
    // The range of the byte after the lead, which is narrower than the others for some leads.
    uint8_t second_low = 0x80;
    uint8_t second_high = 0xBF;

    if (lead < 0x80) {
        return 1;
    } else if (lead >= 0xC2 && lead <= 0xDF) {
        continuation_count = 1;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        continuation_count = 2;
        if (lead == 0xE0) {
            // Anything lower would be an overlong encoding of a character below U+0800.
            second_low = 0xA0;
        } else if (lead == 0xED) {
            // Anything higher would be a surrogate.
            second_high = 0x9F;
        }
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        continuation_count = 3;
        if (lead == 0xF0) {
            // Anything lower would be an overlong encoding of a character below U+10000.
            second_low = 0x90;
        } else if (lead == 0xF4) {
            // Anything higher would be above U+10FFFF.
            second_high = 0x8F;
        }
    } else {
        // 0x80 to 0xBF is a continuation byte without a lead, 0xC0 and 0xC1 would be overlong
        // encodings of ASCII, and 0xF5 to 0xFF would be above U+10FFFF.
        return 0;
    }

    if (continuation_count > text.length - offset - 1) {
        return 0;
    }
    for (size_t i = 1; i <= continuation_count; i += 1) {
        uint8_t byte_value = (uint8_t)text.bytes[offset + i];
        uint8_t low = 0x80;
        uint8_t high = 0xBF;
        if (i == 1) {
            low = second_low;
            high = second_high;
        }
        if (byte_value < low || byte_value > high) {
            return 0;
        }
    }

    return continuation_count + 1;
}

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

/**
 * Returns whether `character` is an operator that is also a token with an `=` after it, and sets
 * `*plain_type` to the token of the character alone and `*equals_type` to the token with the `=`.
 */
static bool tokenizer_operator_types(char character, Token_Type *plain_type,
                                     Token_Type *equals_type) {
    switch (character) {
    case '=':
        *plain_type = TOKEN_TYPE_EQUALS;
        *equals_type = TOKEN_TYPE_EQUALS_EQUALS;
        return true;
    case '>':
        *plain_type = TOKEN_TYPE_GREATER;
        *equals_type = TOKEN_TYPE_GREATER_EQUALS;
        return true;
    case '<':
        *plain_type = TOKEN_TYPE_LESS;
        *equals_type = TOKEN_TYPE_LESS_EQUALS;
        return true;
    case '*':
        *plain_type = TOKEN_TYPE_ASTERISK;
        *equals_type = TOKEN_TYPE_ASTERISK_EQUALS;
        return true;
    case '+':
        *plain_type = TOKEN_TYPE_PLUS;
        *equals_type = TOKEN_TYPE_PLUS_EQUALS;
        return true;
    case '/':
        *plain_type = TOKEN_TYPE_SLASH;
        *equals_type = TOKEN_TYPE_SLASH_EQUALS;
        return true;
    default:
        return false;
    }
}

/**
 * Returns the keyword token type of `text`, or `TOKEN_TYPE_IDENTIFIER` when it is not a reserved
 * word. `not` followed by `=` must be handled before, because that is the token `not=`.
 */
static Token_Type tokenizer_keyword_type(Str text) {
    if (str_equals(text, STR("proc"))) {
        return TOKEN_TYPE_KEYWORD_PROC;
    } else if (str_equals(text, STR("break"))) {
        return TOKEN_TYPE_KEYWORD_BREAK;
    } else if (str_equals(text, STR("const"))) {
        return TOKEN_TYPE_KEYWORD_CONST;
    } else if (str_equals(text, STR("continue"))) {
        return TOKEN_TYPE_KEYWORD_CONTINUE;
    } else if (str_equals(text, STR("for"))) {
        return TOKEN_TYPE_KEYWORD_FOR;
    } else if (str_equals(text, STR("in"))) {
        return TOKEN_TYPE_KEYWORD_IN;
    } else if (str_equals(text, STR("if"))) {
        return TOKEN_TYPE_KEYWORD_IF;
    } else if (str_equals(text, STR("else"))) {
        return TOKEN_TYPE_KEYWORD_ELSE;
    } else if (str_equals(text, STR("return"))) {
        return TOKEN_TYPE_KEYWORD_RETURN;
    } else if (str_equals(text, STR("true"))) {
        return TOKEN_TYPE_KEYWORD_TRUE;
    } else if (str_equals(text, STR("false"))) {
        return TOKEN_TYPE_KEYWORD_FALSE;
    } else if (str_equals(text, STR("not"))) {
        return TOKEN_TYPE_KEYWORD_NOT;
    } else if (str_equals(text, STR("and"))) {
        return TOKEN_TYPE_KEYWORD_AND;
    } else if (str_equals(text, STR("or"))) {
        return TOKEN_TYPE_KEYWORD_OR;
    }
    return TOKEN_TYPE_IDENTIFIER;
}

Tokenize_Result tokenize(Str source, Array_Slice token_buffer) {
    assert(token_buffer.elements != NULL);
    assert(source.bytes != NULL || source.length == 0);

    // Offsets are stored in 32 bits, see `Token`.
    assert(source.length <= UINT32_MAX);

    Token *token_elements = token_buffer.elements;
    Tokenize_Result result = {
        .tokens = array_slice_make(token_buffer.elements, 0),
        .error_offset = 0,
        .status = TOKENIZE_STATUS_OK,
    };

    // One slot is kept free for the `END` token, so that it can always be added at the end.
    assert(token_buffer.length >= 1);
    size_t const token_count_max = token_buffer.length - 1;

    // An iteration consumes at least one byte, except the one that handles the start of a line,
    // which happens once per line. So the loop ends after at most twice `source.length` steps.
    size_t offset = 0;
    bool is_line_start = true;
    Token_Type plain_type = TOKEN_TYPE_END;
    Token_Type equals_type = TOKEN_TYPE_END;
    while (offset < source.length) {
        char character = source.bytes[offset];

        if (is_line_start) {
            // The run of spaces and tabs before the first other character on the line.
            size_t end = offset;
            size_t space_count = 0;
            size_t tab_offset = 0;
            bool has_tab = false;
            while (end < source.length && (source.bytes[end] == ' ' || source.bytes[end] == '\t')) {
                if (source.bytes[end] == ' ') {
                    space_count += 1;
                } else if (has_tab == false) {
                    has_tab = true;
                    tab_offset = end;
                }
                end += 1;
            }

            bool is_blank = end == source.length || source.bytes[end] == '\n' ||
                            source.bytes[end] == '\r';
            is_line_start = false;

            if (is_blank) {
                // Blank lines carry no indentation, so trailing spaces on them are harmless.
                offset = end;
            } else if (has_tab) {
                result.status = TOKENIZE_STATUS_INDENTATION_TAB;
                result.error_offset = tab_offset;
                return result;
            } else if (space_count % INDENTATION_SPACES_PER_LEVEL != 0) {
                result.status = TOKENIZE_STATUS_INDENTATION_WIDTH_INVALID;
                result.error_offset = end;
                return result;
            } else if (result.tokens.length == token_count_max) {
                result.status = TOKENIZE_STATUS_TOKEN_LIMIT;
                result.error_offset = offset;
                return result;
            } else {
                // Every non-blank line gets an indentation token, even at level zero, because
                // there is no separate end-of-line token to tell where a line starts.
                token_elements[result.tokens.length] = (Token){
                    .token_type = TOKEN_TYPE_INDENTATION,
                    .source_offset = (uint32_t)offset,
                    .payload.indentation.level =
                        (uint32_t)(space_count / INDENTATION_SPACES_PER_LEVEL),
                };
                result.tokens.length += 1;
                offset = end;
            }
            continue;
        }

        if (char_is_whitespace(character)) {
            // Ignore whitespace, but remember that a new line begins after a line feed.
            if (character == '\n') {
                is_line_start = true;
            }
            offset += 1;
            continue;
        }
        // If the token buffer is full, the tokenizer cannot continue, so it returns an error.
        else if (result.tokens.length == token_count_max) {
            result.status = TOKENIZE_STATUS_TOKEN_LIMIT;
            result.error_offset = offset;
            return result;
        }
        // A string is everything up to the next quote on the same line. It is not null-terminated
        // anywhere, because the token only keeps where its bytes are and how many there are.
        else if (character == '"') {
            // Bounded by `source.length`, since each iteration consumes at least one byte. A
            // character that is not ASCII takes several bytes, which are checked together, so
            // that the string is valid UTF-8 as a whole.
            size_t end = offset + 1;
            while (end < source.length) {
                char part = source.bytes[end];

                if (part == '"' || part == '\\' || part == '\n' || part == '\r') {
                    break;
                }

                size_t sequence_size = utf8_sequence_size(source, end);
                if (sequence_size == 0) {
                    result.status = TOKENIZE_STATUS_STRING_LITERAL_ENCODING_INVALID;
                    result.error_offset = end;
                    return result;
                }
                end += sequence_size;
            }

            // The end of the source, and the end of the line, both mean that no quote came.
            bool is_terminated = end < source.length && source.bytes[end] != '\n' &&
                                 source.bytes[end] != '\r';
            if (is_terminated == false) {
                result.status = TOKENIZE_STATUS_STRING_LITERAL_UNTERMINATED;
                result.error_offset = offset;
                return result;
            }
            if (source.bytes[end] == '\\') {
                result.status = TOKENIZE_STATUS_STRING_LITERAL_BACKSLASH;
                result.error_offset = end;
                return result;
            }

            assert(source.bytes[end] == '"');
            token_elements[result.tokens.length] = (Token){
                .token_type = TOKEN_TYPE_STRING_LITERAL,
                .source_offset = (uint32_t)offset,
                .payload.string_literal.text = str_make(source.bytes + offset + 1,
                                                        end - offset - 1),
            };
            result.tokens.length += 1;
            offset = end + 1;
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
                .source_offset = (uint32_t)offset,
                .payload.integer_literal.value = value,
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

            // `not` directly followed by `=`, with nothing in between, is the single token `not=`.
            // It is told apart here because it starts like an identifier. A longer identifier
            // such as `cannot` is not affected, and `not =` with a space is a name and an `=`.
            if (str_equals(text, STR("not")) && end < source.length && source.bytes[end] == '=') {
                token_elements[result.tokens.length] = (Token){
                    .token_type = TOKEN_TYPE_NOT_EQUALS,
                    .source_offset = (uint32_t)offset,
                };
                result.tokens.length += 1;
                offset = end + 1;
                continue;
            }

            // Keywords are scanned like identifiers and told apart afterwards, so that a longer
            // identifier such as `returned` is not split into a keyword and a remainder.
            Token_Type token_type = tokenizer_keyword_type(text);

            token_elements[result.tokens.length] = (Token){
                .token_type = token_type,
                .source_offset = (uint32_t)offset,
                .payload.identifier.text = text,
            };
            result.tokens.length += 1;
            offset = end;
        }
        // A label is an `@` directly followed by a name: no space may be in between. A reserved
        // word is not a name, as it is not for a variable.
        else if (character == '@') {
            size_t end = offset + 1;
            bool is_name_start = end < source.length &&
                                 (char_is_letter(source.bytes[end]) || source.bytes[end] == '_');
            if (is_name_start == false) {
                result.status = TOKENIZE_STATUS_LABEL_INVALID;
                result.error_offset = offset;
                return result;
            }
            while (end < source.length && (char_is_letter(source.bytes[end]) ||
                                           char_is_digit(source.bytes[end]) ||
                                           source.bytes[end] == '_')) {
                end += 1;
            }

            Str name = str_make(source.bytes + offset + 1, end - offset - 1);
            if (tokenizer_keyword_type(name) != TOKEN_TYPE_IDENTIFIER) {
                result.status = TOKENIZE_STATUS_LABEL_INVALID;
                result.error_offset = offset;
                return result;
            }

            token_elements[result.tokens.length] = (Token){
                .token_type = TOKEN_TYPE_LABEL,
                .source_offset = (uint32_t)offset,
                .payload.identifier.text = name,
            };
            result.tokens.length += 1;
            offset = end;
        }
        // An operator that is a single character, or that character with an `=` directly after it,
        // which is one token: `=` and `==`, `<` and `<=`, `>` and `>=`, `*` and `*=`, `+` and `+=`,
        // `/` and `/=`.
        else if (tokenizer_operator_types(character, &plain_type, &equals_type)) {
            Token_Type token_type = plain_type;
            size_t size_bytes = 1;
            if (offset + 1 < source.length && source.bytes[offset + 1] == '=') {
                token_type = equals_type;
                size_bytes = 2;
            }

            token_elements[result.tokens.length] = (Token){
                .token_type = token_type,
                .source_offset = (uint32_t)offset,
            };
            result.tokens.length += 1;
            offset += size_bytes;
        }
        else if (character == (char)TOKEN_TYPE_HASH) {
            token_elements[result.tokens.length] = (Token){
                .token_type = TOKEN_TYPE_HASH,
                .source_offset = (uint32_t)offset,
            };
            result.tokens.length += 1;
            offset += 1;
        }
        // The brackets and the braces are single tokens that mean the same everywhere.
        else if (character == (char)TOKEN_TYPE_BRACE_LEFT ||
                 character == (char)TOKEN_TYPE_BRACE_RIGHT ||
                 character == (char)TOKEN_TYPE_BRACKET_LEFT ||
                 character == (char)TOKEN_TYPE_BRACKET_RIGHT) {
            token_elements[result.tokens.length] = (Token){
                .token_type = (Token_Type)character,
                .source_offset = (uint32_t)offset,
            };
            result.tokens.length += 1;
            offset += 1;
        }
        else if (character == (char)TOKEN_TYPE_COMMA) {
            token_elements[result.tokens.length] = (Token){
                .token_type = TOKEN_TYPE_COMMA,
                .source_offset = (uint32_t)offset,
            };
            result.tokens.length += 1;
            offset += 1;
        }
        else if (character == (char)TOKEN_TYPE_PARENTHESIS_LEFT) {
            token_elements[result.tokens.length] = (Token){
                .token_type = TOKEN_TYPE_PARENTHESIS_LEFT,
                .source_offset = (uint32_t)offset,
            };
            result.tokens.length += 1;
            offset += 1;
        }
        else if (character == (char)TOKEN_TYPE_PARENTHESIS_RIGHT) {
            token_elements[result.tokens.length] = (Token){
                .token_type = TOKEN_TYPE_PARENTHESIS_RIGHT,
                .source_offset = (uint32_t)offset,
            };
            result.tokens.length += 1;
            offset += 1;
        }
        else if (character == (char)TOKEN_TYPE_TYPE_SEPARATOR) {
            token_elements[result.tokens.length] = (Token){
                .token_type = TOKEN_TYPE_TYPE_SEPARATOR,
                .source_offset = (uint32_t)offset,
            };
            result.tokens.length += 1;
            offset += 1;
        }
        // A `-` is the minus operator, unless the two subsequent characters form a `->` or a `-=`
        // token, which are recognized as single tokens.
        else if (character == (char)TOKEN_TYPE_MINUS) {
            Token_Type token_type = TOKEN_TYPE_MINUS;
            size_t size_bytes = 1;
            if (offset + 1 < source.length && source.bytes[offset + 1] == '>') {
                token_type = TOKEN_TYPE_ARROW;
                size_bytes = 2;
            } else if (offset + 1 < source.length && source.bytes[offset + 1] == '=') {
                token_type = TOKEN_TYPE_MINUS_EQUALS;
                size_bytes = 2;
            }

            token_elements[result.tokens.length] = (Token){
                .token_type = token_type,
                .source_offset = (uint32_t)offset,
            };
            result.tokens.length += 1;
            offset += size_bytes;
        }
        // The only token that starts with a `.` is `..<`, or `..<=` when an `=` follows it
        // directly. A lone `.` or `..` is an unexpected character, since nothing else uses it yet.
        else if (character == '.') {
            bool is_range = offset + 2 < source.length && source.bytes[offset + 1] == '.' &&
                            source.bytes[offset + 2] == '<';
            if (is_range == false) {
                result.status = TOKENIZE_STATUS_UNEXPECTED_CHARACTER;
                result.error_offset = offset;
                return result;
            }

            Token_Type token_type = TOKEN_TYPE_RANGE_EXCLUSIVE;
            size_t size_bytes = 3;
            if (offset + 3 < source.length && source.bytes[offset + 3] == '=') {
                token_type = TOKEN_TYPE_RANGE_INCLUSIVE;
                size_bytes = 4;
            }

            token_elements[result.tokens.length] = (Token){
                .token_type = token_type,
                .source_offset = (uint32_t)offset,
            };
            result.tokens.length += 1;
            offset += size_bytes;
        }
        // Unrecognized characters are an error, since the language has no error recovery yet.
        else {
            result.status = TOKENIZE_STATUS_UNEXPECTED_CHARACTER;
            result.error_offset = offset;
            return result;
        }
    }

    // Always exactly one `END` token, and always last.
    // This helps parsing because it never need a bounds check
    // to look at the token that follows the last real one.
    assert(result.tokens.length <= token_count_max);
    token_elements[result.tokens.length] = (Token){
        .token_type = TOKEN_TYPE_END,
        .source_offset = (uint32_t)source.length,
    };
    result.tokens.length += 1;

    assert(result.tokens.length <= token_buffer.length);
    assert(result.status == TOKENIZE_STATUS_OK);
    return result;
}
