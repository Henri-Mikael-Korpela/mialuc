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
#include <mialu/limits.h>
#include <mialu/package.h>
#include <mialu/parser.h>
#include <mialu/process.h>
#include <mialu/report.h>
#include <mialu/semantic.h>
#include <mialu/str.h>
#include <mialu/tokenizer.h>
#include <mialu/wasm-text.h>
#include <mialu/wasm.h>
#include <mialu/x64-text.h>
#include <mialu/x64.h>

// The version of the compiler is given by the build, from the file VERSION, see CMakeLists.txt.
#ifndef MIALU_VERSION_STRING
#error "MIALU_VERSION_STRING is not defined: build with CMake, which reads it from the file VERSION."
#endif

enum {
    // Bounds the text of a string that `dump-types` prints as the value of a constant, in bytes.
    // The rest is left out, so that an editor is not given the whole of a long string for each use
    // of the constant.
    MAIN_VALUE_BYTES_MAX = 400,
    // Bounds the text of the documentation comment that `dump-types` prints for a name, in bytes. The
    // rest is left out, like that of a value.
    MAIN_DOC_BYTES_MAX = 4000,
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
    COMMAND_MODE_DUMP_OUTLINE,
    COMMAND_MODE_DUMP_STRUCT_SIZES,
    COMMAND_MODE_DUMP_TYPES,
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
 * The mode run has the arguments that are given to the program, which come after the source file:
 * `argument_count` C strings from `arguments`, which are the ones of `argv`. `reporter` is how the
 * errors of the command are reported, and goes where the command goes, so that every function that
 * finds an error can report it.
 */
typedef struct {
    Reporter reporter;
    char const *const *arguments;
    size_t argument_count;
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
    // The text of the strings that have escape sequences, which the tokens view.
    Array_Slice string_text;
    // The tokens without the comments, which is what the parser takes.
    Array_Slice code_tokens;
    Array_Slice nodes;
    Array_Slice bindings;
    Array_Slice constants;
    // The text of the strings that `++` makes, which the syntax tree views.
    Array_Slice text;
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
static char const  *main_constant_type_text                (Constant_Type type, bool is_constant);
static bool         main_definition_type_write             (Ast_Node const *nodes,
                                                            Semantic_Binding const *bindings,
                                                            Constant_Value const *constants,
                                                            size_t node_count,
                                                            Ast_Node_Index target,
                                                            size_t depth, char *text,
                                                            size_t text_size_bytes);
static bool         main_array_literal_type_write          (Ast_Node const *nodes,
                                                            Ast_Node_Index literal, char *text,
                                                            size_t text_size_bytes);
static bool         main_expression_type_write             (Ast_Node const *nodes,
                                                            Semantic_Binding const *bindings,
                                                            Constant_Value const *constants,
                                                            size_t node_count,
                                                            Ast_Node_Index expression,
                                                            size_t depth, char *text,
                                                            size_t text_size_bytes);
static Ast_Node_Index main_struct_field                (Ast_Node const *nodes,
                                                            Ast_Node_Index struct_definition,
                                                            Str name);
static void         main_struct_literal_rows_print         (Str source, Ast_Node const *nodes,
                                                            size_t node_count,
                                                            Ast_Node_Index literal);
static Ast_Node_Index main_struct_of_name              (Ast_Node const *nodes, size_t node_count,
                                                            Str name);
static bool        main_constant_value_write              (Ast_Node const *nodes,
                                                            Semantic_Binding const *bindings,
                                                            Constant_Value const *constants,
                                                            size_t node_count,
                                                            Ast_Node_Index target, char *text,
                                                            size_t text_size_bytes);
static void         main_type_row_print                    (Str source, Str name,
                                                            char const *text, char const *note,
                                                            Str value, char const *plain_value,
                                                            Str doc);
static Str          main_doc_comment_write                 (Str source, size_t offset, char *text,
                                                            size_t text_size_bytes);
static Str          main_concatenated_value                (Ast_Node const *nodes,
                                                            Constant_Value const *constants,
                                                            size_t node_count,
                                                            Ast_Node_Index target);
static void         main_iterator_note_write               (Ast_Node const *nodes,
                                                            Semantic_Binding const *bindings,
                                                            Constant_Value const *constants,
                                                            size_t node_count,
                                                            Ast_Node_Index statement, char *note,
                                                            size_t note_size_bytes);
static bool         main_constant_type_is_untyped          (Constant_Type type);
static Ast_Node_Index main_enum_of_access              (Ast_Node const *nodes,
                                                            Semantic_Binding const *bindings,
                                                            Ast_Node_Index access);
static Ast_Node_Index main_enum_of_member              (Ast_Node const *nodes, size_t node_count,
                                                            Ast_Node_Index member);
static Ast_Node_Index main_enum_of_member_name         (Ast_Node const *nodes, size_t node_count,
                                                            Str name);
static bool         main_node_type_is_arithmetic          (Ast_Node_Type node_type);
static bool         main_node_type_is_type                 (Ast_Node_Type node_type);
static bool         main_procedure_text_write              (Ast_Node const *nodes,
                                                            Ast_Node_Index procedure, char *text,
                                                            size_t text_size_bytes);
static bool         main_built_in_result_write             (Str name, size_t result_index,
                                                            char *text, size_t text_size_bytes);
static bool         main_result_type_write                 (Ast_Node const *nodes,
                                                            Semantic_Binding const *bindings,
                                                            Ast_Node_Index definition, char *text,
                                                            size_t text_size_bytes);
static bool         main_text_write                        (char *text, size_t text_size_bytes,
                                                            Str part);
static bool         main_type_text_write                   (Ast_Node const *nodes,
                                                            Ast_Node_Index type_node, char *text,
                                                            size_t text_size_bytes);
static int32_t      main_run_command_front                 (Command command,
                                                            Buffers const *buffers,
                                                            Packages *packages);
static int32_t      main_run_command                       (Command command,
                                                            Buffers const *buffers);
static int32_t      main_run_command_build                 (Command command, Buffers const *buffers,
                                                            Str source, Parse_Result parse_result,
                                                            Semantic_Result semantic_result);
static int32_t      main_run_command_build_x64             (Command command, Buffers const *buffers,
                                                            Str source, Parse_Result parse_result,
                                                            Semantic_Result semantic_result);
static void         main_run_command_print_outline         (Str source, Parse_Result parse_result,
                                                            size_t first_node);
static void         main_run_command_print_struct_sizes    (Str source, Parse_Result parse_result,
                                                            size_t first_node);
static void         main_run_command_print_tokens          (Array_Slice tokens);
static void         main_run_command_print_types           (Str source, Parse_Result parse_result,
                                                            Semantic_Result semantic_result,
                                                            size_t first_node);
static Str          main_run_command_read_file             (char const *path, Array_Slice buffer);
static bool         main_run_command_write_file            (char const *path, Array_Slice bytes);

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
    command.reporter.is_color = process_standard_error_has_color();
    if (command.mode == COMMAND_MODE_INVALID) {
        fprintf(stderr,
                "Usage: %s run <source file> [arguments of the program]\n"
                "       %s dump-ast <source file>\n"
                "       %s dump-asm --target x64-win32 <source file>\n"
                "       %s dump-outline <source file>\n"
                "       %s dump-struct-sizes <source file>\n"
                "       %s dump-types <source file>\n"
                "       %s build --target wasm -o <output file> <source file>\n"
                "       %s build --target wat -o <output file> <source file>\n"
                "       %s build --target x64-win32 [--debug] -o <output file> <source file>\n"
                "       %s --version\n",
                argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0],
                argv[0], argv[0]);
        return 2;
    }

    // Allocated once, here, because the buffers are too large for the default Windows stack.
    Buffers buffers = {
        .source = array_slice_make(malloc(SOURCE_SIZE_BYTES_MAX * sizeof(byte)),
                                   SOURCE_SIZE_BYTES_MAX),
        .tokens = array_slice_make(malloc(TOKEN_COUNT_MAX * sizeof(Token)), TOKEN_COUNT_MAX),
        // An escape sequence is never longer than the text that it stands for is written.
        .string_text = array_slice_make(malloc(SOURCE_SIZE_BYTES_MAX * sizeof(char)),
                                        SOURCE_SIZE_BYTES_MAX),
        .code_tokens = array_slice_make(malloc(TOKEN_COUNT_MAX * sizeof(Token)), TOKEN_COUNT_MAX),
        // A node needs at least one token, so as many nodes as tokens suffice, except that `++`
        // makes two, which a source that is made of little else can run out of, and the parser then
        // reports the limit.
        .nodes = array_slice_make(malloc(TOKEN_COUNT_MAX * sizeof(Ast_Node)), TOKEN_COUNT_MAX),
        // The analysis has one binding for each node.
        .bindings = array_slice_make(malloc(TOKEN_COUNT_MAX * sizeof(Semantic_Binding)),
                                     TOKEN_COUNT_MAX),
        // And one value that is known when the program is compiled for each node.
        .constants = array_slice_make(malloc(TOKEN_COUNT_MAX * sizeof(Constant_Value)),
                                      TOKEN_COUNT_MAX),
        .text = array_slice_make(malloc(CONSTANT_TEXT_SIZE_BYTES_MAX * sizeof(char)),
                                 CONSTANT_TEXT_SIZE_BYTES_MAX),
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
        buffers.string_text.elements == NULL || buffers.code_tokens.elements == NULL || buffers.nodes.elements == NULL ||
        buffers.bindings.elements == NULL || buffers.constants.elements == NULL ||
        buffers.text.elements == NULL ||
        buffers.output.elements == NULL || buffers.output_text.elements == NULL ||
        buffers.labels.elements == NULL ||
        buffers.layouts.elements == NULL || buffers.symbols.elements == NULL ||
        buffers.debug_rows.elements == NULL ||
        buffers.source_path.elements == NULL) {
        report_general(&command.reporter, ERROR_CODE_OUT_OF_MEMORY, NULL, 0);
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
    free(buffers.text.elements);
    free(buffers.constants.elements);
    free(buffers.bindings.elements);
    free(buffers.nodes.elements);
    free(buffers.code_tokens.elements);
    free(buffers.string_text.elements);
    free(buffers.tokens.elements);
    free(buffers.source.elements);
    return exit_code;
}

/**
 * Understands the command line:
 *
 *     mialuc run <source file> [arguments of the program]
 *     mialuc dump-ast <source file>
 *     mialuc dump-asm --target x64-win32 <source file>
 *     mialuc dump-outline <source file>
 *     mialuc dump-struct-sizes <source file>
 *     mialuc dump-types <source file>
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
        .arguments = NULL,
        .argument_count = 0,
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
        if (args.length < 3) {
            return invalid;
        }
        // Only a program for the processor can be run, and it is not run in WebAssembly. Windows
        // on x64 is the only machine that a program can run on for now. What comes after the source
        // file is for the program, as if it had been called with them after its name.
        return (Command){
            .arguments = argv + 3,
            .argument_count = args.length - 3,
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

    if (strcmp(argv[1], "dump-outline") == 0) {
        if (args.length != 3) {
            return invalid;
        }
        return (Command){
            .source_path = argv[2],
            .output_path = NULL,
            .mode = COMMAND_MODE_DUMP_OUTLINE,
            .target = COMMAND_TARGET_NONE,
            .is_debug = false,
        };
    }

    if (strcmp(argv[1], "dump-types") == 0) {
        if (args.length != 3) {
            return invalid;
        }
        return (Command){
            .source_path = argv[2],
            .output_path = NULL,
            .mode = COMMAND_MODE_DUMP_TYPES,
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
// The files that the program imports, while a command is run, so that an error about one of them
// is told with its path and its lines. It is NULL when no command is.
static Packages const *main_packages = NULL;

static int32_t main_run_command(Command command, Buffers const *buffers) {
    assert(buffers != NULL);
    assert(command.source_path != NULL);
    assert(command.mode != COMMAND_MODE_INVALID);

    // The directory of the standard library of this project is where the build was made from, which
    // is what a build from the sources uses, and a release has the directory `lib` next to the
    // compiler instead, see `packages_make`.
    Packages packages = packages_make(MIALU_STD_DIRECTORY);
    main_packages = &packages;
    command.reporter.packages = &packages;
    int32_t exit_code = main_run_command_front(command, buffers, &packages);
    main_packages = NULL;
    packages_free(&packages);
    return exit_code;
}

/**
 * Runs `command`, with the files that it imports in `packages`, which the caller frees.
 */
static int32_t main_run_command_front(Command command, Buffers const *buffers,
                                      Packages *packages) {

    char const *path = command.source_path;
    Str source = main_run_command_read_file(path, buffers->source);

    // An unreadable source is `bytes == NULL`. An empty file gives a non-NULL `Str` of length
    // zero.
    if (source.bytes == NULL) {
        report_general(&command.reporter, ERROR_CODE_SOURCE_UNREADABLE, path,
                       SOURCE_SIZE_BYTES_MAX);
        return 1;
    }

    Tokenize_Result result = tokenize(source, buffers->tokens, buffers->string_text);
    if (result.status != TOKENIZE_STATUS_OK) {
        report_tokenize_error(&command.reporter, path, source, result);
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

    // The files that the program imports come first, in the same nodes, and the program after them.
    Package_Result imported = packages_load(packages, &code_tokens, buffers->nodes,
                                            SOURCE_SIZE_BYTES_MAX, TOKEN_COUNT_MAX);
    if (imported.status != PACKAGE_STATUS_OK) {
        report_package_error(&command.reporter, path, source, imported);
        return 1;
    }
    Parse_Result parse_result = parse_at(code_tokens, buffers->nodes, packages->imported_end);
    if (parse_result.status != PARSE_STATUS_OK) {
        report_parse_error(&command.reporter, path, source, code_tokens, parse_result);
        return 1;
    }

    // The tree is printed before it is analyzed, so that a program with a bad name can be looked
    // at. It is the one of the program: the imported files are put together after.
    if (is_inspection) {
        printf("\n");
        ast_print(parse_result.nodes, parse_result.root);
    }
    packages_merge(packages, parse_result.nodes.elements, parse_result.root);

    Semantic_Result semantic_result = semantic_analyze(parse_result.nodes, parse_result.root,
                                                       packages->imported_end, buffers->bindings,
                                                       buffers->constants, buffers->text);
    if (semantic_result.status != SEMANTIC_STATUS_OK) {
        report_semantic_error(&command.reporter, path, source, semantic_result);
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
    case COMMAND_MODE_DUMP_OUTLINE:
        main_run_command_print_outline(source, parse_result, packages->imported_end);
        return 0;
    case COMMAND_MODE_DUMP_STRUCT_SIZES:
        main_run_command_print_struct_sizes(source, parse_result, packages->imported_end);
        return 0;
    case COMMAND_MODE_DUMP_TYPES:
        main_run_command_print_types(source, parse_result, semantic_result,
                                     packages->imported_end);
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
 * Prints a line of `dump-outline`: the depth, the kind, and the line and column of the name, both
 * counted from 1 with columns that count bytes, the length of the name in bytes and the name,
 * separated by tabs. Nothing is printed for a name that is not in the source.
 */
static void main_outline_row_print(Str source, int depth, char const *kind, Str name) {
    if (name.bytes < source.bytes || name.bytes >= source.bytes + source.length) {
        return;
    }
    Str_Position position = str_position_at(source, (size_t)(name.bytes - source.bytes));
    printf("%d\t%s\t%zu\t%zu\t%zu\t%.*s\n", depth, kind, position.line, position.column,
           name.length, (int)name.length, (char const *)name.bytes);
}

/**
 * Prints a line for each definition of the module in the order of the source, and below a struct
 * a line for each of its fields and below an enum a line for each of its members, which is what an
 * editor shows as the outline of a file. The kind is `procedure`, `struct`, `enum`, `constant` or
 * `variable` for a definition, and `field` or `member` for what is in a struct or an enum.
 */
static void main_run_command_print_outline(Str source, Parse_Result parse_result,
                                          size_t first_node) {
    assert(parse_result.status == PARSE_STATUS_OK);

    Ast_Node const *nodes = parse_result.nodes.elements;
    Ast_Node_Index definition = nodes[parse_result.root].first_child;
    for (size_t i = 0; i < parse_result.nodes.length && definition != AST_NODE_INDEX_NONE; i += 1) {
        Ast_Node const *node = &nodes[definition];
        Ast_Node_Index current = definition;
        definition = node->next_sibling;
        // The definitions of the imported files are not in this source.
        if (current < first_node) {
            continue;
        }

        if (node->node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION) {
            main_outline_row_print(source, 0, "variable", ast_definition_name(node));
            continue;
        }
        if (node->node_type != AST_NODE_TYPE_CONSTANT_DEFINITION) {
            continue;
        }

        Ast_Node_Type value_type = nodes[node->first_child].node_type;
        char const *kind = "constant";
        char const *child_kind = NULL;
        if (value_type == AST_NODE_TYPE_PROCEDURE) {
            kind = "procedure";
        } else if (value_type == AST_NODE_TYPE_STRUCT_TYPE) {
            kind = "struct";
            child_kind = "field";
        } else if (value_type == AST_NODE_TYPE_ENUM_TYPE) {
            kind = "enum";
            child_kind = "member";
        }
        main_outline_row_print(source, 0, kind, ast_definition_name(node));

        if (child_kind == NULL) {
            continue;
        }
        for (Ast_Node_Index child = nodes[node->first_child].first_child;
             child != AST_NODE_INDEX_NONE; child = nodes[child].next_sibling) {
            if (nodes[child].node_type == AST_NODE_TYPE_FIELD) {
                main_outline_row_print(source, 1, child_kind, nodes[child].payload.field.name);
            } else if (nodes[child].node_type == AST_NODE_TYPE_ENUM_MEMBER) {
                main_outline_row_print(source, 1, child_kind,
                                       nodes[child].payload.enum_member.name);
            }
        }
    }
}

/**
 * Prints a line for each struct that is defined as a constant of the module, in the order of the
 * source: the line of its definition, counted from 1, its name, its size in bytes and its
 * alignment in bytes, separated by tabs. The sizes are those of the program for the processor, see
 * `x64_struct_size_bytes`. This is what an editor shows above the definition of a struct. A struct
 * that has no size, because it contains itself, is left out.
 */
static void main_run_command_print_struct_sizes(Str source, Parse_Result parse_result,
                                               size_t first_node) {
    assert(parse_result.status == PARSE_STATUS_OK);

    Ast_Node const *nodes = parse_result.nodes.elements;
    Ast_Node_Index definition = nodes[parse_result.root].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < parse_result.nodes.length && definition != AST_NODE_INDEX_NONE; i += 1) {
        Ast_Node const *node = &nodes[definition];
        Ast_Node_Index current = definition;
        definition = node->next_sibling;
        if (current < first_node || node->node_type != AST_NODE_TYPE_CONSTANT_DEFINITION ||
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
 * Writes `part`, a view of the source, to `text` as a zero-terminated string. Returns false when
 * they do not fit in `text_size_bytes`.
 */
static bool main_text_write(char *text, size_t text_size_bytes, Str part) {
    int written = snprintf(text, text_size_bytes, "%.*s", (int)part.length,
                           (char const *)part.bytes);
    return written >= 0 && (size_t)written < text_size_bytes;
}

/**
 * Returns whether `node_type` is a type that `main_type_text_write` writes.
 */
static bool main_node_type_is_type(Ast_Node_Type node_type) {
    return node_type == AST_NODE_TYPE_TYPE_NAME || node_type == AST_NODE_TYPE_POINTER_TYPE ||
           node_type == AST_NODE_TYPE_ARRAY_TYPE;
}

/**
 * Returns the constant definition of the enum that has the member `member`, an
 * `AST_NODE_TYPE_ENUM_MEMBER`, or `AST_NODE_INDEX_NONE`. An enum is always the value of a constant
 * of the module, so the search goes over the definitions.
 */
static Ast_Node_Index main_enum_of_member(Ast_Node const *nodes, size_t node_count,
                                          Ast_Node_Index member) {
    assert(member < node_count);

    for (Ast_Node_Index index = 0; index < node_count; index += 1) {
        if (nodes[index].node_type != AST_NODE_TYPE_CONSTANT_DEFINITION ||
            nodes[nodes[index].first_child].node_type != AST_NODE_TYPE_ENUM_TYPE) {
            continue;
        }
        for (Ast_Node_Index child = nodes[nodes[index].first_child].first_child;
             child != AST_NODE_INDEX_NONE; child = nodes[child].next_sibling) {
            if (child == member) {
                return index;
            }
        }
    }
    return AST_NODE_INDEX_NONE;
}

/**
 * Returns the constant definition of the only enum that has a member called `name`, for the
 * literal `.RED` whose enum is not written and depends on the place it is given to, which this
 * does not know. Returns `AST_NODE_INDEX_NONE` when no enum has such a member or more than one has,
 * since the literal could be any of them.
 */
static Ast_Node_Index main_enum_of_member_name(Ast_Node const *nodes, size_t node_count,
                                               Str name) {
    Ast_Node_Index found = AST_NODE_INDEX_NONE;
    for (Ast_Node_Index index = 0; index < node_count; index += 1) {
        if (nodes[index].node_type != AST_NODE_TYPE_CONSTANT_DEFINITION ||
            nodes[nodes[index].first_child].node_type != AST_NODE_TYPE_ENUM_TYPE) {
            continue;
        }
        for (Ast_Node_Index child = nodes[nodes[index].first_child].first_child;
             child != AST_NODE_INDEX_NONE; child = nodes[child].next_sibling) {
            if (str_equals(nodes[child].payload.enum_member.name, name)) {
                if (found != AST_NODE_INDEX_NONE) {
                    return AST_NODE_INDEX_NONE;
                }
                found = index;
            }
        }
    }
    return found;
}

/**
 * Returns the constant definition of the enum that `access`, an `AST_NODE_TYPE_FIELD_ACCESS`, is a
 * member of, as in `Color.RED`, or `AST_NODE_INDEX_NONE` when it is not the member of an enum.
 */
static Ast_Node_Index main_enum_of_access(Ast_Node const *nodes, Semantic_Binding const *bindings,
                                          Ast_Node_Index access) {
    assert(nodes[access].node_type == AST_NODE_TYPE_FIELD_ACCESS);

    Ast_Node_Index base = nodes[access].first_child;
    if (nodes[base].node_type != AST_NODE_TYPE_IDENTIFIER ||
        bindings[base].kind != SEMANTIC_BINDING_KIND_CONSTANT) {
        return AST_NODE_INDEX_NONE;
    }
    Ast_Node_Index definition = bindings[base].target;
    if (nodes[nodes[definition].first_child].node_type != AST_NODE_TYPE_ENUM_TYPE) {
        return AST_NODE_INDEX_NONE;
    }
    return definition;
}

/**
 * Returns whether `node_type` is an operator of arithmetic, whose type is that of its operands.
 */
static bool main_node_type_is_arithmetic(Ast_Node_Type node_type) {
    return node_type == AST_NODE_TYPE_ADDITION || node_type == AST_NODE_TYPE_SUBTRACTION ||
           node_type == AST_NODE_TYPE_MULTIPLICATION || node_type == AST_NODE_TYPE_DIVISION;
}

/**
 * Returns whether `type` is one of a literal, which becomes the type of the place that uses it.
 */
static bool main_constant_type_is_untyped(Constant_Type type) {
    return type == CONSTANT_TYPE_UNTYPED_BOOL || type == CONSTANT_TYPE_UNTYPED_FLOAT ||
           type == CONSTANT_TYPE_UNTYPED_INTEGER || type == CONSTANT_TYPE_UNTYPED_STRING;
}

/**
 * Writes the type of the value at `result_index` that the built-in procedure `name` gives, to
 * `text`: `Int` for the first value of `try_str_to_int` and `Bool` for the second, `Str` and `Bool`
 * for `utf8_decode`, and the one type of the others, which have one value. Returns false when
 * `name` is not a built-in procedure, or it has no value at `result_index`, or the text does not
 * fit.
 */
static bool main_built_in_result_write(Str name, size_t result_index, char *text,
                                       size_t text_size_bytes) {
    char const *type = NULL;
    if (str_equals(name, STR("try_str_to_int")) && result_index < 2) {
        type = result_index == 0 ? "Int" : "Bool";
    } else if (str_equals(name, STR("utf8_decode")) && result_index < 2) {
        type = result_index == 0 ? "Str" : "Bool";
    } else if (result_index == 0) {
        if (str_equals(name, STR("length_of"))) {
            type = "Int";
        } else if (str_equals(name, STR("int_to_byte"))) {
            type = "Byte";
        } else if (str_equals(name, STR("int_to_i32"))) {
            type = "I32";
        } else if (str_equals(name, STR("int_to_str"))) {
            type = "Str";
        } else if (str_equals(name, STR("str_to_c_str"))) {
            type = "C_Str";
        }
    }
    if (type == NULL) {
        return false;
    }
    return main_text_write(text, text_size_bytes, str_make(type, strlen(type)));
}

/**
 * Writes the type of the variable `definition`, a name of the values of a call that is not the
 * first, to `text`: the one that the procedure returns at its place. Such a definition has no
 * children, and its leader has the call. Returns false when the call is not one of a procedure of
 * the program, or does not fit.
 */
static bool main_result_type_write(Ast_Node const *nodes, Semantic_Binding const *bindings,
                                   Ast_Node_Index definition, char *text, size_t text_size_bytes) {
    Ast_Node const *node = &nodes[definition];
    if (node->node_type != AST_NODE_TYPE_VARIABLE_DEFINITION || node->result_index == 0) {
        return false;
    }
    Ast_Node_Index call = nodes[ast_variable_leader(nodes, definition)].first_child;
    Semantic_Binding binding = bindings[call];
    if (nodes[call].node_type == AST_NODE_TYPE_CALL && binding.kind == SEMANTIC_BINDING_KIND_NONE) {
        // A built-in procedure has no definition, so the types of its values are known by name.
        return main_built_in_result_write(nodes[call].payload.call.name, node->result_index, text,
                                          text_size_bytes);
    }
    if (nodes[call].node_type != AST_NODE_TYPE_CALL ||
        binding.kind != SEMANTIC_BINDING_KIND_CONSTANT) {
        return false;
    }
    Ast_Node const *procedure = &nodes[nodes[binding.target].first_child];
    if (procedure->node_type != AST_NODE_TYPE_PROCEDURE ||
        ast_procedure_return_count(procedure) <= node->result_index) {
        return false;
    }
    return main_text_write(text, text_size_bytes,
                           ast_procedure_return_type_at(procedure, node->result_index));
}

/**
 * Writes the text of the type `type_node`, an `AST_NODE_TYPE_TYPE_NAME`, a pointer type or an array
 * type, to `text` as it is in the source, such as `^Int` or `[3]Int`. Returns false when it is
 * another node, or the text does not fit in `text_size_bytes`, which includes the terminating zero.
 */
static bool main_type_text_write(Ast_Node const *nodes, Ast_Node_Index type_node, char *text,
                                 size_t text_size_bytes) {
    assert(nodes != NULL);
    assert(text != NULL);
    assert(text_size_bytes > 0);

    size_t length = 0;
    // A pointer type and an array type have the type that they are made of as their only child, so
    // the prefixes are written one after the other down to the name, without calling itself. A
    // type that is deeper than the blocks can be inside each other is not worth showing.
    for (size_t depth = 0; depth < AST_BLOCK_DEPTH_MAX; depth += 1) {
        Ast_Node const *node = &nodes[type_node];
        int written = 0;
        if (node->node_type == AST_NODE_TYPE_POINTER_TYPE) {
            written = snprintf(text + length, text_size_bytes - length, "^");
        } else if (node->node_type == AST_NODE_TYPE_ARRAY_TYPE) {
            if (node->payload.array_type.length_kind == AST_ARRAY_LENGTH_FIXED) {
                written = snprintf(text + length, text_size_bytes - length, "[%lld]",
                                   (long long)node->payload.array_type.length);
            } else if (node->payload.array_type.length_kind == AST_ARRAY_LENGTH_INFERRED) {
                written = snprintf(text + length, text_size_bytes - length, "[const]");
            } else if (node->payload.array_type.length_kind == AST_ARRAY_LENGTH_DYNAMIC) {
                written = snprintf(text + length, text_size_bytes - length, "[dynamic]");
            } else {
                written = snprintf(text + length, text_size_bytes - length, "[]");
            }
        } else if (node->node_type == AST_NODE_TYPE_TYPE_NAME) {
            return main_text_write(text + length, text_size_bytes - length,
                                   node->payload.type_name.name);
        } else {
            return false;
        }
        if (written < 0 || length + (size_t)written >= text_size_bytes ||
            node->first_child == AST_NODE_INDEX_NONE) {
            return false;
        }
        length += (size_t)written;
        type_node = node->first_child;
    }
    return false;
}

/**
 * Returns the name of the type of a value that is known when the program is compiled. A literal
 * has no type of its own, so a variable that is given one deduces the usual type, and a constant
 * says that it is untyped, because the place that uses it can want another size.
 */
static char const *main_constant_type_text(Constant_Type type, bool is_constant) {
    switch (type) {
    case CONSTANT_TYPE_BOOL:
        return "Bool";
    case CONSTANT_TYPE_BYTE:
        return "Byte";
    case CONSTANT_TYPE_F32:
        return "F32";
    case CONSTANT_TYPE_F64:
        return "F64";
    case CONSTANT_TYPE_I32:
        return "I32";
    case CONSTANT_TYPE_I64:
        return "I64";
    case CONSTANT_TYPE_INT:
        return "Int";
    case CONSTANT_TYPE_RUNE:
        return "Rune";
    case CONSTANT_TYPE_STR:
        return "Str";
    case CONSTANT_TYPE_U32:
        return "U32";
    case CONSTANT_TYPE_U64:
        return "U64";
    case CONSTANT_TYPE_UNTYPED_BOOL:
        return is_constant ? "untyped Bool" : "Bool";
    case CONSTANT_TYPE_UNTYPED_FLOAT:
        return is_constant ? "untyped float" : "F64";
    case CONSTANT_TYPE_UNTYPED_INTEGER:
        return is_constant ? "untyped Int" : "Int";
    case CONSTANT_TYPE_UNTYPED_STRING:
        return is_constant ? "untyped Str" : "Str";
    }
    assert(false);
    return "";
}

/**
 * Writes the signature of `procedure`, an `AST_NODE_TYPE_PROCEDURE`, to `text`, such as
 * `proc(a: Int, b: ^Int) Int`. Returns false when it does not fit.
 */
static bool main_procedure_text_write(Ast_Node const *nodes, Ast_Node_Index procedure, char *text,
                                      size_t text_size_bytes) {
    assert(nodes != NULL);
    assert(nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);

    int written = snprintf(text, text_size_bytes, "proc(");
    if (written < 0 || (size_t)written >= text_size_bytes) {
        return false;
    }
    size_t length = (size_t)written;

    bool is_first = true;
    for (Ast_Node_Index child = nodes[procedure].first_child; child != AST_NODE_INDEX_NONE;
         child = nodes[child].next_sibling) {
        // The parameters are the first children, and the statements of the body follow them.
        if (nodes[child].node_type != AST_NODE_TYPE_PARAMETER) {
            break;
        }
        Str name = nodes[child].payload.parameter.name;
        written = snprintf(text + length, text_size_bytes - length, "%s%.*s: ",
                           is_first ? "" : ", ", (int)name.length, (char const *)name.bytes);
        if (written < 0 || length + (size_t)written >= text_size_bytes) {
            return false;
        }
        length += (size_t)written;
        if (not main_type_text_write(nodes, nodes[child].first_child, text + length,
                                     text_size_bytes - length)) {
            return false;
        }
        length += strlen(text + length);
        is_first = false;
    }

    Str return_type = nodes[procedure].payload.procedure.return_type;
    if (return_type.bytes == NULL) {
        written = snprintf(text + length, text_size_bytes - length, ")");
    } else {
        written = snprintf(text + length, text_size_bytes - length, ") %.*s",
                           (int)return_type.length, (char const *)return_type.bytes);
    }
    return written >= 0 && length + (size_t)written < text_size_bytes;
}

/**
 * Writes the type of what the definition `target` declares to `text`: the type that the
 * definition gives, otherwise the type that its value has. For a value, the names that it is made
 * of are followed, and so are the operands of arithmetic and a call of a procedure that returns one
 * value, and a value of any other shape has no type here. Returns false when the type is not known
 * or does not fit. A struct and an enum are types and not values, so they have none. `depth` is
 * how many definitions and expressions the type has been followed through, see
 * `main_expression_type_write`, which this one and it call each other for.
 */
static bool main_definition_type_write(Ast_Node const *nodes, Semantic_Binding const *bindings,
                                       Constant_Value const *constants, size_t node_count,
                                       Ast_Node_Index target, size_t depth, char *text,
                                       size_t text_size_bytes) {
    assert(nodes != NULL);
    assert(bindings != NULL);
    assert(constants != NULL);
    assert(target < node_count);

    // A variable that is given another name is followed to its definition. That definition is
    // always above, since a variable cannot be used before it is defined, so the chain is shorter
    // than the number of nodes.
    for (size_t step = 0; step < node_count; step += 1) {
        Ast_Node const *node = &nodes[target];
        // The expression that gives the value, and whether the name that is defined is a constant.
        Ast_Node_Index value = AST_NODE_INDEX_NONE;
        bool is_constant = false;

        if (node->node_type == AST_NODE_TYPE_PARAMETER) {
            return main_type_text_write(nodes, node->first_child, text, text_size_bytes);
        } else if (node->node_type == AST_NODE_TYPE_FOR_STATEMENT) {
            // The iterator of a range is an `Int`, and the one of an array or a slice is one of
            // its elements, so it has the type that the brackets of the array leave.
            if (nodes[node->first_child].node_type == AST_NODE_TYPE_RANGE) {
                return main_text_write(text, text_size_bytes, STR("Int"));
            }
            char sequence[256] = "";
            if (not main_expression_type_write(nodes, bindings, constants, node_count,
                                               node->first_child, depth + 1, sequence,
                                               sizeof(sequence))) {
                return false;
            }
            char const *close = sequence[0] == '[' ? strchr(sequence, ']') : NULL;
            return close != NULL &&
                   main_text_write(text, text_size_bytes, str_make(close + 1, strlen(close + 1)));
        } else if (node->node_type == AST_NODE_TYPE_CONSTANT_DEFINITION) {
            value = node->first_child;
            if (nodes[value].node_type == AST_NODE_TYPE_PROCEDURE) {
                return main_procedure_text_write(nodes, value, text, text_size_bytes);
            }
            if (nodes[value].node_type == AST_NODE_TYPE_STRUCT_TYPE ||
                nodes[value].node_type == AST_NODE_TYPE_ENUM_TYPE) {
                return false;
            }
            // The type of a typed constant comes after the value.
            if (nodes[value].next_sibling != AST_NODE_INDEX_NONE) {
                return main_type_text_write(nodes, nodes[value].next_sibling, text,
                                            text_size_bytes);
            }
            is_constant = true;
        } else if (node->node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION ||
                   node->node_type == AST_NODE_TYPE_VARIABLE_DEFINITION) {
            value = node->first_child;
            // The attributes of a global variable come before its type.
            while (value != AST_NODE_INDEX_NONE &&
                   nodes[value].node_type == AST_NODE_TYPE_ATTRIBUTE) {
                value = nodes[value].next_sibling;
            }
            if (value == AST_NODE_INDEX_NONE) {
                return main_result_type_write(nodes, bindings, target, text, text_size_bytes);
            }
            if (main_node_type_is_type(nodes[value].node_type)) {
                // A type that leaves the length to the value, `[const]Int`, gets the length of
                // the array literal that gives the value.
                Ast_Node_Index literal = nodes[value].next_sibling;
                if (nodes[value].node_type == AST_NODE_TYPE_ARRAY_TYPE &&
                    nodes[value].payload.array_type.length_kind == AST_ARRAY_LENGTH_INFERRED &&
                    literal != AST_NODE_INDEX_NONE &&
                    nodes[literal].node_type == AST_NODE_TYPE_ARRAY_LITERAL) {
                    return main_array_literal_type_write(nodes, literal, text, text_size_bytes);
                }
                return main_type_text_write(nodes, value, text, text_size_bytes);
            }
        } else {
            return false;
        }

        // Arithmetic has the type of its operands, which agree, so the left one tells it, unless
        // that is a literal, which takes the type of the other. The depth is bounded as the
        // evaluation of constants is, since the expression is nested that deep at the most.
        for (size_t depth = 0; depth < CONSTANT_EXPRESSION_DEPTH_MAX &&
                               constants[value].status != CONSTANT_STATUS_OK &&
                               main_node_type_is_arithmetic(nodes[value].node_type);
             depth += 1) {
            Ast_Node_Index left = nodes[value].first_child;
            Ast_Node_Index right = nodes[left].next_sibling;
            bool is_left_literal = constants[left].status == CONSTANT_STATUS_OK &&
                                   main_constant_type_is_untyped(constants[left].type);
            value = is_left_literal ? right : left;
        }
        if (nodes[value].node_type == AST_NODE_TYPE_COMPARISON ||
            nodes[value].node_type == AST_NODE_TYPE_AND ||
            nodes[value].node_type == AST_NODE_TYPE_OR ||
            nodes[value].node_type == AST_NODE_TYPE_NOT) {
            return main_text_write(text, text_size_bytes, STR("Bool"));
        }

        if (nodes[value].node_type == AST_NODE_TYPE_FIELD_ACCESS) {
            // A member of an enum, such as `Color.RED`, has the enum as its type.
            Ast_Node_Index enum_definition = main_enum_of_access(nodes, bindings, value);
            if (enum_definition != AST_NODE_INDEX_NONE) {
                return main_text_write(text, text_size_bytes,
                                       nodes[enum_definition].payload.constant_definition.name);
            }
        }

        if (nodes[value].node_type == AST_NODE_TYPE_CAST) {
            // A cast has the type that it names, also when its operand is a constant.
            return main_text_write(text, text_size_bytes, nodes[value].payload.cast.type_name);
        }

        if (nodes[value].node_type == AST_NODE_TYPE_ARRAY_LITERAL ||
            nodes[value].node_type == AST_NODE_TYPE_FIELD_ACCESS ||
            nodes[value].node_type == AST_NODE_TYPE_DEREFERENCE ||
            nodes[value].node_type == AST_NODE_TYPE_SLICE ||
            nodes[value].node_type == AST_NODE_TYPE_SUBSCRIPT) {
            return main_expression_type_write(nodes, bindings, constants, node_count, value,
                                              depth + 1, text, text_size_bytes);
        }

        Constant_Value known = constants[value];
        if (known.status == CONSTANT_STATUS_OK) {
            int written = snprintf(text, text_size_bytes, "%s",
                                   main_constant_type_text(known.type, is_constant));
            return written >= 0 && (size_t)written < text_size_bytes;
        }

        Ast_Node const *value_node = &nodes[value];
        Semantic_Binding binding = bindings[value];
        if (value_node->node_type == AST_NODE_TYPE_STRUCT_LITERAL &&
            value_node->payload.struct_literal.name.bytes != NULL) {
            return main_text_write(text, text_size_bytes, value_node->payload.struct_literal.name);
        }
        if (value_node->node_type == AST_NODE_TYPE_IDENTIFIER &&
            binding.kind != SEMANTIC_BINDING_KIND_NONE &&
            binding.kind != SEMANTIC_BINDING_KIND_JUMP_TARGET && binding.target != target) {
            target = binding.target;
            continue;
        }
        if (value_node->node_type == AST_NODE_TYPE_CALL &&
            binding.kind == SEMANTIC_BINDING_KIND_NONE) {
            // `make` gives a value of the type that it is given as its first argument.
            if (str_equals(value_node->payload.call.name, STR("make")) &&
                value_node->first_child != AST_NODE_INDEX_NONE) {
                return main_type_text_write(nodes, value_node->first_child, text,
                                            text_size_bytes);
            }
            // The first value of a built-in procedure.
            return main_built_in_result_write(value_node->payload.call.name, 0, text,
                                              text_size_bytes);
        }
        if (value_node->node_type == AST_NODE_TYPE_CALL &&
            binding.kind == SEMANTIC_BINDING_KIND_CONSTANT) {
            Ast_Node const *procedure = &nodes[nodes[binding.target].first_child];
            // The first of several names of the values of a call has the first type that the
            // procedure returns, as the others have theirs, see `main_result_type_write`. A call
            // that gives several values and is the value of one name has no type of one value.
            bool is_first_of_several = nodes[target].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION &&
                                       nodes[target].result_count > 1 &&
                                       nodes[target].result_index == 0;
            if (procedure->node_type != AST_NODE_TYPE_PROCEDURE ||
                (ast_procedure_return_count(procedure) != 1 &&
                 not (is_first_of_several && ast_procedure_return_count(procedure) > 1))) {
                return false;
            }
            return main_text_write(text, text_size_bytes,
                                   ast_procedure_return_type_at(procedure, 0));
        }
        return false;
    }
    return false;
}

/**
 * Writes the type of the array literal `literal` to `text`, such as `[5]Int`. A literal that starts
 * with `[const]Int`, which leaves the length to the elements, has the number of its elements as the
 * length, which is known when the program is compiled, so it is written as the length. Returns
 * false when the type of the elements cannot be written or it does not fit.
 */
static bool main_array_literal_type_write(Ast_Node const *nodes, Ast_Node_Index literal,
                                          char *text, size_t text_size_bytes) {
    assert(nodes[literal].node_type == AST_NODE_TYPE_ARRAY_LITERAL);

    // The first child is the type that starts the literal, and the elements follow it.
    Ast_Node_Index type_node = nodes[literal].first_child;
    if (nodes[type_node].node_type != AST_NODE_TYPE_ARRAY_TYPE) {
        return false;
    }
    if (nodes[type_node].payload.array_type.length_kind != AST_ARRAY_LENGTH_INFERRED) {
        return main_type_text_write(nodes, type_node, text, text_size_bytes);
    }

    size_t element_count = 0;
    for (Ast_Node_Index element = nodes[type_node].next_sibling; element != AST_NODE_INDEX_NONE;
         element = nodes[element].next_sibling) {
        element_count += 1;
    }
    int written = snprintf(text, text_size_bytes, "[%zu]", element_count);
    if (written < 0 || (size_t)written >= text_size_bytes) {
        return false;
    }
    // The type of the elements is the child of the array type, written the way any type is.
    return main_type_text_write(nodes, nodes[type_node].first_child, text + written,
                                text_size_bytes - (size_t)written);
}

/**
 * Returns the constant definition of the struct called `name`, or `AST_NODE_INDEX_NONE`. A struct
 * is always the value of a constant of the module, so the search goes over the definitions.
 */
static Ast_Node_Index main_struct_of_name(Ast_Node const *nodes, size_t node_count, Str name) {
    for (Ast_Node_Index index = 0; index < node_count; index += 1) {
        if (nodes[index].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION &&
            nodes[nodes[index].first_child].node_type == AST_NODE_TYPE_STRUCT_TYPE &&
            str_equals(nodes[index].payload.constant_definition.name, name)) {
            return index;
        }
    }
    return AST_NODE_INDEX_NONE;
}

/**
 * Returns the `AST_NODE_TYPE_FIELD` called `name` of the struct that the constant definition
 * `struct_definition` defines, or `AST_NODE_INDEX_NONE`.
 */
static Ast_Node_Index main_struct_field(Ast_Node const *nodes, Ast_Node_Index struct_definition,
                                        Str name) {
    Ast_Node_Index struct_type = nodes[struct_definition].first_child;
    for (Ast_Node_Index field = nodes[struct_type].first_child; field != AST_NODE_INDEX_NONE;
         field = nodes[field].next_sibling) {
        if (str_equals(nodes[field].payload.field.name, name)) {
            return field;
        }
    }
    return AST_NODE_INDEX_NONE;
}

/**
 * Writes the type of the value that `expression` has to `text`, for the shapes whose type follows
 * from the types of the names in it: a name, a field of a struct, a pointer that is dereferenced,
 * an element of an array and a struct literal. A field is looked up in the struct that its base
 * has, which may be behind a pointer, since a field is read through one as well. Returns false for
 * any other shape, or when a type is not known.
 *
 * It and `main_definition_type_write` call each other, because the type of a name is the type of
 * its value. A definition is above its uses, so that ends, but `depth` bounds it anyway, at the
 * depth that a constant expression is allowed to have.
 */
static bool main_expression_type_write(Ast_Node const *nodes, Semantic_Binding const *bindings,
                                       Constant_Value const *constants, size_t node_count,
                                       Ast_Node_Index expression, size_t depth, char *text,
                                       size_t text_size_bytes) {
    assert(expression < node_count);

    if (depth >= CONSTANT_EXPRESSION_DEPTH_MAX) {
        return false;
    }
    Ast_Node const *node = &nodes[expression];
    Semantic_Binding binding = bindings[expression];
    switch (node->node_type) {
    case AST_NODE_TYPE_IDENTIFIER:
        if (binding.kind == SEMANTIC_BINDING_KIND_NONE ||
            binding.kind == SEMANTIC_BINDING_KIND_JUMP_TARGET) {
            return false;
        }
        return main_definition_type_write(nodes, bindings, constants, node_count, binding.target,
                                          depth + 1, text, text_size_bytes);
    case AST_NODE_TYPE_CAST:
        return main_text_write(text, text_size_bytes, node->payload.cast.type_name);
    case AST_NODE_TYPE_STRUCT_LITERAL:
        return node->payload.struct_literal.name.bytes != NULL &&
               main_text_write(text, text_size_bytes, node->payload.struct_literal.name);
    case AST_NODE_TYPE_ARRAY_LITERAL:
        return main_array_literal_type_write(nodes, expression, text, text_size_bytes);
    case AST_NODE_TYPE_DEREFERENCE:
    case AST_NODE_TYPE_SLICE:
    case AST_NODE_TYPE_SUBSCRIPT:
    case AST_NODE_TYPE_FIELD_ACCESS: {
        char base[256] = "";
        if (not main_expression_type_write(nodes, bindings, constants, node_count,
                                           node->first_child, depth + 1, base, sizeof(base))) {
            return false;
        }
        if (node->node_type == AST_NODE_TYPE_DEREFERENCE) {
            // The pointer type has the caret first, and what is pointed to is the rest.
            return base[0] == '^' &&
                   main_text_write(text, text_size_bytes, str_make(base + 1, strlen(base + 1)));
        }
        if (node->node_type == AST_NODE_TYPE_SUBSCRIPT ||
            node->node_type == AST_NODE_TYPE_SLICE) {
            // The type of an array or a slice has the brackets first, and the element is the rest.
            char const *close = base[0] == '[' ? strchr(base, ']') : NULL;
            if (close == NULL) {
                return false;
            }
            if (node->node_type == AST_NODE_TYPE_SLICE) {
                // A range of the elements views them, and its length is known only when the
                // program runs, whatever the array has.
                int written = snprintf(text, text_size_bytes, "[]%s", close + 1);
                return written >= 0 && (size_t)written < text_size_bytes;
            }
            return main_text_write(text, text_size_bytes, str_make(close + 1, strlen(close + 1)));
        }
        char const *struct_name = base[0] == '^' ? base + 1 : base;
        Ast_Node_Index struct_definition = main_struct_of_name(
            nodes, node_count, str_make(struct_name, strlen(struct_name)));
        if (struct_definition == AST_NODE_INDEX_NONE) {
            return false;
        }
        Ast_Node_Index field = main_struct_field(nodes, struct_definition,
                                                 node->payload.field_access.name);
        return field != AST_NODE_INDEX_NONE &&
               main_type_text_write(nodes, nodes[field].first_child, text, text_size_bytes);
    }
    default:
        return false;
    }
}

/**
 * Returns the text of the constant that `target` defines when its value is a string that `++`
 * joined, so that an editor can show what the constants are joined to, or `STR_EMPTY` when it is
 * not one, also when the target is not a constant. The string of a `++` is the literal that comes
 * right after it in the tree, which is how it is told from a string that is written out, so a
 * constant that is given another constant that is joined has it as well.
 */
static Str main_concatenated_value(Ast_Node const *nodes, Constant_Value const *constants,
                                   size_t node_count, Ast_Node_Index target) {
    if (target >= node_count || nodes[target].node_type != AST_NODE_TYPE_CONSTANT_DEFINITION ||
        constants[target].status != CONSTANT_STATUS_OK) {
        return STR_EMPTY;
    }
    Ast_Node_Index literal = constants[target].string;
    if (literal == AST_NODE_INDEX_NONE || literal == 0 || literal >= node_count ||
        (nodes[literal - 1].node_type != AST_NODE_TYPE_CONCATENATION &&
         nodes[literal - 1].node_type != AST_NODE_TYPE_CALL)) {
        return STR_EMPTY;
    }
    assert(nodes[literal].node_type == AST_NODE_TYPE_STRING_LITERAL);
    return nodes[literal].payload.string_literal.text;
}

/**
 * Writes the value of the constant that `target` defines to `text` as it is written in a program,
 * when it is a number, a truth value or a rune, which the compiler worked out: `4096` for the
 * constant that `SIZE :: 64 * 64` is, and `2147483648 (0x80000000)` for a number that is written
 * more clearly in hexadecimal too, which is every one from 256 up. A float is written with the
 * fewest digits that read back as the same float. A string is left to
 * `main_concatenated_value`, and a constant that is not a value, such as a procedure or a member
 * of an enum, has none. Returns false when there is none to show.
 */
static bool main_constant_value_write(Ast_Node const *nodes, Semantic_Binding const *bindings,
                                      Constant_Value const *constants, size_t node_count,
                                      Ast_Node_Index target, char *text, size_t text_size_bytes) {
    if (target >= node_count || nodes[target].node_type != AST_NODE_TYPE_CONSTANT_DEFINITION ||
        constants[target].status != CONSTANT_STATUS_OK ||
        constants[target].string != AST_NODE_INDEX_NONE) {
        return false;
    }
    Ast_Node_Index value_node = nodes[target].first_child;
    // The member of an enum has the number of the member, which is not what it is to a reader.
    if (nodes[value_node].node_type == AST_NODE_TYPE_FIELD_ACCESS &&
        main_enum_of_access(nodes, bindings, value_node) != AST_NODE_INDEX_NONE) {
        return false;
    }

    Constant_Value value = constants[target];
    int written = -1;
    switch (value.type) {
    case CONSTANT_TYPE_BOOL:
    case CONSTANT_TYPE_UNTYPED_BOOL:
        written = snprintf(text, text_size_bytes, "%s", value.integer != 0 ? "true" : "false");
        break;
    case CONSTANT_TYPE_F32:
    case CONSTANT_TYPE_F64:
    case CONSTANT_TYPE_UNTYPED_FLOAT: {
        // The fewest digits that read back as the same number, which is also what the program means.
        int digits = 15;
        for (; digits < 17; digits += 1) {
            char probe[64];
            (void)snprintf(probe, sizeof(probe), "%.*g", digits, value.floating);
            if (strtod(probe, NULL) == value.floating) {
                break;
            }
        }
        written = snprintf(text, text_size_bytes, "%.*g", digits, value.floating);
        break;
    }
    case CONSTANT_TYPE_BYTE:
    case CONSTANT_TYPE_I32:
    case CONSTANT_TYPE_I64:
    case CONSTANT_TYPE_INT:
    case CONSTANT_TYPE_U32:
    case CONSTANT_TYPE_U64:
    case CONSTANT_TYPE_UNTYPED_INTEGER:
        if (value.integer >= 256) {
            written = snprintf(text, text_size_bytes, "%" PRId64 " (0x%" PRIX64 ")", value.integer,
                               (uint64_t)value.integer);
        } else {
            written = snprintf(text, text_size_bytes, "%" PRId64, value.integer);
        }
        break;
    case CONSTANT_TYPE_RUNE:
        if (value.integer >= 0x20 && value.integer < 0x7F && value.integer != '\'' &&
            value.integer != '\\') {
            written = snprintf(text, text_size_bytes, "'%c' (%" PRId64 ")", (int)value.integer,
                               value.integer);
        } else {
            written = snprintf(text, text_size_bytes, "U+%04" PRIX64, (uint64_t)value.integer);
        }
        break;
    case CONSTANT_TYPE_STR:
    case CONSTANT_TYPE_UNTYPED_STRING:
        return false;
    }
    return written >= 0 && (size_t)written < text_size_bytes;
}

/**
 * Writes the documentation comment of the definition that starts at `offset` in `source` to `text`
 * and returns it, or returns `STR_EMPTY` when there is none. It is the lines of `///` right above
 * the line of the definition, which a blank line or any other line ends: each has the `///` and a
 * space after it taken off, and they are joined with line feeds. The text that does not fit is cut
 * before a character.
 */
static Str main_doc_comment_write(Str source, size_t offset, char *text, size_t text_size_bytes) {
    if (offset > source.length) {
        return STR_EMPTY;
    }

    // The lines are found from the line of the definition upwards, and written from the top.
    enum { DOC_LINES_MAX = 128 };
    size_t starts[DOC_LINES_MAX];
    size_t ends[DOC_LINES_MAX];
    size_t count = 0;
    size_t line_start = offset;
    while (line_start > 0 && source.bytes[line_start - 1] != '\n') {
        line_start -= 1;
    }
    while (line_start > 0 && count < DOC_LINES_MAX) {
        size_t end = line_start - 1;
        size_t start = end;
        while (start > 0 && source.bytes[start - 1] != '\n') {
            start -= 1;
        }
        // The line without its indentation and its carriage return.
        size_t first = start;
        while (first < end && (source.bytes[first] == ' ' || source.bytes[first] == '\t')) {
            first += 1;
        }
        size_t last = end;
        if (last > first && source.bytes[last - 1] == '\r') {
            last -= 1;
        }
        if (last - first < 3 || memcmp(source.bytes + first, "///", 3) != 0) {
            break;
        }
        // `////` and more is a line of decoration and not a documentation comment.
        if (last - first > 3 && source.bytes[first + 3] == '/') {
            break;
        }
        first += 3;
        if (first < last && source.bytes[first] == ' ') {
            first += 1;
        }
        starts[count] = first;
        ends[count] = last;
        count += 1;
        line_start = start;
    }
    if (count == 0) {
        return STR_EMPTY;
    }

    size_t length = 0;
    for (size_t i = count; i > 0; i -= 1) {
        size_t line_length = ends[i - 1] - starts[i - 1];
        size_t needed = line_length + (length > 0 ? 1 : 0);
        if (length + needed >= text_size_bytes) {
            break;
        }
        if (length > 0) {
            text[length] = '\n';
            length += 1;
        }
        memcpy(text + length, source.bytes + starts[i - 1], line_length);
        length += line_length;
    }
    // A text cut in the middle of a character is cut before it.
    while (length > 0 && length < text_size_bytes && count > 0 &&
           ((unsigned char)text[length - 1] & 0xC0u) == 0x80u) {
        length -= 1;
    }
    return str_make(text, length);
}

/**
 * Prints `value` as a string in double quotes on one line, which is the seventh field of a line of
 * `dump-types`: a backslash, a double quote and the control characters, a tab and a line break
 * among them, are written with a backslash, as `\t` and `\n`, and the rest as it is, since
 * it is UTF-8. A value of more than `bytes_max` bytes is cut before a character, and
 * three dots follow the closing quote.
 */
static void main_value_print(Str value, size_t bytes_max) {
    size_t length = value.length;
    if (length > bytes_max) {
        length = bytes_max;
        // A byte that is `10` in its high bits goes on a character that started before it.
        while (length > 0 && ((unsigned char)value.bytes[length] & 0xC0u) == 0x80u) {
            length -= 1;
        }
    }

    putchar('"');
    for (size_t i = 0; i < length; i += 1) {
        unsigned char byte = (unsigned char)value.bytes[i];
        switch (byte) {
        case '\\':
            fputs("\\\\", stdout);
            break;
        case '"':
            fputs("\\\"", stdout);
            break;
        case '\n':
            fputs("\\n", stdout);
            break;
        case '\r':
            fputs("\\r", stdout);
            break;
        case '\t':
            fputs("\\t", stdout);
            break;
        default:
            if (byte < 0x20 || byte == 0x7F) {
                printf("\\x%02X", (unsigned)byte);
            } else {
                putchar(byte);
            }
            break;
        }
    }
    putchar('"');
    if (length < value.length) {
        fputs("...", stdout);
    }
}

/**
 * Prints the line of `dump-types` for the name `name`, a view of the source, with `text` as the
 * type, see `main_run_command_print_types`. A `note` that is not empty is a sixth field, and a
 * `value` that is not empty is a seventh, after a sixth that is then empty when there is no note.
 */
static void main_type_row_print(Str source, Str name, char const *text, char const *note,
                                Str value, char const *plain_value, Str doc) {
    // The names are views of the source, so the offset of a name is where its bytes are. A member
    // after a dot is not where its node starts, which is why the node is not used.
    // A name that the compiler made up and that is not in the source has no place to show.
    if (name.bytes < source.bytes || name.bytes >= source.bytes + source.length) {
        return;
    }
    size_t name_offset = (size_t)(name.bytes - source.bytes);
    Str_Position position = str_position_at(source, name_offset);
    printf("%zu\t%zu\t%zu\t%.*s\t%s", position.line, position.column, name.length,
           (int)name.length, (char const *)name.bytes, text);
    if (note[0] != '\0' || value.bytes != NULL || plain_value[0] != '\0' || doc.bytes != NULL) {
        printf("\t%s", note);
    }
    if (value.bytes != NULL) {
        printf("\t");
        main_value_print(value, MAIN_VALUE_BYTES_MAX);
    } else if (plain_value[0] != '\0') {
        printf("\t%s", plain_value);
    } else if (doc.bytes != NULL) {
        printf("\t");
    }
    if (doc.bytes != NULL) {
        printf("\t");
        main_value_print(doc, MAIN_DOC_BYTES_MAX);
    }
    printf("\n");
}

/**
 * Writes the array or slice that the for statement `statement` goes over, which the iterator is an
 * element of, to `note` as it is declared, such as `people: []Person`: its name, when it is a
 * name, and its type. Only the type is written for any other expression, such as `[]Person`. The
 * note is empty for a range, whose iterator is no element, and when the type is not known.
 */
static void main_iterator_note_write(Ast_Node const *nodes, Semantic_Binding const *bindings,
                                     Constant_Value const *constants, size_t node_count,
                                     Ast_Node_Index statement, char *note,
                                     size_t note_size_bytes) {
    assert(statement < node_count);
    assert(note_size_bytes > 0);

    note[0] = '\0';
    Ast_Node_Index sequence = nodes[statement].first_child;
    if (nodes[sequence].node_type == AST_NODE_TYPE_RANGE) {
        return;
    }
    char type[256] = "";
    if (not main_expression_type_write(nodes, bindings, constants, node_count, sequence, 0, type,
                                       sizeof(type))) {
        return;
    }
    // Only a name can be said as it is, since the names are views of the source and an expression
    // is not.
    int written = 0;
    if (nodes[sequence].node_type == AST_NODE_TYPE_IDENTIFIER) {
        Str name = nodes[sequence].payload.identifier.name;
        written = snprintf(note, note_size_bytes, "%.*s: %s", (int)name.length,
                           (char const *)name.bytes, type);
    } else {
        written = snprintf(note, note_size_bytes, "%s", type);
    }
    // A note that does not fit is left out whole and not cut.
    if (written < 0 || (size_t)written >= note_size_bytes) {
        note[0] = '\0';
    }
}

/**
 * Prints the lines of `dump-types` for the fields that the struct literal `literal` gives a value,
 * each with the type of the field in the struct that the literal names. A literal without a name,
 * and a field that the struct does not have, print nothing.
 */
static void main_struct_literal_rows_print(Str source, Ast_Node const *nodes, size_t node_count,
                                           Ast_Node_Index literal) {
    Str struct_name = nodes[literal].payload.struct_literal.name;
    if (struct_name.bytes == NULL) {
        return;
    }
    Ast_Node_Index struct_definition = main_struct_of_name(nodes, node_count, struct_name);
    if (struct_definition == AST_NODE_INDEX_NONE) {
        return;
    }
    for (Ast_Node_Index initializer = nodes[literal].first_child;
         initializer != AST_NODE_INDEX_NONE; initializer = nodes[initializer].next_sibling) {
        Str name = nodes[initializer].payload.field_initializer.name;
        Ast_Node_Index field = main_struct_field(nodes, struct_definition, name);
        char text[256] = "";
        if (field != AST_NODE_INDEX_NONE &&
            main_type_text_write(nodes, nodes[field].first_child, text, sizeof(text))) {
            main_type_row_print(source, name, text, "", STR_EMPTY, "", STR_EMPTY);
        }
    }
}

/**
 * Returns the signature of the built-in procedure `name`, written like the type of a procedure
 * that is defined in a program, or NULL when `name` is not one. The types of the parameters that a
 * built-in takes in more than one way are written with `|` between them, which no type of the
 * source is, and a built-in that gives two values has them between parentheses.
 */
static char const *main_built_in_signature(Str name) {
    if (str_equals(name, STR("append"))) {
        return "proc(array: ^[dynamic]T, value: T, ...)";
    }
    if (str_equals(name, STR("length_of"))) {
        return "proc(elements: [N]T | [dynamic]T | []T | Str) Int";
    }
    if (str_equals(name, STR("int_to_byte"))) {
        return "proc(number: Int) Byte";
    }
    if (str_equals(name, STR("int_to_i32"))) {
        return "proc(number: Int | I64) I32";
    }
    if (str_equals(name, STR("int_to_str"))) {
        return "proc(number: Int | I32 | I64) Str";
    }
    if (str_equals(name, STR("str_to_c_str"))) {
        return "proc(text: Str) C_Str";
    }
    if (str_equals(name, STR("try_str_to_int"))) {
        return "proc(text: Str) (Int, Bool)";
    }
    if (str_equals(name, STR("utf8_decode"))) {
        return "proc(bytes: [N]Byte | []Byte) (Str, Bool)";
    }
    if (str_equals(name, STR("make"))) {
        return "proc(type: []T, count: Int) []T";
    }
    if (str_equals(name, STR("destroy"))) {
        return "proc(elements: []T)";
    }
    return NULL;
}

/**
 * Prints a line for each place where a constant, a global variable, a parameter or a variable is
 * defined or used, and whose type is known: the line and the column of its name, both counted from
 * 1, the length of the name in bytes, the name, and the type as it is written in the source,
 * separated by tabs. The columns count bytes, as the other tools of the compiler do. A for
 * statement's iterator has the array or slice that it is an element of as a sixth field, and a
 * constant that is a string that `++` joined has the joined text as a seventh, and one that is a
 * number, a truth value or a rune has its value, see `main_type_row_print`. This is what an editor shows when the cursor rests on a name.
 */
static void main_run_command_print_types(Str source, Parse_Result parse_result,
                                         Semantic_Result semantic_result, size_t first_node) {
    assert(parse_result.status == PARSE_STATUS_OK);
    assert(semantic_result.status == SEMANTIC_STATUS_OK);

    Ast_Node const *nodes = parse_result.nodes.elements;
    Semantic_Binding const *bindings = semantic_result.bindings.elements;
    Constant_Value const *constants = semantic_result.constants.elements;
    // The nodes of the imported files are before the ones of the program.
    for (Ast_Node_Index index = (Ast_Node_Index)first_node; index < parse_result.nodes.length;
         index += 1) {
        Ast_Node const *node = &nodes[index];
        Str name = STR_EMPTY;
        Ast_Node_Index target = AST_NODE_INDEX_NONE;
        // The enum that a member belongs to, for the names whose type is that.
        Ast_Node_Index enum_definition = AST_NODE_INDEX_NONE;
        // The expression whose type is the type of the name, for a field that is read.
        Ast_Node_Index expression = AST_NODE_INDEX_NONE;
        // The type that the name is given as it is, for a field that is defined.
        Ast_Node_Index type_node = AST_NODE_INDEX_NONE;
        switch (node->node_type) {
        case AST_NODE_TYPE_CONSTANT_DEFINITION:
            name = node->payload.constant_definition.name;
            target = index;
            break;
        case AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION:
            name = node->payload.global_variable_definition.name;
            target = index;
            break;
        case AST_NODE_TYPE_PARAMETER:
            name = node->payload.parameter.name;
            target = index;
            break;
        case AST_NODE_TYPE_FOR_STATEMENT:
            // The iterator is a variable that the statement defines.
            name = node->payload.for_statement.name;
            target = index;
            break;
        case AST_NODE_TYPE_VARIABLE_DEFINITION:
            name = node->payload.variable_definition.name;
            target = index;
            break;
        case AST_NODE_TYPE_IDENTIFIER:
        case AST_NODE_TYPE_CALL:
            name = node->node_type == AST_NODE_TYPE_IDENTIFIER ? node->payload.identifier.name
                                                               : node->payload.call.name;
            // A built-in procedure and a jump have no definition to take the type from.
            if (bindings[index].kind != SEMANTIC_BINDING_KIND_NONE &&
                bindings[index].kind != SEMANTIC_BINDING_KIND_JUMP_TARGET) {
                target = bindings[index].target;
            }
            break;
        case AST_NODE_TYPE_ENUM_MEMBER:
            // The type of a member is its enum. It has no definition to take it from, so the
            // enum is the one that is the target.
            name = node->payload.enum_member.name;
            enum_definition = main_enum_of_member(nodes, parse_result.nodes.length, index);
            break;
        case AST_NODE_TYPE_ENUM_LITERAL:
            name = node->payload.enum_literal.name;
            enum_definition = main_enum_of_member_name(nodes, parse_result.nodes.length, name);
            break;
        case AST_NODE_TYPE_FIELD_ACCESS:
            name = node->payload.field_access.name;
            enum_definition = main_enum_of_access(nodes, bindings, index);
            if (enum_definition == AST_NODE_INDEX_NONE) {
                expression = index;
            }
            break;
        case AST_NODE_TYPE_FIELD:
            // The type of a field is its first child. A field that is a struct of its own has no
            // type that can be written.
            name = node->payload.field.name;
            type_node = node->first_child;
            break;
        case AST_NODE_TYPE_STRUCT_LITERAL:
            // The fields that are given a value have the type that the field of the struct has,
            // which a literal without the name of its struct does not tell.
            main_struct_literal_rows_print(source, nodes, parse_result.nodes.length, index);
            continue;
        default:
            break;
        }
        // The name `_` takes the place of a value that is not wanted, and is no variable.
        if (name.bytes == NULL || (name.length == 1 && name.bytes[0] == '_')) {
            continue;
        }

        // A call of a built-in procedure shows its signature, which no definition has.
        char const *built_in = NULL;
        if (node->node_type == AST_NODE_TYPE_CALL &&
            bindings[index].kind == SEMANTIC_BINDING_KIND_NONE) {
            built_in = main_built_in_signature(name);
        }

        char text[256] = "";
        if (built_in != NULL) {
            if (not main_text_write(text, sizeof(text), str_make(built_in, strlen(built_in)))) {
                continue;
            }
        } else if (target != AST_NODE_INDEX_NONE &&
                   nodes[target].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION &&
                   (nodes[nodes[target].first_child].node_type == AST_NODE_TYPE_STRUCT_TYPE ||
                    nodes[nodes[target].first_child].node_type == AST_NODE_TYPE_ENUM_TYPE)) {
            // A struct and an enum are types, which have no type of a value to show, so the hover
            // says what kind of type it is, with the documentation of it below.
            Ast_Node const *type = &nodes[nodes[target].first_child];
            int written = snprintf(text, sizeof(text), "%s", "struct");
            if (type->node_type == AST_NODE_TYPE_ENUM_TYPE) {
                written = snprintf(text, sizeof(text), "enum %.*s",
                                   (int)type->payload.enum_type.value_type.length,
                                   type->payload.enum_type.value_type.bytes);
            }
            if (written < 0 || (size_t)written >= sizeof(text)) {
                continue;
            }
        } else if (enum_definition != AST_NODE_INDEX_NONE) {
            if (not main_text_write(text, sizeof(text),
                                    nodes[enum_definition].payload.constant_definition.name)) {
                continue;
            }
        } else if (type_node != AST_NODE_INDEX_NONE) {
            if (not main_type_text_write(nodes, type_node, text, sizeof(text))) {
                continue;
            }
        } else if (expression != AST_NODE_INDEX_NONE) {
            if (not main_expression_type_write(nodes, bindings, constants,
                                               parse_result.nodes.length, expression, 0, text,
                                               sizeof(text))) {
                continue;
            }
        } else if (target == AST_NODE_INDEX_NONE ||
                   not main_definition_type_write(nodes, bindings, constants,
                                                  parse_result.nodes.length, target, 0, text,
                                                  sizeof(text))) {
            continue;
        }
        // The iterator of a for statement says what it is an element of.
        char note[256] = "";
        if (target != AST_NODE_INDEX_NONE &&
            nodes[target].node_type == AST_NODE_TYPE_FOR_STATEMENT) {
            main_iterator_note_write(nodes, bindings, constants, parse_result.nodes.length, target,
                                     note, sizeof(note));
        }
        // A constant that is a string that `++` joined shows the text that it joined.
        Str value = main_concatenated_value(nodes, constants, parse_result.nodes.length, target);
        // A constant that is a number, a truth value or a rune shows the value that was worked out.
        char plain_value[64] = "";
        if (value.bytes == NULL) {
            (void)main_constant_value_write(nodes, bindings, constants, parse_result.nodes.length,
                                            target, plain_value, sizeof(plain_value));
        }
        // The documentation comment above the definition of the name, which is the description of
        // the item that is defined, where it is defined and where it is used.
        Ast_Node_Index doc_node = AST_NODE_INDEX_NONE;
        if (target != AST_NODE_INDEX_NONE &&
            (nodes[target].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION ||
             nodes[target].node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION)) {
            doc_node = target;
        } else if (node->node_type == AST_NODE_TYPE_FIELD ||
                   node->node_type == AST_NODE_TYPE_ENUM_MEMBER) {
            doc_node = index;
        } else if (enum_definition != AST_NODE_INDEX_NONE) {
            // A use of a member of an enum has the documentation of the member that is defined.
            for (Ast_Node_Index member = nodes[nodes[enum_definition].first_child].first_child;
                 member != AST_NODE_INDEX_NONE; member = nodes[member].next_sibling) {
                if (nodes[member].node_type == AST_NODE_TYPE_ENUM_MEMBER &&
                    str_equals(nodes[member].payload.enum_member.name, name)) {
                    doc_node = member;
                }
            }
        }
        char doc_text[MAIN_DOC_BYTES_MAX + 1];
        Str doc = STR_EMPTY;
        if (doc_node != AST_NODE_INDEX_NONE) {
            // The nodes of an imported file have their offsets in the source of that file.
            Str doc_source = source;
            if (doc_node < first_node && main_packages != NULL) {
                (void)packages_node_source(main_packages, doc_node, &doc_source);
            }
            if (doc_node >= first_node || doc_source.bytes != source.bytes) {
                doc = main_doc_comment_write(doc_source, nodes[doc_node].source_offset, doc_text,
                                             sizeof(doc_text));
            }
        }
        main_type_row_print(source, name, text, note, value, plain_value, doc);
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
        report_wasm_error(&command.reporter, path, source, build_result);
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
            report_wasm_text_error(&command.reporter, path, text_result);
            return 1;
        }
        // The text is the filled start of the text buffer, which is writable, unlike the `Str`.
        output = array_slice_make(buffers->output_text.elements, text_result.text.length);
        break;
    }
    }

    if (not main_run_command_write_file(command.output_path, output)) {
        report_general(&command.reporter, ERROR_CODE_OUTPUT_UNWRITABLE, command.output_path, 0);
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
            report_general(&command.reporter, ERROR_CODE_PATH_ABSOLUTE_UNKNOWN, path, 0);
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
        report_x64_error(&command.reporter, path, source, build_result);
        return 1;
    }

    if (command.mode == COMMAND_MODE_DUMP_ASM) {
        // The assembly is formatted from the executable file rather than written from the syntax
        // tree, so that it always shows exactly what the build wrote.
        X64_Text_Result text_result = x64_text_format(build_result.bytes, build_result.symbols,
                                                      buffers->labels, buffers->output_text);
        if (text_result.status != X64_TEXT_STATUS_OK) {
            report_x64_text_error(&command.reporter, path, text_result);
            return 1;
        }
        if (fwrite(text_result.text.bytes, sizeof(byte), text_result.text.length, stdout) !=
            text_result.text.length) {
            report_general(&command.reporter, ERROR_CODE_ASSEMBLY_WRITE_FAILED, path, 0);
            return 1;
        }
        return 0;
    }

    if (command.mode == COMMAND_MODE_BUILD) {
        if (not main_run_command_write_file(command.output_path, build_result.bytes)) {
            report_general(&command.reporter, ERROR_CODE_OUTPUT_UNWRITABLE, command.output_path, 0);
            return 1;
        }
        return 0;
    }

    assert(command.mode == COMMAND_MODE_RUN);
    // The program writes through the C library, which buffers, and the compiler may too.
    (void)fflush(stdout);
    Process_Result process_result =
        process_run_executable(build_result.bytes, command.arguments, command.argument_count);
    switch (process_result.status) {
    case PROCESS_STATUS_FILE_FAILED:
        report_general(&command.reporter, ERROR_CODE_PROGRAM_FILE_FAILED, path, 0);
        return 1;
    case PROCESS_STATUS_OK:
        return process_result.exit_code;
    case PROCESS_STATUS_START_FAILED:
        report_general(&command.reporter, ERROR_CODE_PROGRAM_START_FAILED, path, 0);
        return 1;
    case PROCESS_STATUS_ARGUMENTS_TOO_LONG:
        report_general(&command.reporter, ERROR_CODE_PROGRAM_ARGUMENTS_TOO_LONG, path, 0);
        return 1;
    case PROCESS_STATUS_UNSUPPORTED:
        report_general(&command.reporter, ERROR_CODE_RUN_UNSUPPORTED, path, 0);
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
        case TOKEN_KIND_DOT_DOT:
            printf("DOT_DOT\n");
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
        case TOKEN_KIND_KEYWORD_DYNAMIC:
            printf("KEYWORD_DYNAMIC\n");
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
        case TOKEN_KIND_PLUS_PLUS:
            printf("PLUS_PLUS\n");
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
