#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <mialu/array_slice.h>
#include <mialu/ast.h>
#include <mialu/common_types.h>
#include <mialu/parser.h>
#include <mialu/str.h>
#include <mialu/tokenizer.h>

enum {
    // Larger sources are rejected. The tokenizer works on one buffer that is allocated once at
    // startup, so a bound keeps memory use predictable.
    SOURCE_SIZE_BYTES_MAX = 1024 * 1024,
    // Bounds the token buffer, which is allocated once at startup. A token is at least one byte,
    // so this is far more tokens than a source of the maximum size is likely to contain.
    TOKEN_COUNT_MAX = 64 * 1024,
};

/**
 * Reads the file given on the command line, tokenizes it and prints the tokens. Everything is in
 * `main` on purpose, so that the buffers are freed in one place on every path.
 */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <source file>\n", argv[0]);
        return 2;
    }

    char const *path = argv[1];

    // Allocated once, here, because the buffers are too large for the default Windows stack.
    Array_Slice source_buffer = {
        .elements = malloc(SOURCE_SIZE_BYTES_MAX * sizeof(byte)),
        .length = SOURCE_SIZE_BYTES_MAX,
    };
    Array_Slice token_buffer = {
        .elements = malloc(TOKEN_COUNT_MAX * sizeof(Token)),
        .length = TOKEN_COUNT_MAX,
    };
    // A node needs at least one token, so as many nodes as tokens always suffice.
    Array_Slice node_buffer = {
        .elements = malloc(TOKEN_COUNT_MAX * sizeof(Ast_Node)),
        .length = TOKEN_COUNT_MAX,
    };
    if (source_buffer.elements == NULL || token_buffer.elements == NULL ||
        node_buffer.elements == NULL) {
        fprintf(stderr, "Out of memory.\n");
        free(node_buffer.elements);
        free(token_buffer.elements);
        free(source_buffer.elements);
        return 1;
    }

    // Binary mode so that Windows does not translate line endings and shift offsets.
    FILE *file = fopen(path, "rb");

    // Stays empty (`bytes == NULL`) when the file cannot be opened or read, or does not fit in the
    // buffer. An empty file gives a non-NULL `Str` of length zero.
    Str source = STR_EMPTY;
    if (file != NULL) {
        size_t size_bytes = fread(source_buffer.elements, sizeof(byte), source_buffer.length, file);
        bool is_read_error = ferror(file) != 0;
        // Reading one more byte after a full buffer tells "exactly full" apart from "too large".
        bool is_at_end = fgetc(file) == EOF;
        bool is_close_error = fclose(file) != 0;

        if (is_read_error == false && is_close_error == false && is_at_end) {
            assert(size_bytes <= source_buffer.length);
            source = str_make(source_buffer.elements, size_bytes);
        }
    }

    int32_t exit_code = 1;
    if (source.bytes == NULL) {
        fprintf(stderr, "%s: cannot read file, or file is larger than %d bytes.\n", path,
                SOURCE_SIZE_BYTES_MAX);
    } else {
        Tokenize_Result result = tokenize(source, token_buffer);

        if (result.status == TOKENIZE_STATUS_OK) {
            assert(result.tokens.length <= TOKEN_COUNT_MAX);

            Token const *token_elements = result.tokens.elements;
            for (size_t i = 0; i < result.tokens.length; i += 1) {
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
                    printf("IDENTIFIER %.*s\n",
                        (int)token.payload.identifier.text.length,
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

            Parse_Result parse_result = parse(result.tokens, node_buffer);
            if (parse_result.status == PARSE_STATUS_OK) {
                printf("\n");
                ast_print(parse_result.nodes, parse_result.root);
                exit_code = 0;
            } else {
                assert(parse_result.error_token_index < result.tokens.length);

                size_t error_offset = token_elements[parse_result.error_token_index].source_offset;
                Str_Position position = str_position_at(source, error_offset);

                switch (parse_result.status) {
                case PARSE_STATUS_OK:
                    assert(false);
                    break;
                case PARSE_STATUS_NODE_LIMIT:
                    fprintf(stderr, "%s:%zu:%zu: more than %d syntax tree nodes.\n", path,
                            position.line, position.column, TOKEN_COUNT_MAX);
                    break;
                case PARSE_STATUS_UNEXPECTED_TOKEN:
                    fprintf(stderr, "%s:%zu:%zu: expected %.*s.\n", path, position.line,
                            position.column, (int)parse_result.expected.length,
                            parse_result.expected.bytes);
                    break;
                }
            }
        } else {
            assert(result.error_offset < source.length);

            // Line and column are only needed for the message, so they are computed here rather
            // than tracked for every byte while tokenizing.
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
                fprintf(stderr, "%s:%zu:%zu: indentation must be a multiple of 4 spaces.\n", path,
                        line, column);
                break;
            case TOKENIZE_STATUS_INTEGER_LITERAL_TOO_LARGE:
                fprintf(stderr, "%s:%zu:%zu: integer literal does not fit in 64 bits.\n", path,
                        line, column);
                break;
            case TOKENIZE_STATUS_TOKEN_LIMIT:
                fprintf(stderr, "%s:%zu:%zu: more than %d tokens.\n", path, line, column,
                        TOKEN_COUNT_MAX);
                break;
            case TOKENIZE_STATUS_UNEXPECTED_CHARACTER: {
                char character = source.bytes[result.error_offset];
                // The cast is needed because `%x` takes an `unsigned`, and a plain `char` may be
                // signed.
                fprintf(stderr, "%s:%zu:%zu: unexpected character '%c' (0x%02x).\n", path, line,
                        column, character, (unsigned)(uint8_t)character);
                break;
            }
            }
        }
    }

    free(node_buffer.elements);
    free(token_buffer.elements);
    free(source_buffer.elements);
    return exit_code;
}
