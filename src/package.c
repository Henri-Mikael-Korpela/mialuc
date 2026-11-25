#include <assert.h>
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
#include <mialu/package.h>
#include <mialu/parser.h>
#include <mialu/process.h>
#include <mialu/str.h>
#include <mialu/tokenizer.h>

// The indentation of a line that is not indented, which is where an import starts.
static uint32_t const PACKAGE_INDENTATION_LEVEL_TOP = 0;

enum {
    /**
     * The most definitions that one imported file has, which the names of its references are looked
     * up among.
     */
    PACKAGE_DEFINITION_COUNT_MAX = 4096,
    /**
     * The longest path of a file of the library, with the directory.
     */
    PACKAGE_PATH_SIZE_BYTES_MAX = 4096,
    /**
     * How many `import foreign` of the files are remembered, to leave out the ones that are the
     * same as an earlier one.
     */
    PACKAGE_FOREIGN_IMPORT_COUNT_MAX = 32,
};

/**
 * What the mangling of one file needs: where the new names go, the package name, and the names of
 * the definitions of the file, which are the names that a reference is looked for among.
 */
typedef struct {
    Packages *packages;
    Str prefix;
    Str *names;
    size_t name_count;
    bool is_full;
} Package_Mangling;

static Package_Result package_import        (Packages *packages, Str import_text, size_t depth,
                                             Array_Slice nodes, size_t source_size_bytes_max,
                                             size_t token_count_max);
static Package_Result package_imports_load  (Packages *packages, Array_Slice code_tokens,
                                             Str *package_names, size_t *package_name_count,
                                             size_t depth, Array_Slice nodes,
                                             size_t source_size_bytes_max,
                                             size_t token_count_max);
static Package_Status package_mangle        (Packages *packages, Ast_Node *nodes, size_t start,
                                             size_t end, Str prefix, Str *subject);
static Str            package_mangle_name   (Package_Mangling *mangling, Str name);
static Str            package_mangle_text   (Package_Mangling *mangling, Str text);
static bool           package_names_has     (Package_Mangling const *mangling, Str name);
static bool           package_import_path_valid(Str import_text, Str *path);
static bool           package_is_name_byte  (char byte_value);
static char          *package_file_path_make(char const *directory, size_t directory_length,
                                             Str path);
static bool           package_file_read     (char const *path, size_t size_bytes_max, Str *source);
static void           package_tokens_qualify(Token *tokens, size_t *count, Str const *package_names,
                                             size_t package_name_count);
static Str            package_tokens_package_name(Token const *tokens, size_t count);

Packages packages_make(char const *library_directory) {
    Packages packages = {
        .file_count = 0,
        .order_count = 0,
        .names = NULL,
        .names_length = 0,
        .library_directories = {NULL, library_directory},
        .executable_library = NULL,
        .imported_end = 0,
        .error_tokens = NULL,
        .error_code_tokens = NULL,
    };

    // The directory `lib` next to the compiler is the library of a release. It is looked in first,
    // so that a release is not changed by the sources of the project that it was built from.
    char directory[PACKAGE_PATH_SIZE_BYTES_MAX] = {0};
    Str executable = process_executable_directory(
        array_slice_make(directory, sizeof(directory) - sizeof("/lib")));
    if (executable.bytes != NULL) {
        memcpy(directory + executable.length, "/lib", sizeof("/lib"));
        size_t length = executable.length + sizeof("/lib") - 1;
        packages.executable_library = malloc(length + 1);
        if (packages.executable_library != NULL) {
            memcpy(packages.executable_library, directory, length + 1);
            packages.library_directories[0] = packages.executable_library;
        }
    }
    return packages;
}

void packages_free(Packages *packages) {
    assert(packages != NULL);

    for (size_t i = 0; i < packages->file_count; i += 1) {
        free(packages->files[i].path);
        free((void *)packages->files[i].source.bytes);
        free(packages->files[i].text);
    }
    free(packages->names);
    free(packages->executable_library);
    free(packages->error_tokens);
    free(packages->error_code_tokens);
    packages->file_count = 0;
    packages->names = NULL;
    packages->executable_library = NULL;
    packages->error_tokens = NULL;
    packages->error_code_tokens = NULL;
}

Package_Result packages_load(Packages *packages, Array_Slice *code_tokens, Array_Slice nodes,
                             size_t source_size_bytes_max, size_t token_count_max) {
    assert(packages != NULL);
    assert(code_tokens != NULL);
    assert(packages->file_count == 0);

    Str package_names[PACKAGE_FILE_COUNT_MAX] = {{0}};
    size_t package_name_count = 0;
    Package_Result result = package_imports_load(packages, *code_tokens, package_names,
                                                 &package_name_count, 0, nodes,
                                                 source_size_bytes_max, token_count_max);
    if (result.status != PACKAGE_STATUS_OK) {
        return result;
    }

    // The program writes the name of a package and of a definition of it with a dot between, which
    // the stages after this take as one name.
    package_tokens_qualify(code_tokens->elements, &code_tokens->length, package_names,
                           package_name_count);
    return result;
}

void packages_merge(Packages const *packages, Ast_Node *nodes, Ast_Node_Index root) {
    assert(packages != NULL);
    assert(nodes[root].node_type == AST_NODE_TYPE_MODULE);

    Ast_Node_Index head = AST_NODE_INDEX_NONE;
    Ast_Node_Index tail = AST_NODE_INDEX_NONE;
    // What the files have imported from the platform: the same one is kept once, because the
    // analysis allows one for each target.
    bool has_libc = false;
    Str foreign_texts[PACKAGE_FOREIGN_IMPORT_COUNT_MAX] = {{0}};
    size_t foreign_text_count = 0;

    for (size_t part = 0; part <= packages->order_count; part += 1) {
        Ast_Node_Index module = root;
        if (part < packages->order_count) {
            module = (Ast_Node_Index)packages->files[packages->order[part]].node_start;
        }
        Ast_Node_Index child = nodes[module].first_child;
        // The children of the root of the program are the ones that the loop below links, so the
        // list is read before it is changed.
        while (child != AST_NODE_INDEX_NONE) {
            Ast_Node_Index next = nodes[child].next_sibling;
            bool is_kept = nodes[child].node_type != AST_NODE_TYPE_IMPORT;
            if (nodes[child].node_type == AST_NODE_TYPE_FOREIGN_IMPORT) {
                Ast_Foreign_Target target = nodes[child].payload.foreign_import.target;
                if (target == AST_FOREIGN_TARGET_LIBC) {
                    is_kept = not has_libc;
                    has_libc = true;
                } else if (target == AST_FOREIGN_TARGET_JS) {
                    Str text = ast_foreign_import_name(&nodes[child]);
                    for (size_t i = 0; i < foreign_text_count; i += 1) {
                        if (str_equals(foreign_texts[i], text)) {
                            is_kept = false;
                        }
                    }
                    if (is_kept && foreign_text_count < PACKAGE_FOREIGN_IMPORT_COUNT_MAX) {
                        foreign_texts[foreign_text_count] = text;
                        foreign_text_count += 1;
                    }
                }
            }
            if (is_kept) {
                nodes[child].next_sibling = AST_NODE_INDEX_NONE;
                if (tail == AST_NODE_INDEX_NONE) {
                    head = child;
                } else {
                    nodes[tail].next_sibling = child;
                }
                tail = child;
            }
            child = next;
        }
    }

    nodes[root].first_child = head;
    nodes[root].last_child = tail;
}

bool packages_node_source(Packages const *packages, size_t node, Str *source) {
    assert(packages != NULL);

    for (size_t i = 0; i < packages->file_count; i += 1) {
        if (node >= packages->files[i].node_start && node < packages->files[i].node_end) {
            *source = packages->files[i].source;
            return true;
        }
    }
    return false;
}

bool packages_locate(Packages const *packages, Str subject, char const **path, Str *source) {
    assert(packages != NULL);

    for (size_t i = 0; i < packages->file_count; i += 1) {
        Str file_source = packages->files[i].source;
        if (subject.bytes >= file_source.bytes &&
            subject.bytes <= file_source.bytes + file_source.length) {
            *path = packages->files[i].path;
            *source = file_source;
            return true;
        }
    }
    return false;
}

/**
 * Loads the files that the tokens `code_tokens` import, which are the ones at the start of a line:
 * `import` and a string. `package_names` gets the name of each one's package, so that the file can
 * write them with a dot. Returns the first problem.
 */
static Package_Result package_imports_load(Packages *packages, Array_Slice code_tokens,
                                           Str *package_names, size_t *package_name_count,
                                           size_t depth, Array_Slice nodes,
                                           size_t source_size_bytes_max,
                                           size_t token_count_max) {
    Package_Result result = {.status = PACKAGE_STATUS_OK};
    Token const *tokens = code_tokens.elements;
    for (size_t i = 0; i + 2 < code_tokens.length; i += 1) {
        if (tokens[i].kind != TOKEN_KIND_INDENTATION ||
            tokens[i].payload.indentation.level != PACKAGE_INDENTATION_LEVEL_TOP ||
            tokens[i + 1].kind != TOKEN_KIND_KEYWORD_IMPORT ||
            tokens[i + 2].kind != TOKEN_KIND_STRING_LITERAL) {
            continue;
        }
        Str import_text = tokens[i + 2].payload.string_literal.text;
        result = package_import(packages, import_text, depth, nodes, source_size_bytes_max,
                                token_count_max);
        if (result.status != PACKAGE_STATUS_OK) {
            return result;
        }
        // The file is loaded now, so it has the package name, which is kept once.
        for (size_t j = 0; j < packages->file_count; j += 1) {
            if (not str_equals(packages->files[j].import_text, import_text) ||
                packages->files[j].package_name.bytes == NULL) {
                continue;
            }
            bool is_known = false;
            for (size_t k = 0; k < *package_name_count; k += 1) {
                is_known = is_known || str_equals(package_names[k], packages->files[j].package_name);
            }
            if (not is_known && *package_name_count < PACKAGE_FILE_COUNT_MAX) {
                package_names[*package_name_count] = packages->files[j].package_name;
                *package_name_count += 1;
            }
        }
    }
    return result;
}

/**
 * Loads the file that `import_text` names, such as `mu:filesystem`, and the files that it imports,
 * unless it was loaded already. The nodes of the file are made from `packages->imported_end`, and
 * the names of its definitions are given the name of its package.
 */
static Package_Result package_import(Packages *packages, Str import_text, size_t depth,
                                     Array_Slice nodes, size_t source_size_bytes_max,
                                     size_t token_count_max) {
    Package_Result result = {.status = PACKAGE_STATUS_OK, .subject = import_text};

    Str path = STR_EMPTY;
    if (not package_import_path_valid(import_text, &path)) {
        // The vendor says which of the two problems it is: a text without a colon, or with a path
        // that has more than names, is not valid, and a vendor that is not `mu` is not known.
        result.status = PACKAGE_STATUS_IMPORT_PATH_INVALID;
        if (path.bytes != NULL) {
            result.status = PACKAGE_STATUS_VENDOR_UNKNOWN;
        }
        return result;
    }
    if (depth >= PACKAGE_DEPTH_MAX) {
        result.status = PACKAGE_STATUS_LIMIT;
        return result;
    }
    for (size_t i = 0; i < packages->file_count; i += 1) {
        if (str_equals(packages->files[i].import_text, import_text)) {
            if (packages->files[i].is_loading) {
                result.status = PACKAGE_STATUS_CYCLE;
            }
            return result;
        }
    }
    if (packages->file_count == PACKAGE_FILE_COUNT_MAX) {
        result.status = PACKAGE_STATUS_LIMIT;
        return result;
    }
    size_t index = packages->file_count;
    Package_File *file = &packages->files[index];
    *file = (Package_File){
        .path = NULL,
        .import_text = import_text,
        .source = STR_EMPTY,
        .text = NULL,
        .package_name = STR_EMPTY,
        .node_start = 0,
        .node_end = 0,
        .is_loading = true,
    };
    packages->file_count += 1;

    // The first directory that has the file is the one. The path of the last one that was tried is
    // what the error is about when none has.
    for (size_t i = 0; i < 2 and file->source.bytes == NULL; i += 1) {
        char const *directory = packages->library_directories[i];
        if (directory == NULL) {
            continue;
        }
        free(file->path);
        file->path = package_file_path_make(directory, strlen(directory), path);
        if (file->path == NULL) {
            result.status = PACKAGE_STATUS_OUT_OF_MEMORY;
            return result;
        }
        (void)package_file_read(file->path, source_size_bytes_max, &file->source);
    }
    if (file->source.bytes == NULL) {
        result.status = PACKAGE_STATUS_FILE_UNREADABLE;
        result.path = file->path;
        return result;
    }

    Token *tokens = malloc(token_count_max * sizeof(Token));
    Token *code_tokens = malloc(token_count_max * sizeof(Token));
    if (tokens == NULL || code_tokens == NULL) {
        free(tokens);
        free(code_tokens);
        result.status = PACKAGE_STATUS_OUT_OF_MEMORY;
        return result;
    }
    // The text of the strings that have escape sequences, which the tokens view, so it stays with
    // the file. It is as long as the source, which is as long as the text can be.
    file->text = malloc(file->source.length + 1);
    if (file->text == NULL) {
        free(tokens);
        free(code_tokens);
        result.status = PACKAGE_STATUS_OUT_OF_MEMORY;
        return result;
    }
    Tokenize_Result tokenized = tokenize(file->source, array_slice_make(tokens, token_count_max),
                                         array_slice_make(file->text, file->source.length + 1));
    if (tokenized.status != TOKENIZE_STATUS_OK) {
        free(packages->error_tokens);
        packages->error_tokens = tokens;
        free(code_tokens);
        result.status = PACKAGE_STATUS_TOKENIZE_ERROR;
        result.path = file->path;
        result.source = file->source;
        result.tokenize_result = tokenized;
        return result;
    }
    Array_Slice code = tokens_without_comments(tokenized.tokens,
                                               array_slice_make(code_tokens, token_count_max));

    // The files that this one imports come first, and with them the names of the packages that this
    // one writes with a dot, which are its own too.
    Str package_names[PACKAGE_FILE_COUNT_MAX] = {{0}};
    size_t package_name_count = 0;
    result = package_imports_load(packages, code, package_names, &package_name_count, depth + 1,
                                  nodes, source_size_bytes_max, token_count_max);
    if (result.status != PACKAGE_STATUS_OK) {
        free(tokens);
        free(code_tokens);
        return result;
    }
    Str own_name = package_tokens_package_name(code.elements, code.length);
    file->package_name = own_name;
    if (own_name.bytes != NULL && package_name_count < PACKAGE_FILE_COUNT_MAX) {
        package_names[package_name_count] = own_name;
        package_name_count += 1;
    }
    package_tokens_qualify(code.elements, &code.length, package_names, package_name_count);

    Parse_Result parsed = parse_at(code, nodes, packages->imported_end);
    if (parsed.status != PARSE_STATUS_OK) {
        free(packages->error_tokens);
        free(packages->error_code_tokens);
        packages->error_tokens = tokens;
        packages->error_code_tokens = code_tokens;
        result.status = PACKAGE_STATUS_PARSE_ERROR;
        result.path = file->path;
        result.source = file->source;
        result.code_tokens = code;
        result.parse_result = parsed;
        return result;
    }
    free(tokens);
    free(code_tokens);

    Ast_Node *node_elements = nodes.elements;
    file->node_start = parsed.root;
    file->node_end = parsed.nodes.length;
    packages->imported_end = parsed.nodes.length;
    Package_Status status = package_mangle(packages, node_elements, file->node_start,
                                           file->node_end, own_name, &result.subject);
    if (status != PACKAGE_STATUS_OK) {
        result.status = status;
        result.path = file->path;
        result.source = file->source;
        return result;
    }

    file->is_loading = false;
    packages->order[packages->order_count] = index;
    packages->order_count += 1;
    return result;
}

/**
 * Checks the text of an import, `vendor:path`, and writes the path under the vendor to `path`:
 * names, and slashes between them. Returns false when it is not that. The vendor must be `mu`; when
 * the text is of the right shape and the vendor is another, `path` is set and false is returned,
 * which is how the vendor is told from a shape that is wrong.
 */
static bool package_import_path_valid(Str import_text, Str *path) {
    *path = STR_EMPTY;
    size_t colon = 0;
    while (colon < import_text.length && import_text.bytes[colon] != ':') {
        colon += 1;
    }
    if (colon == 0 || colon == import_text.length) {
        return false;
    }
    Str rest = str_make(import_text.bytes + colon + 1, import_text.length - colon - 1);
    if (rest.length == 0 || rest.bytes[0] == '/' || rest.bytes[rest.length - 1] == '/') {
        return false;
    }
    for (size_t i = 0; i < rest.length; i += 1) {
        bool is_slash = rest.bytes[i] == '/';
        if (not package_is_name_byte(rest.bytes[i]) and not is_slash and rest.bytes[i] != '-') {
            return false;
        }
        if (is_slash and i + 1 < rest.length and rest.bytes[i + 1] == '/') {
            return false;
        }
    }
    *path = rest;
    return str_equals(str_make(import_text.bytes, colon), STR("mu"));
}

static bool package_is_name_byte(char byte_value) {
    return (byte_value >= 'a' && byte_value <= 'z') || (byte_value >= 'A' && byte_value <= 'Z') ||
           (byte_value >= '0' && byte_value <= '9') || byte_value == '_';
}

/**
 * Makes the path of the file `path` under `directory`, with `.mu` after it, as a string that the
 * caller frees. Returns NULL when there is no memory or it is too long.
 */
static char *package_file_path_make(char const *directory, size_t directory_length, Str path) {
    size_t length = directory_length + 1 + path.length + sizeof(".mu");
    if (length > PACKAGE_PATH_SIZE_BYTES_MAX) {
        return NULL;
    }
    char *result = malloc(length);
    if (result == NULL) {
        return NULL;
    }
    memcpy(result, directory, directory_length);
    result[directory_length] = '/';
    memcpy(result + directory_length + 1, path.bytes, path.length);
    memcpy(result + directory_length + 1 + path.length, ".mu", sizeof(".mu"));
    return result;
}

/**
 * Reads the file at `path`, whole, into memory that the caller frees. Returns false when it cannot
 * be read, or is larger than `size_bytes_max`.
 */
static bool package_file_read(char const *path, size_t size_bytes_max, Str *source) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    char *bytes = malloc(size_bytes_max + 1);
    if (bytes == NULL) {
        fclose(file);
        return false;
    }
    size_t length = fread(bytes, 1, size_bytes_max + 1, file);
    bool is_failed = ferror(file) != 0 || length > size_bytes_max;
    fclose(file);
    if (is_failed) {
        free(bytes);
        return false;
    }
    *source = str_make(bytes, length);
    return true;
}

/**
 * Returns the name of the package that the tokens of a file start with, `package name`, or an empty
 * `Str` when they do not, which the parser reports.
 */
static Str package_tokens_package_name(Token const *tokens, size_t count) {
    if (count >= 3 && tokens[0].kind == TOKEN_KIND_INDENTATION &&
        tokens[1].kind == TOKEN_KIND_KEYWORD_PACKAGE && tokens[2].kind == TOKEN_KIND_IDENTIFIER) {
        return tokens[2].payload.identifier.text;
    }
    return STR_EMPTY;
}

/**
 * Makes one token of a package name, a dot and a name that are written one after the other, which
 * are three tokens, where the package name is one of `package_names`. The text of the token is the
 * three, which are together in the source.
 */
static void package_tokens_qualify(Token *tokens, size_t *count, Str const *package_names,
                                   size_t package_name_count) {
    size_t length = *count;
    for (size_t i = 0; i + 2 < length; i += 1) {
        if (tokens[i].kind != TOKEN_KIND_IDENTIFIER || tokens[i + 1].kind != TOKEN_KIND_DOT ||
            tokens[i + 2].kind != TOKEN_KIND_IDENTIFIER) {
            continue;
        }
        Str package = tokens[i].payload.identifier.text;
        Str name = tokens[i + 2].payload.identifier.text;
        bool is_package = false;
        for (size_t j = 0; j < package_name_count; j += 1) {
            is_package = is_package || str_equals(package_names[j], package);
        }
        // No space is in between, so the three are one text in the source.
        if (not is_package ||
            tokens[i + 1].source_offset != tokens[i].source_offset + package.length ||
            tokens[i + 2].source_offset != tokens[i + 1].source_offset + 1) {
            continue;
        }
        tokens[i].payload.identifier.text = str_make(package.bytes, package.length + 1 + name.length);
        memmove(&tokens[i + 1], &tokens[i + 3], (length - i - 3) * sizeof(Token));
        length -= 2;
    }
    *count = length;
}

/**
 * Writes `prefix.name` to the memory of the names, and returns it, or returns an empty `Str` when
 * there is no room.
 */
static Str package_name_write(Package_Mangling *mangling, Str first, Str second) {
    Packages *packages = mangling->packages;
    if (packages->names == NULL) {
        packages->names = malloc(PACKAGE_NAMES_SIZE_BYTES_MAX);
        if (packages->names == NULL) {
            mangling->is_full = true;
            return STR_EMPTY;
        }
    }
    size_t length = first.length + 1 + second.length;
    if (length > PACKAGE_NAMES_SIZE_BYTES_MAX - packages->names_length) {
        mangling->is_full = true;
        return STR_EMPTY;
    }
    char *place = packages->names + packages->names_length;
    memcpy(place, first.bytes, first.length);
    place[first.length] = '.';
    memcpy(place + first.length + 1, second.bytes, second.length);
    packages->names_length += length;
    return str_make(place, length);
}

static bool package_names_has(Package_Mangling const *mangling, Str name) {
    for (size_t i = 0; i < mangling->name_count; i += 1) {
        if (str_equals(mangling->names[i], name)) {
            return true;
        }
    }
    return false;
}

/**
 * Gives `name` with the package name before it when it is the name of a definition of the file, and
 * `name` itself when it is not.
 */
static Str package_mangle_name(Package_Mangling *mangling, Str name) {
    if (not package_names_has(mangling, name)) {
        return name;
    }
    Str result = package_name_write(mangling, mangling->prefix, name);
    if (result.bytes == NULL) {
        return name;
    }
    return result;
}

/**
 * Gives `text`, which is a type as it is written, such as `^File` or `[4]File`, with the package
 * name before each name in it that is the name of a definition of the file. Where a name is after a
 * dot it is of another package, and is left as it is.
 */
static Str package_mangle_text(Package_Mangling *mangling, Str text) {
    // The text is made in two passes: the first counts what is added, and the second writes it.
    size_t added = 0;
    size_t i = 0;
    while (i < text.length) {
        if (not package_is_name_byte(text.bytes[i])) {
            i += 1;
            continue;
        }
        size_t start = i;
        while (i < text.length && package_is_name_byte(text.bytes[i])) {
            i += 1;
        }
        bool is_after_dot = start > 0 && text.bytes[start - 1] == '.';
        bool is_before_dot = i < text.length && text.bytes[i] == '.';
        Str word = str_make(text.bytes + start, i - start);
        bool is_number = text.bytes[start] >= '0' && text.bytes[start] <= '9';
        if (not is_number && not is_after_dot && not is_before_dot &&
            package_names_has(mangling, word)) {
            added += mangling->prefix.length + 1;
        }
    }
    if (added == 0) {
        return text;
    }

    Packages *packages = mangling->packages;
    if (packages->names == NULL) {
        packages->names = malloc(PACKAGE_NAMES_SIZE_BYTES_MAX);
        if (packages->names == NULL) {
            mangling->is_full = true;
            return text;
        }
    }
    if (text.length + added > PACKAGE_NAMES_SIZE_BYTES_MAX - packages->names_length) {
        mangling->is_full = true;
        return text;
    }
    char *place = packages->names + packages->names_length;
    size_t length = 0;
    i = 0;
    while (i < text.length) {
        if (not package_is_name_byte(text.bytes[i])) {
            place[length] = text.bytes[i];
            length += 1;
            i += 1;
            continue;
        }
        size_t start = i;
        while (i < text.length && package_is_name_byte(text.bytes[i])) {
            i += 1;
        }
        bool is_after_dot = start > 0 && text.bytes[start - 1] == '.';
        bool is_before_dot = i < text.length && text.bytes[i] == '.';
        Str word = str_make(text.bytes + start, i - start);
        bool is_number = text.bytes[start] >= '0' && text.bytes[start] <= '9';
        if (not is_number && not is_after_dot && not is_before_dot &&
            package_names_has(mangling, word)) {
            memcpy(place + length, mangling->prefix.bytes, mangling->prefix.length);
            length += mangling->prefix.length;
            place[length] = '.';
            length += 1;
        }
        memcpy(place + length, word.bytes, word.length);
        length += word.length;
    }
    assert(length == text.length + added);
    packages->names_length += length;
    return str_make(place, length);
}

/**
 * Gives the names of the definitions of the file whose nodes are from `start` to `end`, whose
 * module is the node `start`, the name of the package before them, and the same in each reference
 * to one in the file: a name, a call, a type, and a struct literal. So the file keeps calling
 * `file_open` that, while every other file, and the program, calls it `mu.file_open`. A
 * parameter, a variable or an iterator with the name of a definition would be taken for it, so it
 * is an error, as it is in the program. Returns the status, and the name in `subject`.
 */
static Package_Status package_mangle(Packages *packages, Ast_Node *nodes, size_t start, size_t end,
                                     Str prefix, Str *subject) {
    assert(nodes[start].node_type == AST_NODE_TYPE_MODULE);

    // A file with no package name has the parse error, which is reported before this.
    if (prefix.bytes == NULL) {
        return PACKAGE_STATUS_OK;
    }

    Str names[PACKAGE_DEFINITION_COUNT_MAX];
    Package_Mangling mangling = {
        .packages = packages,
        .prefix = prefix,
        .names = names,
        .name_count = 0,
        .is_full = false,
    };
    for (Ast_Node_Index child = nodes[start].first_child; child != AST_NODE_INDEX_NONE;
         child = nodes[child].next_sibling) {
        if (nodes[child].node_type != AST_NODE_TYPE_CONSTANT_DEFINITION &&
            nodes[child].node_type != AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION) {
            continue;
        }
        if (mangling.name_count == PACKAGE_DEFINITION_COUNT_MAX) {
            return PACKAGE_STATUS_LIMIT;
        }
        names[mangling.name_count] = ast_definition_name(&nodes[child]);
        mangling.name_count += 1;
    }

    for (size_t i = start; i < end; i += 1) {
        Ast_Node *node = &nodes[i];
        switch (node->node_type) {
        case AST_NODE_TYPE_CONSTANT_DEFINITION:
            node->payload.constant_definition.name =
                package_mangle_name(&mangling, node->payload.constant_definition.name);
            break;
        case AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION:
            node->payload.global_variable_definition.name =
                package_mangle_name(&mangling, node->payload.global_variable_definition.name);
            break;
        case AST_NODE_TYPE_IDENTIFIER:
            node->payload.identifier.name =
                package_mangle_name(&mangling, node->payload.identifier.name);
            break;
        case AST_NODE_TYPE_CALL:
            node->payload.call.name = package_mangle_name(&mangling, node->payload.call.name);
            break;
        case AST_NODE_TYPE_TYPE_NAME:
            node->payload.type_name.name =
                package_mangle_text(&mangling, node->payload.type_name.name);
            break;
        case AST_NODE_TYPE_CAST:
            node->payload.cast.type_name =
                package_mangle_text(&mangling, node->payload.cast.type_name);
            break;
        case AST_NODE_TYPE_STRUCT_LITERAL:
            node->payload.struct_literal.name =
                package_mangle_text(&mangling, node->payload.struct_literal.name);
            break;
        case AST_NODE_TYPE_PROCEDURE:
        case AST_NODE_TYPE_PROCEDURE_TYPE:
            node->payload.procedure.return_type =
                package_mangle_text(&mangling, node->payload.procedure.return_type);
            break;
        case AST_NODE_TYPE_PARAMETER:
            if (package_names_has(&mangling, node->payload.parameter.name)) {
                *subject = node->payload.parameter.name;
                return PACKAGE_STATUS_SHADOWS;
            }
            break;
        case AST_NODE_TYPE_VARIABLE_DEFINITION:
            if (package_names_has(&mangling, node->payload.variable_definition.name)) {
                *subject = node->payload.variable_definition.name;
                return PACKAGE_STATUS_SHADOWS;
            }
            break;
        case AST_NODE_TYPE_FOR_STATEMENT:
            if (package_names_has(&mangling, node->payload.for_statement.name)) {
                *subject = node->payload.for_statement.name;
                return PACKAGE_STATUS_SHADOWS;
            }
            break;
        default:
            break;
        }
        if (mangling.is_full) {
            return PACKAGE_STATUS_LIMIT;
        }
    }
    return PACKAGE_STATUS_OK;
}
