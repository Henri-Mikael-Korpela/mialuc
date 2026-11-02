#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <mialu/common-types.h>
#include <mialu/array_slice.h>
#include <mialu/str.h>

enum {
    // Larger sources are rejected. The tokenizer works on one buffer that is allocated once at
    // startup, so a bound keeps memory use predictable.
    SOURCE_SIZE_BYTES_MAX = 1024 * 1024,
    // Bounds the token buffer, which is allocated once at startup. A token is at least one byte,
    // so this is far more tokens than a source of the maximum size is likely to contain.
    TOKEN_COUNT_MAX = 64 * 1024,
};

typedef enum {
    TOKEN_TYPE_IDENTIFIER,
} Token_Type;

typedef struct {
    Str text;
    Token_Type token_type;
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

static int32_t         main_run                    (char const *path, Array_Slice source_buffer, Array_Slice token_buffer);
static Str             main_read_file              (char const *path, Array_Slice buffer);
static void            main_print_tokens           (Array_Slice tokens);
static void            main_print_error            (char const *path, Str source, Tokenize_Result result);
static Tokenize_Result tokenize                    (Str source, Array_Slice token_buffer);
static size_t          tokenize_scan_identifier    (Str source, size_t offset);
static bool            tokenize_is_whitespace      (char character);
static bool            tokenize_is_identifier_start(char character);
static bool            tokenize_is_identifier_part (char character);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <source file>\n", argv[0]);
        return 2;
    }

    // Allocated once, here, because the buffers are too large for the default Windows stack.
    Array_Slice source_buffer = {
        .elements = malloc(SOURCE_SIZE_BYTES_MAX * sizeof(byte)),
        .length = SOURCE_SIZE_BYTES_MAX,
    };
    Array_Slice token_buffer = {
        .elements = malloc(TOKEN_COUNT_MAX * sizeof(Token)),
        .length = TOKEN_COUNT_MAX,
    };
    if (source_buffer.elements == NULL || token_buffer.elements == NULL) {
        fprintf(stderr, "Out of memory.\n");
        free(token_buffer.elements);
        free(source_buffer.elements);
        return 1;
    }

    int32_t exit_code = main_run(argv[1], source_buffer, token_buffer);

    free(token_buffer.elements);
    free(source_buffer.elements);
    return exit_code;
}

/**
 * Tokenizes the file at `path` and prints the tokens. Returns the process exit code. It is split
 * from `main` so that `main` can free the buffers on every path.
 */
static int32_t main_run(char const *path, Array_Slice source_buffer, Array_Slice token_buffer) {
    assert(path != NULL);
    assert(source_buffer.elements != NULL);
    assert(token_buffer.elements != NULL);

    Str source = main_read_file(path, source_buffer);
    if (source.bytes == NULL) {
        fprintf(stderr, "%s: cannot read file, or file is larger than %d bytes.\n", path,
                SOURCE_SIZE_BYTES_MAX);
        return 1;
    }

    Tokenize_Result result = tokenize(source, token_buffer);
    if (result.status == TOKENIZE_STATUS_OK) {
        main_print_tokens(result.tokens);
        return 0;
    }

    main_print_error(path, source, result);
    return 1;
}

/**
 * Reads the whole file into `buffer`. The returned `Str` has `bytes == NULL` when the file cannot
 * be opened or read, or does not fit in `buffer`, which holds `byte` elements. An empty file gives
 * a non-NULL `Str` of length zero.
 */
static Str main_read_file(char const *path, Array_Slice buffer) {
    assert(path != NULL);
    assert(buffer.elements != NULL);
    assert(buffer.length > 0);

    // Binary mode so that Windows does not translate line endings and shift offsets.
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return STR_EMPTY;
    }

    size_t size_bytes = fread(buffer.elements, sizeof(byte), buffer.length, file);
    bool is_read_error = ferror(file) != 0;
    // Reading one more byte after a full buffer tells "exactly full" apart from "too large".
    bool is_at_end = fgetc(file) == EOF;
    fclose(file);

    if (is_read_error || is_at_end == false) {
        return STR_EMPTY;
    }

    assert(size_bytes <= buffer.length);
    return str_make(buffer.elements, size_bytes);
}

static void main_print_tokens(Array_Slice tokens) {
    assert(tokens.elements != NULL);
    assert(tokens.length <= TOKEN_COUNT_MAX);

    Token const *token_elements = tokens.elements;
    for (size_t i = 0; i < tokens.length; i += 1) {
        Token token = token_elements[i];

        switch (token.token_type) {
        case TOKEN_TYPE_IDENTIFIER:
            // The cast is needed because `%.*s` takes an `int` precision.
            printf("IDENTIFIER %.*s\n", (int)token.text.length, token.text.bytes);
            break;
        }
    }
}

static void main_print_error(char const *path, Str source, Tokenize_Result result) {
    assert(result.status != TOKENIZE_STATUS_OK);
    assert(result.error_offset < source.length);

    // Line and column are only needed for the message, so they are computed here rather than
    // tracked for every byte while tokenizing.
    size_t line = 1;
    size_t column = 1;
    for (size_t i = 0; i < result.error_offset; i += 1) {
        if (source.bytes[i] == '\n') {
            line += 1;
            column = 1;
        } else {
            column += 1;
        }
    }

    switch (result.status) {
    case TOKENIZE_STATUS_OK:
        assert(false);
        break;
    case TOKENIZE_STATUS_TOKEN_LIMIT:
        fprintf(stderr, "%s:%zu:%zu: more than %d tokens.\n", path, line, column, TOKEN_COUNT_MAX);
        break;
    case TOKENIZE_STATUS_UNEXPECTED_CHARACTER: {
        char character = source.bytes[result.error_offset];
        // The cast is needed because `%x` takes an `unsigned`, and a plain `char` may be signed.
        fprintf(stderr, "%s:%zu:%zu: unexpected character '%c' (0x%02x).\n", path, line, column,
                character, (unsigned)(uint8_t)character);
        break;
    }
    }
}

/**
 * Splits `source` into tokens, skipping whitespace. Stops at the first character that cannot start
 * a token, since the language has no error recovery yet.
 */
static Tokenize_Result tokenize(Str source, Array_Slice token_buffer) {
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

        if (tokenize_is_whitespace(character)) {
            offset += 1;
        } else if (tokenize_is_identifier_start(character)) {
            if (result.tokens.length == token_buffer.length) {
                result.status = TOKENIZE_STATUS_TOKEN_LIMIT;
                result.error_offset = offset;
                return result;
            }

            size_t end = tokenize_scan_identifier(source, offset);
            token_elements[result.tokens.length] = (Token){
                .text = {.bytes = source.bytes + offset, .length = end - offset},
                .token_type = TOKEN_TYPE_IDENTIFIER,
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

/**
 * Returns the offset just past the identifier that starts at `offset`.
 */
static size_t tokenize_scan_identifier(Str source, size_t offset) {
    assert(offset < source.length);
    assert(tokenize_is_identifier_start(source.bytes[offset]));

    // The start character is already known to be valid, so scanning continues after it.
    size_t end = offset + 1;
    while (end < source.length) {
        if (tokenize_is_identifier_part(source.bytes[end])) {
            end += 1;
        } else {
            break;
        }
    }

    assert(end > offset);
    assert(end <= source.length);
    return end;
}

// Only the characters the language treats as spacing; the locale-dependent `isspace` is avoided so
// that behavior does not change between machines.
static bool tokenize_is_whitespace(char character) {
    return character == ' ' || character == '\t' || character == '\r' || character == '\n';
}

// ASCII only, for the same locale-independence reason as `tokenize_is_whitespace`.
static bool tokenize_is_identifier_start(char character) {
    bool is_lower = character >= 'a' && character <= 'z';
    bool is_upper = character >= 'A' && character <= 'Z';
    return is_lower || is_upper || character == '_';
}

// An identifier part is a start character or a digit, which is how identifiers cannot begin with a
// digit but may contain one.
static bool tokenize_is_identifier_part(char character) {
    bool is_digit = character >= '0' && character <= '9';
    return tokenize_is_identifier_start(character) || is_digit;
}
