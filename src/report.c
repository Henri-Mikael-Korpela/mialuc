#include <assert.h>
#include <inttypes.h>
#include <iso646.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <mialu/common_types.h>
#include <mialu/diagnostic.h>
#include <mialu/limits.h>
#include <mialu/report.h>
#include <mialu/str.h>
#include <mialu/tokenizer.h>

enum {
    // The codes are numbers below this, and the catalog has a place for each of them, so that the
    // text of an error is found by its code without a search. The numbers that are not used are
    // empty places.
    REPORT_CODE_COUNT = 1000,
    // The most bytes of the name, or of whatever else the error is about, that go into its text. A
    // longer one is cut and ends with `...`, so that an error is a few lines, whatever the program
    // has.
    REPORT_SUBJECT_LENGTH_MAX = 80,
    // The most bytes of each part of an error after the text of the catalog is filled in.
    REPORT_PART_SIZE_BYTES = 2048,
};

/**
 * The text of one kind of error, as templates. A part that the error has no use for is NULL. In a
 * template `{subject}` is the name, or the piece of the program, that the error is about, `{detail}`
 * is another piece of text, `{number}` is a number, such as a limit, and `{hex}` is the same number
 * in two hexadecimal digits.
 *
 * The title says what is wrong, in a sentence that names the thing, without a full stop. The label
 * is for the place that the carets point at, and says what is wrong there, briefly. The note says
 * why, which is the rule of the language, and the help says what to do. A part that would only say
 * again what another one says is left out.
 */
typedef struct {
    char const *title;
    char const *label;
    char const *note;
    char const *help;
} Report_Entry;

/**
 * Everything that the text of one error is made of.
 */
typedef struct {
    // The file, or NULL for an error that is not about a file.
    char const *path;
    Str source;
    Str subject;
    Str detail;
    size_t span_offset;
    size_t span_length;
    int64_t number;
    Error_Code code;
    bool has_span;
} Report_Error;

/**
 * A text that is being written to a buffer that has room for `capacity` bytes.
 */
typedef struct {
    char *bytes;
    size_t capacity;
    size_t length;
} Report_Text;

// The catalog is sparse, and a code has the place of its number, which `-Woverride-init`, part of
// `-Wextra`, turns into an error when two codes have the same number.
static Report_Entry const REPORT_ENTRIES[REPORT_CODE_COUNT] = {
    [ERROR_CODE_SOURCE_UNREADABLE] = {
        .title = "cannot read the source file",
        .note = "the file does not exist or cannot be opened, or it is larger than {number} bytes, "
                "which is the most that the compiler reads",
        .help = "check the path of the file",
    },
    [ERROR_CODE_OUTPUT_UNWRITABLE] = {
        .title = "cannot write the output file",
        .help = "check that the directory of the file exists, and that the file is not open in "
                "another program",
    },
    [ERROR_CODE_PATH_ABSOLUTE_UNKNOWN] = {
        .title = "cannot find the absolute path of the source file",
        .note = "debug information names the source file by its absolute path, which is how a "
                "debugger finds it whatever directory it runs in",
    },
    [ERROR_CODE_OUT_OF_MEMORY] = {
        .title = "out of memory",
        .note = "the compiler allocates what it needs once, when it starts, and the system did not "
                "give it that",
    },
    [ERROR_CODE_ASSEMBLY_WRITE_FAILED] = {
        .title = "cannot write the assembly to the standard output",
    },
    [ERROR_CODE_PROGRAM_FILE_FAILED] = {
        .title = "cannot write the program to the directory for temporary files",
        .note = "a program is written to a file there to run it",
    },
    [ERROR_CODE_PROGRAM_START_FAILED] = {
        .title = "the system cannot start the program",
        .note = "the system refused the file, for example because it is not a program for this "
                "machine",
    },
    [ERROR_CODE_PROGRAM_ARGUMENTS_TOO_LONG] = {
        .title = "the arguments of the program are longer than the system takes",
        .note = "the command line of a program is at most 32767 characters, with the name of the "
                "program",
    },
    [ERROR_CODE_RUN_UNSUPPORTED] = {
        .title = "running a program is only supported on Windows",
        .help = "build the wasm target to run a program elsewhere, see docs/playground.md",
    },

    // The tokens.
    [ERROR_CODE_COMMENT_ENCODING_INVALID] = {
        .title = "a comment must be valid UTF-8",
        .label = "this byte is not valid UTF-8",
        .note = "source code is UTF-8 text, and comments are part of it",
    },
    [ERROR_CODE_DOCUMENTATION_COMMENT_TRAILING] = {
        .title = "a documentation comment must be on lines of its own",
        .label = "this '///' comes after code on its line",
        .note = "a documentation comment documents the code after it, so it cannot follow code on "
                "the same line",
        .help = "use '//' for a note after code on a line",
    },
    [ERROR_CODE_INDENTATION_TAB] = {
        .title = "indentation must use spaces, not tabs",
        .label = "this is a tab",
        .help = "indent with four spaces for each level",
    },
    [ERROR_CODE_INDENTATION_WIDTH_INVALID] = {
        .title = "indentation must be a multiple of 4 spaces",
        .label = "indented by {number} spaces",
        .help = "indent with four spaces for each level",
    },
    [ERROR_CODE_FLOAT_LITERAL_DIGIT_INVALID] = {
        .title = "'{detail}' is not a digit of a float literal",
        .label = "not a digit",
        .note = "a float literal has the digits 0 to 9 and nothing else after its dot, since there "
                "is no exponent and no suffix",
    },
    [ERROR_CODE_FLOAT_LITERAL_SEPARATOR_INVALID] = {
        .title = "a digit separator '_' must be between two digits",
        .label = "this '_' is not between two digits",
        .help = "put a digit on each side of it, as in 1_000.5, or remove it",
    },
    [ERROR_CODE_FLOAT_LITERAL_TOO_LARGE] = {
        .title = "float literal is larger than the largest F64",
        .label = "the largest F64 is about 1.8e308",
    },
    [ERROR_CODE_FLOAT_LITERAL_TOO_LONG] = {
        .title = "float literal has more than 128 characters",
        .label = "too long",
    },
    [ERROR_CODE_INTEGER_LITERAL_DIGIT_INVALID] = {
        .title = "'{detail}' is not a digit of an integer literal",
        .label = "not a digit",
        .note = "an integer literal has the digits 0 to 9, or 0 and 1 after '0b', or 0 to 9 and a "
                "to f after '0x'",
    },
    [ERROR_CODE_INTEGER_LITERAL_DIGITS_MISSING] = {
        .title = "integer literal has no digits after its prefix",
        .label = "needs a digit after the prefix",
        .note = "'0b' is followed by binary digits and '0x' by hexadecimal digits",
    },
    [ERROR_CODE_INTEGER_LITERAL_SEPARATOR_INVALID] = {
        .title = "a digit separator '_' must be between two digits",
        .label = "this '_' is not between two digits",
        .help = "put a digit on each side of it, as in 1_000, or remove it",
    },
    [ERROR_CODE_INTEGER_LITERAL_TOO_LARGE] = {
        .title = "integer literal is larger than the largest Int",
        .label = "the largest Int is 9223372036854775807",
    },
    [ERROR_CODE_LABEL_INVALID] = {
        .title = "a label cannot be a reserved word",
        .label = "this is a reserved word",
        .note = "a label is a ':' directly followed by a name, and the name is not a reserved word",
    },
    [ERROR_CODE_RUNE_LITERAL_EMPTY] = {
        .title = "a rune literal has no character",
        .label = "there is no character between the quotes",
        .note = "a rune literal has a character between its quotes",
    },
    [ERROR_CODE_RUNE_LITERAL_ESCAPE_INVALID] = {
        .title = "this is not an escape sequence of a rune",
        .label = "not an escape sequence",
        .note = "an escape sequence is a backslash and one of a, b, e, f, n, r, t, v, \\, ' or \", "
                "three octal digits up to 377, x and two hexadecimal digits, u and four, or U and "
                "eight, with a number that is a Unicode character, which is at most 10FFFF and not "
                "D800 to DFFF",
    },
    [ERROR_CODE_RUNE_LITERAL_ENCODING_INVALID] = {
        .title = "the character of a rune must be valid UTF-8",
        .label = "this byte is not valid UTF-8",
        .note = "source code is UTF-8 text",
    },
    [ERROR_CODE_RUNE_LITERAL_TOO_LONG] = {
        .title = "a rune literal has more than one character",
        .label = "more than one character between the quotes",
        .note = "a rune literal has one character between its quotes",
        .help = "write more than one character as a string, between double quotes",
    },
    [ERROR_CODE_RUNE_LITERAL_UNTERMINATED] = {
        .title = "a rune literal has no closing quote",
        .label = "no closing quote on this line",
        .note = "a rune literal ends on the line where it starts",
    },
    [ERROR_CODE_STRING_LITERAL_BACKSLASH] = {
        .title = "a string cannot contain a backslash",
        .label = "this backslash",
        .note = "escape sequences are not supported in strings yet, and the backslash is kept for "
                "them, so that strings that are written today keep their meaning when the "
                "language gets them",
    },
    [ERROR_CODE_STRING_LITERAL_ENCODING_INVALID] = {
        .title = "a string must be valid UTF-8",
        .label = "this byte is not valid UTF-8",
        .note = "source code is UTF-8 text",
    },
    [ERROR_CODE_STRING_LITERAL_UNTERMINATED] = {
        .title = "string has no closing quote",
        .label = "no closing quote on this line",
        .note = "a string ends on the line where it starts",
    },
    [ERROR_CODE_TOKEN_LIMIT] = {
        .title = "more than {number} tokens",
        .label = "the tokens end here",
        .note = "the compiler reads at most {number} tokens of a file, so that the memory that it "
                "needs is known when it starts",
    },
    [ERROR_CODE_UNEXPECTED_CHARACTER] = {
        .title = "unexpected character '{detail}' (0x{hex})",
        .label = "this cannot start a token",
    },

    // The syntax.
    [ERROR_CODE_NODE_LIMIT] = {
        .title = "more than {number} syntax tree nodes",
        .label = "the nodes end here",
        .note = "the compiler builds at most {number} nodes of the syntax tree for a program, so "
                "that the memory that it needs is known when it starts",
    },
    [ERROR_CODE_UNEXPECTED_TOKEN] = {
        .title = "unexpected {subject}",
        .label = "expected {detail}",
    },
};

// The code of each status of the parser.
static Error_Code const REPORT_PARSE_CODES[] = {
    [PARSE_STATUS_NODE_LIMIT] = ERROR_CODE_NODE_LIMIT,
    [PARSE_STATUS_UNEXPECTED_TOKEN] = ERROR_CODE_UNEXPECTED_TOKEN,
};

// The code of each status of the tokenizer. A status that is not in the table is not an error.
static Error_Code const REPORT_TOKENIZE_CODES[] = {
    [TOKENIZE_STATUS_COMMENT_ENCODING_INVALID] = ERROR_CODE_COMMENT_ENCODING_INVALID,
    [TOKENIZE_STATUS_DOCUMENTATION_COMMENT_TRAILING] = ERROR_CODE_DOCUMENTATION_COMMENT_TRAILING,
    [TOKENIZE_STATUS_FLOAT_LITERAL_DIGIT_INVALID] = ERROR_CODE_FLOAT_LITERAL_DIGIT_INVALID,
    [TOKENIZE_STATUS_FLOAT_LITERAL_SEPARATOR_INVALID] =
        ERROR_CODE_FLOAT_LITERAL_SEPARATOR_INVALID,
    [TOKENIZE_STATUS_FLOAT_LITERAL_TOO_LARGE] = ERROR_CODE_FLOAT_LITERAL_TOO_LARGE,
    [TOKENIZE_STATUS_FLOAT_LITERAL_TOO_LONG] = ERROR_CODE_FLOAT_LITERAL_TOO_LONG,
    [TOKENIZE_STATUS_INDENTATION_TAB] = ERROR_CODE_INDENTATION_TAB,
    [TOKENIZE_STATUS_INDENTATION_WIDTH_INVALID] = ERROR_CODE_INDENTATION_WIDTH_INVALID,
    [TOKENIZE_STATUS_INTEGER_LITERAL_DIGIT_INVALID] = ERROR_CODE_INTEGER_LITERAL_DIGIT_INVALID,
    [TOKENIZE_STATUS_INTEGER_LITERAL_DIGITS_MISSING] = ERROR_CODE_INTEGER_LITERAL_DIGITS_MISSING,
    [TOKENIZE_STATUS_INTEGER_LITERAL_SEPARATOR_INVALID] =
        ERROR_CODE_INTEGER_LITERAL_SEPARATOR_INVALID,
    [TOKENIZE_STATUS_INTEGER_LITERAL_TOO_LARGE] = ERROR_CODE_INTEGER_LITERAL_TOO_LARGE,
    [TOKENIZE_STATUS_LABEL_INVALID] = ERROR_CODE_LABEL_INVALID,
    [TOKENIZE_STATUS_RUNE_LITERAL_EMPTY] = ERROR_CODE_RUNE_LITERAL_EMPTY,
    [TOKENIZE_STATUS_RUNE_LITERAL_ENCODING_INVALID] = ERROR_CODE_RUNE_LITERAL_ENCODING_INVALID,
    [TOKENIZE_STATUS_RUNE_LITERAL_ESCAPE_INVALID] = ERROR_CODE_RUNE_LITERAL_ESCAPE_INVALID,
    [TOKENIZE_STATUS_RUNE_LITERAL_TOO_LONG] = ERROR_CODE_RUNE_LITERAL_TOO_LONG,
    [TOKENIZE_STATUS_RUNE_LITERAL_UNTERMINATED] = ERROR_CODE_RUNE_LITERAL_UNTERMINATED,
    [TOKENIZE_STATUS_STRING_LITERAL_BACKSLASH] = ERROR_CODE_STRING_LITERAL_BACKSLASH,
    [TOKENIZE_STATUS_STRING_LITERAL_ENCODING_INVALID] = ERROR_CODE_STRING_LITERAL_ENCODING_INVALID,
    [TOKENIZE_STATUS_STRING_LITERAL_UNTERMINATED] = ERROR_CODE_STRING_LITERAL_UNTERMINATED,
    [TOKENIZE_STATUS_TOKEN_LIMIT] = ERROR_CODE_TOKEN_LIMIT,
    [TOKENIZE_STATUS_UNEXPECTED_CHARACTER] = ERROR_CODE_UNEXPECTED_CHARACTER,
};

static void   report_emit(Reporter const *reporter, Report_Error const *error);
static Str    report_expand(char const *template, Report_Error const *error, char *buffer,
                            size_t buffer_size_bytes);
static bool   report_is_word_byte(char character);
static size_t report_length_character(Str source, size_t offset);
static size_t report_length_literal(Str source, size_t offset);
static size_t report_length_token(Str source, size_t offset, Token_Kind kind);
static size_t report_length_word(Str source, size_t offset);
static void   report_text_append(Report_Text *text, Str part);
static void   report_text_append_subject(Report_Text *text, Str subject);

void report_general(Reporter const *reporter, Error_Code code, char const *path,
                    int64_t number) {
    assert(reporter != NULL);
    assert(code > 0);

    Report_Error error = {
        .path = path,
        .source = STR_EMPTY,
        .subject = STR_EMPTY,
        .detail = STR_EMPTY,
        .span_offset = 0,
        .span_length = 0,
        .number = number,
        .code = code,
        .has_span = false,
    };
    report_emit(reporter, &error);
}

void report_tokenize_error(Reporter const *reporter, char const *path, Str source,
                           Tokenize_Result result) {
    assert(reporter != NULL);
    assert(path != NULL);
    assert(result.status != TOKENIZE_STATUS_OK);
    assert(result.error_offset < source.length);

    size_t offset = result.error_offset;
    size_t length = 1;
    Str detail = STR_EMPTY;
    int64_t number = 0;
    // How much of the source the error is about depends on what the tokenizer found. Some are
    // about one character, some about a whole literal.
    switch (result.status) {
    case TOKENIZE_STATUS_OK:
        assert(false);
        break;
    case TOKENIZE_STATUS_COMMENT_ENCODING_INVALID:
    case TOKENIZE_STATUS_FLOAT_LITERAL_SEPARATOR_INVALID:
    case TOKENIZE_STATUS_INDENTATION_TAB:
    case TOKENIZE_STATUS_INTEGER_LITERAL_SEPARATOR_INVALID:
    case TOKENIZE_STATUS_RUNE_LITERAL_ENCODING_INVALID:
    case TOKENIZE_STATUS_STRING_LITERAL_BACKSLASH:
    case TOKENIZE_STATUS_STRING_LITERAL_ENCODING_INVALID:
        break;
    case TOKENIZE_STATUS_DOCUMENTATION_COMMENT_TRAILING:
        length = sizeof("///") - 1;
        break;
    case TOKENIZE_STATUS_FLOAT_LITERAL_DIGIT_INVALID:
    case TOKENIZE_STATUS_INTEGER_LITERAL_DIGIT_INVALID:
        detail = str_make(source.bytes + offset, 1);
        break;
    case TOKENIZE_STATUS_FLOAT_LITERAL_TOO_LARGE:
    case TOKENIZE_STATUS_FLOAT_LITERAL_TOO_LONG:
    case TOKENIZE_STATUS_INTEGER_LITERAL_DIGITS_MISSING:
    case TOKENIZE_STATUS_INTEGER_LITERAL_TOO_LARGE:
    case TOKENIZE_STATUS_RUNE_LITERAL_EMPTY:
    case TOKENIZE_STATUS_RUNE_LITERAL_TOO_LONG:
    case TOKENIZE_STATUS_RUNE_LITERAL_UNTERMINATED:
    case TOKENIZE_STATUS_STRING_LITERAL_UNTERMINATED:
        length = report_length_literal(source, offset);
        break;
    case TOKENIZE_STATUS_INDENTATION_WIDTH_INVALID:
        // The error is where the indentation ends, so the spaces before it are what is wrong.
        while (offset > 0 && source.bytes[offset - 1] == ' ') {
            offset -= 1;
        }
        length = result.error_offset - offset;
        number = (int64_t)length;
        break;
    case TOKENIZE_STATUS_LABEL_INVALID:
        // The colon and the name.
        length = 1 + report_length_word(source, offset + 1);
        detail = str_make(source.bytes + offset + 1, length - 1);
        break;
    case TOKENIZE_STATUS_RUNE_LITERAL_ESCAPE_INVALID:
        // The backslash and what follows it, if that is on the line.
        if (offset + 1 < source.length && source.bytes[offset + 1] != '\n') {
            length = 2;
        }
        break;
    case TOKENIZE_STATUS_TOKEN_LIMIT:
        number = TOKEN_COUNT_MAX;
        break;
    case TOKENIZE_STATUS_UNEXPECTED_CHARACTER:
        length = report_length_character(source, offset);
        detail = str_make(source.bytes + offset, length);
        number = (int64_t)(byte)source.bytes[offset];
        break;
    }

    Error_Code code = REPORT_TOKENIZE_CODES[result.status];
    assert(code > 0);

    Report_Error error = {
        .path = path,
        .source = source,
        .subject = str_make(source.bytes + offset, length),
        .detail = detail,
        .span_offset = offset,
        .span_length = length,
        .number = number,
        .code = code,
        .has_span = true,
    };
    report_emit(reporter, &error);
}

void report_parse_error(Reporter const *reporter, char const *path, Str source, Array_Slice tokens,
                        Parse_Result result) {
    assert(reporter != NULL);
    assert(path != NULL);
    assert(result.status != PARSE_STATUS_OK);
    assert(result.error_token_index < tokens.length);

    Token const *token_elements = tokens.elements;
    size_t token_index = result.error_token_index;
    // A line starts with an indentation token, which has no text. What the parser found there is
    // the first token of the line, which is the next one.
    if (token_elements[token_index].kind == TOKEN_KIND_INDENTATION &&
        token_index + 1 < tokens.length) {
        token_index += 1;
    }
    Token const *token = &token_elements[token_index];
    size_t offset = token->source_offset;
    size_t length = report_length_token(source, offset, token->kind);
    Str subject = STR_EMPTY;
    // What the parser found is the text of the token, or the thing that the token stands for when
    // it has no text of its own.
    char found[REPORT_SUBJECT_LENGTH_MAX + 8] = {0};
    if (token->kind == TOKEN_KIND_END || token->kind == TOKEN_KIND_INDENTATION) {
        // The end of the file is after the last line break, which is a line of its own that has
        // nothing on it. The error is at the end of the last line that has something on it.
        while (offset > 0 && (source.bytes[offset - 1] == '\n' ||
                              source.bytes[offset - 1] == '\r' ||
                              source.bytes[offset - 1] == ' ')) {
            offset -= 1;
        }
        length = 1;
        subject = STR("end of the file");
        if (token->kind == TOKEN_KIND_INDENTATION) {
            subject = STR("end of the line");
        }
    } else {
        // The text goes between quotes, so that a name or a symbol is told from the words around
        // it.
        size_t shown = length;
        if (shown > REPORT_SUBJECT_LENGTH_MAX) {
            shown = REPORT_SUBJECT_LENGTH_MAX;
        }
        int found_length = snprintf(found, sizeof(found), "'%.*s'", (int)shown,
                                    source.bytes + offset);
        assert(found_length > 0);
        subject = str_make(found, (size_t)found_length);
    }
    // Only the end of the file is at the end of the source, which is not a byte of it.
    if (source.length > 0 && offset >= source.length) {
        offset = source.length - 1;
    }

    Error_Code code = REPORT_PARSE_CODES[result.status];
    assert(code > 0);

    Report_Error error = {
        .path = path,
        .source = source,
        .subject = subject,
        .detail = result.expected,
        .span_offset = offset,
        .span_length = length,
        .number = TOKEN_COUNT_MAX,
        .code = code,
        .has_span = true,
    };
    report_emit(reporter, &error);
}

/**
 * Returns the length in bytes of the token of kind `kind` that starts at `offset` in `source`,
 * which is at least one. A name is its letters, a number and a string are like a literal, a label
 * is the colon and the name, and a symbol is the characters of operators that run together, such as
 * `->` and `::=`.
 */
static size_t report_length_token(Str source, size_t offset, Token_Kind kind) {
    assert(offset <= source.length);

    if (offset == source.length) {
        return 1;
    }
    char first = source.bytes[offset];
    if (kind == TOKEN_KIND_LABEL) {
        return 1 + report_length_word(source, offset + 1);
    }
    if (first == '"' || first == '\'' || (first >= '0' && first <= '9')) {
        return report_length_literal(source, offset);
    }
    if (report_is_word_byte(first)) {
        return report_length_word(source, offset);
    }
    if (first == ' ') {
        // An indentation is the spaces.
        size_t length = 0;
        while (offset + length < source.length && source.bytes[offset + length] == ' ') {
            length += 1;
        }
        return length;
    }
    // A symbol is up to four characters of the ones that operators are made of, such as `..<=`.
    size_t length = 1;
    char const operators[] = "-:=.<>+*/%!";
    while (length < 4 && offset + length < source.length &&
           strchr(operators, source.bytes[offset + length]) != NULL &&
           strchr(operators, first) != NULL) {
        length += 1;
    }
    return length;
}

/**
 * Returns the length in bytes of the character that starts at `offset` in `source`, which is the
 * one byte of an ASCII character, or the bytes that a lead byte of UTF-8 says, as far as the source
 * goes. A byte that is not a lead byte is a character of its own.
 */
static size_t report_length_character(Str source, size_t offset) {
    assert(offset < source.length);

    byte lead = (byte)source.bytes[offset];
    size_t length = 1;
    if (lead >= 0xF0) {
        length = 4;
    } else if (lead >= 0xE0) {
        length = 3;
    } else if (lead >= 0xC0) {
        length = 2;
    }
    if (offset + length > source.length) {
        length = source.length - offset;
    }
    return length;
}

static bool report_is_word_byte(char character) {
    bool is_letter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
    bool is_digit = character >= '0' && character <= '9';
    return is_letter || is_digit || character == '_';
}

/**
 * Returns the length in bytes of the name that starts at `offset` in `source`, which is the letters,
 * digits and underscores that follow there. It may be zero.
 */
static size_t report_length_word(Str source, size_t offset) {
    assert(offset <= source.length);

    size_t length = 0;
    while (offset + length < source.length && report_is_word_byte(source.bytes[offset + length])) {
        length += 1;
    }
    return length;
}

/**
 * Returns the length in bytes of the literal that starts at `offset` in `source`. A string or a rune
 * goes to its closing quote, or to the end of its line when it has none. A number is the letters,
 * digits, underscores and dots that follow.
 */
static size_t report_length_literal(Str source, size_t offset) {
    assert(offset < source.length);

    char first = source.bytes[offset];
    size_t index = offset;
    if (first == '"' || first == '\'') {
        index += 1;
        while (index < source.length && source.bytes[index] != '\n' &&
               source.bytes[index] != '\r') {
            // A backslash in a rune takes the character after it, so that `'\''` is one literal.
            // A string has no escape sequences.
            if (first == '\'' && source.bytes[index] == '\\' && index + 1 < source.length &&
                source.bytes[index + 1] != '\n') {
                index += 2;
            } else if (source.bytes[index] == first) {
                return index + 1 - offset;
            } else {
                index += 1;
            }
        }
        return index - offset;
    }
    while (index < source.length &&
           (report_is_word_byte(source.bytes[index]) || source.bytes[index] == '.')) {
        index += 1;
    }
    if (index == offset) {
        return 1;
    }
    return index - offset;
}

/**
 * Fills in the text of the catalog for `error` and prints it.
 */
static void report_emit(Reporter const *reporter, Report_Error const *error) {
    assert(reporter != NULL);
    assert(error != NULL);
    assert(error->code > 0);
    assert((size_t)error->code < REPORT_CODE_COUNT);

    Report_Entry const *entry = &REPORT_ENTRIES[error->code];
    // A code without text has been written in the enumeration and not in the catalog.
    assert(entry->title != NULL);

    char title[REPORT_PART_SIZE_BYTES] = {0};
    char label[REPORT_PART_SIZE_BYTES] = {0};
    char note[REPORT_PART_SIZE_BYTES] = {0};
    char help[REPORT_PART_SIZE_BYTES] = {0};
    Diagnostic diagnostic = {
        .path = STR_EMPTY,
        .source = error->source,
        .title = report_expand(entry->title, error, title, sizeof(title)),
        .label = report_expand(entry->label, error, label, sizeof(label)),
        .note = report_expand(entry->note, error, note, sizeof(note)),
        .help = report_expand(entry->help, error, help, sizeof(help)),
        .span_offset = error->span_offset,
        .span_length = error->span_length,
        .code = (uint32_t)error->code,
        .has_span = error->has_span,
    };
    if (error->path != NULL) {
        diagnostic.path = str_make(error->path, strlen(error->path));
    }
    diagnostic_print(stderr, &diagnostic, reporter->is_color);
}

/**
 * Returns `template` with its placeholders replaced, written to `buffer`, or an empty `Str`
 * (`bytes == NULL`) for a template that is NULL.
 */
static Str report_expand(char const *template, Report_Error const *error, char *buffer,
                         size_t buffer_size_bytes) {
    assert(error != NULL);
    assert(buffer != NULL);

    if (template == NULL) {
        return STR_EMPTY;
    }

    Report_Text text = {.bytes = buffer, .capacity = buffer_size_bytes, .length = 0};
    size_t index = 0;
    while (template[index] != '\0') {
        if (template[index] != '{') {
            report_text_append(&text, str_make(&template[index], 1));
            index += 1;
            continue;
        }
        char const *rest = &template[index];
        char number_text[32] = {0};
        if (strncmp(rest, "{subject}", sizeof("{subject}") - 1) == 0) {
            report_text_append_subject(&text, error->subject);
            index += sizeof("{subject}") - 1;
        } else if (strncmp(rest, "{detail}", sizeof("{detail}") - 1) == 0) {
            report_text_append(&text, error->detail);
            index += sizeof("{detail}") - 1;
        } else if (strncmp(rest, "{number}", sizeof("{number}") - 1) == 0) {
            int length = snprintf(number_text, sizeof(number_text), "%" PRId64, error->number);
            assert(length > 0);
            report_text_append(&text, str_make(number_text, (size_t)length));
            index += sizeof("{number}") - 1;
        } else if (strncmp(rest, "{hex}", sizeof("{hex}") - 1) == 0) {
            int length = snprintf(number_text, sizeof(number_text), "%02" PRIx64,
                                  (uint64_t)error->number);
            assert(length > 0);
            report_text_append(&text, str_make(number_text, (size_t)length));
            index += sizeof("{hex}") - 1;
        } else {
            // A brace that is not a placeholder is a mistake in the catalog.
            assert(false);
            index += 1;
        }
    }
    assert(text.length < text.capacity);

    text.bytes[text.length] = '\0';
    return str_make(text.bytes, text.length);
}

/**
 * Appends `part` to `text`, as much of it as fits, keeping a byte for the end.
 */
static void report_text_append(Report_Text *text, Str part) {
    assert(text != NULL);
    assert(text->length < text->capacity);

    size_t room = text->capacity - 1 - text->length;
    size_t count = part.length;
    if (count > room) {
        count = room;
    }
    if (count > 0) {
        memcpy(text->bytes + text->length, part.bytes, count);
    }
    text->length += count;
}

/**
 * Appends what of `subject` is shown in the text of an error to `text`: its first line, and at most
 * `REPORT_SUBJECT_LENGTH_MAX` bytes of it, cut where a character starts. A subject that is cut ends
 * with `...`, so that a reader knows there is more.
 */
static void report_text_append_subject(Report_Text *text, Str subject) {
    assert(text != NULL);
    assert(subject.bytes != NULL || subject.length == 0);

    size_t length = 0;
    while (length < subject.length && length < REPORT_SUBJECT_LENGTH_MAX &&
           subject.bytes[length] != '\n' && subject.bytes[length] != '\r') {
        length += 1;
    }
    // A byte that continues a character, 0b10xxxxxx, is not a place to cut.
    while (length > 0 && length < subject.length &&
           (((byte)subject.bytes[length]) & 0xC0) == 0x80) {
        length -= 1;
    }
    report_text_append(text, str_make(subject.bytes, length));
    if (length < subject.length) {
        report_text_append(text, STR("..."));
    }
}
