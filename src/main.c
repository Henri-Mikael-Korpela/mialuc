#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mialu/array_slice.h>
#include <mialu/ast.h>
#include <mialu/common_types.h>
#include <mialu/parser.h>
#include <mialu/str.h>
#include <mialu/tokenizer.h>
#include <mialu/wasm.h>

enum {
    // Larger sources are rejected. The tokenizer works on one buffer that is allocated once at
    // startup, so a bound keeps memory use predictable.
    SOURCE_SIZE_BYTES_MAX = 1024 * 1024,
    // Bounds the token buffer, which is allocated once at startup. A token is at least one byte,
    // so this is far more tokens than a source of the maximum size is likely to contain.
    TOKEN_COUNT_MAX = 64 * 1024,
    // Bounds the module that the build mode writes. The module is allocated once at startup. It
    // is mostly a few bytes per token, so this is far more than a source of the maximum size is
    // likely to need.
    OUTPUT_SIZE_BYTES_MAX = 1024 * 1024,
};

// Alphabetical, because nothing depends on the order. `COMMAND_KIND_INVALID` is the result of a
// command line that is not understood.
typedef enum {
    COMMAND_KIND_BUILD,
    COMMAND_KIND_INVALID,
    COMMAND_KIND_RUN,
} Command_Kind;

/**
 * What the command line asks for. `output_path` is NULL unless the mode is build. The paths are
 * the C strings of `argv`, because they are only handed to `fopen`.
 */
typedef struct {
    char const *source_path;
    char const *output_path;
    Command_Kind mode;
} Command;

/**
 * The memory that every mode works in. It is allocated once at startup and freed in `main`, so
 * that it is freed in one place on every path.
 */
typedef struct {
    Array_Slice source;
    Array_Slice tokens;
    Array_Slice nodes;
    Array_Slice output;
} Buffers;

static Command main_command_parse                    (int argc, char *argv[]);
static int32_t main_run_command                      (Command command, Buffers const *buffers);
static void    main_run_command_print_tokens         (Array_Slice tokens);
static Str     main_run_command_read_file            (char const *path, Array_Slice buffer);
static void    main_run_command_report_parse_error   (char const *path, Str source,
                                                      Array_Slice tokens,
                                                      Parse_Result parse_result);
static void    main_run_command_report_tokenize_error(char const *path, Str source,
                                                      Tokenize_Result result);
static void    main_run_command_report_wasm_error    (char const *path, Str source,
                                                      Wasm_Build_Result result);
static bool    main_run_command_write_file           (char const *path, Array_Slice bytes);

/**
 * Has two modes. `run` reads a source file, tokenizes and parses it, and prints the tokens and
 * the syntax tree. `build` does the same silently and writes the program for a target. Everything
 * that allocates is in `main` on purpose, so that the buffers are freed in one place on every
 * path.
 */
int main(int argc, char *argv[]) {
    Command command = main_command_parse(argc, argv);
    if (command.mode == COMMAND_KIND_INVALID) {
        fprintf(stderr,
                "Usage: %s run <source file>\n"
                "       %s build --target wasm <output file> <source file>\n",
                argv[0], argv[0]);
        return 2;
    }

    // Allocated once, here, because the buffers are too large for the default Windows stack.
    Buffers buffers = {
        .source =
            {
                .elements = malloc(SOURCE_SIZE_BYTES_MAX * sizeof(byte)),
                .length = SOURCE_SIZE_BYTES_MAX,
            },
        .tokens =
            {
                .elements = malloc(TOKEN_COUNT_MAX * sizeof(Token)),
                .length = TOKEN_COUNT_MAX,
            },
        // A node needs at least one token, so as many nodes as tokens always suffice.
        .nodes =
            {
                .elements = malloc(TOKEN_COUNT_MAX * sizeof(Ast_Node)),
                .length = TOKEN_COUNT_MAX,
            },
        .output =
            {
                .elements = malloc(OUTPUT_SIZE_BYTES_MAX * sizeof(byte)),
                .length = OUTPUT_SIZE_BYTES_MAX,
            },
    };

    int32_t exit_code = 1;
    if (buffers.source.elements == NULL || buffers.tokens.elements == NULL ||
        buffers.nodes.elements == NULL || buffers.output.elements == NULL) {
        fprintf(stderr, "Out of memory.\n");
    } else {
        exit_code = main_run_command(command, &buffers);
    }

    free(buffers.output.elements);
    free(buffers.nodes.elements);
    free(buffers.tokens.elements);
    free(buffers.source.elements);
    return exit_code;
}

/**
 * Understands the command line:
 *
 *     mialuc run <source file>
 *     mialuc build --target wasm <output file> <source file>
 *
 * Returns a command with mode `COMMAND_KIND_INVALID` when it is anything else.
 */
static Command main_command_parse(int argc, char *argv[]) {
    assert(argc >= 1);
    assert(argv != NULL);

    Command invalid = {
        .source_path = NULL,
        .output_path = NULL,
        .mode = COMMAND_KIND_INVALID,
    };
    if (argc < 2) {
        return invalid;
    }

    if (strcmp(argv[1], "run") == 0) {
        if (argc != 3) {
            return invalid;
        }
        return (Command){
            .source_path = argv[2],
            .output_path = NULL,
            .mode = COMMAND_KIND_RUN,
        };
    }

    if (strcmp(argv[1], "build") == 0) {
        // WebAssembly is the only target, but naming it keeps the command line the same when
        // another one is added.
        if (argc != 6) {
            return invalid;
        }
        if (strcmp(argv[2], "--target") != 0) {
            return invalid;
        }
        if (strcmp(argv[3], "wasm") != 0) {
            return invalid;
        }
        return (Command){
            .source_path = argv[5],
            .output_path = argv[4],
            .mode = COMMAND_KIND_BUILD,
        };
    }

    return invalid;
}

/**
 * Does what the command says. Returns the exit code, which is 0 on success.
 */
static int32_t main_run_command(Command command, Buffers const *buffers) {
    assert(buffers != NULL);
    assert(command.source_path != NULL);
    assert(command.mode != COMMAND_KIND_INVALID);

    char const *path = command.source_path;
    Str source = main_run_command_read_file(path, buffers->source);

    // An unreadable source is `bytes == NULL`. An empty file gives a non-NULL `Str` of length
    // zero.
    if (source.bytes == NULL) {
        fprintf(stderr, "%s: cannot read file, or file is larger than %d bytes.\n", path,
                SOURCE_SIZE_BYTES_MAX);
        return 1;
    }

    Tokenize_Result result = tokenize(source, buffers->tokens);
    if (result.status != TOKENIZE_STATUS_OK) {
        main_run_command_report_tokenize_error(path, source, result);
        return 1;
    }
    assert(result.tokens.length <= TOKEN_COUNT_MAX);

    if (command.mode == COMMAND_KIND_RUN) {
        main_run_command_print_tokens(result.tokens);
    }

    Parse_Result parse_result = parse(result.tokens, buffers->nodes);
    if (parse_result.status != PARSE_STATUS_OK) {
        main_run_command_report_parse_error(path, source, result.tokens, parse_result);
        return 1;
    }

    switch (command.mode) {
    case COMMAND_KIND_BUILD: {
        assert(command.output_path != NULL);

        Wasm_Build_Result build_result = wasm_build(parse_result.nodes, parse_result.root,
                                                    buffers->output);
        if (build_result.status != WASM_BUILD_STATUS_OK) {
            main_run_command_report_wasm_error(path, source, build_result);
            return 1;
        }

        if (main_run_command_write_file(command.output_path, build_result.bytes) == false) {
            fprintf(stderr, "%s: cannot write file.\n", command.output_path);
            return 1;
        }
        return 0;
    }
    case COMMAND_KIND_INVALID:
        assert(false);
        return 1;
    case COMMAND_KIND_RUN:
        printf("\n");
        ast_print(parse_result.nodes, parse_result.root);
        return 0;
    }

    assert(false);
    return 1;
}

/**
 * Reads the whole file into `buffer`, and returns its content as a view of the buffer. Returns
 * an empty `Str` (`bytes == NULL`) when the file cannot be opened or read, or does not fit.
 */
static Str main_run_command_read_file(char const *path, Array_Slice buffer) {
    assert(path != NULL);
    assert(buffer.elements != NULL);

    // Binary mode so that Windows does not translate line endings and shift offsets.
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return STR_EMPTY;
    }

    size_t size_bytes = fread(buffer.elements, sizeof(byte), buffer.length, file);
    bool is_read_error = ferror(file) != 0;
    // Reading one more byte after a full buffer tells "exactly full" apart from "too large".
    bool is_at_end = fgetc(file) == EOF;
    bool is_close_error = fclose(file) != 0;

    if (is_read_error || is_close_error || is_at_end == false) {
        return STR_EMPTY;
    }

    assert(size_bytes <= buffer.length);
    return str_make(buffer.elements, size_bytes);
}

/**
 * Writes `bytes`, which holds `byte` elements, to a new file at `path`. Returns false when the
 * file cannot be written completely.
 */
static bool main_run_command_write_file(char const *path, Array_Slice bytes) {
    assert(path != NULL);
    assert(bytes.elements != NULL);

    // Binary mode so that Windows does not turn the byte 0x0A into a line ending.
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }

    size_t written = fwrite(bytes.elements, sizeof(byte), bytes.length, file);
    bool is_close_error = fclose(file) != 0;

    return written == bytes.length && is_close_error == false;
}

/**
 * Prints one token per line, for the run mode.
 */
static void main_run_command_print_tokens(Array_Slice tokens) {
    assert(tokens.elements != NULL);
    assert(tokens.length >= 1);

    Token const *token_elements = tokens.elements;
    for (size_t i = 0; i < tokens.length; i += 1) {
        Token token = token_elements[i];

        switch (token.token_type) {
        case TOKEN_TYPE_ARROW:
            printf("ARROW\n");
            break;
        case TOKEN_TYPE_COMMA:
            printf("COMMA\n");
            break;
        case TOKEN_TYPE_END:
            printf("END\n");
            break;
        case TOKEN_TYPE_IDENTIFIER:
            // The cast is needed because `%.*s` takes an `int` precision.
            printf("IDENTIFIER %.*s\n", (int)token.payload.identifier.text.length,
                   token.payload.identifier.text.bytes);
            break;
        case TOKEN_TYPE_INDENTATION:
            printf("INDENTATION %" PRIu32 "\n", token.payload.indentation.level);
            break;
        case TOKEN_TYPE_INTEGER_LITERAL:
            printf("INTEGER_LITERAL %" PRId64 "\n", token.payload.integer_literal.value);
            break;
        case TOKEN_TYPE_KEYWORD_PROC:
            printf("KEYWORD_PROC\n");
            break;
        case TOKEN_TYPE_KEYWORD_RETURN:
            printf("KEYWORD_RETURN\n");
            break;
        case TOKEN_TYPE_PARENTHESIS_LEFT:
            printf("PARENTHESIS_LEFT\n");
            break;
        case TOKEN_TYPE_PARENTHESIS_RIGHT:
            printf("PARENTHESIS_RIGHT\n");
            break;
        case TOKEN_TYPE_PLUS:
            printf("PLUS\n");
            break;
        case TOKEN_TYPE_TYPE_SEPARATOR:
            printf("TYPE_SEPARATOR\n");
            break;
        }
    }
}

static void main_run_command_report_tokenize_error(char const *path, Str source,
                                                   Tokenize_Result result) {
    assert(result.status != TOKENIZE_STATUS_OK);
    assert(result.error_offset < source.length);

    // Line and column are only needed for the message, so they are computed here rather than
    // tracked for every byte while tokenizing.
    Str_Position position = str_position_at(source, result.error_offset);
    size_t line = position.line;
    size_t column = position.column;

    switch (result.status) {
    case TOKENIZE_STATUS_OK:
        assert(false);
        break;
    case TOKENIZE_STATUS_INDENTATION_TAB:
        fprintf(stderr, "%s:%zu:%zu: indentation must use spaces, not tabs.\n", path, line,
                column);
        break;
    case TOKENIZE_STATUS_INDENTATION_WIDTH_INVALID:
        fprintf(stderr, "%s:%zu:%zu: indentation must be a multiple of 4 spaces.\n", path, line,
                column);
        break;
    case TOKENIZE_STATUS_INTEGER_LITERAL_TOO_LARGE:
        fprintf(stderr, "%s:%zu:%zu: integer literal does not fit in 64 bits.\n", path, line,
                column);
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

static void main_run_command_report_parse_error(char const *path, Str source, Array_Slice tokens,
                                    Parse_Result parse_result) {
    assert(parse_result.status != PARSE_STATUS_OK);
    assert(parse_result.error_token_index < tokens.length);

    Token const *token_elements = tokens.elements;
    size_t error_offset = token_elements[parse_result.error_token_index].source_offset;
    Str_Position position = str_position_at(source, error_offset);

    switch (parse_result.status) {
    case PARSE_STATUS_OK:
        assert(false);
        break;
    case PARSE_STATUS_NODE_LIMIT:
        fprintf(stderr, "%s:%zu:%zu: more than %d syntax tree nodes.\n", path, position.line,
                position.column, TOKEN_COUNT_MAX);
        break;
    case PARSE_STATUS_UNEXPECTED_TOKEN:
        fprintf(stderr, "%s:%zu:%zu: expected %.*s.\n", path, position.line, position.column,
                (int)parse_result.expected.length, parse_result.expected.bytes);
        break;
    }
}

static void main_run_command_report_wasm_error(char const *path, Str source,
                                               Wasm_Build_Result result) {
    assert(result.status != WASM_BUILD_STATUS_OK);

    // The output being too large is about the whole program, so it has no place in the source.
    if (result.status == WASM_BUILD_STATUS_OUTPUT_TOO_LARGE) {
        fprintf(stderr, "%s: the WebAssembly module is larger than %d bytes.\n", path,
                OUTPUT_SIZE_BYTES_MAX);
        return;
    }

    // The subject views the source, so its distance from the start is its offset.
    assert(result.subject.bytes >= source.bytes);
    size_t offset = (size_t)(result.subject.bytes - source.bytes);
    assert(offset <= source.length);
    Str_Position position = str_position_at(source, offset);

    // The cast is needed because `%.*s` takes an `int` precision.
    int subject_length = (int)result.subject.length;
    switch (result.status) {
    case WASM_BUILD_STATUS_DEFINITION_NAME_DUPLICATE:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is already defined.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_EXPRESSION_TOO_DEEP:
        fprintf(stderr, "%s:%zu:%zu: an expression in '%.*s' has too many nested operators.\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_IDENTIFIER_UNKNOWN:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not a parameter of the procedure.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_OK:
    case WASM_BUILD_STATUS_OUTPUT_TOO_LARGE:
        assert(false);
        break;
    case WASM_BUILD_STATUS_PARAMETER_NAME_DUPLICATE:
        fprintf(stderr, "%s:%zu:%zu: parameter '%.*s' is declared twice.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_RETURN_TYPE_MISSING:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' returns a value but has no return type.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_TYPE_UNSUPPORTED:
        fprintf(stderr, "%s:%zu:%zu: type '%.*s' is not supported by the wasm target.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    }
}
