#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <mialu/common_types.h>
#include <mialu/process.h>

#ifdef _WIN32

#include <windows.h>

enum {
    // The longest path that the system functions below take, and room for the suffix that is added.
    PROCESS_PATH_SIZE_BYTES = MAX_PATH + 8,
};

/**
 * Makes the name of a new file in the directory for temporary files, which ends with `.exe`. The
 * system makes a file with a name that no other has and the name is that with the suffix, so that
 * the system does not mistake the program for something else to open. Returns false when the
 * directory or the name could not be had.
 */
static bool process_temporary_path_make(char *path, char *placeholder) {
    char directory[MAX_PATH + 1] = {0};
    DWORD directory_length = GetTempPathA(sizeof(directory), directory);
    if (directory_length == 0 || directory_length > MAX_PATH) {
        return false;
    }
    // The placeholder is the file that the system made, and the program is written next to it.
    if (GetTempFileNameA(directory, "mia", 0, placeholder) == 0) {
        return false;
    }
    size_t placeholder_length = strlen(placeholder);
    if (placeholder_length + sizeof(".exe") > PROCESS_PATH_SIZE_BYTES) {
        return false;
    }
    memcpy(path, placeholder, placeholder_length);
    memcpy(path + placeholder_length, ".exe", sizeof(".exe"));
    return true;
}

/**
 * Writes `program` to a new file at `path`. Returns false when it cannot be written completely.
 */
static bool process_file_write(char const *path, Array_Slice program) {
    // Binary mode so that Windows does not turn the byte 0x0A into a line ending.
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    size_t written = fwrite(program.elements, sizeof(byte), program.length, file);
    bool is_close_error = fclose(file) != 0;
    return written == program.length && is_close_error == false;
}

Process_Result process_run_executable(Array_Slice program) {
    assert(program.elements != NULL);
    assert(program.length > 0);

    Process_Result result = {.exit_code = 0, .status = PROCESS_STATUS_FILE_FAILED};
    char path[PROCESS_PATH_SIZE_BYTES] = {0};
    char placeholder[PROCESS_PATH_SIZE_BYTES] = {0};
    if (process_temporary_path_make(path, placeholder) == false) {
        return result;
    }

    if (process_file_write(path, program)) {
        // The program shares the standard streams of this process, so its output appears where the
        // output of the compiler would, also when it is redirected to a file or a pipe.
        STARTUPINFOA startup = {0};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION process = {0};

        result.status = PROCESS_STATUS_START_FAILED;
        if (CreateProcessA(path, NULL, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process)) {
            DWORD exit_code = 0;
            if (WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0 &&
                GetExitCodeProcess(process.hProcess, &exit_code)) {
                // The exit code is 32 bits, and an `int` by convention; the cast keeps the bits.
                result.exit_code = (int32_t)exit_code;
                result.status = PROCESS_STATUS_OK;
            }
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
    }

    // Failing to delete leaves a file in the temporary directory, which the system cleans up, and
    // there is nothing else to do about it, so the result is not checked.
    (void)DeleteFileA(path);
    (void)DeleteFileA(placeholder);
    return result;
}

#else

Process_Result process_run_executable(Array_Slice program) {
    assert(program.elements != NULL);
    assert(program.length > 0);

    return (Process_Result){.exit_code = 0, .status = PROCESS_STATUS_UNSUPPORTED};
}

#endif
