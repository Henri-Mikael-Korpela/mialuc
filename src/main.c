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
#include <mialu/process.h>
#include <mialu/semantic.h>
#include <mialu/str.h>
#include <mialu/tokenizer.h>
#include <mialu/wasm-text.h>
#include <mialu/wasm.h>
#include <mialu/x64.h>

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
// that do not write a program, and for `run` without a target, which shows the tokens and the
// syntax tree.
typedef enum {
    COMMAND_TARGET_NONE,
    COMMAND_TARGET_WASM,
    COMMAND_TARGET_WAT,
    COMMAND_TARGET_X64_WIN32,
} Command_Target;

/**
 * What the command line asks for. `output_path` is NULL unless the mode is build, and `target` is
 * `COMMAND_TARGET_NONE` unless the mode is build or the mode is run with a target. The paths are the C strings of `argv`, because they are only handed
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
    // The tokens without the comments, which is what the parser takes.
    Array_Slice code_tokens;
    Array_Slice nodes;
    Array_Slice bindings;
    Array_Slice output;
    Array_Slice output_text;
} Buffers;

static Command      main_command_parse                     (Array_Slice args);
static Command_Target main_command_target_parse            (char const *name);
static int32_t      main_run_command                       (Command command,
                                                            Buffers const *buffers);
static int32_t      main_run_command_build                 (Command command, Buffers const *buffers,
                                                            Str source, Parse_Result parse_result,
                                                            Semantic_Result semantic_result);
static int32_t      main_run_command_build_x64             (Command command, Buffers const *buffers,
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
static void         main_run_command_report_x64_error      (char const *path, Str source,
                                                            X64_Build_Result result);
static void         main_run_command_report_wasm_text_error(char const *path,
                                                            Wasm_Text_Result result);
static bool         main_run_command_write_file            (char const *path, Array_Slice bytes);
static Str_Position main_subject_position                  (Str source, Str subject);

/**
 * Has two modes. `run` reads a source file, tokenizes, parses and analyzes it, and prints the
 * tokens and the syntax tree, or, with a target that can run on this machine, builds the program
 * and runs it. `build` does the same silently and writes the program for a target.
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
                "       %s run --target x64-win32 <source file>\n"
                "       %s build --target wasm -o <output file> <source file>\n"
                "       %s build --target wat -o <output file> <source file>\n"
                "       %s build --target x64-win32 -o <output file> <source file>\n",
                argv[0], argv[0], argv[0], argv[0], argv[0]);
        return 2;
    }

    // Allocated once, here, because the buffers are too large for the default Windows stack.
    Buffers buffers = {
        .source = array_slice_make(malloc(SOURCE_SIZE_BYTES_MAX * sizeof(byte)),
                                   SOURCE_SIZE_BYTES_MAX),
        .tokens = array_slice_make(malloc(TOKEN_COUNT_MAX * sizeof(Token)), TOKEN_COUNT_MAX),
        .code_tokens = array_slice_make(malloc(TOKEN_COUNT_MAX * sizeof(Token)), TOKEN_COUNT_MAX),
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
        buffers.code_tokens.elements == NULL || buffers.nodes.elements == NULL ||
        buffers.bindings.elements == NULL ||
        buffers.output.elements == NULL || buffers.output_text.elements == NULL) {
        fprintf(stderr, "Out of memory.\n");
    } else {
        exit_code = main_run_command(command, &buffers);
    }

    free(buffers.output_text.elements);
    free(buffers.output.elements);
    free(buffers.bindings.elements);
    free(buffers.nodes.elements);
    free(buffers.code_tokens.elements);
    free(buffers.tokens.elements);
    free(buffers.source.elements);
    return exit_code;
}

/**
 * Understands the command line:
 *
 *     mialuc run <source file>
 *     mialuc run --target x64-win32 <source file>
 *     mialuc build --target wasm -o <output file> <source file>
 *     mialuc build --target wat -o <output file> <source file>
 *     mialuc build --target x64-win32 -o <output file> <source file>
 *
 * The output file of a build can also be written without `-o`, right after the target, which is
 * how it was written before there was a flag for it. Returns a command with mode
 * `COMMAND_MODE_INVALID` when it is anything else.
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
        if (args.length == 3) {
            return (Command){
                .source_path = argv[2],
                .output_path = NULL,
                .mode = COMMAND_MODE_RUN,
                .target = COMMAND_TARGET_NONE,
            };
        }
        // Only a program for the processor can be run, and it is not run in WebAssembly.
        if (args.length == 5 && strcmp(argv[2], "--target") == 0 &&
            main_command_target_parse(argv[3]) == COMMAND_TARGET_X64_WIN32) {
            return (Command){
                .source_path = argv[4],
                .output_path = NULL,
                .mode = COMMAND_MODE_RUN,
                .target = COMMAND_TARGET_X64_WIN32,
            };
        }
        return invalid;
    }

    if (strcmp(argv[1], "build") == 0) {
        if (args.length != 6 && args.length != 7) {
            return invalid;
        }
        if (strcmp(argv[2], "--target") != 0) {
            return invalid;
        }
        Command_Target target = main_command_target_parse(argv[3]);
        if (target == COMMAND_TARGET_NONE) {
            return invalid;
        }

        // The output file is after `-o`, or directly after the target.
        if (args.length == 7) {
            if (strcmp(argv[4], "-o") != 0) {
                return invalid;
            }
            return (Command){
                .source_path = argv[6],
                .output_path = argv[5],
                .mode = COMMAND_MODE_BUILD,
                .target = target,
            };
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
 * Returns the target that `name` stands for, or `COMMAND_TARGET_NONE` if there is none. `wat` is
 * the WebAssembly text format, which is the same module as `wasm` in a form that a person can
 * read, and `x64-win32` is a program for Windows on x64.
 */
static Command_Target main_command_target_parse(char const *name) {
    assert(name != NULL);

    if (strcmp(name, "wasm") == 0) {
        return COMMAND_TARGET_WASM;
    }
    if (strcmp(name, "wat") == 0) {
        return COMMAND_TARGET_WAT;
    }
    if (strcmp(name, "x64-win32") == 0) {
        return COMMAND_TARGET_X64_WIN32;
    }
    return COMMAND_TARGET_NONE;
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

    // Run without a target shows what the front end made. With one it runs the program, whose
    // output is the only output.
    bool is_inspection = command.mode == COMMAND_MODE_RUN && command.target == COMMAND_TARGET_NONE;
    if (is_inspection) {
        main_run_command_print_tokens(result.tokens);
    }

    // The comments are tokens for the sake of tools, but the parser does not look at them.
    Array_Slice code_tokens = tokens_without_comments(result.tokens, buffers->code_tokens);
    Parse_Result parse_result = parse(code_tokens, buffers->nodes);
    if (parse_result.status != PARSE_STATUS_OK) {
        main_run_command_report_parse_error(path, source, code_tokens, parse_result);
        return 1;
    }

    // The tree is printed before it is analyzed, so that a program with a bad name can be looked
    // at.
    if (is_inspection) {
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
        if (is_inspection) {
            return 0;
        }
        return main_run_command_build_x64(command, buffers, source, parse_result, semantic_result);
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

    if (command.target == COMMAND_TARGET_X64_WIN32) {
        return main_run_command_build_x64(command, buffers, source, parse_result,
                                          semantic_result);
    }

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
    case COMMAND_TARGET_X64_WIN32:
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
 * Builds the parsed and analyzed program for Windows on x64. A build writes the executable file to
 * the output file, and a run writes it to a temporary file and runs it, and its exit code is the
 * exit code of the compiler. Returns the exit code, which is 0 on success.
 */
static int32_t main_run_command_build_x64(Command command, Buffers const *buffers, Str source,
                                          Parse_Result parse_result,
                                          Semantic_Result semantic_result) {
    assert(command.target == COMMAND_TARGET_X64_WIN32);
    assert(parse_result.status == PARSE_STATUS_OK);
    assert(semantic_result.status == SEMANTIC_STATUS_OK);

    char const *path = command.source_path;
    X64_Build_Result build_result = x64_build(parse_result.nodes, semantic_result.bindings,
                                              parse_result.root, buffers->output);
    if (build_result.status != X64_BUILD_STATUS_OK) {
        main_run_command_report_x64_error(path, source, build_result);
        return 1;
    }

    if (command.mode == COMMAND_MODE_BUILD) {
        if (main_run_command_write_file(command.output_path, build_result.bytes) == false) {
            fprintf(stderr, "%s: cannot write file.\n", command.output_path);
            return 1;
        }
        return 0;
    }

    assert(command.mode == COMMAND_MODE_RUN);
    // The program writes through the C library, which buffers, and the compiler may too.
    (void)fflush(stdout);
    Process_Result process_result = process_run_executable(build_result.bytes);
    switch (process_result.status) {
    case PROCESS_STATUS_FILE_FAILED:
        fprintf(stderr, "%s: cannot write the program to the directory for temporary files.\n",
                path);
        return 1;
    case PROCESS_STATUS_OK:
        return process_result.exit_code;
    case PROCESS_STATUS_START_FAILED:
        fprintf(stderr, "%s: the system cannot start the program.\n", path);
        return 1;
    case PROCESS_STATUS_UNSUPPORTED:
        fprintf(stderr, "%s: running a program is only supported on Windows.\n", path);
        return 1;
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

        switch (token.kind) {
        case TOKEN_KIND_ARROW:
            printf("ARROW\n");
            break;
        case TOKEN_KIND_ASTERISK:
            printf("ASTERISK\n");
            break;
        case TOKEN_KIND_ASTERISK_EQUALS:
            printf("ASTERISK_EQUALS\n");
            break;
        case TOKEN_KIND_BRACE_LEFT:
            printf("BRACE_LEFT\n");
            break;
        case TOKEN_KIND_BRACE_RIGHT:
            printf("BRACE_RIGHT\n");
            break;
        case TOKEN_KIND_BRACKET_LEFT:
            printf("BRACKET_LEFT\n");
            break;
        case TOKEN_KIND_BRACKET_RIGHT:
            printf("BRACKET_RIGHT\n");
            break;
        case TOKEN_KIND_CARET:
            printf("CARET\n");
            break;
        case TOKEN_KIND_COMMA:
            printf("COMMA\n");
            break;
        case TOKEN_KIND_DOT:
            printf("DOT\n");
            break;
        case TOKEN_KIND_COMMENT:
            printf("COMMENT %.*s\n", (int)token.payload.comment.text.length,
                   token.payload.comment.text.bytes);
            break;
        case TOKEN_KIND_DOCUMENTATION_COMMENT:
            printf("DOCUMENTATION_COMMENT %.*s\n", (int)token.payload.comment.text.length,
                   token.payload.comment.text.bytes);
            break;
        case TOKEN_KIND_ELLIPSIS:
            printf("ELLIPSIS\n");
            break;
        case TOKEN_KIND_END:
            printf("END\n");
            break;
        case TOKEN_KIND_EQUALS:
            printf("EQUALS\n");
            break;
        case TOKEN_KIND_EQUALS_EQUALS:
            printf("EQUALS_EQUALS\n");
            break;
        case TOKEN_KIND_GREATER:
            printf("GREATER\n");
            break;
        case TOKEN_KIND_GREATER_EQUALS:
            printf("GREATER_EQUALS\n");
            break;
        case TOKEN_KIND_HASH:
            printf("HASH\n");
            break;
        case TOKEN_KIND_IDENTIFIER:
            // The cast is needed because `%.*s` takes an `int` precision.
            printf("IDENTIFIER %.*s\n", (int)token.payload.identifier.text.length,
                   token.payload.identifier.text.bytes);
            break;
        case TOKEN_KIND_INDENTATION:
            printf("INDENTATION %" PRIu32 "\n", token.payload.indentation.level);
            break;
        case TOKEN_KIND_INTEGER_LITERAL:
            printf("INTEGER_LITERAL %" PRId64 "\n", token.payload.integer_literal.value);
            break;
        case TOKEN_KIND_KEYWORD_BREAK:
            printf("KEYWORD_BREAK\n");
            break;
        case TOKEN_KIND_KEYWORD_CASE:
            printf("KEYWORD_CASE\n");
            break;
        case TOKEN_KIND_KEYWORD_CONST:
            printf("KEYWORD_CONST\n");
            break;
        case TOKEN_KIND_KEYWORD_CONTINUE:
            printf("KEYWORD_CONTINUE\n");
            break;
        case TOKEN_KIND_KEYWORD_ELSE:
            printf("KEYWORD_ELSE\n");
            break;
        case TOKEN_KIND_KEYWORD_ENUM:
            printf("KEYWORD_ENUM\n");
            break;
        case TOKEN_KIND_KEYWORD_FALSE:
            printf("KEYWORD_FALSE\n");
            break;
        case TOKEN_KIND_KEYWORD_AND:
            printf("KEYWORD_AND\n");
            break;
        case TOKEN_KIND_KEYWORD_FOR:
            printf("KEYWORD_FOR\n");
            break;
        case TOKEN_KIND_KEYWORD_FOREIGN:
            printf("KEYWORD_FOREIGN\n");
            break;
        case TOKEN_KIND_KEYWORD_IF:
            printf("KEYWORD_IF\n");
            break;
        case TOKEN_KIND_KEYWORD_IMPORT:
            printf("KEYWORD_IMPORT\n");
            break;
        case TOKEN_KIND_KEYWORD_IN:
            printf("KEYWORD_IN\n");
            break;
        case TOKEN_KIND_KEYWORD_MATCH:
            printf("KEYWORD_MATCH\n");
            break;
        case TOKEN_KIND_KEYWORD_NIL:
            printf("KEYWORD_NIL\n");
            break;
        case TOKEN_KIND_KEYWORD_NOT:
            printf("KEYWORD_NOT\n");
            break;
        case TOKEN_KIND_KEYWORD_OR:
            printf("KEYWORD_OR\n");
            break;
        case TOKEN_KIND_KEYWORD_PROC:
            printf("KEYWORD_PROC\n");
            break;
        case TOKEN_KIND_KEYWORD_RETURN:
            printf("KEYWORD_RETURN\n");
            break;
        case TOKEN_KIND_KEYWORD_STRUCT:
            printf("KEYWORD_STRUCT\n");
            break;
        case TOKEN_KIND_KEYWORD_TRUE:
            printf("KEYWORD_TRUE\n");
            break;
        case TOKEN_KIND_KEYWORD_TYPE_INFO_OF:
            printf("KEYWORD_TYPE_INFO_OF\n");
            break;
        case TOKEN_KIND_KEYWORD_UNION:
            printf("KEYWORD_UNION\n");
            break;
        case TOKEN_KIND_LABEL:
            printf("LABEL %.*s\n", (int)token.payload.identifier.text.length,
                   token.payload.identifier.text.bytes);
            break;
        case TOKEN_KIND_LESS:
            printf("LESS\n");
            break;
        case TOKEN_KIND_LESS_EQUALS:
            printf("LESS_EQUALS\n");
            break;
        case TOKEN_KIND_MINUS:
            printf("MINUS\n");
            break;
        case TOKEN_KIND_MINUS_EQUALS:
            printf("MINUS_EQUALS\n");
            break;
        case TOKEN_KIND_NOT_EQUALS:
            printf("NOT_EQUALS\n");
            break;
        case TOKEN_KIND_PARENTHESIS_LEFT:
            printf("PARENTHESIS_LEFT\n");
            break;
        case TOKEN_KIND_PARENTHESIS_RIGHT:
            printf("PARENTHESIS_RIGHT\n");
            break;
        case TOKEN_KIND_PERCENT:
            printf("PERCENT\n");
            break;
        case TOKEN_KIND_PLUS:
            printf("PLUS\n");
            break;
        case TOKEN_KIND_PLUS_EQUALS:
            printf("PLUS_EQUALS\n");
            break;
        case TOKEN_KIND_RANGE_EXCLUSIVE:
            printf("RANGE_EXCLUSIVE\n");
            break;
        case TOKEN_KIND_RANGE_INCLUSIVE:
            printf("RANGE_INCLUSIVE\n");
            break;
        case TOKEN_KIND_SLASH:
            printf("SLASH\n");
            break;
        case TOKEN_KIND_SLASH_EQUALS:
            printf("SLASH_EQUALS\n");
            break;
        case TOKEN_KIND_STRING_LITERAL:
            printf("STRING_LITERAL \"%.*s\"\n", (int)token.payload.string_literal.text.length,
                   token.payload.string_literal.text.bytes);
            break;
        case TOKEN_KIND_TYPE_SEPARATOR:
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
    case TOKENIZE_STATUS_COMMENT_ENCODING_INVALID:
        fprintf(stderr, "%s:%zu:%zu: a comment must be valid UTF-8.\n", path, line, column);
        break;
    case TOKENIZE_STATUS_DOCUMENTATION_COMMENT_TRAILING:
        fprintf(stderr, "%s:%zu:%zu: a documentation comment '///' must be on lines of its own, "
                "after code on a line use '//'.\n", path, line, column);
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
    case TOKENIZE_STATUS_LABEL_INVALID:
        fprintf(stderr, "%s:%zu:%zu: a label is '@' directly followed by a name that is not a "
                "reserved word.\n", path, line, column);
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
                character, (unsigned)(byte)character);
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

static void main_run_command_report_x64_error(char const *path, Str source,
                                              X64_Build_Result result) {
    assert(result.status != X64_BUILD_STATUS_OK);

    // These are about the whole program, so they have no place in the source.
    if (result.status == X64_BUILD_STATUS_OUTPUT_TOO_LARGE) {
        fprintf(stderr, "%s: the program is larger than %d bytes.\n", path,
                OUTPUT_SIZE_BYTES_MAX);
        return;
    }
    if (result.status == X64_BUILD_STATUS_MAIN_INVALID && result.subject.bytes == NULL) {
        fprintf(stderr, "%s: a program needs a procedure 'main :: proc() Int ->' to start with.\n",
                path);
        return;
    }

    Str_Position position = main_subject_position(source, result.subject);

    // The cast is needed because `%.*s` takes an `int` precision.
    int subject_length = (int)result.subject.length;
    switch (result.status) {
    case X64_BUILD_STATUS_CALL_ARGUMENT_COUNT_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: the call of '%.*s' has another number of arguments than the "
                "procedure has parameters.\n", path, position.line, position.column,
                subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_CALL_ARGUMENT_TYPE_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: an argument of the call of '%.*s' is not of the type of its "
                "parameter.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case X64_BUILD_STATUS_CALL_RESULT_MISSING:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' returns nothing, so its call has no value.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_FOREIGN_TARGET_UNSUPPORTED:
        fprintf(stderr, "%s:%zu:%zu: the foreign procedure '%.*s' is declared in a 'foreign js' "
                "block, and the x64 target imports from 'foreign libc'.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_MAIN_INVALID:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' must be 'main :: proc() Int ->', a procedure without "
                "parameters that returns an Int, which is the exit code.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_OK:
    case X64_BUILD_STATUS_OUTPUT_TOO_LARGE:
        assert(false);
        break;
    case X64_BUILD_STATUS_PARAMETER_COUNT_UNSUPPORTED:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' has more than %d parameters, which the x64 target "
                "does not support.\n", path, position.line, position.column, subject_length,
                result.subject.bytes, X64_PARAMETER_COUNT_MAX);
        break;
    case X64_BUILD_STATUS_PROCEDURE_COUNT_EXCEEDED:
        fprintf(stderr, "%s:%zu:%zu: more than %d procedures.\n", path, position.line,
                position.column, X64_PROCEDURE_COUNT_MAX);
        break;
    case X64_BUILD_STATUS_RETURN_TYPE_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: the value that '%.*s' returns is not of its return type.\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_RETURN_TYPE_MISSING:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' has no return type, so it cannot return a value.\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_TYPE_UNSUPPORTED:
        fprintf(stderr, "%s:%zu:%zu: type '%.*s' is not supported by the x64 target, which has "
                "Int and C_Str.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case X64_BUILD_STATUS_UNSUPPORTED:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' has something that the x64 target does not build "
                "yet.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
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
    case WASM_BUILD_STATUS_ADDRESS_INVALID:
        fprintf(stderr, "%s:%zu:%zu: cannot take the address of '%.*s'. Only a variable of a "
                "procedure, a field or an element of one, and what a pointer points to have an "
                "address, and not a parameter, an iterator, a global variable, a value, an array "
                "or an array slice.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not an Int, so it cannot be used in arithmetic.\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_ARRAY_ELEMENT_TYPE_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not of the type of the elements of the array.\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_ARRAY_EXPECTED:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not an array or an array slice.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_ARRAY_INITIALIZER_INVALID:
        fprintf(stderr, "%s:%zu:%zu: variable '%.*s' is an array, so it must be given an array "
                "literal, such as [const]Int { 1, 2 }.\n", path, position.line, position.column,
                subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_ARRAY_LENGTH_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: the array of '%.*s' has a different number of elements than "
                "the length in its type.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_ARRAY_TYPE_MISPLACED:
        fprintf(stderr, "%s:%zu:%zu: an array of '%.*s' cannot be the type here. A parameter can "
                "be an array slice, as in []%.*s, and a global variable can be neither.\n", path,
                position.line, position.column, subject_length, result.subject.bytes,
                subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_FIELD_BASE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: the field '%.*s' is read or assigned through a value that "
                "is not a variable, a parameter or an element of an array.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_FIELD_TARGET_READ_ONLY:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is a parameter or an iterator, which cannot be "
                "assigned to, so neither can its fields. A field of what a pointer points to can "
                "be.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_BUILT_IN_UNSUPPORTED:
        fprintf(stderr, "%s:%zu:%zu: the built-in procedure '%.*s' is not supported by the wasm "
                "target.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_FOREIGN_TARGET_UNSUPPORTED:
        fprintf(stderr, "%s:%zu:%zu: the foreign procedure '%.*s' is declared in a 'foreign "
                "libc' block, which the wasm target cannot import from. It imports from "
                "'foreign js'.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_STRUCT_EXPECTED:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not a struct or a pointer to one, so it has no "
                "fields.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_STRUCT_FIELD_DUPLICATE:
        fprintf(stderr, "%s:%zu:%zu: the field '%.*s' is given a value twice.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_STRUCT_FIELD_MISSING:
        fprintf(stderr, "%s:%zu:%zu: the field '%.*s' is not given a value. Either every field is "
                "given a value, or the braces are empty and every field has its default value.\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_STRUCT_FIELD_TYPE_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: the value of the field '%.*s' is not of the type of the "
                "field.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_STRUCT_FIELD_UNKNOWN:
        fprintf(stderr, "%s:%zu:%zu: the struct has no field called '%.*s'.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_STRUCT_RECURSIVE:
        fprintf(stderr, "%s:%zu:%zu: the struct with the field '%.*s' contains itself, so it "
                "would be infinitely large.\n", path, position.line, position.column,
                subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_CALL_ARGUMENT_COUNT_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: the call of '%.*s' has a different number of arguments than "
                "the procedure has parameters.\n", path, position.line, position.column,
                subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_CALL_ARGUMENT_TYPE_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: an argument of the call of '%.*s' is not of the type of its "
                "parameter.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_CALL_RESULT_MISSING:
        fprintf(stderr, "%s:%zu:%zu: the procedure '%.*s' returns no value, so its call cannot be "
                "used as one.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_CALL_TARGET_NOT_PROCEDURE:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' does not hold a pointer to a procedure, so it cannot "
                "be called.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_COMPARISON_OPERAND_TYPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' cannot be compared here: both operands of a comparison "
                "must be Int, or both must be Bool or pointers of one type for == and not=.\n",
                path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_LOGICAL_OPERAND_TYPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not a Bool, so it cannot be used with 'and', 'or' "
                "or 'not'.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_CONDITION_TYPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: the condition at '%.*s' is not a Bool, and an Int is not "
                "converted to one.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_DEFINITION_NAME_RESERVED:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is reserved for the memory of the wasm module.\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_ENUM_MEMBER_UNKNOWN:
        fprintf(stderr, "%s:%zu:%zu: the enum has no member '%.*s'.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_ENUM_TYPE_UNKNOWN:
        fprintf(stderr, "%s:%zu:%zu: the enum of '.%.*s' is not known here, so write it, as in "
                "'Color.%.*s', or give the member to something that has an enum type.\n", path,
                position.line, position.column, subject_length, result.subject.bytes,
                subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_EXPRESSION_TOO_DEEP:
        fprintf(stderr, "%s:%zu:%zu: an expression in '%.*s' has too many nested operators.\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_INDEX_TYPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not an Int, so it cannot be an index.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_IDENTIFIER_NOT_VALUE:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is a struct or an enum, which the wasm target "
                "cannot use as a value.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_OK:
    case WASM_BUILD_STATUS_OUTPUT_TOO_LARGE:
        assert(false);
        break;
    case WASM_BUILD_STATUS_NIL_TYPE_UNKNOWN:
        fprintf(stderr, "%s:%zu:%zu: variable '%.*s' is given nil, which does not say what it "
                "points to, so the variable needs a pointer type, such as '%.*s : ^Int = nil'.\n",
                path, position.line, position.column, subject_length, result.subject.bytes,
                subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_POINTER_EXPECTED:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not a pointer, so '^' cannot follow it.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_POINTER_TARGET_TYPE_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: the value assigned through '%.*s' is not of the type that it "
                "points to.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_RANGE_TYPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not an Int, so it cannot be the start or the "
                "end of a range.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
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
    case WASM_BUILD_STATUS_TYPE_INFO_MISPLACED:
        fprintf(stderr, "%s:%zu:%zu: type_info_of('%.*s') exists only when the program is "
                "compiled, so a field has to follow it right away, as in "
                "type_info_of(%.*s).members. A global variable can be given that and nothing "
                "more.\n", path, position.line, position.column, subject_length,
                result.subject.bytes, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_TYPE_INFO_UNSUPPORTED:
        fprintf(stderr, "%s:%zu:%zu: the wasm target has information about enums only, and '%.*s' "
                "is not one.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_TYPE_UNSUPPORTED:
        fprintf(stderr, "%s:%zu:%zu: type '%.*s' is not supported by the wasm target.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_UNION_CASE_UNKNOWN:
        fprintf(stderr, "%s:%zu:%zu: the case '.%.*s' is not a member of the enum of the field "
                "that the union is matched against.\n", path, position.line, position.column,
                subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_UNION_DISCRIMINANT_INVALID:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' cannot be matched by a union: it must be a field of "
                "the struct, outside of the cases, and of an enum type.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_UNION_FIELD_KIND_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: the field '%.*s' is in a case of a union, but the struct "
                "object is given another member for the field that the union is matched "
                "against, so the field does not exist there.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_UNION_FIELD_UNMATCHED:
        fprintf(stderr, "%s:%zu:%zu: the field '%.*s' is in a case of a union, so it can only be "
                "used where the field that the union is matched against is known to be the "
                "member of the case, such as inside 'if x.kind == .MEMBER ->' and without "
                "changing 'x' or 'x.kind' in it.\n", path, position.line, position.column,
                subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_UNION_KIND_NOT_CONSTANT:
        fprintf(stderr, "%s:%zu:%zu: the field '%.*s' is in a case of a union, so the struct "
                "object must give the field that the union is matched against a member of the "
                "enum that is written out, such as '.MEMBER', and not another value.\n", path,
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
    case SEMANTIC_STATUS_ASSIGNMENT_TARGET_ITERATOR:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is the iterator of a for loop, so it cannot be "
                "assigned to.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case SEMANTIC_STATUS_ATTRIBUTE_DUPLICATE:
        fprintf(stderr, "%s:%zu:%zu: the attribute '%.*s' is given twice.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_ATTRIBUTE_UNKNOWN:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not an attribute of a global variable. The only "
                "one is 'static'.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case SEMANTIC_STATUS_FIELD_NAME_DUPLICATE:
        fprintf(stderr, "%s:%zu:%zu: the struct already has a field called '%.*s'.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_FOREIGN_IMPORT_DUPLICATE:
        fprintf(stderr, "%s:%zu:%zu: the program already imports foreign procedures from "
                "this target, and can do it once for each target.\n", path, position.line,
                position.column);
        break;
    case SEMANTIC_STATUS_FOREIGN_IMPORT_MISSING:
        fprintf(stderr, "%s:%zu:%zu: the foreign procedure '%.*s' needs an 'import foreign' of "
                "its target above it.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case SEMANTIC_STATUS_GLOBAL_NOT_STATIC:
        fprintf(stderr, "%s:%zu:%zu: global variable '%.*s' must have the attribute #(static).\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_CALL_TARGET_NOT_PROCEDURE:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not a procedure, so it cannot be called.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_ENUM_MEMBER_DUPLICATE:
        fprintf(stderr, "%s:%zu:%zu: the enum has the member '%.*s' twice.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_DEFINITION_NAME_DUPLICATE:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is already defined.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_IDENTIFIER_UNKNOWN:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not a variable or parameter of the procedure, a "
                "constant or a global variable.\n", path, position.line, position.column,
                subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_JUMP_OUTSIDE_LOOP:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not inside a loop.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_BREAK_OUTSIDE_SCOPE:
        fprintf(stderr, "%s:%zu:%zu: 'break' is not inside a loop or a scope.\n", path,
                position.line, position.column);
        break;
    case SEMANTIC_STATUS_LABEL_DUPLICATE:
        fprintf(stderr, "%s:%zu:%zu: the label '%.*s' is already used by a statement around "
                "this one.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case SEMANTIC_STATUS_LABEL_NOT_LOOP:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not the label of a loop, so 'continue' cannot go on "
                "with it.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case SEMANTIC_STATUS_LABEL_UNKNOWN:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not the label of a statement around this "
                "'break' or 'continue'.\n", path, position.line, position.column, subject_length,
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
        fprintf(stderr, "%s:%zu:%zu: parameter '%.*s' has the name of a constant or a global "
                "variable, which it would hide.\n", path, position.line, position.column,
                subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_VARIABLE_NAME_DUPLICATE:
        fprintf(stderr, "%s:%zu:%zu: variable '%.*s' is defined twice.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_VARIABLE_NAME_SHADOWS:
        fprintf(stderr, "%s:%zu:%zu: variable '%.*s' has the name of a constant, a global "
                "variable or a parameter, which it would hide.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    }
}

/**
 * Returns the line and column in `source` of `subject`, which must be a view into it. The reports
 * of the analysis and of the build both point at a name in the source this way.
 */
static Str_Position main_subject_position(Str source, Str subject) {
    // The subject views the source, so its distance from the start is its offset. The exception is
    // the built-in struct that `type_info_of` adds, whose name is not in the source, so an error
    // about it, which a program that defines the same name causes, points at the start.
    if (subject.bytes < source.bytes || subject.bytes > source.bytes + source.length) {
        return str_position_at(source, 0);
    }
    size_t offset = (size_t)(subject.bytes - source.bytes);
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
