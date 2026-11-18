#ifndef MIALU_PACKAGE_H
#define MIALU_PACKAGE_H

#include <stdbool.h>
#include <stddef.h>

#include <mialu/array_slice.h>
#include <mialu/ast.h>
#include <mialu/parser.h>
#include <mialu/str.h>
#include <mialu/tokenizer.h>

/**
 * Putting the source files of a program together. `import "mu:filesystem"` names a source file of
 * the library of a vendor: the text before the colon is the vendor, and `mu` is the standard
 * library of Mialu, and the text after it is the path under the vendor, without `.mu`. The file is
 * found in the directory `lib` that is next to the compiler, as it is in a release, or else in the
 * directory `src/mialu-std` of this project, which is where a build from the sources finds it.
 *
 * The files that a file imports are parsed before it, into the same buffer of nodes, and the
 * definitions of every imported file are part of the program, but their names are the name of the
 * package, a dot and the name that the definition has, so that `file_open` of the package `mialu`
 * is `mialu.file_open` to the program that imports it, and to every other package. Inside the
 * file that defines it, the name is written as it is, and the files that are put together stay
 * apart in what they call things, so that a name of the program, which does not have a package
 * before it, cannot be one of a package. The package name is in the source as three tokens, the
 * name, a dot and the name of the definition, which are one token when the files are put together,
 * so the parser and the stages after it see the name that it is.
 */

// Alphabetical, because nothing depends on the order.
typedef enum {
    // The files that import each other, which is a loop: the subject is the text of the import
    // that closes it.
    PACKAGE_STATUS_CYCLE,
    // A file that is imported cannot be read: it is not in any directory of the library, or it is
    // larger than the sources can be. The subject is the text of the import.
    PACKAGE_STATUS_FILE_UNREADABLE,
    // The text of an import is not a vendor, a colon and a path of names and slashes. The subject
    // is the text.
    PACKAGE_STATUS_IMPORT_PATH_INVALID,
    // More files than the program can put together, or more imports below each other, or more text
    // for the names of the definitions than there is room for.
    PACKAGE_STATUS_LIMIT,
    PACKAGE_STATUS_OK,
    // The memory for a file could not be made.
    PACKAGE_STATUS_OUT_OF_MEMORY,
    // A file that is imported does not parse: `parse_result` and `code_tokens` say how, and
    // `path` and `source` are the file.
    PACKAGE_STATUS_PARSE_ERROR,
    // A parameter, a variable or an iterator of an imported file has the name of a definition of
    // the same file, which it would hide. The subject is the name.
    PACKAGE_STATUS_SHADOWS,
    // A file that is imported does not tokenize: `tokenize_result` says how, and `path` and
    // `source` are the file.
    PACKAGE_STATUS_TOKENIZE_ERROR,
    // The vendor of an import is not `mu`. The subject is the text of the import.
    PACKAGE_STATUS_VENDOR_UNKNOWN,
} Package_Status;

enum {
    // The most files that are put together with the imports of each other: the program is one
    // more.
    PACKAGE_FILE_COUNT_MAX = 32,
    // The most files that import each other in a chain, which is far more than a program needs and
    // bounds the recursion.
    PACKAGE_DEPTH_MAX = 8,
    // The most bytes of text that the names of the definitions of the imported files take, with
    // the package name that goes before each.
    PACKAGE_NAMES_SIZE_BYTES_MAX = 256 * 1024,
};

/**
 * One file that is imported. `source` is the bytes of the file, which the nodes view, so it lives
 * as long as `Packages`. The nodes of the file are from `node_start`, which is its `AST_NODE_TYPE_
 * MODULE`, up to but not including `node_end`.
 */
typedef struct {
    char *path;
    Str import_text;
    Str source;
    Str package_name;
    size_t node_start;
    size_t node_end;
    bool is_loading;
} Package_File;

/**
 * The imported files, and the memory that the names of their definitions are in. It is made with
 * `packages_make` and freed with `packages_free`.
 */
typedef struct {
    Package_File files[PACKAGE_FILE_COUNT_MAX];
    size_t file_count;
    // The files in the order that they were loaded, which has a file after those that it imports.
    size_t order[PACKAGE_FILE_COUNT_MAX];
    size_t order_count;
    char *names;
    size_t names_length;
    // The directory of the library that is next to the compiler, and the one of this project, in
    // the order that they are looked in. The first is empty when the compiler cannot tell where it
    // is.
    char const *library_directories[2];
    char *executable_library;
    // The nodes below this one are of imported files, and the others are of the program. The
    // program has the nodes from here to the end.
    size_t imported_end;
    // What the file that could not be tokenized or parsed has, which the result views, until the
    // memory is freed.
    Token *error_tokens;
    Token *error_code_tokens;
} Packages;

/**
 * What came of putting the files together. When the status is not `PACKAGE_STATUS_OK`, `subject`
 * says what it is about, and views the source of a file, which `packages_locate` finds. A tokenize
 * or parse error is the one of the file at `path`.
 */
typedef struct {
    Str subject;
    char const *path;
    Str source;
    Tokenize_Result tokenize_result;
    Array_Slice code_tokens;
    Parse_Result parse_result;
    Package_Status status;
} Package_Result;

/**
 * Makes `Packages` with no files, which looks for the library in `library_directory`, a directory
 * of this project that the build gives, and in the directory `lib` next to the compiler.
 */
Packages packages_make(char const *library_directory);

/**
 * Frees the memory of `packages`.
 */
void packages_free(Packages *packages);

/**
 * Reads, tokenizes and parses every file that the program imports, and the ones that they import,
 * into `nodes` from its first node, and gives the qualified names in `code_tokens`, the tokens of
 * the program, their one token. `code_tokens.length` is the length of the tokens afterwards. The
 * program is parsed next, with `parse_at` from `packages->imported_end`. `source_size_bytes_max` and
 * `token_count_max` bound a file that is imported.
 */
Package_Result packages_load(Packages *packages, Array_Slice *code_tokens, Array_Slice nodes,
                             size_t source_size_bytes_max, size_t token_count_max);

/**
 * Puts the definitions of the imported files and the ones of the program, whose module is `root`,
 * into the children of `root`, the imported ones first, in the order that they were loaded, which
 * has a file after the files that it imports. An import statement is taken out, and so is an
 * `import foreign` that an earlier file has the same of already. `nodes` has the nodes of all of
 * them.
 */
void packages_merge(Packages const *packages, Ast_Node *nodes, Ast_Node_Index root);

/**
 * Finds which imported file `subject`, a view of a source, is part of, and gives its path and
 * source. Returns false when it is not part of any, as when it is part of the program.
 */
bool packages_locate(Packages const *packages, Str subject, char const **path, Str *source);

#endif
