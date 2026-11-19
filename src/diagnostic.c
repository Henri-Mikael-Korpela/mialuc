#include <assert.h>
#include <inttypes.h>
#include <iso646.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <mialu/common_types.h>
#include <mialu/diagnostic.h>
#include <mialu/str.h>

// The escape sequences of the colors, which are the ones that Rust uses for its errors: bold red
// for the error itself, and everything that points at it, bold blue for the frame around the source
// and bold cyan for the help. They are the standard ones of ANSI terminals, which VS Code and the
// console of Windows 10 show.
static char const DIAGNOSTIC_STYLE_BOLD[] = "\x1b[1m";
static char const DIAGNOSTIC_STYLE_ERROR[] = "\x1b[1;31m";
static char const DIAGNOSTIC_STYLE_FRAME[] = "\x1b[1;34m";
static char const DIAGNOSTIC_STYLE_HELP[] = "\x1b[1;36m";
static char const DIAGNOSTIC_STYLE_RESET[] = "\x1b[0m";

enum {
    // The notes and helps are broken into lines of at most this many characters, so that they can
    // be read in a terminal of the usual width without the terminal breaking them in the middle of
    // a word. A word that is longer than the line stays on a line of its own.
    DIAGNOSTIC_LINE_WIDTH = 100,
    // The width of the gutter, which is the width of the line number, when there is no line
    // number, as in an error about a file as a whole: it has room for the `=` of a note.
    DIAGNOSTIC_GUTTER_WIDTH_MIN = 1,
    // The most characters of a line of the source that are shown. A longer line shows this many
    // around the span, and the span is `DIAGNOSTIC_SOURCE_LEAD` characters from the start of them
    // when there is text before it.
    DIAGNOSTIC_SOURCE_WIDTH_MAX = 100,
    DIAGNOSTIC_SOURCE_LEAD = 30,
};

/**
 * Where the line that has a byte of the source starts and ends, as offsets in the source. The end
 * is the offset of the line break, or of the end of the source, and a carriage return before the
 * line break is not part of the line.
 */
typedef struct {
    size_t start;
    size_t end;
} Diagnostic_Line;

static size_t          diagnostic_character_count   (Str text);
static size_t          diagnostic_digit_count       (size_t number);
static Diagnostic_Line diagnostic_line_at           (Str source, size_t offset);
static void            diagnostic_print_gutter      (FILE *output, size_t width, bool is_color);
static void            diagnostic_print_hint        (FILE *output, size_t width, char const *kind,
                                                     char const *style, Str text, bool is_color);
static void            diagnostic_print_header      (FILE *output, Diagnostic const *diagnostic,
                                                     bool is_color);
static void            diagnostic_print_location    (FILE *output, Diagnostic const *diagnostic,
                                                     size_t width, Str_Position position,
                                                     bool is_color);
static void            diagnostic_print_snippet     (FILE *output, Diagnostic const *diagnostic,
                                                     size_t width, Str_Position position,
                                                     bool is_color);
static void            diagnostic_print_wrapped     (FILE *output, Str text, size_t indent_width);
static void            diagnostic_style_begin       (FILE *output, char const *style, bool is_color);
static void            diagnostic_style_end         (FILE *output, bool is_color);

void diagnostic_print(FILE *output, Diagnostic const *diagnostic, bool is_color) {
    assert(output != NULL);
    assert(diagnostic != NULL);
    assert(diagnostic->code > 0);
    assert(diagnostic->title.bytes != NULL);
    assert(not diagnostic->has_span || diagnostic->path.bytes != NULL);
    assert(not diagnostic->has_span || diagnostic->span_offset <= diagnostic->source.length);

    Str_Position position = {.line = 0, .column = 0};
    size_t width = DIAGNOSTIC_GUTTER_WIDTH_MIN;
    if (diagnostic->has_span) {
        position = str_position_at(diagnostic->source, diagnostic->span_offset);
        width = diagnostic_digit_count(position.line);
    }

    diagnostic_print_header(output, diagnostic, is_color);
    if (diagnostic->path.bytes != NULL) {
        diagnostic_print_location(output, diagnostic, width, position, is_color);
    }
    if (diagnostic->has_span) {
        diagnostic_print_snippet(output, diagnostic, width, position, is_color);
    }

    // A blank line of the gutter separates what points at the source from what is said about it.
    bool has_hint = diagnostic->note.bytes != NULL || diagnostic->help.bytes != NULL;
    if (has_hint) {
        diagnostic_print_gutter(output, width, is_color);
        fputc('\n', output);
    }
    if (diagnostic->note.bytes != NULL) {
        diagnostic_print_hint(output, width, "note", DIAGNOSTIC_STYLE_BOLD, diagnostic->note,
                              is_color);
    }
    if (diagnostic->help.bytes != NULL) {
        diagnostic_print_hint(output, width, "help", DIAGNOSTIC_STYLE_HELP, diagnostic->help,
                              is_color);
    }
}

/**
 * Prints the first line: `error[E0101]: title`.
 */
static void diagnostic_print_header(FILE *output, Diagnostic const *diagnostic, bool is_color) {
    diagnostic_style_begin(output, DIAGNOSTIC_STYLE_ERROR, is_color);
    fprintf(output, "error[E%04" PRIu32 "]", diagnostic->code);
    diagnostic_style_end(output, is_color);
    diagnostic_style_begin(output, DIAGNOSTIC_STYLE_BOLD, is_color);
    // The cast is needed because `%.*s` takes an `int` precision.
    fprintf(output, ": %.*s", (int)diagnostic->title.length, diagnostic->title.bytes);
    diagnostic_style_end(output, is_color);
    fputc('\n', output);
}

/**
 * Prints the line with the arrow and the place, such as ` --> src/main.mu:3:14`. The place is the
 * last thing on the line, with nothing after it, which is what a terminal needs to take it as a
 * link.
 */
static void diagnostic_print_location(FILE *output, Diagnostic const *diagnostic, size_t width,
                                      Str_Position position, bool is_color) {
    for (size_t i = 0; i < width; i += 1) {
        fputc(' ', output);
    }
    diagnostic_style_begin(output, DIAGNOSTIC_STYLE_FRAME, is_color);
    fputs("-->", output);
    diagnostic_style_end(output, is_color);
    fprintf(output, " %.*s", (int)diagnostic->path.length, diagnostic->path.bytes);
    if (diagnostic->has_span) {
        fprintf(output, ":%zu:%zu", position.line, position.column);
    }
    fputc('\n', output);
}

/**
 * Prints the line of the source that has the start of the span, and under it the carets that
 * underline the span, with the label after them.
 */
static void diagnostic_print_snippet(FILE *output, Diagnostic const *diagnostic, size_t width,
                                     Str_Position position, bool is_color) {
    Str source = diagnostic->source;
    Diagnostic_Line line = diagnostic_line_at(source, diagnostic->span_offset);
    assert(line.start <= diagnostic->span_offset);
    assert(diagnostic->span_offset <= line.end + 1);

    diagnostic_print_gutter(output, width, is_color);
    fputc('\n', output);

    // The carets start under the first byte of the span, and run to the end of the span or of the
    // line, whichever is first. A span at the end of a line, or an empty one, has one caret.
    size_t span_end = diagnostic->span_offset + diagnostic->span_length;
    if (span_end > line.end) {
        span_end = line.end;
    }
    size_t offset_start = diagnostic->span_offset;
    if (offset_start > line.end) {
        offset_start = line.end;
    }
    size_t indent_count = diagnostic_character_count(
        str_make(source.bytes + line.start, offset_start - line.start));
    size_t caret_count = 1;
    if (span_end > offset_start) {
        caret_count = diagnostic_character_count(
            str_make(source.bytes + offset_start, span_end - offset_start));
    }

    // A line that is wider than a screen shows the part of it around the span, with `...` where the
    // rest is, because a line that wraps would take the carets away from what they point at.
    size_t line_count = diagnostic_character_count(
        str_make(source.bytes + line.start, line.end - line.start));
    size_t window_start = 0;
    size_t window_end = line_count;
    if (line_count > DIAGNOSTIC_SOURCE_WIDTH_MAX) {
        if (indent_count > DIAGNOSTIC_SOURCE_LEAD) {
            window_start = indent_count - DIAGNOSTIC_SOURCE_LEAD;
        }
        if (window_start + DIAGNOSTIC_SOURCE_WIDTH_MAX > line_count) {
            window_start = line_count - DIAGNOSTIC_SOURCE_WIDTH_MAX;
        }
        window_end = window_start + DIAGNOSTIC_SOURCE_WIDTH_MAX;
    }
    bool has_prefix = window_start > 0;
    bool has_suffix = window_end < line_count;

    // The line number, right-aligned in the width of the gutter, which is that of this number.
    diagnostic_style_begin(output, DIAGNOSTIC_STYLE_FRAME, is_color);
    fprintf(output, "%*zu |", (int)width, position.line);
    diagnostic_style_end(output, is_color);
    fputc(' ', output);
    if (has_prefix) {
        fputs("...", output);
    }
    // A tab is one space, so that a character is one column, which the carets rely on.
    size_t character_index = 0;
    size_t character_current = 0;
    for (size_t i = line.start; i < line.end; i += 1) {
        char character = source.bytes[i];
        // The bytes that continue a character are 0b10xxxxxx, and belong to the one before them.
        if (((byte)character & 0xC0) != 0x80) {
            character_current = character_index;
            character_index += 1;
        }
        if (character_current >= window_start && character_current < window_end) {
            if (character == '\t') {
                character = ' ';
            }
            fputc(character, output);
        }
    }
    if (has_suffix) {
        fputs("...", output);
    }
    fputc('\n', output);

    // The carets are under the span, which may be partly or wholly out of the part that is shown.
    size_t caret_indent = indent_count - window_start;
    if (has_prefix) {
        caret_indent += sizeof("...") - 1;
    }
    if (indent_count + caret_count > window_end) {
        caret_count = window_end - indent_count;
        if (indent_count >= window_end) {
            caret_count = 1;
        }
    }
    diagnostic_print_gutter(output, width, is_color);
    fputc(' ', output);
    for (size_t i = 0; i < caret_indent; i += 1) {
        fputc(' ', output);
    }
    diagnostic_style_begin(output, DIAGNOSTIC_STYLE_ERROR, is_color);
    for (size_t i = 0; i < caret_count; i += 1) {
        fputc('^', output);
    }
    if (diagnostic->label.bytes != NULL) {
        fprintf(output, " %.*s", (int)diagnostic->label.length, diagnostic->label.bytes);
    }
    diagnostic_style_end(output, is_color);
    fputc('\n', output);
}

/**
 * Prints a line of the form `  = note: text`, which is broken into lines if the text is long, so
 * that the lines after the first are under the text.
 */
static void diagnostic_print_hint(FILE *output, size_t width, char const *kind,
                                  char const *style, Str text, bool is_color) {
    assert(text.bytes != NULL);

    for (size_t i = 0; i < width; i += 1) {
        fputc(' ', output);
    }
    fputc(' ', output);
    diagnostic_style_begin(output, DIAGNOSTIC_STYLE_FRAME, is_color);
    fputc('=', output);
    diagnostic_style_end(output, is_color);
    fputc(' ', output);
    diagnostic_style_begin(output, style, is_color);
    fprintf(output, "%s:", kind);
    diagnostic_style_end(output, is_color);
    fputc(' ', output);

    // The text starts after the gutter, a space, the `=`, a space, the kind, a colon and a space.
    size_t kind_width = 0;
    while (kind[kind_width] != '\0') {
        kind_width += 1;
    }
    diagnostic_print_wrapped(output, text, width + 1 + 1 + 1 + kind_width + 1 + 1);
    fputc('\n', output);
}

/**
 * Prints `text` as words that are separated by one space, going to a new line, which starts with
 * `indent_width` spaces, when the next word would go past the line width. The cursor is at the
 * indent already when this is called.
 */
static void diagnostic_print_wrapped(FILE *output, Str text, size_t indent_width) {
    size_t column = indent_width;
    size_t index = 0;
    bool is_line_start = true;
    while (index < text.length) {
        while (index < text.length && text.bytes[index] == ' ') {
            index += 1;
        }
        size_t word_start = index;
        while (index < text.length && text.bytes[index] != ' ') {
            index += 1;
        }
        if (index == word_start) {
            break;
        }
        Str word = str_make(text.bytes + word_start, index - word_start);
        size_t word_width = diagnostic_character_count(word);
        if (not is_line_start) {
            if (column + 1 + word_width > DIAGNOSTIC_LINE_WIDTH) {
                fputc('\n', output);
                for (size_t i = 0; i < indent_width; i += 1) {
                    fputc(' ', output);
                }
                column = indent_width;
            } else {
                fputc(' ', output);
                column += 1;
            }
        }
        fwrite(word.bytes, sizeof(byte), word.length, output);
        column += word_width;
        is_line_start = false;
    }
}

/**
 * Prints the empty frame of the gutter, `  |`, without a line break.
 */
static void diagnostic_print_gutter(FILE *output, size_t width, bool is_color) {
    for (size_t i = 0; i < width; i += 1) {
        fputc(' ', output);
    }
    fputc(' ', output);
    diagnostic_style_begin(output, DIAGNOSTIC_STYLE_FRAME, is_color);
    fputc('|', output);
    diagnostic_style_end(output, is_color);
}

static void diagnostic_style_begin(FILE *output, char const *style, bool is_color) {
    if (is_color) {
        fputs(style, output);
    }
}

static void diagnostic_style_end(FILE *output, bool is_color) {
    if (is_color) {
        fputs(DIAGNOSTIC_STYLE_RESET, output);
    }
}

/**
 * Returns the line of `source` that the byte at `offset` is in, which may be the end of the source
 * or a line break, and then the line is the one that ends there.
 */
static Diagnostic_Line diagnostic_line_at(Str source, size_t offset) {
    assert(source.bytes != NULL || source.length == 0);
    assert(offset <= source.length);

    size_t start = offset;
    while (start > 0 && source.bytes[start - 1] != '\n') {
        start -= 1;
    }
    size_t end = offset;
    while (end < source.length && source.bytes[end] != '\n') {
        end += 1;
    }
    // A file with Windows line breaks has a carriage return that is not part of the line.
    if (end > start && source.bytes[end - 1] == '\r') {
        end -= 1;
    }
    return (Diagnostic_Line){.start = start, .end = end};
}

/**
 * Returns the number of characters in `text`, which is valid UTF-8, or at least has no byte that
 * the count could be thrown off by: the bytes that continue a character are not counted. This is
 * the width in columns for the characters that the carets must sit under, except for characters
 * that a terminal shows wider or not at all, which the carets do not account for.
 */
static size_t diagnostic_character_count(Str text) {
    assert(text.bytes != NULL || text.length == 0);

    size_t count = 0;
    for (size_t i = 0; i < text.length; i += 1) {
        // The bytes that continue a character are 0b10xxxxxx.
        if (((byte)text.bytes[i] & 0xC0) != 0x80) {
            count += 1;
        }
    }
    return count;
}

static size_t diagnostic_digit_count(size_t number) {
    size_t count = 1;
    while (number >= 10) {
        number /= 10;
        count += 1;
    }
    return count;
}
