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
#include <mialu/semantic.h>
#include <mialu/str.h>
#include <mialu/tokenizer.h>
#include <mialu/wasm-text.h>
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
    // Bounds the text that the `wat` target writes. The text is allocated once at startup. The
    // largest growth is `i64.add`, one byte of the module that becomes 12 bytes of text, so 16
    // bytes of text per byte of the module is enough.
    OUTPUT_TEXT_SIZE_BYTES_MAX = 16 * OUTPUT_SIZE_BYTES_MAX,
};

// Alphabetical, because nothing depends on the order. `COMMAND_MODE_INVALID` is the result of a
// command line that is not understood.
typedef enum {
    COMMAND_MODE_BUILD,
    COMMAND_MODE_INVALID,
    COMMAND_MODE_RUN,
} Command_Mode;

// Alphabetical, because nothing depends on the order. `COMMAND_TARGET_NONE` is for the modes
// that do not write a program.
typedef enum {
    COMMAND_TARGET_NONE,
    COMMAND_TARGET_WASM,
    COMMAND_TARGET_WAT,
} Command_Target;

/**
 * What the command line asks for. `output_path` is NULL, and `target` is `COMMAND_TARGET_NONE`,
 * unless the mode is build. The paths are the C strings of `argv`, because they are only handed
 * to `fopen`.
 */
typedef struct {
    char const *source_path;
    char const *output_path;
    Command_Mode mode;
    Command_Target target;
} Command;

/**
 * The memory that every mode works in. It is allocated once at startup and freed in `main`, so
 * that it is freed in one place on every path.
 */
typedef struct {
    Array_Slice source;
    Array_Slice tokens;
    Array_Slice nodes;
    Array_Slice bindings;
    Array_Slice output;
    Array_Slice output_text;
} Buffers;

static Command      main_command_parse                     (Array_Slice args);
static int32_t      main_run_command                       (Command command,
                                                            Buffers const *buffers);
static int32_t      main_run_command_build                 (Command command, Buffers const *buffers,
                                                            Str source, Parse_Result parse_result,
                                                            Semantic_Result semantic_result);
static void         main_run_command_print_tokens          (Array_Slice tokens);
static Str          main_run_command_read_file             (char const *path, Array_Slice buffer);
static void         main_run_command_report_parse_error    (char const *path, Str source,
                                                            Array_Slice tokens,
                                                            Parse_Result parse_result);
static void         main_run_command_report_semantic_error (char const *path, Str source,
                                                            Semantic_Result result);
static void         main_run_command_report_tokenize_error (char const *path, Str source,
                                                            Tokenize_Result result);
static void         main_run_command_report_wasm_error     (char const *path, Str source,
                                                            Wasm_Build_Result result);
static void         main_run_command_report_wasm_text_error(char const *path,
                                                            Wasm_Text_Result result);
static bool         main_run_command_write_file            (char const *path, Array_Slice bytes);
static Str_Position main_subject_position                  (Str source, Str subject);

/**
 * Has two modes. `run` reads a source file, tokenizes, parses and analyzes it, and prints the
 * tokens and the syntax tree. `build` does the same silently and writes the program for a target.
 * Everything that allocates is in `main` on purpose, so that the buffers are freed in one place on
 * every path.
 */
int main(int argc, char *argv[]) {
    // The cast is needed because `argc` is an `int`, which `main` must declare. It is never
    // negative.
    Array_Slice args = array_slice_make(argv, (size_t)argc);
    Command command = main_command_parse(args);
    if (command.mode == COMMAND_MODE_INVALID) {
        fprintf(stderr,
                "Usage: %s run <source file>\n"
                "       %s build --target wasm <output file> <source file>\n"
                "       %s build --target wat <output file> <source file>\n",
                argv[0], argv[0], argv[0]);
        return 2;
    }

    // Allocated once, here, because the buffers are too large for the default Windows stack.
    Buffers buffers = {
        .source = array_slice_make(malloc(SOURCE_SIZE_BYTES_MAX * sizeof(byte)),
                                   SOURCE_SIZE_BYTES_MAX),
        .tokens = array_slice_make(malloc(TOKEN_COUNT_MAX * sizeof(Token)), TOKEN_COUNT_MAX),
        // A node needs at least one token, so as many nodes as tokens always suffice.
        .nodes = array_slice_make(malloc(TOKEN_COUNT_MAX * sizeof(Ast_Node)), TOKEN_COUNT_MAX),
        // The analysis has one binding for each node.
        .bindings = array_slice_make(malloc(TOKEN_COUNT_MAX * sizeof(Semantic_Binding)),
                                     TOKEN_COUNT_MAX),
        .output = array_slice_make(malloc(OUTPUT_SIZE_BYTES_MAX * sizeof(byte)),
                                   OUTPUT_SIZE_BYTES_MAX),
        .output_text = array_slice_make(malloc(OUTPUT_TEXT_SIZE_BYTES_MAX * sizeof(byte)),
                                        OUTPUT_TEXT_SIZE_BYTES_MAX),
    };

    int32_t exit_code = 1;
    if (buffers.source.elements == NULL || buffers.tokens.elements == NULL ||
        buffers.nodes.elements == NULL || buffers.bindings.elements == NULL ||
        buffers.output.elements == NULL || buffers.output_text.elements == NULL) {
        fprintf(stderr, "Out of memory.\n");
    } else {
        exit_code = main_run_command(command, &buffers);
    }

    free(buffers.output_text.elements);
    free(buffers.output.elements);
    free(buffers.bindings.elements);
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
 *     mialuc build --target wat <output file> <source file>
 *
 * Returns a command with mode `COMMAND_MODE_INVALID` when it is anything else.
 */
static Command main_command_parse(Array_Slice args) {
    assert(args.elements != NULL);
    assert(args.length >= 1);

    char const *const *argv = args.elements;

    Command invalid = {
        .source_path = NULL,
        .output_path = NULL,
        .mode = COMMAND_MODE_INVALID,
        .target = COMMAND_TARGET_NONE,
    };
    if (args.length < 2) {
        return invalid;
    }

    if (strcmp(argv[1], "run") == 0) {
        if (args.length != 3) {
            return invalid;
        }
        return (Command){
            .source_path = argv[2],
            .output_path = NULL,
            .mode = COMMAND_MODE_RUN,
            .target = COMMAND_TARGET_NONE,
        };
    }

    if (strcmp(argv[1], "build") == 0) {
        if (args.length != 6) {
            return invalid;
        }
        if (strcmp(argv[2], "--target") != 0) {
            return invalid;
        }

        // `wat` is the WebAssembly text format, which is the same module as `wasm` in a form
        // that a person can read.
        Command_Target target = COMMAND_TARGET_NONE;
        if (strcmp(argv[3], "wasm") == 0) {
            target = COMMAND_TARGET_WASM;
        } else if (strcmp(argv[3], "wat") == 0) {
            target = COMMAND_TARGET_WAT;
        } else {
            return invalid;
        }

        return (Command){
            .source_path = argv[5],
            .output_path = argv[4],
            .mode = COMMAND_MODE_BUILD,
            .target = target,
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
    assert(command.mode != COMMAND_MODE_INVALID);

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

    if (command.mode == COMMAND_MODE_RUN) {
        main_run_command_print_tokens(result.tokens);
    }

    Parse_Result parse_result = parse(result.tokens, buffers->nodes);
    if (parse_result.status != PARSE_STATUS_OK) {
        main_run_command_report_parse_error(path, source, result.tokens, parse_result);
        return 1;
    }

    // The tree is printed before it is analyzed, so that a program with a bad name can be looked at.
    if (command.mode == COMMAND_MODE_RUN) {
        printf("\n");
        ast_print(parse_result.nodes, parse_result.root);
    }

    Semantic_Result semantic_result = semantic_analyze(parse_result.nodes, parse_result.root,
                                                       buffers->bindings);
    if (semantic_result.status != SEMANTIC_STATUS_OK) {
        main_run_command_report_semantic_error(path, source, semantic_result);
        return 1;
    }

    switch (command.mode) {
    case COMMAND_MODE_BUILD:
        return main_run_command_build(command, buffers, source, parse_result, semantic_result);
    case COMMAND_MODE_INVALID:
        assert(false);
        return 1;
    case COMMAND_MODE_RUN:
        return 0;
    }

    assert(false);
    return 1;
}

/**
 * Builds the parsed and analyzed program for the target of `command`, and writes it to the output
 * file. Returns the exit code, which is 0 on success. The text is formatted from the binary module
 * rather than written from the syntax tree, so that it always shows exactly what `wasm` writes.
 */
static int32_t main_run_command_build(Command command, Buffers const *buffers, Str source,
                                      Parse_Result parse_result, Semantic_Result semantic_result) {
    assert(command.mode == COMMAND_MODE_BUILD);
    assert(command.output_path != NULL);
    assert(parse_result.status == PARSE_STATUS_OK);
    assert(semantic_result.status == SEMANTIC_STATUS_OK);

    char const *path = command.source_path;
    Wasm_Build_Result build_result = wasm_build(parse_result.nodes, semantic_result.bindings,
                                                parse_result.root, buffers->output);
    if (build_result.status != WASM_BUILD_STATUS_OK) {
        main_run_command_report_wasm_error(path, source, build_result);
        return 1;
    }

    Array_Slice output = build_result.bytes;
    switch (command.target) {
    case COMMAND_TARGET_NONE:
        assert(false);
        return 1;
    case COMMAND_TARGET_WASM:
        break;
    case COMMAND_TARGET_WAT: {
        Wasm_Text_Result text_result = wasm_text_format(build_result.bytes, buffers->output_text);
        if (text_result.status != WASM_TEXT_STATUS_OK) {
            main_run_command_report_wasm_text_error(path, text_result);
            return 1;
        }
        // The text is the filled start of the text buffer, which is writable, unlike the `Str`.
        output = array_slice_make(buffers->output_text.elements, text_result.text.length);
        break;
    }
    }

    if (main_run_command_write_file(command.output_path, output) == false) {
        fprintf(stderr, "%s: cannot write file.\n", command.output_path);
        return 1;
    }
    return 0;
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
        case TOKEN_TYPE_ASTERISK:
            printf("ASTERISK\n");
            break;
        case TOKEN_TYPE_COMMA:
            printf("COMMA\n");
            break;
        case TOKEN_TYPE_END:
            printf("END\n");
            break;
        case TOKEN_TYPE_EQUALS:
            printf("EQUALS\n");
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
        case TOKEN_TYPE_MINUS:
            printf("MINUS\n");
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
        case TOKEN_TYPE_PLUS_EQUALS:
            printf("PLUS_EQUALS\n");
            break;
        case TOKEN_TYPE_SLASH:
            printf("SLASH\n");
            break;
        case TOKEN_TYPE_SLASH_EQUALS:
            printf("SLASH_EQUALS\n");
            break;
        case TOKEN_TYPE_STRING_LITERAL:
            printf("STRING_LITERAL \"%.*s\"\n", (int)token.payload.string_literal.text.length,
                   token.payload.string_literal.text.bytes);
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
    case TOKENIZE_STATUS_STRING_LITERAL_BACKSLASH:
        fprintf(stderr, "%s:%zu:%zu: a string cannot contain a backslash, because escape "
                "sequences are not supported.\n", path, line, column);
        break;
    case TOKENIZE_STATUS_STRING_LITERAL_ENCODING_INVALID:
        fprintf(stderr, "%s:%zu:%zu: a string must be valid UTF-8.\n", path, line, column);
        break;
    case TOKENIZE_STATUS_STRING_LITERAL_UNTERMINATED:
        fprintf(stderr, "%s:%zu:%zu: string has no closing quote on its line.\n", path, line,
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

    Str_Position position = main_subject_position(source, result.subject);

    // The cast is needed because `%.*s` takes an `int` precision.
    int subject_length = (int)result.subject.length;
    switch (result.status) {
    case WASM_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not an Int, so it cannot be used in arithmetic.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_DEFINITION_NAME_RESERVED:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is reserved for the memory of the wasm module.\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_EXPRESSION_TOO_DEEP:
        fprintf(stderr, "%s:%zu:%zu: an expression in '%.*s' has too many nested operators.\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_IDENTIFIER_NOT_VALUE:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is a procedure, which the wasm target cannot use as a "
                "value.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_OK:
    case WASM_BUILD_STATUS_OUTPUT_TOO_LARGE:
        assert(false);
        break;
    case WASM_BUILD_STATUS_RETURN_TYPE_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' returns a value that is not of its return type.\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_RETURN_TYPE_MISSING:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' returns a value but has no return type.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_VARIABLE_TYPE_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: variable '%.*s' is given a value that is not of its type.\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_TYPE_UNSUPPORTED:
        fprintf(stderr, "%s:%zu:%zu: type '%.*s' is not supported by the wasm target.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    }
}

static void main_run_command_report_semantic_error(char const *path, Str source,
                                                   Semantic_Result result) {
    assert(result.status != SEMANTIC_STATUS_OK);

    Str_Position position = main_subject_position(source, result.subject);

    // The cast is needed because `%.*s` takes an `int` precision.
    int subject_length = (int)result.subject.length;
    switch (result.status) {
    case SEMANTIC_STATUS_ASSIGNMENT_TARGET_NOT_VARIABLE:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not a variable, so it cannot be assigned to.\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_DEFINITION_NAME_DUPLICATE:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is already defined.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_EXPRESSION_TOO_DEEP:
        fprintf(stderr, "%s:%zu:%zu: an expression in '%.*s' has too many nested operators.\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_IDENTIFIER_UNKNOWN:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not a variable or parameter of the procedure or a "
                "constant.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case SEMANTIC_STATUS_OK:
        assert(false);
        break;
    case SEMANTIC_STATUS_PARAMETER_NAME_DUPLICATE:
        fprintf(stderr, "%s:%zu:%zu: parameter '%.*s' is declared twice.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_PARAMETER_NAME_SHADOWS:
        fprintf(stderr, "%s:%zu:%zu: parameter '%.*s' has the name of a constant, which it would "
                "hide.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case SEMANTIC_STATUS_VARIABLE_NAME_DUPLICATE:
        fprintf(stderr, "%s:%zu:%zu: variable '%.*s' is defined twice.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_VARIABLE_NAME_SHADOWS:
        fprintf(stderr, "%s:%zu:%zu: variable '%.*s' has the name of a constant or a parameter, "
                "which it would hide.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    }
}

/**
 * Returns the line and column in `source` of `subject`, which must be a view into it. The reports
 * of the analysis and of the build both point at a name in the source this way.
 */
static Str_Position main_subject_position(Str source, Str subject) {
    // The subject views the source, so its distance from the start is its offset.
    assert(subject.bytes >= source.bytes);
    size_t offset = (size_t)(subject.bytes - source.bytes);
    assert(offset <= source.length);
    return str_position_at(source, offset);
}

static void main_run_command_report_wasm_text_error(char const *path, Wasm_Text_Result result) {
    assert(path != NULL);
    assert(result.status != WASM_TEXT_STATUS_OK);

    // The formatter reads what `wasm_build` wrote, so a module that it cannot format is a bug in
    // the compiler rather than in the program, and the message says so.
    switch (result.status) {
    case WASM_TEXT_STATUS_MODULE_MALFORMED:
        fprintf(stderr, "%s: internal error: the WebAssembly module is malformed at byte %zu.\n",
                path, result.error_offset);
        break;
    case WASM_TEXT_STATUS_MODULE_UNSUPPORTED:
        fprintf(stderr,
                "%s: internal error: the WebAssembly module cannot be formatted as text at byte "
                "%zu.\n",
                path, result.error_offset);
        break;
    case WASM_TEXT_STATUS_OK:
        assert(false);
        break;
    case WASM_TEXT_STATUS_OUTPUT_TOO_LARGE:
        fprintf(stderr, "%s: the WebAssembly text is larger than %d bytes.\n", path,
                OUTPUT_TEXT_SIZE_BYTES_MAX);
        break;
    }
}
