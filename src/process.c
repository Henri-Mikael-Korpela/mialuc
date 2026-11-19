#include <assert.h>
#include <iso646.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <mialu/common_types.h>
#include <mialu/process.h>
#include <mialu/str.h>

#ifdef _WIN32

#include <windows.h>

enum {
    // The longest path that the system functions below take, and room for the suffix that is added.
    PROCESS_PATH_SIZE_BYTES = MAX_PATH + 8,
    // The longest command line that `CreateProcess` takes is 32767 characters and the zero after them.
    PROCESS_COMMAND_LINE_SIZE_BYTES = 32768,
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
    return written == program.length && not is_close_error;
}

/**
 * Appends `text` to the command line `line`, which has room for `capacity` characters with the zero
 * after them, at `*length`, quoted so that the runtime of C reads it back as one argument: it is
 * between double quotes when it is empty or has a space, a tab or a double quote, a double quote in
 * it has a backslash before it, and so do the backslashes before a double quote or before the quote
 * that ends it, doubled. A space separates it from what is before it, unless it is the first.
 * Returns false when it does not fit.
 */
static bool process_argument_append(char *line, size_t capacity, size_t *length, char const *text) {
    bool is_quoted = text[0] == '\0';
    for (size_t i = 0; text[i] != '\0'; i += 1) {
        is_quoted = is_quoted || text[i] == ' ' || text[i] == '\t' || text[i] == '"';
    }

    size_t needed = 0;
    size_t backslashes = 0;
    for (size_t i = 0; text[i] != '\0'; i += 1) {
        if (text[i] == '\\') {
            backslashes += 1;
            needed += 1;
        } else if (text[i] == '"') {
            // The backslashes before a double quote are doubled, and the quote has one of its own.
            needed += backslashes + 2;
            backslashes = 0;
        } else {
            backslashes = 0;
            needed += 1;
        }
    }
    if (is_quoted) {
        needed += 2 + backslashes;
    }
    size_t separator = *length > 0 ? 1 : 0;
    if (needed + separator > capacity - *length) {
        return false;
    }

    if (separator == 1) {
        line[*length] = ' ';
        *length += 1;
    }
    if (is_quoted) {
        line[*length] = '"';
        *length += 1;
    }
    backslashes = 0;
    for (size_t i = 0; text[i] != '\0'; i += 1) {
        if (text[i] == '\\') {
            backslashes += 1;
        } else if (text[i] == '"') {
            for (size_t j = 0; j < backslashes; j += 1) {
                line[*length] = '\\';
                *length += 1;
            }
            line[*length] = '\\';
            *length += 1;
            backslashes = 0;
        } else {
            backslashes = 0;
        }
        line[*length] = text[i];
        *length += 1;
    }
    if (is_quoted) {
        for (size_t j = 0; j < backslashes; j += 1) {
            line[*length] = '\\';
            *length += 1;
        }
        line[*length] = '"';
        *length += 1;
    }
    line[*length] = '\0';
    return true;
}

Process_Result process_run_executable(Array_Slice program, char const *const *arguments,
                                      size_t argument_count) {
    assert(program.elements != NULL);
    assert(program.length > 0);
    assert(arguments != NULL || argument_count == 0);

    Process_Result result = {.exit_code = 0, .status = PROCESS_STATUS_FILE_FAILED};
    char path[PROCESS_PATH_SIZE_BYTES] = {0};
    char placeholder[PROCESS_PATH_SIZE_BYTES] = {0};
    if (not process_temporary_path_make(path, placeholder)) {
        return result;
    }

    // The command line is the name of the program and the arguments. The system takes at most 32767
    // characters, and writes to the memory of what it is given.
    static char command_line[PROCESS_COMMAND_LINE_SIZE_BYTES];
    size_t command_line_length = 0;
    command_line[0] = '\0';
    bool is_command_line_made =
        process_argument_append(command_line, sizeof(command_line) - 1, &command_line_length, path);
    for (size_t i = 0; i < argument_count && is_command_line_made; i += 1) {
        is_command_line_made = process_argument_append(command_line, sizeof(command_line) - 1,
                                                       &command_line_length, arguments[i]);
    }
    if (not is_command_line_made) {
        result.status = PROCESS_STATUS_ARGUMENTS_TOO_LONG;
        (void)DeleteFileA(placeholder);
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
        if (CreateProcessA(path, command_line, NULL, NULL, TRUE, 0, NULL, NULL, &startup,
                           &process)) {
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

Str process_path_absolute(char const *path, Array_Slice buffer) {
    assert(path != NULL);
    assert(buffer.elements != NULL);

    // The size that the function takes is a `DWORD`, and a path is far shorter than one.
    DWORD capacity = (DWORD)buffer.length;
    if (buffer.length > MAXDWORD) {
        capacity = MAXDWORD;
    }
    // The length does not count the zero after the path. A path that does not fit gives the size
    // that it needs instead, which counts the zero, so it is not below the capacity.
    DWORD length = GetFullPathNameA(path, capacity, buffer.elements, NULL);
    if (length == 0 || length >= capacity) {
        return STR_EMPTY;
    }
    return str_make(buffer.elements, length);
}

Str process_executable_directory(Array_Slice buffer) {
    assert(buffer.elements != NULL);

    DWORD capacity = (DWORD)buffer.length;
    if (buffer.length > MAXDWORD) {
        capacity = MAXDWORD;
    }
    // The length does not count the zero. A path that does not fit is cut, and the length is then
    // the capacity.
    DWORD length = GetModuleFileNameA(NULL, buffer.elements, capacity);
    if (length == 0 || length >= capacity) {
        return STR_EMPTY;
    }
    char *bytes = buffer.elements;
    while (length > 0 && bytes[length - 1] != '\\' &&bytes[length - 1] != '/') {
        length -= 1;
    }
    // Without the separator, so that the directory can be written like any other.
    if (length > 0) {
        length -= 1;
    }
    return str_make(bytes, length);
}

#else

Str process_executable_directory(Array_Slice buffer) {
    assert(buffer.elements != NULL);

    return STR_EMPTY;
}

Process_Result process_run_executable(Array_Slice program, char const *const *arguments,
                                      size_t argument_count) {
    assert(program.elements != NULL);
    assert(program.length > 0);
    (void)arguments;
    (void)argument_count;

    return (Process_Result){.exit_code = 0, .status = PROCESS_STATUS_UNSUPPORTED};
}

Str process_path_absolute(char const *path, Array_Slice buffer) {
    assert(path != NULL);
    assert(buffer.elements != NULL);

    return str_make(path, strlen(path));
}

#endif
