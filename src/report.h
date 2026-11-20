#ifndef MIALU_REPORT_H
#define MIALU_REPORT_H

#include <stdbool.h>
#include <stdint.h>

#include <mialu/array_slice.h>
#include <mialu/package.h>
#include <mialu/parser.h>
#include <mialu/semantic.h>
#include <mialu/x64.h>
#include <mialu/str.h>
#include <mialu/tokenizer.h>
#include <mialu/wasm-text.h>
#include <mialu/wasm.h>
#include <mialu/x64-text.h>

/**
 * The errors of the compiler, all in one place. The stages of the compiler find an error and say
 * what it is with a status, a view of the source and sometimes a number. This module knows what to
 * say about each kind of error, and has `diagnostic.h` print it, so that every error is laid out
 * the same way and the text of every error is in one file, `report.c`.
 *
 * Every kind of error has a number of its own, which is its code, `E` and four digits in the
 * output, and which never changes or is used for anything else, so that a person can search for it,
 * and a tool can tell it. The numbers are in ranges, one for each part of the compiler:
 *
 * ```
 * E0001 - E0099   the command, the files and the system
 * E0101 - E0199   the tokens: comments, indentation, numbers, runes and strings
 * E0201 - E0299   the syntax
 * E0301 - E0399   the files that a program imports
 * E0401 - E0449   names, attributes, labels and the statements that jump
 * E0451 - E0499   what is known when the program is compiled: constants and array lengths
 * E0501 - E0599   what both targets reject: types, calls, structs, matches and unions
 * E0601 - E0699   what the x64 target rejects, and its limits
 * E0701 - E0799   what the wasm target rejects, and its limits
 * E0901 - E0999   the text of a program as WebAssembly text and as assembly
 * ```
 *
 * The enumeration is in the order of the numbers and not alphabetical, because the numbers are what
 * the documentation, `docs/error-codes.md`, is sorted by.
 */
typedef enum {
    ERROR_CODE_SOURCE_UNREADABLE = 1,
    ERROR_CODE_OUTPUT_UNWRITABLE = 2,
    ERROR_CODE_PATH_ABSOLUTE_UNKNOWN = 3,
    ERROR_CODE_OUT_OF_MEMORY = 4,
    ERROR_CODE_ASSEMBLY_WRITE_FAILED = 5,
    ERROR_CODE_PROGRAM_FILE_FAILED = 6,
    ERROR_CODE_PROGRAM_START_FAILED = 7,
    ERROR_CODE_PROGRAM_ARGUMENTS_TOO_LONG = 8,
    ERROR_CODE_RUN_UNSUPPORTED = 9,

    ERROR_CODE_COMMENT_ENCODING_INVALID = 101,
    ERROR_CODE_DOCUMENTATION_COMMENT_TRAILING = 102,
    ERROR_CODE_INDENTATION_TAB = 103,
    ERROR_CODE_INDENTATION_WIDTH_INVALID = 104,
    ERROR_CODE_FLOAT_LITERAL_DIGIT_INVALID = 105,
    ERROR_CODE_FLOAT_LITERAL_SEPARATOR_INVALID = 106,
    ERROR_CODE_FLOAT_LITERAL_TOO_LARGE = 107,
    ERROR_CODE_FLOAT_LITERAL_TOO_LONG = 108,
    ERROR_CODE_INTEGER_LITERAL_DIGIT_INVALID = 109,
    ERROR_CODE_INTEGER_LITERAL_DIGITS_MISSING = 110,
    ERROR_CODE_INTEGER_LITERAL_SEPARATOR_INVALID = 111,
    ERROR_CODE_INTEGER_LITERAL_TOO_LARGE = 112,
    ERROR_CODE_LABEL_INVALID = 113,
    ERROR_CODE_RUNE_LITERAL_EMPTY = 114,
    ERROR_CODE_RUNE_LITERAL_ESCAPE_INVALID = 115,
    ERROR_CODE_RUNE_LITERAL_ENCODING_INVALID = 116,
    ERROR_CODE_RUNE_LITERAL_TOO_LONG = 117,
    ERROR_CODE_RUNE_LITERAL_UNTERMINATED = 118,
    // 119 was a backslash in a string, which is an escape sequence now. A code is never used again.
    ERROR_CODE_STRING_LITERAL_ENCODING_INVALID = 120,
    ERROR_CODE_STRING_LITERAL_UNTERMINATED = 121,
    ERROR_CODE_TOKEN_LIMIT = 122,
    ERROR_CODE_UNEXPECTED_CHARACTER = 123,
    ERROR_CODE_STRING_LITERAL_ESCAPE_INVALID = 124,

    ERROR_CODE_NODE_LIMIT = 201,
    ERROR_CODE_UNEXPECTED_TOKEN = 202,

    ERROR_CODE_IMPORT_CYCLE = 301,
    ERROR_CODE_IMPORT_FILE_UNREADABLE = 302,
    ERROR_CODE_IMPORT_PATH_INVALID = 303,
    ERROR_CODE_IMPORT_LIMIT = 304,
    ERROR_CODE_IMPORT_SHADOWS = 305,
    ERROR_CODE_IMPORT_VENDOR_UNKNOWN = 306,

    ERROR_CODE_DEFINITION_NAME_DUPLICATE = 401,
    ERROR_CODE_IDENTIFIER_UNKNOWN = 402,
    ERROR_CODE_PARAMETER_NAME_DUPLICATE = 403,
    ERROR_CODE_PARAMETER_NAME_SHADOWS = 404,
    ERROR_CODE_VARIABLE_NAME_DUPLICATE = 405,
    ERROR_CODE_VARIABLE_NAME_SHADOWS = 406,
    ERROR_CODE_FIELD_NAME_DUPLICATE = 407,
    ERROR_CODE_ENUM_MEMBER_DUPLICATE = 408,
    ERROR_CODE_ASSIGNMENT_TARGET_NOT_VARIABLE = 409,
    ERROR_CODE_ASSIGNMENT_TARGET_ITERATOR = 410,
    ERROR_CODE_CALL_TARGET_NOT_PROCEDURE = 411,
    ERROR_CODE_ATTRIBUTE_DUPLICATE = 412,
    ERROR_CODE_ATTRIBUTE_UNKNOWN = 413,
    ERROR_CODE_MATCH_ATTRIBUTE_UNKNOWN = 414,
    ERROR_CODE_GLOBAL_NOT_STATIC = 415,
    ERROR_CODE_GLOBAL_VALUE_NOT_CONSTANT = 416,
    ERROR_CODE_FOREIGN_IMPORT_DUPLICATE = 417,
    ERROR_CODE_FOREIGN_IMPORT_MISSING = 418,
    ERROR_CODE_BREAK_OUTSIDE_SCOPE = 419,
    ERROR_CODE_JUMP_OUTSIDE_LOOP = 420,
    ERROR_CODE_JUMP_OUT_OF_DEFER = 421,
    ERROR_CODE_LABEL_DUPLICATE = 422,
    ERROR_CODE_LABEL_NOT_LOOP = 423,
    ERROR_CODE_LABEL_UNKNOWN = 424,
    ERROR_CODE_DEFER_NESTED = 425,
    ERROR_CODE_DEFER_STATEMENT_INVALID = 426,
    ERROR_CODE_RETURN_IN_DEFER = 427,

    ERROR_CODE_CONSTANT_CYCLE = 451,
    ERROR_CODE_CONSTANT_DIVISION_BY_ZERO = 452,
    ERROR_CODE_CONSTANT_NOT_CONSTANT = 453,
    ERROR_CODE_CONSTANT_OPERAND_TYPE_INVALID = 454,
    ERROR_CODE_CONSTANT_OVERFLOW = 455,
    ERROR_CODE_CONSTANT_TEXT_TOO_LONG = 456,
    ERROR_CODE_CONSTANT_TOO_DEEP = 457,
    ERROR_CODE_CONSTANT_TYPE_INVALID = 458,
    ERROR_CODE_CONSTANT_TYPE_MISMATCH = 459,
    ERROR_CODE_CONSTANT_VALUE_NOT_REPRESENTABLE = 460,
    ERROR_CODE_CONCATENATION_NOT_CONSTANT = 461,
    ERROR_CODE_ARRAY_LENGTH_NOT_CONSTANT = 462,
    ERROR_CODE_ARRAY_LENGTH_INVALID = 463,
    ERROR_CODE_INT_TO_STR_NOT_CONSTANT = 464,

    ERROR_CODE_ARITHMETIC_OPERAND_TYPE_INVALID = 501,
    ERROR_CODE_ARRAY_ELEMENT_TYPE_MISMATCH = 502,
    ERROR_CODE_ARRAY_EXPECTED = 503,
    ERROR_CODE_ARRAY_INITIALIZER_INVALID = 504,
    ERROR_CODE_ARRAY_LENGTH_MISMATCH = 505,
    ERROR_CODE_CALL_ARGUMENT_COUNT_MISMATCH = 506,
    ERROR_CODE_CALL_ARGUMENT_TYPE_MISMATCH = 507,
    ERROR_CODE_CALL_RESULT_MISSING = 508,
    ERROR_CODE_CALL_TARGET_NOT_POINTER = 509,
    ERROR_CODE_CAST_INVALID = 510,
    ERROR_CODE_COMPARISON_OPERAND_TYPE_INVALID = 511,
    ERROR_CODE_CONDITION_TYPE_INVALID = 512,
    ERROR_CODE_ENUM_MEMBER_UNKNOWN = 513,
    ERROR_CODE_ENUM_TYPE_UNKNOWN = 514,
    ERROR_CODE_FIELD_TARGET_READ_ONLY = 515,
    ERROR_CODE_IDENTIFIER_NOT_VALUE = 516,
    ERROR_CODE_INDEX_TYPE_INVALID = 517,
    ERROR_CODE_LOGICAL_OPERAND_TYPE_INVALID = 518,
    ERROR_CODE_MATCH_CASE_DUPLICATE = 519,
    ERROR_CODE_MATCH_PARTIAL_INVALID = 520,
    ERROR_CODE_MATCH_PATTERN_NOT_CONSTANT = 521,
    ERROR_CODE_MATCH_PATTERN_TYPE_MISMATCH = 522,
    ERROR_CODE_MATCH_RANGE_EMPTY = 523,
    ERROR_CODE_MATCH_SUBJECT_TYPE_INVALID = 524,
    ERROR_CODE_NIL_TYPE_UNKNOWN = 525,
    ERROR_CODE_POINTER_EXPECTED = 526,
    ERROR_CODE_POINTER_TARGET_TYPE_MISMATCH = 527,
    ERROR_CODE_RANGE_TYPE_INVALID = 528,
    ERROR_CODE_RESULT_COUNT_MISMATCH = 529,
    ERROR_CODE_RETURN_COUNT_MISMATCH = 530,
    ERROR_CODE_RETURN_TYPE_MISMATCH = 531,
    ERROR_CODE_RETURN_TYPE_MISSING = 532,
    ERROR_CODE_STRUCT_FIELD_DUPLICATE = 533,
    ERROR_CODE_STRUCT_FIELD_TYPE_MISMATCH = 534,
    ERROR_CODE_STRUCT_FIELD_UNKNOWN = 535,
    ERROR_CODE_STRUCT_RECURSIVE = 536,
    ERROR_CODE_TYPE_UNSUPPORTED = 537,
    ERROR_CODE_UNION_CASE_UNKNOWN = 538,
    ERROR_CODE_UNION_DISCRIMINANT_INVALID = 539,
    ERROR_CODE_UNION_FIELD_KIND_MISMATCH = 540,
    ERROR_CODE_UNION_FIELD_UNMATCHED = 541,
    ERROR_CODE_UNION_KIND_NOT_CONSTANT = 542,
    ERROR_CODE_VARIABLE_TYPE_MISMATCH = 543,

    ERROR_CODE_X64_ADDRESS_INVALID = 601,
    ERROR_CODE_X64_ARRAY_TYPE_MISPLACED = 602,
    ERROR_CODE_X64_CONSTANT_COUNT_EXCEEDED = 603,
    ERROR_CODE_X64_EXPRESSION_TOO_DEEP = 604,
    ERROR_CODE_X64_FIELD_BASE_INVALID = 605,
    ERROR_CODE_X64_FOREIGN_TARGET_UNSUPPORTED = 606,
    ERROR_CODE_X64_LIBRARY_INVALID = 607,
    ERROR_CODE_X64_MAIN_INVALID = 608,
    ERROR_CODE_X64_MAIN_MISSING = 609,
    ERROR_CODE_X64_MATCH_NOT_EXHAUSTIVE = 610,
    ERROR_CODE_X64_OUTPUT_TOO_LARGE = 611,
    ERROR_CODE_X64_STRUCT_EXPECTED = 612,
    ERROR_CODE_X64_STRUCT_FIELD_MISSING = 613,
    ERROR_CODE_X64_TYPE_INFO_MISPLACED = 614,
    ERROR_CODE_X64_TYPE_INFO_UNSUPPORTED = 615,
    ERROR_CODE_X64_UNSUPPORTED = 616,

    ERROR_CODE_WASM_ADDRESS_INVALID = 701,
    ERROR_CODE_WASM_ARRAY_TYPE_MISPLACED = 702,
    ERROR_CODE_WASM_BUILT_IN_UNSUPPORTED = 703,
    ERROR_CODE_WASM_DEFINITION_NAME_RESERVED = 704,
    ERROR_CODE_WASM_EXPRESSION_TOO_DEEP = 705,
    ERROR_CODE_WASM_FIELD_BASE_INVALID = 706,
    ERROR_CODE_WASM_FOREIGN_TARGET_UNSUPPORTED = 707,
    ERROR_CODE_WASM_MATCH_NOT_EXHAUSTIVE = 708,
    ERROR_CODE_WASM_OUTPUT_TOO_LARGE = 709,
    ERROR_CODE_WASM_STRUCT_EXPECTED = 710,
    ERROR_CODE_WASM_STRUCT_FIELD_MISSING = 711,
    ERROR_CODE_WASM_TYPE_INFO_MISPLACED = 712,
    ERROR_CODE_WASM_TYPE_INFO_UNSUPPORTED = 713,

    ERROR_CODE_WASM_TEXT_MODULE_MALFORMED = 901,
    ERROR_CODE_WASM_TEXT_MODULE_UNSUPPORTED = 902,
    ERROR_CODE_WASM_TEXT_OUTPUT_TOO_LARGE = 903,
    ERROR_CODE_X64_TEXT_EXECUTABLE_MALFORMED = 904,
    ERROR_CODE_X64_TEXT_EXECUTABLE_UNSUPPORTED = 905,
    ERROR_CODE_X64_TEXT_OUTPUT_TOO_LARGE = 906,
} Error_Code;

/**
 * How the errors are reported. `packages` are the files that the program imports, which an error in
 * one of them is told with the path and the lines of, and it is NULL when there are none.
 * `is_color` says whether the standard error shows colors.
 */
typedef struct {
    Packages const *packages;
    bool is_color;
} Reporter;

/**
 * Reports an error that is about a file as a whole, or about no file, and has no place in a source.
 * `path` is the file, or NULL. `number` is what `{number}` stands for in the text of the error, if
 * it has it, such as a limit.
 */
void report_general(Reporter const *reporter, Error_Code code, char const *path, int64_t number);

/**
 * Reports an error of `tokenize` in `source`, the text of the file at `path`, which is the program
 * or a file that it imports.
 */
void report_tokenize_error(Reporter const *reporter, char const *path, Str source,
                           Tokenize_Result result);

/**
 * Reports an error of `parse` in `source`, the text of the file at `path`, which is the program or
 * a file that it imports. `tokens` are the ones that were parsed, which hold `Token` elements.
 */
void report_parse_error(Reporter const *reporter, char const *path, Str source, Array_Slice tokens,
                        Parse_Result result);

/**
 * Reports what is wrong with the files that the program imports, which `result` says. A tokenize or
 * parse error is told as the one of the file that has it, and the others are told with the import
 * that `result` is about. `path` and `source` are the program, whose imports they are.
 */
void report_package_error(Reporter const *reporter, char const *path, Str source,
                          Package_Result result);

/**
 * Reports an error of `semantic_analyze`, in the program whose source is `source` and whose file is
 * `path`, or in a file that it imports.
 */
void report_semantic_error(Reporter const *reporter, char const *path, Str source,
                           Semantic_Result result);

/**
 * Reports an error of `x64_build`, in the program whose source is `source` and whose file is
 * `path`, or in a file that it imports.
 */
void report_x64_error(Reporter const *reporter, char const *path, Str source,
                      X64_Build_Result result);

/**
 * Reports an error of `wasm_build`, in the program whose source is `source` and whose file is
 * `path`, or in a file that it imports.
 */
void report_wasm_error(Reporter const *reporter, char const *path, Str source,
                       Wasm_Build_Result result);

/**
 * Reports an error of `wasm_text_format` for the program in the file `path`. They are bugs of the
 * compiler, except that the text can be too large.
 */
void report_wasm_text_error(Reporter const *reporter, char const *path, Wasm_Text_Result result);

/**
 * Reports an error of `x64_text_format` for the program in the file `path`. They are bugs of the
 * compiler, except that the assembly can be too large.
 */
void report_x64_text_error(Reporter const *reporter, char const *path, X64_Text_Result result);

#endif
