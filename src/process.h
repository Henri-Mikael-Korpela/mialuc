#ifndef MIALU_H_PROCESS
#define MIALU_H_PROCESS

#include <stdbool.h>
#include <stdint.h>

#include <mialu/array_slice.h>
#include <mialu/str.h>

// Alphabetical, because nothing depends on the order.
typedef enum {
    // The file for the program could not be made or written in the directory for temporary files.
    PROCESS_STATUS_FILE_FAILED,
    PROCESS_STATUS_OK,
    // The program could not be started: the system refused the file, for example because it is not
    // a program for this machine.
    PROCESS_STATUS_START_FAILED,
    // The command line of the program, which is the path of the file and the arguments, is longer
    // than the system takes, which is 32767 characters.
    PROCESS_STATUS_ARGUMENTS_TOO_LONG,
    // Running a program built by the compiler is only done on Windows for now.
    PROCESS_STATUS_UNSUPPORTED,
} Process_Status;

/**
 * `exit_code` is what the program ended with, and is only meaningful on `PROCESS_STATUS_OK`.
 */
typedef struct {
    int32_t exit_code;
    Process_Status status;
} Process_Result;

/**
 * Runs the executable file in `program`, which holds `byte` elements, and waits for it to end. The
 * file is written to the directory for temporary files, run with the standard input, output and
 * error of this process, and deleted afterwards. Nothing of the program is kept in this process, so
 * whatever it does can only harm itself and the system, not the compiler.
 *
 * The program is called as if it had been given `argument_count` arguments, the C strings of
 * `arguments`, after its name: its command line is the path of the file and the arguments, each one
 * quoted where the program needs it to read it back as the one argument, as the runtime of C does
 * for `argv`.
 */
Process_Result process_run_executable(Array_Slice program, char const *const *arguments,
                                      size_t argument_count);

/**
 * Returns the absolute path of the file at `path`, as the system resolves it against the current
 * directory, written to `buffer`, which holds `char` elements. A debugger compares the path of a
 * source file in the debug information with the paths of the files that a person opens, so it is
 * absolute, and does not depend on the directory that the compiler was run in. Returns an empty
 * `Str` (`bytes == NULL`) when the system cannot resolve it or it does not fit. On a system other
 * than Windows it returns `path` itself, because the C library has no function for it, and the
 * programs that have debug information are for Windows.
 */
Str process_path_absolute(char const *path, Array_Slice buffer);

/**
 * Returns the directory that the file of the running program, the compiler, is in, written to
 * `buffer`, which holds `char` elements, without a separator at its end. Returns an empty `Str`
 * (`bytes == NULL`) when the system cannot tell or it does not fit, and always on a system other
 * than Windows.
 */
Str process_executable_directory(Array_Slice buffer);

/**
 * Returns whether the standard error is a terminal that shows colors, so that the errors of the
 * compiler can have them. It is false when the standard error is a file or a pipe, which is where
 * the tools that read the errors get them, and when the environment variable `NO_COLOR` is set to
 * something, which is how a person asks for none. On Windows it also turns on the escape sequences
 * in the console, which it does not show by itself.
 */
bool process_standard_error_has_color(void);

#endif // MIALU_H_PROCESS
