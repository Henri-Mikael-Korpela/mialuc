#ifndef MIALU_REPORT_H
#define MIALU_REPORT_H

#include <stdbool.h>
#include <stdint.h>

#include <mialu/package.h>

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
} Error_Code;

/**
 * How the errors are reported. `packages` are the files that the program imports, which an error in
 * one of them is told with the path and the lines of, and it is NULL when there are none. `is_color`
 * says whether the standard error shows colors.
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

#endif
