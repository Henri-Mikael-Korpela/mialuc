#include <assert.h>
#include <inttypes.h>
#include <iso646.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <mialu/common_types.h>
#include <mialu/diagnostic.h>
#include <mialu/report.h>
#include <mialu/str.h>

enum {
    // The codes are numbers below this, and the catalog has a place for each of them, so that the
    // text of an error is found by its code without a search. The numbers that are not used are
    // empty places.
    REPORT_CODE_COUNT = 1000,
    // The most bytes of the name, or of whatever else the error is about, that go into its text. A
    // longer one is cut and ends with `...`, so that an error is a few lines, whatever the program
    // has.
    REPORT_SUBJECT_LENGTH_MAX = 80,
    // The most bytes of each part of an error after the text of the catalog is filled in.
    REPORT_PART_SIZE_BYTES = 2048,
};

/**
 * The text of one kind of error, as templates. A part that the error has no use for is NULL. In a
 * template `{subject}` is the name, or the piece of the program, that the error is about, `{detail}`
 * is another piece of text, `{number}` is a number, such as a limit, and `{hex}` is the same number
 * in two hexadecimal digits.
 *
 * The title says what is wrong, in a sentence that names the thing, without a full stop. The label
 * is for the place that the carets point at, and says what is wrong there, briefly. The note says
 * why, which is the rule of the language, and the help says what to do. A part that would only say
 * again what another one says is left out.
 */
typedef struct {
    char const *title;
    char const *label;
    char const *note;
    char const *help;
} Report_Entry;

/**
 * Everything that the text of one error is made of.
 */
typedef struct {
    // The file, or NULL for an error that is not about a file.
    char const *path;
    Str source;
    Str subject;
    Str detail;
    size_t span_offset;
    size_t span_length;
    int64_t number;
    Error_Code code;
    bool has_span;
} Report_Error;

/**
 * A text that is being written to a buffer that has room for `capacity` bytes.
 */
typedef struct {
    char *bytes;
    size_t capacity;
    size_t length;
} Report_Text;

// The catalog is sparse, and a code has the place of its number, which `-Woverride-init`, part of
// `-Wextra`, turns into an error when two codes have the same number.
static Report_Entry const REPORT_ENTRIES[REPORT_CODE_COUNT] = {
    [ERROR_CODE_SOURCE_UNREADABLE] = {
        .title = "cannot read the source file",
        .note = "the file does not exist or cannot be opened, or it is larger than {number} bytes, "
                "which is the most that the compiler reads",
        .help = "check the path of the file",
    },
    [ERROR_CODE_OUTPUT_UNWRITABLE] = {
        .title = "cannot write the output file",
        .help = "check that the directory of the file exists, and that the file is not open in "
                "another program",
    },
    [ERROR_CODE_PATH_ABSOLUTE_UNKNOWN] = {
        .title = "cannot find the absolute path of the source file",
        .note = "debug information names the source file by its absolute path, which is how a "
                "debugger finds it whatever directory it runs in",
    },
    [ERROR_CODE_OUT_OF_MEMORY] = {
        .title = "out of memory",
        .note = "the compiler allocates what it needs once, when it starts, and the system did not "
                "give it that",
    },
    [ERROR_CODE_ASSEMBLY_WRITE_FAILED] = {
        .title = "cannot write the assembly to the standard output",
    },
    [ERROR_CODE_PROGRAM_FILE_FAILED] = {
        .title = "cannot write the program to the directory for temporary files",
        .note = "a program is written to a file there to run it",
    },
    [ERROR_CODE_PROGRAM_START_FAILED] = {
        .title = "the system cannot start the program",
        .note = "the system refused the file, for example because it is not a program for this "
                "machine",
    },
    [ERROR_CODE_PROGRAM_ARGUMENTS_TOO_LONG] = {
        .title = "the arguments of the program are longer than the system takes",
        .note = "the command line of a program is at most 32767 characters, with the name of the "
                "program",
    },
    [ERROR_CODE_RUN_UNSUPPORTED] = {
        .title = "running a program is only supported on Windows",
        .help = "build the wasm target to run a program elsewhere, see docs/playground.md",
    },
};

static void report_emit(Reporter const *reporter, Report_Error const *error);
static Str  report_expand(char const *template, Report_Error const *error, char *buffer,
                          size_t buffer_size_bytes);
static void report_text_append(Report_Text *text, Str part);
static void report_text_append_subject(Report_Text *text, Str subject);

void report_general(Reporter const *reporter, Error_Code code, char const *path,
                    int64_t number) {
    assert(reporter != NULL);
    assert(code > 0);

    Report_Error error = {
        .path = path,
        .source = STR_EMPTY,
        .subject = STR_EMPTY,
        .detail = STR_EMPTY,
        .span_offset = 0,
        .span_length = 0,
        .number = number,
        .code = code,
        .has_span = false,
    };
    report_emit(reporter, &error);
}

/**
 * Fills in the text of the catalog for `error` and prints it.
 */
static void report_emit(Reporter const *reporter, Report_Error const *error) {
    assert(reporter != NULL);
    assert(error != NULL);
    assert(error->code > 0);
    assert((size_t)error->code < REPORT_CODE_COUNT);

    Report_Entry const *entry = &REPORT_ENTRIES[error->code];
    // A code without text has been written in the enumeration and not in the catalog.
    assert(entry->title != NULL);

    char title[REPORT_PART_SIZE_BYTES] = {0};
    char label[REPORT_PART_SIZE_BYTES] = {0};
    char note[REPORT_PART_SIZE_BYTES] = {0};
    char help[REPORT_PART_SIZE_BYTES] = {0};
    Diagnostic diagnostic = {
        .path = STR_EMPTY,
        .source = error->source,
        .title = report_expand(entry->title, error, title, sizeof(title)),
        .label = report_expand(entry->label, error, label, sizeof(label)),
        .note = report_expand(entry->note, error, note, sizeof(note)),
        .help = report_expand(entry->help, error, help, sizeof(help)),
        .span_offset = error->span_offset,
        .span_length = error->span_length,
        .code = (uint32_t)error->code,
        .has_span = error->has_span,
    };
    if (error->path != NULL) {
        diagnostic.path = str_make(error->path, strlen(error->path));
    }
    diagnostic_print(stderr, &diagnostic, reporter->is_color);
}

/**
 * Returns `template` with its placeholders replaced, written to `buffer`, or an empty `Str`
 * (`bytes == NULL`) for a template that is NULL.
 */
static Str report_expand(char const *template, Report_Error const *error, char *buffer,
                         size_t buffer_size_bytes) {
    assert(error != NULL);
    assert(buffer != NULL);

    if (template == NULL) {
        return STR_EMPTY;
    }

    Report_Text text = {.bytes = buffer, .capacity = buffer_size_bytes, .length = 0};
    size_t index = 0;
    while (template[index] != '\0') {
        if (template[index] != '{') {
            report_text_append(&text, str_make(&template[index], 1));
            index += 1;
            continue;
        }
        char const *rest = &template[index];
        char number_text[32] = {0};
        if (strncmp(rest, "{subject}", sizeof("{subject}") - 1) == 0) {
            report_text_append_subject(&text, error->subject);
            index += sizeof("{subject}") - 1;
        } else if (strncmp(rest, "{detail}", sizeof("{detail}") - 1) == 0) {
            report_text_append_subject(&text, error->detail);
            index += sizeof("{detail}") - 1;
        } else if (strncmp(rest, "{number}", sizeof("{number}") - 1) == 0) {
            int length = snprintf(number_text, sizeof(number_text), "%" PRId64, error->number);
            assert(length > 0);
            report_text_append(&text, str_make(number_text, (size_t)length));
            index += sizeof("{number}") - 1;
        } else if (strncmp(rest, "{hex}", sizeof("{hex}") - 1) == 0) {
            int length = snprintf(number_text, sizeof(number_text), "%02" PRIx64,
                                  (uint64_t)error->number);
            assert(length > 0);
            report_text_append(&text, str_make(number_text, (size_t)length));
            index += sizeof("{hex}") - 1;
        } else {
            // A brace that is not a placeholder is a mistake in the catalog.
            assert(false);
            index += 1;
        }
    }
    assert(text.length < text.capacity);

    text.bytes[text.length] = '\0';
    return str_make(text.bytes, text.length);
}

/**
 * Appends `part` to `text`, as much of it as fits, keeping a byte for the end.
 */
static void report_text_append(Report_Text *text, Str part) {
    assert(text != NULL);
    assert(text->length < text->capacity);

    size_t room = text->capacity - 1 - text->length;
    size_t count = part.length;
    if (count > room) {
        count = room;
    }
    if (count > 0) {
        memcpy(text->bytes + text->length, part.bytes, count);
    }
    text->length += count;
}

/**
 * Appends what of `subject` is shown in the text of an error to `text`: its first line, and at most
 * `REPORT_SUBJECT_LENGTH_MAX` bytes of it, cut where a character starts. A subject that is cut ends
 * with `...`, so that a reader knows there is more.
 */
static void report_text_append_subject(Report_Text *text, Str subject) {
    assert(text != NULL);
    assert(subject.bytes != NULL || subject.length == 0);

    size_t length = 0;
    while (length < subject.length && length < REPORT_SUBJECT_LENGTH_MAX &&
           subject.bytes[length] != '\n' && subject.bytes[length] != '\r') {
        length += 1;
    }
    // A byte that continues a character, 0b10xxxxxx, is not a place to cut.
    while (length > 0 && length < subject.length &&
           (((byte)subject.bytes[length]) & 0xC0) == 0x80) {
        length -= 1;
    }
    report_text_append(text, str_make(subject.bytes, length));
    if (length < subject.length) {
        report_text_append(text, STR("..."));
    }
}
