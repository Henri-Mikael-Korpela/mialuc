#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * C99 has no `static_assert`, so a failed condition makes the array size negative, which is a
 * compile-time error. `name` must be unique in the file and should say what is being checked. This
 * is a macro because it needs the condition as an expression, and it evaluates it only once.
 */
#define STATIC_ASSERT(condition, name) typedef char name[1 - 2 * !(condition)]

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

/**
 * A length-based view into text it does not own, so that no terminator is needed.
 */
typedef struct {
    char const *bytes;
    size_t length;
} Str;

// Both sizes assume 64-bit pointers, which are the only targets this project builds for.
STATIC_ASSERT(sizeof(Str) == 16, str_size_is_16_bytes);

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
 * On `TOKENIZE_STATUS_OK` only `token_count` is meaningful. Otherwise `error_offset` is the byte
 * offset in the source where tokenizing stopped.
 */
typedef struct {
    size_t token_count;
    size_t error_offset;
    Tokenize_Status status;
} Tokenize_Result;

static int32_t main_run(char const *path, char *source_buffer, Token *tokens);
static Str main_read_file(char const *path, char *buffer, size_t size_bytes_max);
static void main_print_tokens(Token const *tokens, size_t token_count);
static void main_print_error(char const *path, Str source, Tokenize_Result result);
static Tokenize_Result tokenize(Str source, Token *tokens, size_t token_count_max);
static size_t tokenize_scan_identifier(Str source, size_t offset);
static bool tokenize_is_whitespace(char character);
static bool tokenize_is_identifier_start(char character);
static bool tokenize_is_identifier_part(char character);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <source file>\n", argv[0]);
        return 2;
    }

    // Allocated once, here, because the buffers are too large for the default Windows stack.
    char *source_buffer = malloc(SOURCE_SIZE_BYTES_MAX);
    Token *tokens = malloc(TOKEN_COUNT_MAX * sizeof(Token));
    if (source_buffer == NULL || tokens == NULL) {
        fprintf(stderr, "Out of memory.\n");
        free(tokens);
        free(source_buffer);
        return 1;
    }

    int32_t exit_code = main_run(argv[1], source_buffer, tokens);

    free(tokens);
    free(source_buffer);
    return exit_code;
}

/**
 * Tokenizes the file at `path` and prints the tokens. Returns the process exit code. It is split
 * from `main` so that `main` can free the buffers on every path.
 */
static int32_t main_run(char const *path, char *source_buffer, Token *tokens) {
    assert(path != NULL);
    assert(source_buffer != NULL);
    assert(tokens != NULL);

    Str source = main_read_file(path, source_buffer, SOURCE_SIZE_BYTES_MAX);
    if (source.bytes == NULL) {
        fprintf(stderr, "%s: cannot read file, or file is larger than %d bytes.\n", path,
                SOURCE_SIZE_BYTES_MAX);
        return 1;
    }

    Tokenize_Result result = tokenize(source, tokens, TOKEN_COUNT_MAX);
    if (result.status == TOKENIZE_STATUS_OK) {
        main_print_tokens(tokens, result.token_count);
        return 0;
    }

    main_print_error(path, source, result);
    return 1;
}

/**
 * Reads the whole file into `buffer`. The returned `Str` has `bytes == NULL` when the file cannot
 * be opened or read, or does not fit in `size_bytes_max`. An empty file gives a non-NULL `Str` of
 * length zero.
 */
static Str main_read_file(char const *path, char *buffer, size_t size_bytes_max) {
    assert(path != NULL);
    assert(buffer != NULL);
    assert(size_bytes_max > 0);

    // Binary mode so that Windows does not translate line endings and shift offsets.
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return (Str){.bytes = NULL, .length = 0};
    }

    size_t size_bytes = fread(buffer, 1, size_bytes_max, file);
    bool is_read_error = ferror(file) != 0;
    // Reading one more byte after a full buffer tells "exactly full" apart from "too large".
    bool is_at_end = fgetc(file) == EOF;
    fclose(file);

    if (is_read_error || is_at_end == false) {
        return (Str){.bytes = NULL, .length = 0};
    }

    assert(size_bytes <= size_bytes_max);
    return (Str){.bytes = buffer, .length = size_bytes};
}

static void main_print_tokens(Token const *tokens, size_t token_count) {
    assert(tokens != NULL);
    assert(token_count <= TOKEN_COUNT_MAX);

    for (size_t i = 0; i < token_count; i += 1) {
        Token token = tokens[i];

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
static Tokenize_Result tokenize(Str source, Token *tokens, size_t token_count_max) {
    assert(tokens != NULL);
    assert(source.bytes != NULL || source.length == 0);

    Tokenize_Result result = {
        .token_count = 0,
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
            if (result.token_count == token_count_max) {
                result.status = TOKENIZE_STATUS_TOKEN_LIMIT;
                result.error_offset = offset;
                return result;
            }

            size_t end = tokenize_scan_identifier(source, offset);
            tokens[result.token_count] = (Token){
                .text = {.bytes = source.bytes + offset, .length = end - offset},
                .token_type = TOKEN_TYPE_IDENTIFIER,
            };
            result.token_count += 1;
            offset = end;
        } else {
            result.status = TOKENIZE_STATUS_UNEXPECTED_CHARACTER;
            result.error_offset = offset;
            return result;
        }
    }

    assert(result.token_count <= token_count_max);
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
