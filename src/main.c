#include <assert.h>
#include <inttypes.h>
#include <iso646.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mialu/array_slice.h>
#include <mialu/ast.h>
#include <mialu/common_types.h>
#include <mialu/constant.h>
#include <mialu/parser.h>
#include <mialu/process.h>
#include <mialu/semantic.h>
#include <mialu/str.h>
#include <mialu/tokenizer.h>
#include <mialu/version.h>
#include <mialu/wasm-text.h>
#include <mialu/wasm.h>
#include <mialu/x64-text.h>
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
    // Bounds the absolute path of the source file that a debug build writes into the program. It
    // is the longest path that Windows has, so any path that the system resolves fits.
    SOURCE_PATH_SIZE_BYTES_MAX = 32 * 1024,
};

// Alphabetical, because nothing depends on the order. `COMMAND_MODE_INVALID` is the result of a
// command line that is not understood.
typedef enum {
    COMMAND_MODE_BUILD,
    COMMAND_MODE_DUMP_ASM,
    COMMAND_MODE_DUMP_AST,
    COMMAND_MODE_DUMP_STRUCT_SIZES,
    COMMAND_MODE_INVALID,
    COMMAND_MODE_RUN,
} Command_Mode;

// Alphabetical, because nothing depends on the order. `COMMAND_TARGET_NONE` is for the mode that
// does not make a program, which is `dump-ast`.
typedef enum {
    COMMAND_TARGET_NONE,
    COMMAND_TARGET_WASM,
    COMMAND_TARGET_WAT,
    COMMAND_TARGET_X64_WIN32,
} Command_Target;

/**
 * What the command line asks for. `output_path` is NULL unless the mode is build, and `target` is
 * `COMMAND_TARGET_NONE` only when the mode is dump-ast. The mode dump-asm has the target that it
 * shows the assembly of. The target of run is the one that runs on
 * this machine. The paths are the C strings of `argv`, because they are only handed to `fopen`.
 * `is_debug` is set for a build that has debug information, which only the target `x64-win32` has.
 */
typedef struct {
    char const *source_path;
    char const *output_path;
    Command_Mode mode;
    Command_Target target;
    bool is_debug;
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
    Array_Slice constants;
    Array_Slice output;
    Array_Slice output_text;
    // The places of the code of the x64 target that a jump goes to, one byte for each byte of the
    // program, which the assembly gives labels.
    Array_Slice labels;
    // What the x64 target works out about each node: the types and the places of the variables.
    Array_Slice layouts;
    // The procedures of the program that the x64 target builds, which name its assembly.
    Array_Slice symbols;
    // The places of the code that the debug information of the x64 target is about, and the
    // absolute path of the source file that it names.
    Array_Slice debug_rows;
    Array_Slice source_path;
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
static void         main_run_command_print_struct_sizes    (Str source, Parse_Result parse_result);
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
static void         main_run_command_report_x64_text_error (char const *path,
                                                            X64_Text_Result result);
static bool         main_run_command_write_file            (char const *path, Array_Slice bytes);
static Str_Position main_subject_position                  (Str source, Str subject);

/**
 * Has four modes. `run` reads a source file, tokenizes, parses and analyzes it, builds the program
 * for this machine and runs it. `dump-ast` does the same up to the analysis, and prints the tokens
 * and the syntax tree. `dump-asm` builds the program for a target and prints its assembly. `build`
 * builds silently and writes the program for a target.
 * Everything that allocates is in `main` on purpose, so that the buffers are freed in one place on
 * every path.
 */
int main(int argc, char *argv[]) {
    // The cast is needed because `argc` is an `int`, which `main` must declare. It is never
    // negative.
    Array_Slice args = array_slice_make(argv, (size_t)argc);
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("mialuc %s\n", MIALU_VERSION_STRING);
        return 0;
    }
    Command command = main_command_parse(args);
    if (command.mode == COMMAND_MODE_INVALID) {
        fprintf(stderr,
                "Usage: %s run <source file>\n"
                "       %s dump-ast <source file>\n"
                "       %s dump-asm --target x64-win32 <source file>\n"
                "       %s dump-struct-sizes <source file>\n"
                "       %s build --target wasm -o <output file> <source file>\n"
                "       %s build --target wat -o <output file> <source file>\n"
                "       %s build --target x64-win32 [--debug] -o <output file> <source file>\n"
                "       %s --version\n",
                argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0]);
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
        // And one value that is known when the program is compiled for each node.
        .constants = array_slice_make(malloc(TOKEN_COUNT_MAX * sizeof(Constant_Value)),
                                      TOKEN_COUNT_MAX),
        .output = array_slice_make(malloc(OUTPUT_SIZE_BYTES_MAX * sizeof(byte)),
                                   OUTPUT_SIZE_BYTES_MAX),
        .output_text = array_slice_make(malloc(OUTPUT_TEXT_SIZE_BYTES_MAX * sizeof(byte)),
                                        OUTPUT_TEXT_SIZE_BYTES_MAX),
        // The code is part of the program, so it is never larger than the program.
        .labels = array_slice_make(malloc(OUTPUT_SIZE_BYTES_MAX * sizeof(byte)),
                                   OUTPUT_SIZE_BYTES_MAX),
        // And one layout of the x64 target for each node.
        .layouts = array_slice_make(malloc(TOKEN_COUNT_MAX * sizeof(X64_Node_Layout)),
                                    TOKEN_COUNT_MAX),
        .symbols = array_slice_make(malloc(X64_CONSTANT_COUNT_MAX * sizeof(X64_Symbol)),
                                    X64_CONSTANT_COUNT_MAX),
        // As many nodes as tokens, see above, and the most rows that the build notes for each.
        .debug_rows = array_slice_make(
            malloc(X64_DEBUG_ROWS_PER_NODE * TOKEN_COUNT_MAX * sizeof(X64_Debug_Row)),
            X64_DEBUG_ROWS_PER_NODE * TOKEN_COUNT_MAX),
        .source_path = array_slice_make(malloc(SOURCE_PATH_SIZE_BYTES_MAX * sizeof(char)),
                                        SOURCE_PATH_SIZE_BYTES_MAX),
    };

    int32_t exit_code = 1;
    if (buffers.source.elements == NULL || buffers.tokens.elements == NULL ||
        buffers.code_tokens.elements == NULL || buffers.nodes.elements == NULL ||
        buffers.bindings.elements == NULL || buffers.constants.elements == NULL ||
        buffers.output.elements == NULL || buffers.output_text.elements == NULL ||
        buffers.labels.elements == NULL ||
        buffers.layouts.elements == NULL || buffers.symbols.elements == NULL ||
        buffers.debug_rows.elements == NULL ||
        buffers.source_path.elements == NULL) {
        fprintf(stderr, "Out of memory.\n");
    } else {
        exit_code = main_run_command(command, &buffers);
    }

    free(buffers.source_path.elements);
    free(buffers.debug_rows.elements);
    free(buffers.symbols.elements);
    free(buffers.layouts.elements);
    free(buffers.labels.elements);
    free(buffers.output_text.elements);
    free(buffers.output.elements);
    free(buffers.constants.elements);
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
 *     mialuc dump-ast <source file>
 *     mialuc dump-asm --target x64-win32 <source file>
 *     mialuc dump-struct-sizes <source file>
 *     mialuc build --target wasm -o <output file> <source file>
 *     mialuc build --target wat -o <output file> <source file>
 *     mialuc build --target x64-win32 [--debug] -o <output file> <source file>
 *
 * `mialuc --version` is answered in `main`, before the command is parsed. The output file of a
 * build can also be written without `-o`, right after the target or `--debug`, which is how it was
 * written before there was a flag for it. `--debug` comes right after the target, and only the
 * target `x64-win32` has debug information. Returns a command with mode `COMMAND_MODE_INVALID` when
 * it is anything else.
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
        .is_debug = false,
    };
    if (args.length < 2) {
        return invalid;
    }

    if (strcmp(argv[1], "run") == 0) {
        if (args.length != 3) {
            return invalid;
        }
        // Only a program for the processor can be run, and it is not run in WebAssembly. Windows
        // on x64 is the only machine that a program can run on for now.
        return (Command){
            .source_path = argv[2],
            .output_path = NULL,
            .mode = COMMAND_MODE_RUN,
            .target = COMMAND_TARGET_X64_WIN32,
            .is_debug = false,
        };
    }

    if (strcmp(argv[1], "dump-asm") == 0) {
        // The assembly is shown for the target that has it, which is the one for the processor.
        if (args.length != 5 || strcmp(argv[2], "--target") != 0 ||
            main_command_target_parse(argv[3]) != COMMAND_TARGET_X64_WIN32) {
            return invalid;
        }
        return (Command){
            .source_path = argv[4],
            .output_path = NULL,
            .mode = COMMAND_MODE_DUMP_ASM,
            .target = COMMAND_TARGET_X64_WIN32,
            .is_debug = false,
        };
    }

    if (strcmp(argv[1], "dump-struct-sizes") == 0) {
        if (args.length != 3) {
            return invalid;
        }
        return (Command){
            .source_path = argv[2],
            .output_path = NULL,
            .mode = COMMAND_MODE_DUMP_STRUCT_SIZES,
            .target = COMMAND_TARGET_NONE,
            .is_debug = false,
        };
    }

    if (strcmp(argv[1], "dump-ast") == 0) {
        if (args.length != 3) {
            return invalid;
        }
        return (Command){
            .source_path = argv[2],
            .output_path = NULL,
            .mode = COMMAND_MODE_DUMP_AST,
            .target = COMMAND_TARGET_NONE,
            .is_debug = false,
        };
    }

    if (strcmp(argv[1], "build") == 0) {
        if (args.length < 4 || strcmp(argv[2], "--target") != 0) {
            return invalid;
        }
        Command_Target target = main_command_target_parse(argv[3]);
        if (target == COMMAND_TARGET_NONE) {
            return invalid;
        }

        // `--debug` is right after the target, and the debug information is of the program for
        // the processor.
        size_t next = 4;
        bool is_debug = false;
        if (args.length > next && strcmp(argv[next], "--debug") == 0) {
            if (target != COMMAND_TARGET_X64_WIN32) {
                return invalid;
            }
            is_debug = true;
            next += 1;
        }

        // The output file is after `-o`, or directly after the target or `--debug`.
        if (args.length == next + 3) {
            if (strcmp(argv[next], "-o") != 0) {
                return invalid;
            }
            next += 1;
        }
        if (args.length != next + 2) {
            return invalid;
        }
        return (Command){
            .source_path = argv[next + 1],
            .output_path = argv[next],
            .mode = COMMAND_MODE_BUILD,
            .target = target,
            .is_debug = is_debug,
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

    // Dumping the syntax tree shows what the front end made. Running the program has its output
    // as the only output.
    bool is_inspection = command.mode == COMMAND_MODE_DUMP_AST;
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
                                                       buffers->bindings, buffers->constants);
    if (semantic_result.status != SEMANTIC_STATUS_OK) {
        main_run_command_report_semantic_error(path, source, semantic_result);
        return 1;
    }

    switch (command.mode) {
    case COMMAND_MODE_BUILD:
        return main_run_command_build(command, buffers, source, parse_result, semantic_result);
    case COMMAND_MODE_DUMP_ASM:
        return main_run_command_build_x64(command, buffers, source, parse_result,
                                          semantic_result);
    case COMMAND_MODE_DUMP_AST:
        return 0;
    case COMMAND_MODE_DUMP_STRUCT_SIZES:
        main_run_command_print_struct_sizes(source, parse_result);
        return 0;
    case COMMAND_MODE_INVALID:
        assert(false);
        return 1;
    case COMMAND_MODE_RUN:
        return main_run_command_build_x64(command, buffers, source, parse_result, semantic_result);
    }

    assert(false);
    return 1;
}

/**
 * Prints a line for each struct that is defined as a constant of the module, in the order of the
 * source: the line of its definition, counted from 1, its name, its size in bytes and its
 * alignment in bytes, separated by tabs. The sizes are those of the program for the processor, see
 * `x64_struct_size_bytes`. This is what an editor shows above the definition of a struct. A struct
 * that has no size, because it contains itself, is left out.
 */
static void main_run_command_print_struct_sizes(Str source, Parse_Result parse_result) {
    assert(parse_result.status == PARSE_STATUS_OK);

    Ast_Node const *nodes = parse_result.nodes.elements;
    Ast_Node_Index definition = nodes[parse_result.root].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < parse_result.nodes.length && definition != AST_NODE_INDEX_NONE; i += 1) {
        Ast_Node const *node = &nodes[definition];
        definition = node->next_sibling;
        if (node->node_type != AST_NODE_TYPE_CONSTANT_DEFINITION ||
            nodes[node->first_child].node_type != AST_NODE_TYPE_STRUCT_TYPE) {
            continue;
        }

        size_t size_bytes = x64_struct_size_bytes(parse_result.nodes, parse_result.root,
                                                  node->first_child);
        if (size_bytes == SIZE_MAX) {
            continue;
        }
        Str name = node->payload.constant_definition.name;
        Str_Position position = str_position_at(source, node->source_offset);
        printf("%zu\t%.*s\t%zu\t%d\n", position.line, (int)name.length, name.bytes, size_bytes,
               (int)X64_STRUCT_ALIGNMENT_BYTES);
    }
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
    Wasm_Build_Result build_result =
        wasm_build(parse_result.nodes, semantic_result.bindings, semantic_result.constants,
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

    if (not main_run_command_write_file(command.output_path, output)) {
        fprintf(stderr, "%s: cannot write file.\n", command.output_path);
        return 1;
    }
    return 0;
}

/**
 * Builds the parsed and analyzed program for Windows on x64. A build writes the executable file to
 * the output file, a dump-asm prints its assembly, and a run writes it to a temporary file and runs
 * it, and its exit code is the exit code of the compiler. Returns the exit code, which is 0 on
 * success.
 */
static int32_t main_run_command_build_x64(Command command, Buffers const *buffers, Str source,
                                          Parse_Result parse_result,
                                          Semantic_Result semantic_result) {
    assert(command.target == COMMAND_TARGET_X64_WIN32);
    assert(parse_result.status == PARSE_STATUS_OK);
    assert(semantic_result.status == SEMANTIC_STATUS_OK);

    char const *path = command.source_path;

    // The debug information names the source file by its absolute path, which is how a debugger
    // finds it whatever directory it runs in.
    X64_Debug debug = {
        .source = source,
        .source_path = STR_EMPTY,
        .row_buffer = buffers->debug_rows,
    };
    if (command.is_debug) {
        debug.source_path = process_path_absolute(path, buffers->source_path);
        if (debug.source_path.bytes == NULL) {
            fprintf(stderr, "%s: cannot find the absolute path of the file.\n", path);
            return 1;
        }
    }
    X64_Debug const *build_debug = NULL;
    if (command.is_debug) {
        build_debug = &debug;
    }
    X64_Build_Result build_result =
        x64_build(parse_result.nodes, semantic_result.bindings, semantic_result.constants,
                  parse_result.root, buffers->layouts, buffers->output, buffers->symbols,
                  build_debug);
    if (build_result.status != X64_BUILD_STATUS_OK) {
        main_run_command_report_x64_error(path, source, build_result);
        return 1;
    }

    if (command.mode == COMMAND_MODE_DUMP_ASM) {
        // The assembly is formatted from the executable file rather than written from the syntax
        // tree, so that it always shows exactly what the build wrote.
        X64_Text_Result text_result = x64_text_format(build_result.bytes, build_result.symbols,
                                                      buffers->labels, buffers->output_text);
        if (text_result.status != X64_TEXT_STATUS_OK) {
            main_run_command_report_x64_text_error(path, text_result);
            return 1;
        }
        if (fwrite(text_result.text.bytes, sizeof(byte), text_result.text.length, stdout) !=
            text_result.text.length) {
            fprintf(stderr, "Cannot write the assembly to the standard output.\n");
            return 1;
        }
        return 0;
    }

    if (command.mode == COMMAND_MODE_BUILD) {
        if (not main_run_command_write_file(command.output_path, build_result.bytes)) {
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

    if (is_read_error || is_close_error || not is_at_end) {
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

    return written == bytes.length && not is_close_error;
}

/**
 * Prints one token per line, for the dump-ast mode.
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
        case TOKEN_KIND_FLOAT_LITERAL:
            printf("FLOAT_LITERAL %.17g\n", token.payload.float_literal.value);
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
        case TOKEN_KIND_KEYWORD_CAST:
            printf("KEYWORD_CAST\n");
            break;
        case TOKEN_KIND_KEYWORD_CONST:
            printf("KEYWORD_CONST\n");
            break;
        case TOKEN_KIND_KEYWORD_CONTINUE:
            printf("KEYWORD_CONTINUE\n");
            break;
        case TOKEN_KIND_KEYWORD_DEFER:
            printf("KEYWORD_DEFER\n");
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
        case TOKEN_KIND_KEYWORD_PACKAGE:
            printf("KEYWORD_PACKAGE\n");
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
        case TOKEN_KIND_RUNE_LITERAL:
            printf("RUNE_LITERAL U+%04" PRIX32 "\n", token.payload.rune_literal.value);
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
    case TOKENIZE_STATUS_FLOAT_LITERAL_DIGIT_INVALID:
        fprintf(stderr, "%s:%zu:%zu: '%c' is not a digit of the float literal, which has the "
                "digits 0 to 9 and nothing else after the dot, since there is no exponent and no "
                "suffix.\n", path, line, column, source.bytes[result.error_offset]);
        break;
    case TOKENIZE_STATUS_FLOAT_LITERAL_SEPARATOR_INVALID:
        fprintf(stderr, "%s:%zu:%zu: a digit separator '_' must be between two digits.\n", path,
                line, column);
        break;
    case TOKENIZE_STATUS_FLOAT_LITERAL_TOO_LARGE:
        fprintf(stderr, "%s:%zu:%zu: float literal is larger than the largest F64, about "
                "1.8e308.\n", path, line, column);
        break;
    case TOKENIZE_STATUS_FLOAT_LITERAL_TOO_LONG:
        fprintf(stderr, "%s:%zu:%zu: float literal has more than 128 characters.\n", path, line,
                column);
        break;
    case TOKENIZE_STATUS_INTEGER_LITERAL_DIGIT_INVALID:
        fprintf(stderr, "%s:%zu:%zu: '%c' is not a digit of the integer literal, which has the "
                "digits 0 to 9, or 0 and 1 after '0b', or 0 to 9 and a to f after '0x'.\n",
                path, line, column, source.bytes[result.error_offset]);
        break;
    case TOKENIZE_STATUS_INTEGER_LITERAL_DIGITS_MISSING:
        fprintf(stderr, "%s:%zu:%zu: integer literal has no digits after its prefix.\n", path,
                line, column);
        break;
    case TOKENIZE_STATUS_INTEGER_LITERAL_SEPARATOR_INVALID:
        fprintf(stderr, "%s:%zu:%zu: a digit separator '_' must be between two digits.\n", path,
                line, column);
        break;
    case TOKENIZE_STATUS_INTEGER_LITERAL_TOO_LARGE:
        fprintf(stderr, "%s:%zu:%zu: integer literal is larger than the largest Int, "
                "9223372036854775807.\n", path, line, column);
        break;
    case TOKENIZE_STATUS_LABEL_INVALID:
        fprintf(stderr, "%s:%zu:%zu: a label is '@' directly followed by a name that is not a "
                "reserved word.\n", path, line, column);
        break;
    case TOKENIZE_STATUS_RUNE_LITERAL_EMPTY:
        fprintf(stderr, "%s:%zu:%zu: a rune literal has a character between its quotes, and "
                "this has none.\n", path, line, column);
        break;
    case TOKENIZE_STATUS_RUNE_LITERAL_ESCAPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: this is not an escape sequence of a rune: a backslash and "
                "one of a, b, e, f, n, r, t, v, \\, ' or \", three octal digits up to 377, x and "
                "two hexadecimal digits, u and four, or U and eight, with a number that is a "
                "Unicode character, which is at most 10FFFF and not D800 to DFFF.\n", path, line,
                column);
        break;
    case TOKENIZE_STATUS_RUNE_LITERAL_ENCODING_INVALID:
        fprintf(stderr, "%s:%zu:%zu: the character of a rune must be valid UTF-8.\n", path, line,
                column);
        break;
    case TOKENIZE_STATUS_RUNE_LITERAL_TOO_LONG:
        fprintf(stderr, "%s:%zu:%zu: a rune literal has one character between its quotes, and "
                "this has more.\n", path, line, column);
        break;
    case TOKENIZE_STATUS_RUNE_LITERAL_UNTERMINATED:
        fprintf(stderr, "%s:%zu:%zu: a rune literal has no closing quote on its line.\n", path,
                line, column);
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
    case X64_BUILD_STATUS_ADDRESS_INVALID:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' has no address. A variable, an element and what a "
                "pointer points to have one, but not a parameter, an iterator, a value that is "
                "computed, or an array.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case X64_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: an operand of arithmetic at '%.*s' is not an Int or a float, "
                "or is not of the type of the other operand.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_ARRAY_ELEMENT_TYPE_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: the element at '%.*s' is not of the type of the elements of "
                "the array.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case X64_BUILD_STATUS_ARRAY_EXPECTED:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not an array or an array slice.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_ARRAY_INITIALIZER_INVALID:
        fprintf(stderr, "%s:%zu:%zu: the array '%.*s' is not copied, so it is defined with an "
                "array literal and is not assigned to as a whole.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_ARRAY_LENGTH_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: the array literal of '%.*s' has another number of elements "
                "than its length.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case X64_BUILD_STATUS_ARRAY_TYPE_MISPLACED:
        fprintf(stderr, "%s:%zu:%zu: the parameter '%.*s' cannot be an array, which is in the "
                "frame of its procedure, so it takes an array slice, written '[]Type'.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
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
    case X64_BUILD_STATUS_CALL_TARGET_NOT_PROCEDURE:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is called, but it does not hold a pointer to a "
                "procedure.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case X64_BUILD_STATUS_COMPARISON_OPERAND_TYPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: the operands of the comparison at '%.*s' are not two values "
                "that can be compared with its operator.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_CONDITION_TYPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: the condition at '%.*s' is not a Bool, and an Int is not "
                "converted to one.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case X64_BUILD_STATUS_CONSTANT_COUNT_EXCEEDED:
        fprintf(stderr, "%s:%zu:%zu: more than %d constants, procedures and strings.\n", path,
                position.line, position.column, X64_CONSTANT_COUNT_MAX);
        break;
    case X64_BUILD_STATUS_ENUM_MEMBER_UNKNOWN:
        fprintf(stderr, "%s:%zu:%zu: the enum has no member '%.*s'.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_ENUM_TYPE_UNKNOWN:
        fprintf(stderr, "%s:%zu:%zu: nothing says what enum '%.*s' is of, so it is written with "
                "its enum, as in 'Color.RED', or given to a place of an enum type.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_EXPRESSION_TOO_DEEP:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' has an expression nested deeper than %d.\n", path,
                position.line, position.column, subject_length, result.subject.bytes,
                X64_EXPRESSION_DEPTH_MAX);
        break;
    case X64_BUILD_STATUS_FIELD_BASE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: the field '%.*s' is accessed on something that is not a "
                "struct, or a pointer to one, such as a variable, a parameter, an element or a "
                "pointer.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case X64_BUILD_STATUS_FIELD_TARGET_READ_ONLY:
        fprintf(stderr, "%s:%zu:%zu: a field of '%.*s' cannot be assigned to, because it is a "
                "parameter or an iterator, unless it is a pointer.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_FOREIGN_TARGET_UNSUPPORTED:
        fprintf(stderr, "%s:%zu:%zu: the foreign procedure '%.*s' is declared in a 'foreign js' "
                "block, and the x64 target imports from 'foreign libc'.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_IDENTIFIER_NOT_VALUE:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is a type and not a value.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_INDEX_TYPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: the index at '%.*s' is not an Int.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_CAST_INVALID:
        fprintf(stderr, "%s:%zu:%zu: the value cannot be cast to '%.*s': a Byte, a Bool, an Int, a "
                "Rune or a float can be cast to Int, a Byte, an Int or a float to a float, a Byte "
                "or a Rune to Rune, and a Rune to [4]Byte.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_LOGICAL_OPERAND_TYPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: an operand of 'and', 'or' or 'not' at '%.*s' is not a "
                "Bool.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case X64_BUILD_STATUS_MAIN_INVALID:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' must be 'main :: proc() Int ->', a procedure without "
                "parameters that returns an Int, which is the exit code.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_MATCH_CASE_DUPLICATE:
        fprintf(stderr, "%s:%zu:%zu: this case matches a value that a case above it matches "
                "already, so it could never run for that value.\n", path, position.line,
                position.column);
        break;
    case X64_BUILD_STATUS_MATCH_NOT_EXHAUSTIVE:
        fprintf(stderr, "%s:%zu:%zu: the match has no case for a member of the enum, and no "
                "default case. A match that leaves members out has '#(partial)'.\n", path,
                position.line, position.column);
        break;
    case X64_BUILD_STATUS_MATCH_PARTIAL_INVALID:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' only means something for a match of an enum that "
                "has no default case, which is the only match that must have a case for every "
                "value.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case X64_BUILD_STATUS_MATCH_PATTERN_NOT_CONSTANT:
        fprintf(stderr, "%s:%zu:%zu: the pattern of the case is not known when the program is "
                "compiled. It must be a literal or a constant.\n", path, position.line,
                position.column);
        break;
    case X64_BUILD_STATUS_MATCH_PATTERN_TYPE_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: the pattern of the case is not of the type of what is "
                "matched.\n", path, position.line, position.column);
        break;
    case X64_BUILD_STATUS_MATCH_RANGE_EMPTY:
        fprintf(stderr, "%s:%zu:%zu: the range of the case has no numbers in it, so it could "
                "never match.\n", path, position.line, position.column);
        break;
    case X64_BUILD_STATUS_MATCH_SUBJECT_TYPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' cannot be matched. A match takes an Int, a Byte, a "
                "Str or an enum.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case X64_BUILD_STATUS_NIL_TYPE_UNKNOWN:
        fprintf(stderr, "%s:%zu:%zu: 'nil' does not say what '%.*s' points to, so its type is "
                "written, as in 'p : ^Int = nil'.\n", path, position.line, position.column,
                subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_OK:
    case X64_BUILD_STATUS_OUTPUT_TOO_LARGE:
        assert(false);
        break;
    case X64_BUILD_STATUS_POINTER_EXPECTED:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not a pointer, so '^' cannot follow it.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_POINTER_TARGET_TYPE_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: the value that is assigned through '%.*s' is not of the type "
                "that it points to.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case X64_BUILD_STATUS_RANGE_TYPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: a number of the range at '%.*s' is not an Int.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_RETURN_COUNT_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' returns a number of values that is not the number of "
                "its return types.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case X64_BUILD_STATUS_RESULT_COUNT_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: the number of names of '%.*s' is not the number of values "
                "that the call gives.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case X64_BUILD_STATUS_RETURN_TYPE_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: the value that '%.*s' returns is not of its return type.\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_RETURN_TYPE_MISSING:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' has no return type, so it cannot return a value.\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_STRUCT_EXPECTED:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not a struct that is defined, or the literal "
                "without a name is not where a struct is wanted.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_STRUCT_FIELD_DUPLICATE:
        fprintf(stderr, "%s:%zu:%zu: the field '%.*s' is given twice.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_STRUCT_FIELD_MISSING:
        fprintf(stderr, "%s:%zu:%zu: the literal of '%.*s' gives some fields and not all of them, "
                "and it gives either every field or none.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_STRUCT_FIELD_TYPE_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: the value of the field '%.*s' is not of its type.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_STRUCT_FIELD_UNKNOWN:
        fprintf(stderr, "%s:%zu:%zu: the struct has no field '%.*s'.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_STRUCT_RECURSIVE:
        fprintf(stderr, "%s:%zu:%zu: the struct of the field '%.*s' contains itself, which "
                "would never end, so it has a pointer to itself instead.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_TYPE_INFO_MISPLACED:
        fprintf(stderr, "%s:%zu:%zu: the information about '%.*s' is not a value, so a field of "
                "it, such as '.members' or '.name', is read right away.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_TYPE_INFO_UNSUPPORTED:
        fprintf(stderr, "%s:%zu:%zu: 'type_info_of' at '%.*s' is about an enum, and the x64 "
                "target gives no information about another type.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_TYPE_UNSUPPORTED:
        fprintf(stderr, "%s:%zu:%zu: type '%.*s' is not supported by the x64 target.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_UNION_CASE_UNKNOWN:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not a member of the enum of the field that the "
                "union is matched against.\n", path, position.line, position.column,
                subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_UNION_DISCRIMINANT_INVALID:
        fprintf(stderr, "%s:%zu:%zu: the union is matched against '%.*s', which is not a field "
                "of the struct that has an enum type and is not in a case itself.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_UNION_FIELD_KIND_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: the field '%.*s' is in another case than the member that "
                "the literal gives the field that the union is matched against.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_UNION_FIELD_UNMATCHED:
        fprintf(stderr, "%s:%zu:%zu: the field '%.*s' is in a case of a union, and is read or "
                "assigned only where an if statement shows that its object is in the case.\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
        break;
    case X64_BUILD_STATUS_UNION_KIND_NOT_CONSTANT:
        fprintf(stderr, "%s:%zu:%zu: the field '%.*s' is in a case of a union, so the literal "
                "writes out the member that it gives the field that the union is matched "
                "against.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case X64_BUILD_STATUS_UNSUPPORTED:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' has something that the x64 target does not build "
                "yet.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case X64_BUILD_STATUS_VARIABLE_TYPE_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: variable '%.*s' is given a value that is not of its type.\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
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
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not an Int or a float, or is not of the type of the "
                "other operand, so it cannot be used in arithmetic.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
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
    case WASM_BUILD_STATUS_CAST_INVALID:
        fprintf(stderr, "%s:%zu:%zu: the value cannot be cast to '%.*s': a Byte, a Bool, an Int, a "
                "Rune or a float can be cast to Int, a Byte, an Int or a float to a float, a Byte "
                "or a Rune to Rune, and a Rune to [4]Byte.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_COMPARISON_OPERAND_TYPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' cannot be compared here: both operands of a comparison "
                "must be Int, or both must be floats of one type, or both must be Bool or pointers "
                "of one type for == and not=.\n",
                path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_LOGICAL_OPERAND_TYPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not a Bool, so it cannot be used with 'and', 'or' "
                "or 'not'.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_MATCH_CASE_DUPLICATE:
        fprintf(stderr, "%s:%zu:%zu: this case matches a value that a case above it matches "
                "already, so it could never run for that value.\n", path, position.line,
                position.column);
        break;
    case WASM_BUILD_STATUS_MATCH_NOT_EXHAUSTIVE:
        fprintf(stderr, "%s:%zu:%zu: the match has no case for the member '%.*s' of the enum. "
                "Add one, a default case 'case ->', or write 'match #(partial)' to leave members "
                "out.\n", path, position.line, position.column, (int)result.detail.length,
                result.detail.bytes);
        break;
    case WASM_BUILD_STATUS_MATCH_PARTIAL_INVALID:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' only means something for a match of an enum that "
                "has no default case, which is the only match that must have a case for every "
                "value.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_MATCH_PATTERN_NOT_CONSTANT:
        fprintf(stderr, "%s:%zu:%zu: the pattern of the case is not known when the program is "
                "compiled. It must be a literal, a constant or a member of an enum.\n", path,
                position.line, position.column);
        break;
    case WASM_BUILD_STATUS_MATCH_PATTERN_TYPE_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: the pattern of the case is not of the type of what is "
                "matched, as a string is not an Int, a member is of one enum only, a Byte is "
                "from 0 to 255 and a range is only for an Int or a Byte.\n", path, position.line,
                position.column);
        break;
    case WASM_BUILD_STATUS_MATCH_RANGE_EMPTY:
        fprintf(stderr, "%s:%zu:%zu: the range of the case has no numbers in it, so it could "
                "never match.\n", path, position.line, position.column);
        break;
    case WASM_BUILD_STATUS_MATCH_SUBJECT_TYPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' cannot be matched. A match takes an Int, a Byte, a "
                "Str or an enum.\n", path, position.line, position.column, subject_length,
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
    case WASM_BUILD_STATUS_RETURN_COUNT_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' returns a number of values that is not the number of "
                "its return types.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case WASM_BUILD_STATUS_RESULT_COUNT_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: the number of names of '%.*s' is not the number of values "
                "that the call gives.\n", path, position.line, position.column, subject_length,
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
    case SEMANTIC_STATUS_GLOBAL_VALUE_NOT_CONSTANT:
        fprintf(stderr, "%s:%zu:%zu: the initial value of global variable '%.*s' must be known "
                "when the program is compiled: a constant expression, such as a number or "
                "'LIMIT * 2', or a field of the information about a type.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_CALL_TARGET_NOT_PROCEDURE:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not a procedure, so it cannot be called.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_CONSTANT_CYCLE:
        fprintf(stderr, "%s:%zu:%zu: the constant '%.*s' is defined in terms of itself.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_CONSTANT_DIVISION_BY_ZERO:
        fprintf(stderr, "%s:%zu:%zu: the constant expression at '%.*s' divides by zero.\n", path,
                position.line, position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_CONSTANT_NOT_CONSTANT:
        fprintf(stderr, "%s:%zu:%zu: the value of the constant '%.*s' must be known when the "
                "program is compiled: a number, 'true', 'false', a string, another such constant, "
                "or an operator of them.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case SEMANTIC_STATUS_CONSTANT_OPERAND_TYPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: an operator of the constant expression at '%.*s' is given "
                "operands that it does not take: arithmetic takes integers that are untyped or "
                "Int, or floats, but not both, 'and', 'or' and 'not' take truth values, a Byte is "
                "compared with a Byte or a number from 0 to 255, a Rune with a Rune, and "
                "strings are not compared.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_CONSTANT_OVERFLOW:
        fprintf(stderr, "%s:%zu:%zu: the constant expression at '%.*s' has a value that does not "
                "fit in an Int, which goes from -9223372036854775808 to 9223372036854775807, or "
                "in a float, which is finite.\n",
                path, position.line, position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_CONSTANT_TOO_DEEP:
        fprintf(stderr, "%s:%zu:%zu: the constant expression at '%.*s' has more than %d nested "
                "operators and constants.\n", path, position.line, position.column,
                subject_length, result.subject.bytes, CONSTANT_EXPRESSION_DEPTH_MAX);
        break;
    case SEMANTIC_STATUS_CONSTANT_TYPE_INVALID:
        fprintf(stderr, "%s:%zu:%zu: the constant '%.*s' has a type that a constant cannot have. "
                "A constant is an Int, a Byte, a Bool or a Str.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_CONSTANT_TYPE_MISMATCH:
        fprintf(stderr, "%s:%zu:%zu: the value of the constant '%.*s' is not of the type of the "
                "constant.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case SEMANTIC_STATUS_CONSTANT_VALUE_NOT_REPRESENTABLE:
        fprintf(stderr, "%s:%zu:%zu: the number at '%.*s' does not fit in a Byte, which is a "
                "number from 0 to 255.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case SEMANTIC_STATUS_ENUM_MEMBER_DUPLICATE:
        fprintf(stderr, "%s:%zu:%zu: the enum has the member '%.*s' twice.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_DEFER_NESTED:
        fprintf(stderr, "%s:%zu:%zu: 'defer' cannot be inside the statement of another 'defer'.\n",
                path, position.line, position.column);
        break;
    case SEMANTIC_STATUS_DEFER_STATEMENT_INVALID:
        fprintf(stderr, "%s:%zu:%zu: 'defer' takes an assignment, a call, or an if, for or scope "
                "statement, and not a variable definition, 'return', 'break' or 'continue'.\n",
                path, position.line, position.column);
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
    case SEMANTIC_STATUS_JUMP_OUT_OF_DEFER:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' cannot leave the statement of a 'defer', which runs "
                "while its block is being left.\n", path, position.line, position.column,
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
    case SEMANTIC_STATUS_MATCH_ATTRIBUTE_UNKNOWN:
        fprintf(stderr, "%s:%zu:%zu: '%.*s' is not an attribute of a match statement. The only "
                "one is 'partial'.\n", path, position.line, position.column, subject_length,
                result.subject.bytes);
        break;
    case SEMANTIC_STATUS_OK:
        assert(false);
        break;
    case SEMANTIC_STATUS_PARAMETER_NAME_DUPLICATE:
        fprintf(stderr, "%s:%zu:%zu: parameter '%.*s' is declared twice.\n", path, position.line,
                position.column, subject_length, result.subject.bytes);
        break;
    case SEMANTIC_STATUS_RETURN_IN_DEFER:
        fprintf(stderr, "%s:%zu:%zu: 'return' cannot be inside the statement of a 'defer', which "
                "runs while its block is being left.\n", path, position.line, position.column);
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

static void main_run_command_report_x64_text_error(char const *path, X64_Text_Result result) {
    assert(path != NULL);
    assert(result.status != X64_TEXT_STATUS_OK);

    // The formatter reads what `x64_build` wrote, so a file that it cannot format is a bug in the
    // compiler rather than in the program, and the message says so.
    switch (result.status) {
    case X64_TEXT_STATUS_EXECUTABLE_MALFORMED:
        fprintf(stderr, "%s: internal error: the executable is malformed at byte %zu.\n", path,
                result.error_offset);
        break;
    case X64_TEXT_STATUS_EXECUTABLE_UNSUPPORTED:
        fprintf(stderr,
                "%s: internal error: the executable cannot be formatted as assembly at byte "
                "%zu.\n",
                path, result.error_offset);
        break;
    case X64_TEXT_STATUS_OK:
        assert(false);
        break;
    case X64_TEXT_STATUS_OUTPUT_TOO_LARGE:
        fprintf(stderr, "%s: the assembly is larger than %d bytes.\n", path,
                OUTPUT_TEXT_SIZE_BYTES_MAX);
        break;
    }
}
