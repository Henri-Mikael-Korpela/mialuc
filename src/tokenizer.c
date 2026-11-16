// This file contains the implementation of the tokenizer, which converts a source string into
// a sequence of tokens.
//
// The tokenizer is designed to be simple and efficient, with a focus on ASCII characters and
// a limited set of token kinds. Character classes are ASCII only and spelled out instead of
// using `<ctype.h>`, whose results // depend on the locale, so that behavior does not change
// between machines.
#include <assert.h>
#include <iso646.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <mialu/common_types.h>
#include <mialu/tokenizer.h>

/**
 * An integer literal that `tokenizer_integer_literal` read. On `TOKENIZE_STATUS_OK`, `value` is
 * its value and `end` is the offset right after it. Otherwise `error_offset` is where the mistake
 * is.
 */
typedef struct {
    int64_t value;
    size_t end;
    size_t error_offset;
    Tokenize_Status status;
} Tokenize_Integer_Literal;

/**
 * A float literal that `tokenizer_float_literal` read. `is_float` says whether the text is shaped
 * like one, which is digits, a dot and a digit. When it is not, nothing else is set and the text is
 * an integer literal or something else. When it is, then on `TOKENIZE_STATUS_OK` `value` is its
 * value and `end` is the offset right after it, and otherwise `error_offset` is where the mistake
 * is.
 */
typedef struct {
    double value;
    size_t end;
    size_t error_offset;
    Tokenize_Status status;
    bool is_float;
} Tokenize_Float_Literal;

/**
 * A rune literal that `tokenizer_rune_literal` read. On `TOKENIZE_STATUS_OK`, `value` is the code
 * point and `end` is the offset right after the closing quote. Otherwise `error_offset` is where
 * the mistake is.
 */
typedef struct {
    uint32_t value;
    size_t end;
    size_t error_offset;
    Tokenize_Status status;
} Tokenize_Rune_Literal;

enum {
    // Indentation must be a whole number of levels, and one level is always this many spaces.
    INDENTATION_SPACES_PER_LEVEL = 4,
    // The most characters that a float literal can have, digit separators included, which is far
    // more than the 17 digits that tell two `double` values apart. The digits are copied to a
    // buffer of this size, without the separators, for `strtod`.
    TOKENIZER_FLOAT_LITERAL_LENGTH_MAX = 128,
};

static bool   char_is_digit     (char character);
static bool   char_is_letter    (char character);
static bool   char_is_lower     (char character);
static bool   char_is_upper     (char character);
static bool   char_is_whitespace(char character);
static size_t utf8_sequence_size(Str text, size_t offset);
static int    char_digit_value  (char character);
static bool   tokenizer_comment_line_end(Str source, size_t offset, size_t *end);
static Tokenize_Float_Literal tokenizer_float_literal(Str source, size_t offset);
static Tokenize_Rune_Literal tokenizer_rune_literal(Str source, size_t offset);
static uint32_t utf8_decode(Str text, size_t offset, size_t sequence_size);
static bool   tokenizer_escape_read(Str source, size_t offset, uint32_t *value, size_t *length);
static Tokenize_Integer_Literal tokenizer_integer_literal(Str source, size_t offset);
static Token_Kind tokenizer_keyword_type(Str text);
static bool   tokenizer_operator_types(char character, Token_Kind *plain_type,
                                       Token_Kind *equals_type);

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
    byte lead = (byte)text.bytes[offset];
    size_t continuation_count = 0;
    // The range of the byte after the lead, which is narrower than the others for some leads.
    byte second_low = 0x80;
    byte second_high = 0xBF;

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
        byte byte_value = (byte)text.bytes[offset + i];
        byte low = 0x80;
        byte high = 0xBF;
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

/**
 * Returns the value of `character` as a digit of a base up to 16: 0 to 9 for the decimal digits,
 * and 10 to 15 for `a` to `f` and `A` to `F`. Returns -1 for any other character, which is above
 * no base, so that the caller only has to compare the value with its base.
 */
static int char_digit_value(char character) {
    if (char_is_digit(character)) {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
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
 * Finds where the line that has a comment at `offset` ends, which is where its line break or the
 * end of `source` is, and sets `*end` to that. A `
` ends the line too, so that a comment does
 * not hold the carriage return of a Windows line break. Returns false when a character on the way
 * is not valid UTF-8, with `*end` at its start. The check is made here, because comments are views
 * of the source, and a `Str` is promised to be UTF-8.
 */
static bool tokenizer_comment_line_end(Str source, size_t offset, size_t *end) {
    assert(offset <= source.length);
    assert(end != NULL);

    // Bounded by `source.length`, since each iteration consumes at least one byte.
    size_t position = offset;
    while (position < source.length && source.bytes[position] != '\n' &&
           source.bytes[position] != '\n') {
        size_t sequence_size = utf8_sequence_size(source, position);
        if (sequence_size == 0) {
            *end = position;
            return false;
        }
        position += sequence_size;
    }

    *end = position;
    return true;
}

/**
 * Returns whether `character` is an operator that is also a token with an `=` after it, and sets
 * `*plain_type` to the token of the character alone and `*equals_type` to the token with the `=`.
 */
static bool tokenizer_operator_types(char character, Token_Kind *plain_type,
                                     Token_Kind *equals_type) {
    switch (character) {
    case '=':
        *plain_type = TOKEN_KIND_EQUALS;
        *equals_type = TOKEN_KIND_EQUALS_EQUALS;
        return true;
    case '>':
        *plain_type = TOKEN_KIND_GREATER;
        *equals_type = TOKEN_KIND_GREATER_EQUALS;
        return true;
    case '<':
        *plain_type = TOKEN_KIND_LESS;
        *equals_type = TOKEN_KIND_LESS_EQUALS;
        return true;
    case '*':
        *plain_type = TOKEN_KIND_ASTERISK;
        *equals_type = TOKEN_KIND_ASTERISK_EQUALS;
        return true;
    case '+':
        *plain_type = TOKEN_KIND_PLUS;
        *equals_type = TOKEN_KIND_PLUS_EQUALS;
        return true;
    case '/':
        *plain_type = TOKEN_KIND_SLASH;
        *equals_type = TOKEN_KIND_SLASH_EQUALS;
        return true;
    default:
        return false;
    }
}

/**
 * Returns the keyword token kind of `text`, or `TOKEN_KIND_IDENTIFIER` when it is not a reserved
 * word. `not` followed by `=` must be handled before, because that is the token `not=`.
 */
/**
 * Returns the code point of the UTF-8 character that starts at `offset` in `text` and takes
 * `sequence_size` bytes, which `utf8_sequence_size` found to be valid.
 */
static uint32_t utf8_decode(Str text, size_t offset, size_t sequence_size) {
    assert(sequence_size >= 1 && sequence_size <= 4);
    assert(offset + sequence_size <= text.length);

    byte lead = (byte)text.bytes[offset];
    if (sequence_size == 1) {
        return lead;
    }
    // The lead byte has the highest bits of the code point after its own marker of the length,
    // and each continuation byte has 6 more, after its marker `10`.
    uint32_t code_point = lead & (0x7Fu >> sequence_size);
    for (size_t i = 1; i < sequence_size; i += 1) {
        code_point = (code_point << 6) | ((byte)text.bytes[offset + i] & 0x3Fu);
    }
    return code_point;
}

/**
 * Reads the escape sequence that starts at `offset` in `source`, where there is a backslash, and
 * sets `value` to the code point that it stands for and `length` to the number of bytes that it
 * takes, the backslash included. The sequences are:
 *
 * - `\a`, `\b`, `\e`, `\f`, `\n`, `\r`, `\t` and `\v` are the control characters bell, backspace,
 *   escape, form feed, line feed, carriage return, horizontal tab and vertical tab,
 * - `\\`, `\'` and `\"` are the backslash and the two quotes,
 * - a backslash and three octal digits, such as `\101`, is the code point of that number, which is
 *   at most 255, `\x` and two hexadecimal digits, such as `\x41`, is the code point of that number,
 *   `\u` and four, such as `\u0123`, and `\U` and eight, such as `\U0001F680`, are the code point
 *   of that number, which has to be a Unicode character: at most 0x10FFFF and not a surrogate.
 *
 * The digits of a hexadecimal number are of either case. Returns false when the backslash does not
 * start one of them.
 */
static bool tokenizer_escape_read(Str source, size_t offset, uint32_t *value, size_t *length) {
    assert(offset < source.length);
    assert(source.bytes[offset] == '\\');
    assert(value != NULL);
    assert(length != NULL);

    if (offset + 1 >= source.length) {
        return false;
    }
    char kind = source.bytes[offset + 1];

    // The characters that are one letter after the backslash.
    static struct {
        char letter;
        uint32_t code_point;
    } const SIMPLE[] = {
        {'a', 0x07}, {'b', 0x08}, {'e', 0x1B}, {'f', 0x0C}, {'n', 0x0A}, {'r', 0x0D},
        {'t', 0x09}, {'v', 0x0B}, {'\\', '\\'}, {'\'', '\''}, {'"', '"'},
    };
    for (size_t i = 0; i < sizeof(SIMPLE) / sizeof(SIMPLE[0]); i += 1) {
        if (SIMPLE[i].letter == kind) {
            *value = SIMPLE[i].code_point;
            *length = 2;
            return true;
        }
    }

    // A number, in digits of a base, which are all of the count that the kind says: three octal
    // digits, or two, four or eight hexadecimal ones.
    size_t digit_count = 0;
    uint32_t base = 16;
    size_t digits_start = offset + 2;
    if (kind >= '0' && kind <= '7') {
        digit_count = 3;
        base = 8;
        digits_start = offset + 1;
    } else if (kind == 'x') {
        digit_count = 2;
    } else if (kind == 'u') {
        digit_count = 4;
    } else if (kind == 'U') {
        digit_count = 8;
    } else {
        return false;
    }
    if (digits_start + digit_count > source.length) {
        return false;
    }

    uint32_t number = 0;
    for (size_t i = 0; i < digit_count; i += 1) {
        int digit = char_digit_value(source.bytes[digits_start + i]);
        if (digit < 0 || (uint32_t)digit >= base) {
            return false;
        }
        number = number * base + (uint32_t)digit;
    }

    // An octal number is a byte, so it is at most 255, and the others are Unicode characters.
    bool is_surrogate = number >= 0xD800 && number <= 0xDFFF;
    if (number > 0x10FFFF || is_surrogate || (base == 8 && number > 255)) {
        return false;
    }
    *value = number;
    *length = (digits_start - offset) + digit_count;
    return true;
}

/**
 * Reads the rune literal that starts at `offset` in `source`, where there is a single quote: one
 * character, which is valid UTF-8 and so can be any Unicode character, such as `'A'`, `'芋'` or
 * `'🚀'`, or an escape sequence that stands for one, such as `'\n'` or `'\u0123'`, see
 * `tokenizer_escape_read`, and a closing single quote. It is on one line. A character is not
 * several code points, so a letter with an accent that is a base letter and a combining mark is two
 * characters and an error, and the one code point of the letter is a rune.
 */
static Tokenize_Rune_Literal tokenizer_rune_literal(Str source, size_t offset) {
    assert(offset < source.length);
    assert(source.bytes[offset] == '\'');

    Tokenize_Rune_Literal literal = { .status = TOKENIZE_STATUS_OK };

    size_t start = offset + 1;
    bool is_line_end = start >= source.length || source.bytes[start] == '\n' ||
                       source.bytes[start] == '\r';
    if (is_line_end) {
        literal.status = TOKENIZE_STATUS_RUNE_LITERAL_UNTERMINATED;
        literal.error_offset = offset;
        return literal;
    }
    if (source.bytes[start] == '\'') {
        literal.status = TOKENIZE_STATUS_RUNE_LITERAL_EMPTY;
        literal.error_offset = offset;
        return literal;
    }

    size_t sequence_size = 0;
    if (source.bytes[start] == '\\') {
        if (not tokenizer_escape_read(source, start, &literal.value, &sequence_size)) {
            literal.status = TOKENIZE_STATUS_RUNE_LITERAL_ESCAPE_INVALID;
            literal.error_offset = start;
            return literal;
        }
    } else {
        sequence_size = utf8_sequence_size(source, start);
        if (sequence_size == 0) {
            literal.status = TOKENIZE_STATUS_RUNE_LITERAL_ENCODING_INVALID;
            literal.error_offset = start;
            return literal;
        }
        literal.value = utf8_decode(source, start, sequence_size);
    }

    size_t end = start + sequence_size;
    if (end < source.length && source.bytes[end] == '\'') {
        literal.end = end + 1;
        return literal;
    }

    // There is more after the character. A closing quote later on the line says that the literal has
    // too many characters, and none that it is not closed.
    literal.status = TOKENIZE_STATUS_RUNE_LITERAL_UNTERMINATED;
    for (size_t i = end; i < source.length && source.bytes[i] != '\n' && source.bytes[i] != '\r';
         i += 1) {
        if (source.bytes[i] == '\'') {
            literal.status = TOKENIZE_STATUS_RUNE_LITERAL_TOO_LONG;
            break;
        }
    }
    literal.error_offset = offset;
    return literal;
}

/**
 * Reads the float literal that starts at `offset` in `source`, where there is a digit: decimal
 * digits, a dot and decimal digits, such as `3.14` or `0.5`. An underscore is a digit separator
 * between two digits, as in an integer literal, so `1_000.000_1` is one number. There are digits on
 * both sides of the dot, so `1.` and `.5` are not float literals, and the dot of `1..<5` is not one
 * either: a dot that is not followed by a digit is the token of its own that it always was. There
 * is no exponent and no suffix, so a letter after the digits is an error, like the letter of an
 * integer literal. A literal with a prefix, `0b` or `0x`, is an integer. The value is what the
 * digits are nearest to, as `strtod` finds it, which rounds correctly. The decimal point of
 * `strtod` is the one of the "C" locale, which the compiler never changes.
 */
static Tokenize_Float_Literal tokenizer_float_literal(Str source, size_t offset) {
    assert(offset < source.length);
    assert(char_is_digit(source.bytes[offset]));

    Tokenize_Float_Literal literal = { .status = TOKENIZE_STATUS_OK };

    if (source.bytes[offset] == '0' && offset + 1 < source.length &&
        (source.bytes[offset + 1] == 'b' || source.bytes[offset + 1] == 'x')) {
        return literal;
    }

    // The whole part is digits and separators, up to a dot that has a digit after it.
    size_t dot = offset;
    while (dot < source.length && (char_is_digit(source.bytes[dot]) || source.bytes[dot] == '_')) {
        dot += 1;
    }
    if (dot + 1 >= source.length || source.bytes[dot] != '.' ||
        not char_is_digit(source.bytes[dot + 1])) {
        return literal;
    }
    literal.is_float = true;

    // The fraction goes on for as long as there are letters, digits and underscores, like an
    // integer literal does, so that a wrong character is reported as part of the literal.
    size_t end = dot + 1;
    while (end < source.length && (char_is_letter(source.bytes[end]) ||
                                   char_is_digit(source.bytes[end]) ||
                                   source.bytes[end] == '_')) {
        end += 1;
    }
    literal.end = end;

    // One more for the zero that ends the text.
    char text[TOKENIZER_FLOAT_LITERAL_LENGTH_MAX + 1] = {0};
    size_t text_length = 0;
    for (size_t i = offset; i < end; i += 1) {
        char character = source.bytes[i];
        if (i == dot) {
            // The dot is followed by a digit, which was checked above.
            assert(char_is_digit(source.bytes[i + 1]));
        } else if (character == '_') {
            // The first character is a digit, the dot has a digit after it, and a separator
            // must have a digit on both sides.
            bool is_after_digit = i > offset && char_is_digit(source.bytes[i - 1]);
            bool is_before_digit = i + 1 < end && char_is_digit(source.bytes[i + 1]);
            if (not is_after_digit || not is_before_digit) {
                literal.status = TOKENIZE_STATUS_FLOAT_LITERAL_SEPARATOR_INVALID;
                literal.error_offset = i;
                return literal;
            }
            continue;
        } else if (not char_is_digit(character)) {
            literal.status = TOKENIZE_STATUS_FLOAT_LITERAL_DIGIT_INVALID;
            literal.error_offset = i;
            return literal;
        }

        if (text_length == TOKENIZER_FLOAT_LITERAL_LENGTH_MAX) {
            literal.status = TOKENIZE_STATUS_FLOAT_LITERAL_TOO_LONG;
            literal.error_offset = offset;
            return literal;
        }
        text[text_length] = character;
        text_length += 1;
    }

    // `strtod` gives infinity for a number that is too large, and zero or a number that is not
    // normal for one that is too small, which is a value that is nearest.
    char *text_end = NULL;
    double value = strtod(text, &text_end);
    assert(text_end == text + text_length);
    if (value > 1.7976931348623157e308) {
        literal.status = TOKENIZE_STATUS_FLOAT_LITERAL_TOO_LARGE;
        literal.error_offset = offset;
        return literal;
    }
    literal.value = value;
    return literal;
}

/**
 * Reads the integer literal that starts at `offset` in `source`, where there is a digit. It is
 * decimal, or binary when it starts with `0b`, or hexadecimal when it starts with `0x`. An
 * underscore is a digit separator, which only makes the number easier to read, and it must be
 * between two digits, so that `1__000`, `1_` and `0x_FF` are errors: a separator has one meaning,
 * and the start of the digits is always a digit.
 *
 * The literal is taken to go on for as long as there are letters, digits and underscores, so a
 * character that is not a digit of its base, such as the `2` of `0b102` or the `g` of `0xfg`, is
 * reported as a wrong digit of the literal. It would otherwise be split off into a token of its own
 * and give a less clear error, or none: an identifier cannot begin with a digit, so a letter right
 * after the digits is never the start of a name.
 */
static Tokenize_Integer_Literal tokenizer_integer_literal(Str source, size_t offset) {
    assert(offset < source.length);
    assert(char_is_digit(source.bytes[offset]));

    Tokenize_Integer_Literal literal = { .status = TOKENIZE_STATUS_OK };

    // The prefix is lowercase only, so that a literal has one spelling. The digits of a
    // hexadecimal literal can be either case, since `0xff` and `0xFF` are both common.
    int base = 10;
    size_t digits_start = offset;
    if (source.bytes[offset] == '0' && offset + 1 < source.length) {
        if (source.bytes[offset + 1] == 'b') {
            base = 2;
            digits_start = offset + 2;
        } else if (source.bytes[offset + 1] == 'x') {
            base = 16;
            digits_start = offset + 2;
        }
    }

    // Bounded by `source.length`, since each iteration consumes one character.
    size_t end = digits_start;
    while (end < source.length && (char_is_letter(source.bytes[end]) ||
                                   char_is_digit(source.bytes[end]) ||
                                   source.bytes[end] == '_')) {
        end += 1;
    }
    literal.end = end;

    if (digits_start == end) {
        literal.status = TOKENIZE_STATUS_INTEGER_LITERAL_DIGITS_MISSING;
        literal.error_offset = offset;
        return literal;
    }

    int64_t value = 0;
    for (size_t i = digits_start; i < end; i += 1) {
        char character = source.bytes[i];

        // The first character is never a separator and the last one is not either, and a
        // separator is never followed by another, so each one is between two digits.
        if (character == '_') {
            if (i == digits_start || i + 1 == end || source.bytes[i + 1] == '_') {
                literal.status = TOKENIZE_STATUS_INTEGER_LITERAL_SEPARATOR_INVALID;
                literal.error_offset = i;
                return literal;
            }
            continue;
        }

        int digit = char_digit_value(character);
        if (digit < 0 || digit >= base) {
            literal.status = TOKENIZE_STATUS_INTEGER_LITERAL_DIGIT_INVALID;
            literal.error_offset = i;
            return literal;
        }

        // Checked before multiplying, because signed overflow is undefined behavior. A binary or
        // hexadecimal literal has the same limit as a decimal one, because it is the same `Int`,
        // so `0xFFFF_FFFF_FFFF_FFFF` is too large rather than -1.
        if (value > (INT64_MAX - digit) / base) {
            literal.status = TOKENIZE_STATUS_INTEGER_LITERAL_TOO_LARGE;
            literal.error_offset = offset;
            return literal;
        }
        value = value * base + digit;
    }

    literal.value = value;
    return literal;
}

static Token_Kind tokenizer_keyword_type(Str text) {
    if (str_equals(text, STR("proc"))) {
        return TOKEN_KIND_KEYWORD_PROC;
    } else if (str_equals(text, STR("break"))) {
        return TOKEN_KIND_KEYWORD_BREAK;
    } else if (str_equals(text, STR("case"))) {
        return TOKEN_KIND_KEYWORD_CASE;
    } else if (str_equals(text, STR("cast"))) {
        return TOKEN_KIND_KEYWORD_CAST;
    } else if (str_equals(text, STR("const"))) {
        return TOKEN_KIND_KEYWORD_CONST;
    } else if (str_equals(text, STR("continue"))) {
        return TOKEN_KIND_KEYWORD_CONTINUE;
    } else if (str_equals(text, STR("defer"))) {
        return TOKEN_KIND_KEYWORD_DEFER;
    } else if (str_equals(text, STR("for"))) {
        return TOKEN_KIND_KEYWORD_FOR;
    } else if (str_equals(text, STR("foreign"))) {
        return TOKEN_KIND_KEYWORD_FOREIGN;
    } else if (str_equals(text, STR("import"))) {
        return TOKEN_KIND_KEYWORD_IMPORT;
    } else if (str_equals(text, STR("in"))) {
        return TOKEN_KIND_KEYWORD_IN;
    } else if (str_equals(text, STR("match"))) {
        return TOKEN_KIND_KEYWORD_MATCH;
    } else if (str_equals(text, STR("if"))) {
        return TOKEN_KIND_KEYWORD_IF;
    } else if (str_equals(text, STR("else"))) {
        return TOKEN_KIND_KEYWORD_ELSE;
    } else if (str_equals(text, STR("enum"))) {
        return TOKEN_KIND_KEYWORD_ENUM;
    } else if (str_equals(text, STR("return"))) {
        return TOKEN_KIND_KEYWORD_RETURN;
    } else if (str_equals(text, STR("struct"))) {
        return TOKEN_KIND_KEYWORD_STRUCT;
    } else if (str_equals(text, STR("true"))) {
        return TOKEN_KIND_KEYWORD_TRUE;
    } else if (str_equals(text, STR("type_info_of"))) {
        return TOKEN_KIND_KEYWORD_TYPE_INFO_OF;
    } else if (str_equals(text, STR("union"))) {
        return TOKEN_KIND_KEYWORD_UNION;
    } else if (str_equals(text, STR("false"))) {
        return TOKEN_KIND_KEYWORD_FALSE;
    } else if (str_equals(text, STR("nil"))) {
        return TOKEN_KIND_KEYWORD_NIL;
    } else if (str_equals(text, STR("not"))) {
        return TOKEN_KIND_KEYWORD_NOT;
    } else if (str_equals(text, STR("and"))) {
        return TOKEN_KIND_KEYWORD_AND;
    } else if (str_equals(text, STR("or"))) {
        return TOKEN_KIND_KEYWORD_OR;
    }
    return TOKEN_KIND_IDENTIFIER;
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
    // Whether the next token is a comment that begins its line, which a `///` must.
    bool is_comment_line = false;
    Token_Kind plain_type = TOKEN_KIND_END;
    Token_Kind equals_type = TOKEN_KIND_END;
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
                } else if (not has_tab) {
                    has_tab = true;
                    tab_offset = end;
                }
                end += 1;
            }

            bool is_blank = end == source.length || source.bytes[end] == '\n' ||
                            source.bytes[end] == '\r';
            is_line_start = false;

            bool is_comment_only = end + 1 < source.length && source.bytes[end] == '/' &&
                                   source.bytes[end + 1] == '/';

            if (is_blank) {
                // Blank lines carry no indentation, so trailing spaces on them are harmless.
                offset = end;
            } else if (is_comment_only) {
                // A line of only a comment is not part of the structure of the code, so it gets no
                // indentation token, and any indentation is fine. A token would otherwise be left
                // alone on a line when the comment is dropped.
                is_comment_line = true;
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
                    .kind = TOKEN_KIND_INDENTATION,
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
        // A comment is `//` and everything up to the end of the line. It is a token, so that tools
        // can see it, and it is dropped before parsing. It comes before the operators, because it
        // starts with the `/` of one. `///` is a documentation comment, which goes on over the
        // following lines for as long as each of them starts with `///` after its indentation.
        else if (character == '/' && offset + 1 < source.length &&
                 source.bytes[offset + 1] == '/') {
            bool is_documentation = offset + 2 < source.length && source.bytes[offset + 2] == '/';
            if (is_documentation && not is_comment_line) {
                result.status = TOKENIZE_STATUS_DOCUMENTATION_COMMENT_TRAILING;
                result.error_offset = offset;
                return result;
            }
            is_comment_line = false;

            size_t end = 0;
            if (not tokenizer_comment_line_end(source, offset, &end)) {
                result.status = TOKENIZE_STATUS_COMMENT_ENCODING_INVALID;
                result.error_offset = end;
                return result;
            }

            // Each iteration takes a whole line of at least three bytes, so it is bounded by
            // `source.length`.
            while (is_documentation) {
                size_t next = end;
                if (next < source.length && source.bytes[next] == '\n') {
                    next += 1;
                }
                if (next >= source.length || source.bytes[next] != '\n') {
                    break;
                }
                next += 1;
                while (next < source.length &&
                       (source.bytes[next] == ' ' || source.bytes[next] == '\t')) {
                    next += 1;
                }
                bool is_next_documentation = next + 2 < source.length &&
                                             source.bytes[next] == '/' &&
                                             source.bytes[next + 1] == '/' &&
                                             source.bytes[next + 2] == '/';
                if (not is_next_documentation) {
                    break;
                }
                if (not tokenizer_comment_line_end(source, next, &end)) {
                    result.status = TOKENIZE_STATUS_COMMENT_ENCODING_INVALID;
                    result.error_offset = end;
                    return result;
                }
            }

            token_elements[result.tokens.length] = (Token){
                .kind = is_documentation ? TOKEN_KIND_DOCUMENTATION_COMMENT
                                               : TOKEN_KIND_COMMENT,
                .source_offset = (uint32_t)offset,
                .payload.comment.text = str_make(source.bytes + offset, end - offset),
            };
            result.tokens.length += 1;
            offset = end;
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
            if (not is_terminated) {
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
                .kind = TOKEN_KIND_STRING_LITERAL,
                .source_offset = (uint32_t)offset,
                .payload.string_literal.text = str_make(source.bytes + offset + 1,
                                                        end - offset - 1),
            };
            result.tokens.length += 1;
            offset = end + 1;
        }
        // A number is a float literal when it has a dot that is followed by a digit, and an
        // integer literal otherwise. The integer literal is decimal, or binary after `0b`, or
        // hexadecimal after `0x`.
        else if (char_is_digit(character)) {
            Tokenize_Float_Literal float_literal = tokenizer_float_literal(source, offset);
            if (float_literal.is_float) {
                if (float_literal.status != TOKENIZE_STATUS_OK) {
                    result.status = float_literal.status;
                    result.error_offset = float_literal.error_offset;
                    return result;
                }

                token_elements[result.tokens.length] = (Token){
                    .kind = TOKEN_KIND_FLOAT_LITERAL,
                    .source_offset = (uint32_t)offset,
                    .payload.float_literal.value = float_literal.value,
                };
                result.tokens.length += 1;
                offset = float_literal.end;
                continue;
            }

            Tokenize_Integer_Literal literal = tokenizer_integer_literal(source, offset);
            if (literal.status != TOKENIZE_STATUS_OK) {
                result.status = literal.status;
                result.error_offset = literal.error_offset;
                return result;
            }

            token_elements[result.tokens.length] = (Token){
                .kind = TOKEN_KIND_INTEGER_LITERAL,
                .source_offset = (uint32_t)offset,
                .payload.integer_literal.value = literal.value,
            };
            result.tokens.length += 1;
            offset = literal.end;
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
                    .kind = TOKEN_KIND_NOT_EQUALS,
                    .source_offset = (uint32_t)offset,
                };
                result.tokens.length += 1;
                offset = end + 1;
                continue;
            }

            // Keywords are scanned like identifiers and told apart afterwards, so that a longer
            // identifier such as `returned` is not split into a keyword and a remainder.
            Token_Kind token_kind = tokenizer_keyword_type(text);

            token_elements[result.tokens.length] = (Token){
                .kind = token_kind,
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
            if (not is_name_start) {
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
            if (tokenizer_keyword_type(name) != TOKEN_KIND_IDENTIFIER) {
                result.status = TOKENIZE_STATUS_LABEL_INVALID;
                result.error_offset = offset;
                return result;
            }

            token_elements[result.tokens.length] = (Token){
                .kind = TOKEN_KIND_LABEL,
                .source_offset = (uint32_t)offset,
                .payload.identifier.text = name,
            };
            result.tokens.length += 1;
            offset = end;
        }
        // A rune literal is one character between single quotes.
        else if (character == '\'') {
            Tokenize_Rune_Literal literal = tokenizer_rune_literal(source, offset);
            if (literal.status != TOKENIZE_STATUS_OK) {
                result.status = literal.status;
                result.error_offset = literal.error_offset;
                return result;
            }

            token_elements[result.tokens.length] = (Token){
                .kind = TOKEN_KIND_RUNE_LITERAL,
                .source_offset = (uint32_t)offset,
                .payload.rune_literal.value = literal.value,
            };
            result.tokens.length += 1;
            offset = literal.end;
        }
        // An operator that is a single character, or that character with an `=` directly after it,
        // which is one token: `=` and `==`, `<` and `<=`, `>` and `>=`, `*` and `*=`, `+` and `+=`,
        // `/` and `/=`.
        else if (tokenizer_operator_types(character, &plain_type, &equals_type)) {
            Token_Kind token_kind = plain_type;
            size_t size_bytes = 1;
            if (offset + 1 < source.length && source.bytes[offset + 1] == '=') {
                token_kind = equals_type;
                size_bytes = 2;
            }

            token_elements[result.tokens.length] = (Token){
                .kind = token_kind,
                .source_offset = (uint32_t)offset,
            };
            result.tokens.length += 1;
            offset += size_bytes;
        }
        else if (character == (char)TOKEN_KIND_HASH) {
            token_elements[result.tokens.length] = (Token){
                .kind = TOKEN_KIND_HASH,
                .source_offset = (uint32_t)offset,
            };
            result.tokens.length += 1;
            offset += 1;
        }
        // The brackets, the braces, the caret and the percent sign are single tokens that mean the
        // same everywhere: `^` and `%` are never the start of a longer token.
        else if (character == (char)TOKEN_KIND_BRACE_LEFT ||
                 character == (char)TOKEN_KIND_BRACE_RIGHT ||
                 character == (char)TOKEN_KIND_BRACKET_LEFT ||
                 character == (char)TOKEN_KIND_BRACKET_RIGHT ||
                 character == (char)TOKEN_KIND_CARET ||
                 character == (char)TOKEN_KIND_PERCENT) {
            token_elements[result.tokens.length] = (Token){
                .kind = (Token_Kind)character,
                .source_offset = (uint32_t)offset,
            };
            result.tokens.length += 1;
            offset += 1;
        }
        else if (character == (char)TOKEN_KIND_COMMA) {
            token_elements[result.tokens.length] = (Token){
                .kind = TOKEN_KIND_COMMA,
                .source_offset = (uint32_t)offset,
            };
            result.tokens.length += 1;
            offset += 1;
        }
        else if (character == (char)TOKEN_KIND_PARENTHESIS_LEFT) {
            token_elements[result.tokens.length] = (Token){
                .kind = TOKEN_KIND_PARENTHESIS_LEFT,
                .source_offset = (uint32_t)offset,
            };
            result.tokens.length += 1;
            offset += 1;
        }
        else if (character == (char)TOKEN_KIND_PARENTHESIS_RIGHT) {
            token_elements[result.tokens.length] = (Token){
                .kind = TOKEN_KIND_PARENTHESIS_RIGHT,
                .source_offset = (uint32_t)offset,
            };
            result.tokens.length += 1;
            offset += 1;
        }
        else if (character == (char)TOKEN_KIND_TYPE_SEPARATOR) {
            token_elements[result.tokens.length] = (Token){
                .kind = TOKEN_KIND_TYPE_SEPARATOR,
                .source_offset = (uint32_t)offset,
            };
            result.tokens.length += 1;
            offset += 1;
        }
        // A `-` is the minus operator, unless the two subsequent characters form a `->` or a `-=`
        // token, which are recognized as single tokens.
        else if (character == (char)TOKEN_KIND_MINUS) {
            Token_Kind token_kind = TOKEN_KIND_MINUS;
            size_t size_bytes = 1;
            if (offset + 1 < source.length && source.bytes[offset + 1] == '>') {
                token_kind = TOKEN_KIND_ARROW;
                size_bytes = 2;
            } else if (offset + 1 < source.length && source.bytes[offset + 1] == '=') {
                token_kind = TOKEN_KIND_MINUS_EQUALS;
                size_bytes = 2;
            }

            token_elements[result.tokens.length] = (Token){
                .kind = token_kind,
                .source_offset = (uint32_t)offset,
            };
            result.tokens.length += 1;
            offset += size_bytes;
        }
        // The tokens that start with a `.` are `..<`, or `..<=` when an `=` follows it directly,
        // `...`, and the single dot of a field access. A `..` that is not followed by `<` or by a
        // third dot is two dots, which the parser rejects.
        else if (character == '.') {
            if (offset + 2 < source.length && source.bytes[offset + 1] == '.' &&
                source.bytes[offset + 2] == '.') {
                token_elements[result.tokens.length] = (Token){
                    .kind = TOKEN_KIND_ELLIPSIS,
                    .source_offset = (uint32_t)offset,
                };
                result.tokens.length += 1;
                offset += 3;
                continue;
            }

            bool is_range = offset + 2 < source.length && source.bytes[offset + 1] == '.' &&
                            source.bytes[offset + 2] == '<';
            if (not is_range) {
                token_elements[result.tokens.length] = (Token){
                    .kind = TOKEN_KIND_DOT,
                    .source_offset = (uint32_t)offset,
                };
                result.tokens.length += 1;
                offset += 1;
                continue;
            }

            Token_Kind token_kind = TOKEN_KIND_RANGE_EXCLUSIVE;
            size_t size_bytes = 3;
            if (offset + 3 < source.length && source.bytes[offset + 3] == '=') {
                token_kind = TOKEN_KIND_RANGE_INCLUSIVE;
                size_bytes = 4;
            }

            token_elements[result.tokens.length] = (Token){
                .kind = token_kind,
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
        .kind = TOKEN_KIND_END,
        .source_offset = (uint32_t)source.length,
    };
    result.tokens.length += 1;

    assert(result.tokens.length <= token_buffer.length);
    assert(result.status == TOKENIZE_STATUS_OK);
    return result;
}

Array_Slice tokens_without_comments(Array_Slice tokens, Array_Slice code_buffer) {
    assert(tokens.elements != NULL);
    assert(code_buffer.elements != NULL);
    assert(code_buffer.length >= tokens.length);

    Token const *token_elements = tokens.elements;
    Token *code_elements = code_buffer.elements;
    Array_Slice code = array_slice_make(code_buffer.elements, 0);
    for (size_t i = 0; i < tokens.length; i += 1) {
        Token_Kind token_kind = token_elements[i].kind;
        if (token_kind != TOKEN_KIND_COMMENT && token_kind != TOKEN_KIND_DOCUMENTATION_COMMENT) {
            code_elements[code.length] = token_elements[i];
            code.length += 1;
        }
    }

    return code;
}
