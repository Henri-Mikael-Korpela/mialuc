#ifndef MIALU_PROCESS_H
#define MIALU_PROCESS_H

#include <stdint.h>

#include <mialu/array_slice.h>

// Alphabetical, because nothing depends on the order.
typedef enum {
    // The file for the program could not be made or written in the directory for temporary files.
    PROCESS_STATUS_FILE_FAILED,
    PROCESS_STATUS_OK,
    // The program could not be started: the system refused the file, for example because it is not
    // a program for this machine.
    PROCESS_STATUS_START_FAILED,
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
 */
Process_Result process_run_executable(Array_Slice program);

#endif
