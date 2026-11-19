#ifndef MIALU_DIAGNOSTIC_H
#define MIALU_DIAGNOSTIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <mialu/str.h>

/**
 * What one error of the compiler says, in the parts that the layout of an error has, and the
 * layout of the error, which is that of the errors of Rust:
 *
 * ```
 * error[E0101]: the title
 *  --> path/to/file.mu:3:14
 *   |
 * 3 |     x := 9223372036854775808
 *   |          ^^^^^^^^^^^^^^^^^^^ the label
 *   |
 *   = note: the note
 *   = help: the help
 * ```
 *
 * The second line has the file, the line and the column in the form that terminals and editors
 * take as a link, so that a person can open the place with a click, as in VS Code with Ctrl and a
 * click. The text of the parts is already complete: this module only lays it out, and does not know
 * what any error is. Which text an error has is the business of `report.h`.
 *
 * A part that is not there is a `Str` whose `bytes` is NULL.
 */
typedef struct {
    // The file that the error is in. Without it the error is about no file in particular.
    Str path;
    // The text of the file, which `span_offset` is an offset in. Only read when `has_span`.
    Str source;
    // What is wrong, in one line, without a full stop at its end.
    Str title;
    // What is wrong at the place of the span, written beside the carets. Often short.
    Str label;
    // Why the rule is as it is, or what more there is to know.
    Str note;
    // What to do about it.
    Str help;
    // The bytes of the source that the error is about, starting at the byte `span_offset` and
    // `span_length` long. The part of it that is on the first line is underlined.
    size_t span_offset;
    size_t span_length;
    // A second place in the same source, which the error is also about, such as the value that
    // does not fit where the first place is. It is shown below the first, with its own label.
    Str second_label;
    size_t second_offset;
    size_t second_length;
    // The number of the error, which is printed as `E` and four digits, such as `E0101`. Every kind
    // of error has one, and no other has the same.
    uint32_t code;
    bool has_second;
    bool has_span;
} Diagnostic;

/**
 * Writes `diagnostic` to `output`, with the layout above and a line break after the last line. With
 * `is_color` the parts get the colors that make them stand apart, with escape sequences, which
 * `output` must show, so it is false for a file or a pipe. The text is the same without colors.
 */
void diagnostic_print(FILE *output, Diagnostic const *diagnostic, bool is_color);

#endif
