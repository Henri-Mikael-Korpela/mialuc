#include <assert.h>
#include <inttypes.h>
#include <iso646.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <mialu/common_types.h>
#include <mialu/constant.h>
#include <mialu/diagnostic.h>
#include <mialu/limits.h>
#include <mialu/report.h>
#include <mialu/str.h>
#include <mialu/tokenizer.h>

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

    // The tokens.
    [ERROR_CODE_COMMENT_ENCODING_INVALID] = {
        .title = "a comment must be valid UTF-8",
        .label = "this byte is not valid UTF-8",
        .note = "source code is UTF-8 text, and comments are part of it",
    },
    [ERROR_CODE_DOCUMENTATION_COMMENT_TRAILING] = {
        .title = "a documentation comment must be on lines of its own",
        .label = "this '///' comes after code on its line",
        .note = "a documentation comment documents the code after it, so it cannot follow code on "
                "the same line",
        .help = "use '//' for a note after code on a line",
    },
    [ERROR_CODE_INDENTATION_TAB] = {
        .title = "indentation must use spaces, not tabs",
        .label = "this is a tab",
        .help = "indent with four spaces for each level",
    },
    [ERROR_CODE_INDENTATION_WIDTH_INVALID] = {
        .title = "indentation must be a multiple of 4 spaces",
        .label = "indented by {number} spaces",
        .help = "indent with four spaces for each level",
    },
    [ERROR_CODE_FLOAT_LITERAL_DIGIT_INVALID] = {
        .title = "'{detail}' is not a digit of a float literal",
        .label = "not a digit",
        .note = "a float literal has the digits 0 to 9 and nothing else after its dot, since there "
                "is no exponent and no suffix",
    },
    [ERROR_CODE_FLOAT_LITERAL_SEPARATOR_INVALID] = {
        .title = "a digit separator '_' must be between two digits",
        .label = "this '_' is not between two digits",
        .help = "put a digit on each side of it, as in 1_000.5, or remove it",
    },
    [ERROR_CODE_FLOAT_LITERAL_TOO_LARGE] = {
        .title = "float literal is larger than the largest F64",
        .label = "the largest F64 is about 1.8e308",
    },
    [ERROR_CODE_FLOAT_LITERAL_TOO_LONG] = {
        .title = "float literal has more than 128 characters",
        .label = "too long",
    },
    [ERROR_CODE_INTEGER_LITERAL_DIGIT_INVALID] = {
        .title = "'{detail}' is not a digit of an integer literal",
        .label = "not a digit",
        .note = "an integer literal has the digits 0 to 9, or 0 and 1 after '0b', or 0 to 9 and a "
                "to f after '0x'",
    },
    [ERROR_CODE_INTEGER_LITERAL_DIGITS_MISSING] = {
        .title = "integer literal has no digits after its prefix",
        .label = "needs a digit after the prefix",
        .note = "'0b' is followed by binary digits and '0x' by hexadecimal digits",
    },
    [ERROR_CODE_INTEGER_LITERAL_SEPARATOR_INVALID] = {
        .title = "a digit separator '_' must be between two digits",
        .label = "this '_' is not between two digits",
        .help = "put a digit on each side of it, as in 1_000, or remove it",
    },
    [ERROR_CODE_INTEGER_LITERAL_TOO_LARGE] = {
        .title = "integer literal is larger than the largest Int",
        .label = "the largest Int is 9223372036854775807",
    },
    [ERROR_CODE_LABEL_INVALID] = {
        .title = "a label cannot be a reserved word",
        .label = "this is a reserved word",
        .note = "a label is a ':' directly followed by a name, and the name is not a reserved word",
    },
    [ERROR_CODE_RUNE_LITERAL_EMPTY] = {
        .title = "a rune literal has no character",
        .label = "there is no character between the quotes",
        .note = "a rune literal has a character between its quotes",
    },
    [ERROR_CODE_RUNE_LITERAL_ESCAPE_INVALID] = {
        .title = "this is not an escape sequence of a rune",
        .label = "not an escape sequence",
        .note = "an escape sequence is a backslash and one of a, b, e, f, n, r, t, v, \\, ' or \", "
                "three octal digits up to 377, x and two hexadecimal digits, u and four, or U and "
                "eight, with a number that is a Unicode character, which is at most 10FFFF and not "
                "D800 to DFFF",
    },
    [ERROR_CODE_RUNE_LITERAL_ENCODING_INVALID] = {
        .title = "the character of a rune must be valid UTF-8",
        .label = "this byte is not valid UTF-8",
        .note = "source code is UTF-8 text",
    },
    [ERROR_CODE_RUNE_LITERAL_TOO_LONG] = {
        .title = "a rune literal has more than one character",
        .label = "more than one character between the quotes",
        .note = "a rune literal has one character between its quotes",
        .help = "write more than one character as a string, between double quotes",
    },
    [ERROR_CODE_RUNE_LITERAL_UNTERMINATED] = {
        .title = "a rune literal has no closing quote",
        .label = "no closing quote on this line",
        .note = "a rune literal ends on the line where it starts",
    },
    [ERROR_CODE_STRING_LITERAL_BACKSLASH] = {
        .title = "a string cannot contain a backslash",
        .label = "this backslash",
        .note = "escape sequences are not supported in strings yet, and the backslash is kept for "
                "them, so that strings that are written today keep their meaning when the "
                "language gets them",
    },
    [ERROR_CODE_STRING_LITERAL_ENCODING_INVALID] = {
        .title = "a string must be valid UTF-8",
        .label = "this byte is not valid UTF-8",
        .note = "source code is UTF-8 text",
    },
    [ERROR_CODE_STRING_LITERAL_UNTERMINATED] = {
        .title = "string has no closing quote",
        .label = "no closing quote on this line",
        .note = "a string ends on the line where it starts",
    },
    [ERROR_CODE_TOKEN_LIMIT] = {
        .title = "more than {number} tokens",
        .label = "the tokens end here",
        .note = "the compiler reads at most {number} tokens of a file, so that the memory that it "
                "needs is known when it starts",
    },
    [ERROR_CODE_UNEXPECTED_CHARACTER] = {
        .title = "unexpected character '{detail}' (0x{hex})",
        .label = "this cannot start a token",
    },

    // The syntax.
    [ERROR_CODE_NODE_LIMIT] = {
        .title = "more than {number} syntax tree nodes",
        .label = "the nodes end here",
        .note = "the compiler builds at most {number} nodes of the syntax tree for a program, so "
                "that the memory that it needs is known when it starts",
    },
    [ERROR_CODE_UNEXPECTED_TOKEN] = {
        .title = "unexpected {subject}",
        .label = "expected {detail}",
    },

    // The files that a program imports.
    [ERROR_CODE_IMPORT_CYCLE] = {
        .title = "the import '{subject}' is a loop",
        .label = "this import leads back to the file that has it",
        .note = "a file cannot import itself, directly or through the files that it imports",
    },
    [ERROR_CODE_IMPORT_FILE_UNREADABLE] = {
        .title = "cannot read the file of the import '{subject}'",
        .label = "this file cannot be read",
        .note = "the file does not exist, cannot be read, or is larger than a source can be. "
                "Looked for: {detail}",
        .help = "the standard library is in the directory 'lib' next to the compiler, or in "
                "'src/mialu-std' of the project that the compiler was built from",
    },
    [ERROR_CODE_IMPORT_PATH_INVALID] = {
        .title = "the import '{subject}' is not a path of a library file",
        .label = "expected a vendor, a colon and a path",
        .help = "write the vendor, a colon and a path of names and slashes, such as "
                "\"mu:filesystem\"",
    },
    [ERROR_CODE_IMPORT_LIMIT] = {
        .title = "the imports of '{subject}' are more than the program can put together",
        .label = "this import is over the limit",
        .note = "a program can put together {detail}",
    },
    [ERROR_CODE_IMPORT_SHADOWS] = {
        .title = "'{subject}' has the name of a definition of the same file",
        .label = "this would hide the definition",
        .note = "a parameter, a variable or an iterator cannot have the name of a definition of the "
                "file that it is in, which it would hide",
    },
    [ERROR_CODE_IMPORT_VENDOR_UNKNOWN] = {
        .title = "the vendor of the import '{subject}' is not known",
        .label = "unknown vendor",
        .help = "'mu' is the standard library of Mialu",
    },

    // Names, attributes, labels and the statements that jump.
    [ERROR_CODE_DEFINITION_NAME_DUPLICATE] = {
        .title = "'{subject}' is already defined",
        .label = "a definition with this name exists already",
    },
    [ERROR_CODE_IDENTIFIER_UNKNOWN] = {
        .title = "cannot find '{subject}' in this scope",
        .label = "not found in this scope",
        .note = "a name is a variable or a parameter of the procedure, a constant or a global "
                "variable",
    },
    [ERROR_CODE_PARAMETER_NAME_DUPLICATE] = {
        .title = "parameter '{subject}' is declared twice",
        .label = "declared again here",
    },
    [ERROR_CODE_PARAMETER_NAME_SHADOWS] = {
        .title = "parameter '{subject}' has the name of a constant or a global variable",
        .label = "this name is taken",
        .note = "a parameter cannot have the name of a constant or a global variable, which it "
                "would hide",
        .help = "choose another name",
    },
    [ERROR_CODE_VARIABLE_NAME_DUPLICATE] = {
        .title = "variable '{subject}' is defined twice",
        .label = "defined again here",
    },
    [ERROR_CODE_VARIABLE_NAME_SHADOWS] = {
        .title = "variable '{subject}' has the name of a constant, a global variable or a "
                 "parameter",
        .label = "this name is taken",
        .note = "a variable cannot have the name of a constant, a global variable or a parameter, "
                "which it would hide",
        .help = "choose another name",
    },
    [ERROR_CODE_FIELD_NAME_DUPLICATE] = {
        .title = "the struct already has a field called '{subject}'",
        .label = "defined again here",
    },
    [ERROR_CODE_ENUM_MEMBER_DUPLICATE] = {
        .title = "the enum has the member '{subject}' twice",
        .label = "defined again here",
    },
    [ERROR_CODE_ASSIGNMENT_TARGET_NOT_VARIABLE] = {
        .title = "'{subject}' is not a variable, so it cannot be assigned to",
        .label = "cannot be assigned to",
        .note = "a parameter and a constant are not variables",
    },
    [ERROR_CODE_ASSIGNMENT_TARGET_ITERATOR] = {
        .title = "'{subject}' is the iterator of a for loop, so it cannot be assigned to",
        .label = "cannot be assigned to",
    },
    [ERROR_CODE_CALL_TARGET_NOT_PROCEDURE] = {
        .title = "'{subject}' is not a procedure, so it cannot be called",
        .label = "not a procedure",
    },
    [ERROR_CODE_ATTRIBUTE_DUPLICATE] = {
        .title = "the attribute '{subject}' is given twice",
        .label = "given again here",
    },
    [ERROR_CODE_ATTRIBUTE_UNKNOWN] = {
        .title = "'{subject}' is not an attribute of a global variable",
        .label = "unknown attribute",
        .note = "the only attribute of a global variable is 'static'",
    },
    [ERROR_CODE_MATCH_ATTRIBUTE_UNKNOWN] = {
        .title = "'{subject}' is not an attribute of a match statement",
        .label = "unknown attribute",
        .note = "the only attribute of a match statement is 'partial'",
    },
    [ERROR_CODE_GLOBAL_NOT_STATIC] = {
        .title = "global variable '{subject}' must have the attribute #(static)",
        .label = "needs #(static)",
        .note = "a global variable has static storage duration",
        .help = "write the attribute after the name, as in 'counter #(static) := 0'",
    },
    [ERROR_CODE_GLOBAL_VALUE_NOT_CONSTANT] = {
        .title = "the initial value of global variable '{subject}' is not known when the program "
                 "is compiled",
        .label = "not a constant expression",
        .note = "the initial value of a global variable is a constant expression, such as a "
                "number or 'LIMIT * 2', or a field of the information about a type",
    },
    [ERROR_CODE_FOREIGN_IMPORT_DUPLICATE] = {
        .title = "the program already imports foreign procedures from this target",
        .label = "imported again here",
        .note = "a program imports foreign procedures once for each target",
    },
    [ERROR_CODE_FOREIGN_IMPORT_MISSING] = {
        .title = "the foreign procedure '{subject}' has no 'import foreign' of its target above "
                 "it",
        .label = "needs the import of its target above it",
        .help = "add the import of the target above the first foreign block of the target, such "
                "as 'import foreign libc'",
    },
    [ERROR_CODE_BREAK_OUTSIDE_SCOPE] = {
        .title = "'break' is not inside a loop or a scope",
        .label = "nothing to break out of",
    },
    [ERROR_CODE_JUMP_OUTSIDE_LOOP] = {
        .title = "'{subject}' is not inside a loop",
        .label = "not inside a loop",
    },
    [ERROR_CODE_JUMP_OUT_OF_DEFER] = {
        .title = "'{subject}' cannot leave the statement of a 'defer'",
        .label = "leaves the deferred statement",
        .note = "a deferred statement runs while its block is being left",
    },
    [ERROR_CODE_LABEL_DUPLICATE] = {
        .title = "the label '{subject}' is already used by a statement around this one",
        .label = "used again here",
    },
    [ERROR_CODE_LABEL_NOT_LOOP] = {
        .title = "'{subject}' is not the label of a loop, so 'continue' cannot go on with it",
        .label = "not the label of a loop",
        .note = "'continue' goes on with a loop, and not with another statement",
    },
    [ERROR_CODE_LABEL_UNKNOWN] = {
        .title = "'{subject}' is not the label of a statement around this 'break' or 'continue'",
        .label = "no statement around this has the label",
    },
    [ERROR_CODE_DEFER_NESTED] = {
        .title = "'defer' cannot be inside the statement of another 'defer'",
        .label = "inside a deferred statement",
    },
    [ERROR_CODE_DEFER_STATEMENT_INVALID] = {
        .title = "'defer' cannot take this statement",
        .label = "cannot be deferred",
        .note = "'defer' takes an assignment, a call, or an if, for or scope statement, and not a "
                "variable definition, 'return', 'break' or 'continue'",
    },
    [ERROR_CODE_RETURN_IN_DEFER] = {
        .title = "'return' cannot be inside the statement of a 'defer'",
        .label = "inside a deferred statement",
        .note = "a deferred statement runs while its block is being left, maybe by another "
                "'return'",
    },

    // What is known when the program is compiled.
    [ERROR_CODE_CONSTANT_CYCLE] = {
        .title = "the constant '{subject}' is defined in terms of itself",
        .label = "this constant uses itself",
        .note = "the value of a constant is worked out when the program is compiled, and a "
                "constant that uses itself has no value to start from",
    },
    [ERROR_CODE_CONSTANT_DIVISION_BY_ZERO] = {
        .title = "the constant expression at '{subject}' divides by zero",
        .note = "a division by zero has no value, also of floats, which have an infinity for it "
                "in a running program but not in a constant",
    },
    [ERROR_CODE_CONSTANT_NOT_CONSTANT] = {
        .title = "the value of the constant '{subject}' is not known when the program is compiled",
        .label = "not a constant expression",
        .note = "the value of a constant is a number, 'true', 'false', a string, another such "
                "constant, or an operator of them",
    },
    [ERROR_CODE_CONSTANT_OPERAND_TYPE_INVALID] = {
        .title = "an operator of the constant expression at '{subject}' is given operands that it "
                 "does not take",
        .note = "arithmetic takes integers that are untyped or Int, I32 or I64, or floats, but "
                "not both, 'and', 'or' and 'not' take truth values, a Byte is compared with a "
                "Byte or a number from 0 to 255, a Rune with a Rune, strings are compared with "
                "'==' and 'not=' only, '++' takes a string and a string or a rune, and "
                "'int_to_str' takes one integer",
    },
    [ERROR_CODE_CONSTANT_OVERFLOW] = {
        .title = "the constant expression at '{subject}' has a value that does not fit",
        .note = "an Int goes from -9223372036854775808 to 9223372036854775807, and a float must "
                "be finite. Constant arithmetic does not wrap around as the arithmetic of a "
                "running program does, because the number is wrong as soon as it would",
    },
    [ERROR_CODE_CONSTANT_TEXT_TOO_LONG] = {
        .title = "the strings that '++' joins in the constants of the program, at '{subject}', "
                 "are too long together",
        .note = "the strings that '++' joins in the constants of a program have at most {number} "
                "bytes together",
    },
    [ERROR_CODE_CONSTANT_TOO_DEEP] = {
        .title = "the constant expression at '{subject}' has more than {number} nested operators "
                 "and constants",
    },
    [ERROR_CODE_CONSTANT_TYPE_INVALID] = {
        .title = "the constant '{subject}' has a type that a constant cannot have",
        .label = "not the type of a constant",
        .note = "a constant is an Int, an I32, an I64, a U32, a U64, a Byte, a Rune, an F32, an "
                "F64, a Bool or a Str",
    },
    [ERROR_CODE_CONSTANT_TYPE_MISMATCH] = {
        .title = "the value of the constant '{subject}' is not of the type of the constant",
        .label = "not of the type of the constant",
    },
    [ERROR_CODE_CONSTANT_VALUE_NOT_REPRESENTABLE] = {
        .title = "the number at '{subject}' does not fit in a Byte",
        .note = "an untyped integer must fit in the Byte that it becomes, which is a number from "
                "0 to 255",
    },
    [ERROR_CODE_CONCATENATION_NOT_CONSTANT] = {
        .title = "the operands of '++' at '{subject}' are not known when the program is compiled",
        .note = "the operands of '++' are strings, runes, or constants of them, because the text "
                "is joined when the program is compiled",
    },
    [ERROR_CODE_ARRAY_LENGTH_NOT_CONSTANT] = {
        .title = "the length of an array at '{subject}' is not known when the program is compiled",
        .note = "the length of an array is an integer literal, or a constant, or arithmetic of "
                "them",
    },
    [ERROR_CODE_ARRAY_LENGTH_INVALID] = {
        .title = "the length of an array at '{subject}' must be an integer that is not below zero",
    },
    [ERROR_CODE_INT_TO_STR_NOT_CONSTANT] = {
        .title = "the argument of 'int_to_str' at '{subject}' is not known when the program is "
                 "compiled",
        .note = "the argument of 'int_to_str' is an integer, or a constant of one, because the "
                "text is made when the program is compiled",
    },
};

// The code of each status of the analysis.
static Error_Code const REPORT_SEMANTIC_CODES[] = {
    [SEMANTIC_STATUS_DEFINITION_NAME_DUPLICATE] = ERROR_CODE_DEFINITION_NAME_DUPLICATE,
    [SEMANTIC_STATUS_IDENTIFIER_UNKNOWN] = ERROR_CODE_IDENTIFIER_UNKNOWN,
    [SEMANTIC_STATUS_PARAMETER_NAME_DUPLICATE] = ERROR_CODE_PARAMETER_NAME_DUPLICATE,
    [SEMANTIC_STATUS_PARAMETER_NAME_SHADOWS] = ERROR_CODE_PARAMETER_NAME_SHADOWS,
    [SEMANTIC_STATUS_VARIABLE_NAME_DUPLICATE] = ERROR_CODE_VARIABLE_NAME_DUPLICATE,
    [SEMANTIC_STATUS_VARIABLE_NAME_SHADOWS] = ERROR_CODE_VARIABLE_NAME_SHADOWS,
    [SEMANTIC_STATUS_FIELD_NAME_DUPLICATE] = ERROR_CODE_FIELD_NAME_DUPLICATE,
    [SEMANTIC_STATUS_ENUM_MEMBER_DUPLICATE] = ERROR_CODE_ENUM_MEMBER_DUPLICATE,
    [SEMANTIC_STATUS_ASSIGNMENT_TARGET_NOT_VARIABLE] = ERROR_CODE_ASSIGNMENT_TARGET_NOT_VARIABLE,
    [SEMANTIC_STATUS_ASSIGNMENT_TARGET_ITERATOR] = ERROR_CODE_ASSIGNMENT_TARGET_ITERATOR,
    [SEMANTIC_STATUS_CALL_TARGET_NOT_PROCEDURE] = ERROR_CODE_CALL_TARGET_NOT_PROCEDURE,
    [SEMANTIC_STATUS_ATTRIBUTE_DUPLICATE] = ERROR_CODE_ATTRIBUTE_DUPLICATE,
    [SEMANTIC_STATUS_ATTRIBUTE_UNKNOWN] = ERROR_CODE_ATTRIBUTE_UNKNOWN,
    [SEMANTIC_STATUS_MATCH_ATTRIBUTE_UNKNOWN] = ERROR_CODE_MATCH_ATTRIBUTE_UNKNOWN,
    [SEMANTIC_STATUS_GLOBAL_NOT_STATIC] = ERROR_CODE_GLOBAL_NOT_STATIC,
    [SEMANTIC_STATUS_GLOBAL_VALUE_NOT_CONSTANT] = ERROR_CODE_GLOBAL_VALUE_NOT_CONSTANT,
    [SEMANTIC_STATUS_FOREIGN_IMPORT_DUPLICATE] = ERROR_CODE_FOREIGN_IMPORT_DUPLICATE,
    [SEMANTIC_STATUS_FOREIGN_IMPORT_MISSING] = ERROR_CODE_FOREIGN_IMPORT_MISSING,
    [SEMANTIC_STATUS_BREAK_OUTSIDE_SCOPE] = ERROR_CODE_BREAK_OUTSIDE_SCOPE,
    [SEMANTIC_STATUS_JUMP_OUTSIDE_LOOP] = ERROR_CODE_JUMP_OUTSIDE_LOOP,
    [SEMANTIC_STATUS_JUMP_OUT_OF_DEFER] = ERROR_CODE_JUMP_OUT_OF_DEFER,
    [SEMANTIC_STATUS_LABEL_DUPLICATE] = ERROR_CODE_LABEL_DUPLICATE,
    [SEMANTIC_STATUS_LABEL_NOT_LOOP] = ERROR_CODE_LABEL_NOT_LOOP,
    [SEMANTIC_STATUS_LABEL_UNKNOWN] = ERROR_CODE_LABEL_UNKNOWN,
    [SEMANTIC_STATUS_DEFER_NESTED] = ERROR_CODE_DEFER_NESTED,
    [SEMANTIC_STATUS_DEFER_STATEMENT_INVALID] = ERROR_CODE_DEFER_STATEMENT_INVALID,
    [SEMANTIC_STATUS_RETURN_IN_DEFER] = ERROR_CODE_RETURN_IN_DEFER,
    [SEMANTIC_STATUS_CONSTANT_CYCLE] = ERROR_CODE_CONSTANT_CYCLE,
    [SEMANTIC_STATUS_CONSTANT_DIVISION_BY_ZERO] = ERROR_CODE_CONSTANT_DIVISION_BY_ZERO,
    [SEMANTIC_STATUS_CONSTANT_NOT_CONSTANT] = ERROR_CODE_CONSTANT_NOT_CONSTANT,
    [SEMANTIC_STATUS_CONSTANT_OPERAND_TYPE_INVALID] = ERROR_CODE_CONSTANT_OPERAND_TYPE_INVALID,
    [SEMANTIC_STATUS_CONSTANT_OVERFLOW] = ERROR_CODE_CONSTANT_OVERFLOW,
    [SEMANTIC_STATUS_CONSTANT_TEXT_TOO_LONG] = ERROR_CODE_CONSTANT_TEXT_TOO_LONG,
    [SEMANTIC_STATUS_CONSTANT_TOO_DEEP] = ERROR_CODE_CONSTANT_TOO_DEEP,
    [SEMANTIC_STATUS_CONSTANT_TYPE_INVALID] = ERROR_CODE_CONSTANT_TYPE_INVALID,
    [SEMANTIC_STATUS_CONSTANT_TYPE_MISMATCH] = ERROR_CODE_CONSTANT_TYPE_MISMATCH,
    [SEMANTIC_STATUS_CONSTANT_VALUE_NOT_REPRESENTABLE] = ERROR_CODE_CONSTANT_VALUE_NOT_REPRESENTABLE,
    [SEMANTIC_STATUS_CONCATENATION_NOT_CONSTANT] = ERROR_CODE_CONCATENATION_NOT_CONSTANT,
    [SEMANTIC_STATUS_ARRAY_LENGTH_NOT_CONSTANT] = ERROR_CODE_ARRAY_LENGTH_NOT_CONSTANT,
    [SEMANTIC_STATUS_ARRAY_LENGTH_INVALID] = ERROR_CODE_ARRAY_LENGTH_INVALID,
    [SEMANTIC_STATUS_INT_TO_STR_NOT_CONSTANT] = ERROR_CODE_INT_TO_STR_NOT_CONSTANT,
};

// The code of each status of putting the imports together that is about an import. The statuses of
// a file that does not tokenize or parse have the codes of those errors, and are told by them.
static Error_Code const REPORT_PACKAGE_CODES[] = {
    [PACKAGE_STATUS_CYCLE] = ERROR_CODE_IMPORT_CYCLE,
    [PACKAGE_STATUS_FILE_UNREADABLE] = ERROR_CODE_IMPORT_FILE_UNREADABLE,
    [PACKAGE_STATUS_IMPORT_PATH_INVALID] = ERROR_CODE_IMPORT_PATH_INVALID,
    [PACKAGE_STATUS_LIMIT] = ERROR_CODE_IMPORT_LIMIT,
    [PACKAGE_STATUS_SHADOWS] = ERROR_CODE_IMPORT_SHADOWS,
    [PACKAGE_STATUS_VENDOR_UNKNOWN] = ERROR_CODE_IMPORT_VENDOR_UNKNOWN,
};

// The code of each status of the parser.
static Error_Code const REPORT_PARSE_CODES[] = {
    [PARSE_STATUS_NODE_LIMIT] = ERROR_CODE_NODE_LIMIT,
    [PARSE_STATUS_UNEXPECTED_TOKEN] = ERROR_CODE_UNEXPECTED_TOKEN,
};

// The code of each status of the tokenizer. A status that is not in the table is not an error.
static Error_Code const REPORT_TOKENIZE_CODES[] = {
    [TOKENIZE_STATUS_COMMENT_ENCODING_INVALID] = ERROR_CODE_COMMENT_ENCODING_INVALID,
    [TOKENIZE_STATUS_DOCUMENTATION_COMMENT_TRAILING] = ERROR_CODE_DOCUMENTATION_COMMENT_TRAILING,
    [TOKENIZE_STATUS_FLOAT_LITERAL_DIGIT_INVALID] = ERROR_CODE_FLOAT_LITERAL_DIGIT_INVALID,
    [TOKENIZE_STATUS_FLOAT_LITERAL_SEPARATOR_INVALID] =
        ERROR_CODE_FLOAT_LITERAL_SEPARATOR_INVALID,
    [TOKENIZE_STATUS_FLOAT_LITERAL_TOO_LARGE] = ERROR_CODE_FLOAT_LITERAL_TOO_LARGE,
    [TOKENIZE_STATUS_FLOAT_LITERAL_TOO_LONG] = ERROR_CODE_FLOAT_LITERAL_TOO_LONG,
    [TOKENIZE_STATUS_INDENTATION_TAB] = ERROR_CODE_INDENTATION_TAB,
    [TOKENIZE_STATUS_INDENTATION_WIDTH_INVALID] = ERROR_CODE_INDENTATION_WIDTH_INVALID,
    [TOKENIZE_STATUS_INTEGER_LITERAL_DIGIT_INVALID] = ERROR_CODE_INTEGER_LITERAL_DIGIT_INVALID,
    [TOKENIZE_STATUS_INTEGER_LITERAL_DIGITS_MISSING] = ERROR_CODE_INTEGER_LITERAL_DIGITS_MISSING,
    [TOKENIZE_STATUS_INTEGER_LITERAL_SEPARATOR_INVALID] =
        ERROR_CODE_INTEGER_LITERAL_SEPARATOR_INVALID,
    [TOKENIZE_STATUS_INTEGER_LITERAL_TOO_LARGE] = ERROR_CODE_INTEGER_LITERAL_TOO_LARGE,
    [TOKENIZE_STATUS_LABEL_INVALID] = ERROR_CODE_LABEL_INVALID,
    [TOKENIZE_STATUS_RUNE_LITERAL_EMPTY] = ERROR_CODE_RUNE_LITERAL_EMPTY,
    [TOKENIZE_STATUS_RUNE_LITERAL_ENCODING_INVALID] = ERROR_CODE_RUNE_LITERAL_ENCODING_INVALID,
    [TOKENIZE_STATUS_RUNE_LITERAL_ESCAPE_INVALID] = ERROR_CODE_RUNE_LITERAL_ESCAPE_INVALID,
    [TOKENIZE_STATUS_RUNE_LITERAL_TOO_LONG] = ERROR_CODE_RUNE_LITERAL_TOO_LONG,
    [TOKENIZE_STATUS_RUNE_LITERAL_UNTERMINATED] = ERROR_CODE_RUNE_LITERAL_UNTERMINATED,
    [TOKENIZE_STATUS_STRING_LITERAL_BACKSLASH] = ERROR_CODE_STRING_LITERAL_BACKSLASH,
    [TOKENIZE_STATUS_STRING_LITERAL_ENCODING_INVALID] = ERROR_CODE_STRING_LITERAL_ENCODING_INVALID,
    [TOKENIZE_STATUS_STRING_LITERAL_UNTERMINATED] = ERROR_CODE_STRING_LITERAL_UNTERMINATED,
    [TOKENIZE_STATUS_TOKEN_LIMIT] = ERROR_CODE_TOKEN_LIMIT,
    [TOKENIZE_STATUS_UNEXPECTED_CHARACTER] = ERROR_CODE_UNEXPECTED_CHARACTER,
};

static void   report_emit(Reporter const *reporter, Report_Error const *error);
static void   report_emit_at_subject(Reporter const *reporter, Error_Code code, char const *path,
                                     Str source, Str subject, Str detail, int64_t number);
static Str    report_expand(char const *template, Report_Error const *error, char *buffer,
                            size_t buffer_size_bytes);
static bool   report_is_word_byte(char character);
static size_t report_length_character(Str source, size_t offset);
static size_t report_length_literal(Str source, size_t offset);
static size_t report_length_token(Str source, size_t offset, Token_Kind kind);
static size_t report_length_word(Str source, size_t offset);
static void   report_text_append(Report_Text *text, Str part);
static void   report_text_append_subject(Report_Text *text, Str subject);

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

void report_tokenize_error(Reporter const *reporter, char const *path, Str source,
                           Tokenize_Result result) {
    assert(reporter != NULL);
    assert(path != NULL);
    assert(result.status != TOKENIZE_STATUS_OK);
    assert(result.error_offset < source.length);

    size_t offset = result.error_offset;
    size_t length = 1;
    Str detail = STR_EMPTY;
    int64_t number = 0;
    // How much of the source the error is about depends on what the tokenizer found. Some are
    // about one character, some about a whole literal.
    switch (result.status) {
    case TOKENIZE_STATUS_OK:
        assert(false);
        break;
    case TOKENIZE_STATUS_COMMENT_ENCODING_INVALID:
    case TOKENIZE_STATUS_FLOAT_LITERAL_SEPARATOR_INVALID:
    case TOKENIZE_STATUS_INDENTATION_TAB:
    case TOKENIZE_STATUS_INTEGER_LITERAL_SEPARATOR_INVALID:
    case TOKENIZE_STATUS_RUNE_LITERAL_ENCODING_INVALID:
    case TOKENIZE_STATUS_STRING_LITERAL_BACKSLASH:
    case TOKENIZE_STATUS_STRING_LITERAL_ENCODING_INVALID:
        break;
    case TOKENIZE_STATUS_DOCUMENTATION_COMMENT_TRAILING:
        length = sizeof("///") - 1;
        break;
    case TOKENIZE_STATUS_FLOAT_LITERAL_DIGIT_INVALID:
    case TOKENIZE_STATUS_INTEGER_LITERAL_DIGIT_INVALID:
        detail = str_make(source.bytes + offset, 1);
        break;
    case TOKENIZE_STATUS_FLOAT_LITERAL_TOO_LARGE:
    case TOKENIZE_STATUS_FLOAT_LITERAL_TOO_LONG:
    case TOKENIZE_STATUS_INTEGER_LITERAL_DIGITS_MISSING:
    case TOKENIZE_STATUS_INTEGER_LITERAL_TOO_LARGE:
    case TOKENIZE_STATUS_RUNE_LITERAL_EMPTY:
    case TOKENIZE_STATUS_RUNE_LITERAL_TOO_LONG:
    case TOKENIZE_STATUS_RUNE_LITERAL_UNTERMINATED:
    case TOKENIZE_STATUS_STRING_LITERAL_UNTERMINATED:
        length = report_length_literal(source, offset);
        break;
    case TOKENIZE_STATUS_INDENTATION_WIDTH_INVALID:
        // The error is where the indentation ends, so the spaces before it are what is wrong.
        while (offset > 0 && source.bytes[offset - 1] == ' ') {
            offset -= 1;
        }
        length = result.error_offset - offset;
        number = (int64_t)length;
        break;
    case TOKENIZE_STATUS_LABEL_INVALID:
        // The colon and the name.
        length = 1 + report_length_word(source, offset + 1);
        detail = str_make(source.bytes + offset + 1, length - 1);
        break;
    case TOKENIZE_STATUS_RUNE_LITERAL_ESCAPE_INVALID:
        // The backslash and what follows it, if that is on the line.
        if (offset + 1 < source.length && source.bytes[offset + 1] != '\n') {
            length = 2;
        }
        break;
    case TOKENIZE_STATUS_TOKEN_LIMIT:
        number = TOKEN_COUNT_MAX;
        break;
    case TOKENIZE_STATUS_UNEXPECTED_CHARACTER:
        length = report_length_character(source, offset);
        detail = str_make(source.bytes + offset, length);
        number = (int64_t)(byte)source.bytes[offset];
        break;
    }

    Error_Code code = REPORT_TOKENIZE_CODES[result.status];
    assert(code > 0);

    Report_Error error = {
        .path = path,
        .source = source,
        .subject = str_make(source.bytes + offset, length),
        .detail = detail,
        .span_offset = offset,
        .span_length = length,
        .number = number,
        .code = code,
        .has_span = true,
    };
    report_emit(reporter, &error);
}

void report_parse_error(Reporter const *reporter, char const *path, Str source, Array_Slice tokens,
                        Parse_Result result) {
    assert(reporter != NULL);
    assert(path != NULL);
    assert(result.status != PARSE_STATUS_OK);
    assert(result.error_token_index < tokens.length);

    Token const *token_elements = tokens.elements;
    size_t token_index = result.error_token_index;
    // A line starts with an indentation token, which has no text. What the parser found there is
    // the first token of the line, which is the next one.
    if (token_elements[token_index].kind == TOKEN_KIND_INDENTATION &&
        token_index + 1 < tokens.length) {
        token_index += 1;
    }
    Token const *token = &token_elements[token_index];
    size_t offset = token->source_offset;
    size_t length = report_length_token(source, offset, token->kind);
    Str subject = STR_EMPTY;
    // What the parser found is the text of the token, or the thing that the token stands for when
    // it has no text of its own.
    char found[REPORT_SUBJECT_LENGTH_MAX + 8] = {0};
    if (token->kind == TOKEN_KIND_END || token->kind == TOKEN_KIND_INDENTATION) {
        // The end of the file is after the last line break, which is a line of its own that has
        // nothing on it. The error is at the end of the last line that has something on it.
        while (offset > 0 && (source.bytes[offset - 1] == '\n' ||
                              source.bytes[offset - 1] == '\r' ||
                              source.bytes[offset - 1] == ' ')) {
            offset -= 1;
        }
        length = 1;
        subject = STR("end of the file");
        if (token->kind == TOKEN_KIND_INDENTATION) {
            subject = STR("end of the line");
        }
    } else {
        // The text goes between quotes, so that a name or a symbol is told from the words around
        // it.
        size_t shown = length;
        if (shown > REPORT_SUBJECT_LENGTH_MAX) {
            shown = REPORT_SUBJECT_LENGTH_MAX;
        }
        int found_length = snprintf(found, sizeof(found), "'%.*s'", (int)shown,
                                    source.bytes + offset);
        assert(found_length > 0);
        subject = str_make(found, (size_t)found_length);
    }
    // Only the end of the file is at the end of the source, which is not a byte of it.
    if (source.length > 0 && offset >= source.length) {
        offset = source.length - 1;
    }

    Error_Code code = REPORT_PARSE_CODES[result.status];
    assert(code > 0);

    Report_Error error = {
        .path = path,
        .source = source,
        .subject = subject,
        .detail = result.expected,
        .span_offset = offset,
        .span_length = length,
        .number = TOKEN_COUNT_MAX,
        .code = code,
        .has_span = true,
    };
    report_emit(reporter, &error);
}

void report_package_error(Reporter const *reporter, char const *path, Str source,
                          Package_Result result) {
    assert(reporter != NULL);
    assert(path != NULL);
    assert(result.status != PACKAGE_STATUS_OK);

    // A file that does not tokenize or parse is told by the error of the tokens or of the syntax,
    // in that file.
    if (result.status == PACKAGE_STATUS_TOKENIZE_ERROR) {
        report_tokenize_error(reporter, result.path, result.source, result.tokenize_result);
        return;
    }
    if (result.status == PACKAGE_STATUS_PARSE_ERROR) {
        report_parse_error(reporter, result.path, result.source, result.code_tokens,
                           result.parse_result);
        return;
    }
    if (result.status == PACKAGE_STATUS_OUT_OF_MEMORY) {
        report_general(reporter, ERROR_CODE_OUT_OF_MEMORY, NULL, 0);
        return;
    }

    Error_Code code = REPORT_PACKAGE_CODES[result.status];
    assert(code > 0);

    char limits[160] = {0};
    Str detail = STR_EMPTY;
    if (result.status == PACKAGE_STATUS_FILE_UNREADABLE) {
        // The file that was looked for, or why there was none to look for.
        char const *looked_for = "no directory of the library is known";
        if (result.path != NULL) {
            looked_for = result.path;
        }
        detail = str_make(looked_for, strlen(looked_for));
    }
    if (result.status == PACKAGE_STATUS_LIMIT) {
        int length = snprintf(limits, sizeof(limits),
                              "at most %d files, %d imports below each other, and %d bytes of names",
                              PACKAGE_FILE_COUNT_MAX, PACKAGE_DEPTH_MAX,
                              PACKAGE_NAMES_SIZE_BYTES_MAX);
        assert(length > 0);
        detail = str_make(limits, (size_t)length);
    }
    report_emit_at_subject(reporter, code, path, source, result.subject, detail, 0);
}

void report_semantic_error(Reporter const *reporter, char const *path, Str source,
                           Semantic_Result result) {
    assert(reporter != NULL);
    assert(path != NULL);
    assert(result.status != SEMANTIC_STATUS_OK);

    Error_Code code = REPORT_SEMANTIC_CODES[result.status];
    assert(code > 0);

    // The limits that two of the errors tell.
    int64_t number = 0;
    if (result.status == SEMANTIC_STATUS_CONSTANT_TEXT_TOO_LONG) {
        number = CONSTANT_TEXT_SIZE_BYTES_MAX;
    }
    if (result.status == SEMANTIC_STATUS_CONSTANT_TOO_DEEP) {
        number = CONSTANT_EXPRESSION_DEPTH_MAX;
    }
    report_emit_at_subject(reporter, code, path, source, result.subject, STR_EMPTY, number);
}

/**
 * Reports an error that is about `subject`, a view of a source, which is the text that the carets
 * are under. The source is the one that `subject` is part of: the program, or a file that it
 * imports, which `reporter` finds, and then the path and the lines are those of that file. A
 * subject that is part of no source has an error without a place in it.
 */
static void report_emit_at_subject(Reporter const *reporter, Error_Code code, char const *path,
                                   Str source, Str subject, Str detail, int64_t number) {
    assert(reporter != NULL);
    assert(path != NULL);

    char const *file_path = path;
    Str file_source = source;
    if (reporter->packages != NULL && subject.bytes != NULL) {
        char const *imported_path = NULL;
        Str imported_source = STR_EMPTY;
        if (packages_locate(reporter->packages, subject, &imported_path, &imported_source)) {
            file_path = imported_path;
            file_source = imported_source;
        }
    }

    Report_Error error = {
        .path = file_path,
        .source = file_source,
        .subject = subject,
        .detail = detail,
        .span_offset = 0,
        .span_length = subject.length,
        .number = number,
        .code = code,
        .has_span = false,
    };
    // The subject views the source, so its distance from the start is its offset. Something that
    // the compiler adds, such as the built-in struct of `type_info_of`, is not in any source.
    if (subject.bytes != NULL && subject.bytes >= file_source.bytes &&
        subject.bytes <= file_source.bytes + file_source.length) {
        error.has_span = true;
        error.span_offset = (size_t)(subject.bytes - file_source.bytes);
    }
    report_emit(reporter, &error);
}

/**
 * Returns the length in bytes of the token of kind `kind` that starts at `offset` in `source`,
 * which is at least one. A name is its letters, a number and a string are like a literal, a label
 * is the colon and the name, and a symbol is the characters of operators that run together, such as
 * `->` and `::=`.
 */
static size_t report_length_token(Str source, size_t offset, Token_Kind kind) {
    assert(offset <= source.length);

    if (offset == source.length) {
        return 1;
    }
    char first = source.bytes[offset];
    if (kind == TOKEN_KIND_LABEL) {
        return 1 + report_length_word(source, offset + 1);
    }
    if (first == '"' || first == '\'' || (first >= '0' && first <= '9')) {
        return report_length_literal(source, offset);
    }
    if (report_is_word_byte(first)) {
        return report_length_word(source, offset);
    }
    if (first == ' ') {
        // An indentation is the spaces.
        size_t length = 0;
        while (offset + length < source.length && source.bytes[offset + length] == ' ') {
            length += 1;
        }
        return length;
    }
    // A symbol is up to four characters of the ones that operators are made of, such as `..<=`.
    size_t length = 1;
    char const operators[] = "-:=.<>+*/%!";
    while (length < 4 && offset + length < source.length &&
           strchr(operators, source.bytes[offset + length]) != NULL &&
           strchr(operators, first) != NULL) {
        length += 1;
    }
    return length;
}

/**
 * Returns the length in bytes of the character that starts at `offset` in `source`, which is the
 * one byte of an ASCII character, or the bytes that a lead byte of UTF-8 says, as far as the source
 * goes. A byte that is not a lead byte is a character of its own.
 */
static size_t report_length_character(Str source, size_t offset) {
    assert(offset < source.length);

    byte lead = (byte)source.bytes[offset];
    size_t length = 1;
    if (lead >= 0xF0) {
        length = 4;
    } else if (lead >= 0xE0) {
        length = 3;
    } else if (lead >= 0xC0) {
        length = 2;
    }
    if (offset + length > source.length) {
        length = source.length - offset;
    }
    return length;
}

static bool report_is_word_byte(char character) {
    bool is_letter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
    bool is_digit = character >= '0' && character <= '9';
    return is_letter || is_digit || character == '_';
}

/**
 * Returns the length in bytes of the name that starts at `offset` in `source`, which is the letters,
 * digits and underscores that follow there. It may be zero.
 */
static size_t report_length_word(Str source, size_t offset) {
    assert(offset <= source.length);

    size_t length = 0;
    while (offset + length < source.length && report_is_word_byte(source.bytes[offset + length])) {
        length += 1;
    }
    return length;
}

/**
 * Returns the length in bytes of the literal that starts at `offset` in `source`. A string or a rune
 * goes to its closing quote, or to the end of its line when it has none. A number is the letters,
 * digits, underscores and dots that follow.
 */
static size_t report_length_literal(Str source, size_t offset) {
    assert(offset < source.length);

    char first = source.bytes[offset];
    size_t index = offset;
    if (first == '"' || first == '\'') {
        index += 1;
        while (index < source.length && source.bytes[index] != '\n' &&
               source.bytes[index] != '\r') {
            // A backslash in a rune takes the character after it, so that `'\''` is one literal.
            // A string has no escape sequences.
            if (first == '\'' && source.bytes[index] == '\\' && index + 1 < source.length &&
                source.bytes[index + 1] != '\n') {
                index += 2;
            } else if (source.bytes[index] == first) {
                return index + 1 - offset;
            } else {
                index += 1;
            }
        }
        return index - offset;
    }
    while (index < source.length &&
           (report_is_word_byte(source.bytes[index]) || source.bytes[index] == '.')) {
        index += 1;
    }
    if (index == offset) {
        return 1;
    }
    return index - offset;
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
            report_text_append(&text, error->detail);
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
