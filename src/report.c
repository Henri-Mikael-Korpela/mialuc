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
#include <mialu/wasm-text.h>
#include <mialu/x64-text.h>
#include <mialu/x64.h>

enum {
    /**
     * The codes are numbers below this, and the catalog has a place for each of them, so that the
     * text of an error is found by its code without a search.
     * The numbers that are not used are empty places.
     */
    REPORT_CODE_COUNT = 1000,
    /**
     * The most bytes of the name, or of whatever else the error is about, that go into its text.
     * A longer one is cut and ends with `...`, so that an error is a few lines, whatever the
     * program has.
     */
    REPORT_SUBJECT_LENGTH_MAX = 80,
    /**
     * The most bytes of each part of an error after the text of the catalog is filled in.
     */
    REPORT_PART_SIZE_BYTES = 2048,
};

/**
 * The text of one kind of error, as templates. A part that the error has no use for is NULL. In a
 * template `{subject}` is the name, or the piece of the program, that the error is about,
 * `{detail}` and `{other}` are two more pieces of text, `{target}` is the name of the target,
 * `{number}` is a number, such as a limit, and `{hex}` is the same number in two hexadecimal
 * digits.
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
    char const *second_label;
} Report_Entry;

/**
 * The texts and the number that an error is told with besides its subject: `{detail}` and `{other}`
 * are two more pieces of text, `{target}` is the name of the target that built the program, and
 * `{number}` is a number, such as a limit. A text that an error does not have is empty.
 */
typedef struct {
    Str detail;
    Str other;
    Str target;
    int64_t number;
    // A second place in the same source, as an offset, for an error that is about two. It is only
    // given when `has_second` is set.
    size_t second_offset;
    bool has_second;
} Report_Texts;

/**
 * Everything that the text of one error is made of.
 */
typedef struct {
    // The file, or NULL for an error that is not about a file.
    char const *path;
    Str source;
    Str subject;
    Str detail;
    Str other;
    Str target;
    size_t span_offset;
    size_t span_length;
    int64_t number;
    size_t second_offset;
    Error_Code code;
    bool has_second;
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
    [ERROR_CODE_TYPE_PARAMETER_INVALID] = {
        .title = "a type parameter cannot be a reserved word",
        .label = "this is a reserved word",
        .note = "a type parameter is a '$' directly followed by a name, and the name is not a "
                "reserved word",
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
    [ERROR_CODE_STRING_LITERAL_ESCAPE_INVALID] = {
        .title = "this is not an escape sequence of a string",
        .label = "not an escape sequence",
        .note = "an escape sequence is a backslash and one of a, b, e, f, n, r, t, v, \\, ' or \", "
                "three octal digits up to 377, x and two hexadecimal digits, u and four, or U and "
                "eight, with a number that is a Unicode character, which is at most 10FFFF and not "
                "D800 to DFFF",
        .help = "write a backslash that is not the start of one as two backslashes",
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
    [ERROR_CODE_RETURN_VALUE_MISSING] = {
        .title = "'return' has no value, but the procedure returns {detail}",
        .label = "expected a return value of type {detail}, but none was given",
        .note = "a procedure that has a return type returns a value of it with every 'return'",
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
        .note = "a parameter, a variable or an iterator cannot have the name of a definition of "
                "the file that it is in, which it would hide",
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
    [ERROR_CODE_TYPE_ARGUMENT_MISPLACED] = {
        .title = "the call of '{subject}' gives a type where a value is wanted",
        .note = "a call gives a type only to a type parameter of the type 'type_id', when it gives "
                "every parameter, and to the first argument of 'make'",
    },
    [ERROR_CODE_TYPE_ARGUMENT_INVALID] = {
        .title = "the call of '{subject}' gives a type parameter something that is not a type",
        .note = "a call that gives every parameter gives each parameter of the type 'type_id' a "
                "type, such as 'Int' or '[]Byte'",
    },
    [ERROR_CODE_TYPE_PARAMETER_DUPLICATE] = {
        .title = "the type parameter '{subject}' is declared twice",
        .label = "declared again here",
        .help = "write '$' before the name of a type parameter once, and the name without it where "
                "it is used again",
    },
    [ERROR_CODE_TYPE_PARAMETER_MISPLACED] = {
        .title = "'{subject}' declares a type parameter, which only a parameter of a procedure does",
        .label = "a type parameter",
        .help = "declare it in the type of a parameter, or write the name without '$'",
    },
    [ERROR_CODE_TYPE_PARAMETER_UNDEDUCED] = {
        .title = "the type of the type parameter '{detail}' of '{subject}' cannot be deduced from "
                 "the arguments of the call",
        .note = "a type parameter gets the type of an argument whose parameter has it in its type, "
                "and the type of that argument must be known",
        .help = "give every parameter, the type parameters too, as in 'zero(Int)'",
    },
    [ERROR_CODE_TYPE_PARAMETER_CONFLICT] = {
        .title = "the type parameter '{detail}' of '{subject}' is deduced as '{other}' and as "
                 "'{target}'",
        .note = "a type parameter is one type in a call, so the arguments whose parameters have it "
                "must agree about it",
    },
    [ERROR_CODE_PROCEDURE_NOT_CALLED] = {
        .title = "'{subject}' is a polymorphic procedure, so it can only be called",
        .label = "not called",
        .note = "a polymorphic procedure is a procedure for each of the types that it is called "
                "with, so it is no one value",
    },
    [ERROR_CODE_STRUCT_TYPE_ARGUMENT_COUNT_MISMATCH] = {
        .title = "'{subject}' gives a polymorphic struct another number of type arguments than its "
                 "{number} type parameters",
        .help = "write a type for each type parameter between parentheses after the name of the "
                "struct, as in 'Pair(Int)'",
    },
    [ERROR_CODE_STRUCT_NOT_POLYMORPHIC] = {
        .title = "'{subject}' gives type arguments to a type that is not a polymorphic struct",
        .note = "only a struct that has type parameters, as in 'struct($T: type_id)', takes type "
                "arguments",
    },
    [ERROR_CODE_SPECIALIZATION_LIMIT] = {
        .title = "the copies of the polymorphic definitions at '{subject}' go past a limit of the "
                 "compiler, {number}",
        .note = "a procedure that calls itself with a larger type each time needs copies without "
                "end",
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

    // What both targets reject.
    [ERROR_CODE_ARITHMETIC_OPERAND_TYPE_INVALID] = {
        .title = "an operand of arithmetic at '{subject}' is not of a type that it takes",
        .note = "an operand of arithmetic is an Int, an I32, an I64, a U32, a U64 or a float, and "
                "is of the type of the other operand",
    },
    [ERROR_CODE_ARRAY_ELEMENT_TYPE_MISMATCH] = {
        .title = "the element at '{subject}' is not of the type of the elements of the array",
    },
    [ERROR_CODE_ARRAY_EXPECTED] = {
        .title = "'{subject}' is not an array or an array slice",
        .label = "not an array or an array slice",
    },
    [ERROR_CODE_ARRAY_INITIALIZER_INVALID] = {
        .title = "the array '{subject}' must be given an array literal",
        .label = "not an array literal",
        .note = "an array is not copied, so it is defined with an array literal and is not "
                "assigned to as a whole",
        .help = "write an array literal, such as [const]Int {{ 1, 2 }",
    },
    [ERROR_CODE_ARRAY_LENGTH_MISMATCH] = {
        .title = "the array of '{subject}' has a different number of elements than the length in "
                 "its type",
    },
    [ERROR_CODE_CALL_ARGUMENT_COUNT_MISMATCH] = {
        .title = "the call of '{subject}' has a different number of arguments than the procedure "
                 "has parameters",
        .label = "a different number of arguments",
    },
    [ERROR_CODE_CALL_ARGUMENT_TYPE_MISMATCH] = {
        .title = "an argument of the call of '{subject}' is not of the type of its parameter",
    },
    [ERROR_CODE_CALL_RESULT_MISSING] = {
        .title = "the procedure '{subject}' returns no value, so its call cannot be used as one",
    },
    [ERROR_CODE_CALL_TARGET_NOT_POINTER] = {
        .title = "'{subject}' does not hold a pointer to a procedure, so it cannot be called",
    },
    [ERROR_CODE_CAST_INVALID] = {
        .title = "the value cannot be cast to '{subject}'",
        .note = "a Byte, a Bool, an I32, an I64, a U32, a U64, an Int, a Rune or a float can be "
                "cast to Int or I64, an integer or a Rune to a U64, a Byte, a Bool, a U32 or a "
                "Rune to a U32, a Byte, a Bool, an I32 or a Rune to an I32, a Byte, an I32, an "
                "I64, a U32, an Int or a float to a float, a Byte or a Rune to Rune, and a Rune "
                "to [4]Byte",
    },
    [ERROR_CODE_COMPARISON_OPERAND_TYPE_INVALID] = {
        .title = "{detail} cannot be compared with {other}",
        .label = "this operand cannot be compared here",
        .note = "values of different types cannot be compared, except an untyped constant or 'nil' "
                "with the type that it fits, a Byte with an ASCII rune, and arrays, dynamic arrays "
                "or array slices of one type of elements with each other, and only numbers have an "
                "order",
    },
    [ERROR_CODE_CONDITION_TYPE_INVALID] = {
        .title = "the condition at '{subject}' is not a Bool",
        .label = "not a Bool",
        .note = "an Int is not converted to a Bool",
    },
    [ERROR_CODE_ENUM_MEMBER_UNKNOWN] = {
        .title = "the enum has no member '{subject}'",
        .label = "not a member of the enum",
    },
    [ERROR_CODE_ENUM_TYPE_UNKNOWN] = {
        .title = "the enum of '{subject}' is not known here",
        .label = "nothing says what enum this is of",
        .help = "write the member with its enum, as in 'Color.RED', or give it to something that "
                "has an enum type",
    },
    [ERROR_CODE_FIELD_TARGET_READ_ONLY] = {
        .title = "a field of '{subject}' cannot be assigned to",
        .note = "'{subject}' is a parameter or an iterator, which cannot be assigned to, so "
                "neither can its fields. A field of what a pointer points to can be",
    },
    [ERROR_CODE_IDENTIFIER_NOT_VALUE] = {
        .title = "'{subject}' is a type and not a value",
        .label = "this is a type",
    },
    [ERROR_CODE_INDEX_TYPE_INVALID] = {
        .title = "'{subject}' is not an Int, so it cannot be an index",
        .label = "not an Int",
    },
    [ERROR_CODE_LOGICAL_OPERAND_TYPE_INVALID] = {
        .title = "'{subject}' is not a Bool, so it cannot be used with 'and', 'or' or 'not'",
        .label = "not a Bool",
    },
    [ERROR_CODE_MATCH_CASE_DUPLICATE] = {
        .title = "this case matches a value that a case above it matches already",
        .label = "it could never run for that value",
    },
    [ERROR_CODE_MATCH_PARTIAL_INVALID] = {
        .title = "'{subject}' only means something for a match of an enum that has no default "
                 "case",
        .note = "a match of an enum that has no default case is the only match that must have a "
                "case for every value",
    },
    [ERROR_CODE_MATCH_PATTERN_NOT_CONSTANT] = {
        .title = "the pattern of the case is not known when the program is compiled",
        .label = "not a constant",
        .help = "the pattern is a literal, a constant or a member of an enum",
    },
    [ERROR_CODE_MATCH_PATTERN_TYPE_MISMATCH] = {
        .title = "the pattern of the case is not of the type of what is matched",
        .label = "not of the type of the match",
        .note = "a string is not an Int, a member is of one enum only, a Byte is from 0 to 255 "
                "or a rune of ASCII and a range is only for an Int or a Byte",
    },
    [ERROR_CODE_MATCH_RANGE_EMPTY] = {
        .title = "the range of the case has no numbers in it, so it could never match",
        .label = "an empty range",
    },
    [ERROR_CODE_MATCH_SUBJECT_TYPE_INVALID] = {
        .title = "'{subject}' cannot be matched",
        .label = "not an integer, a Str or an enum",
        .note = "a match takes an integer, a Str or an enum",
    },
    [ERROR_CODE_NIL_TYPE_UNKNOWN] = {
        .title = "'nil' does not say what '{subject}' points to",
        .label = "the type of this is not known",
        .help = "write the type of the variable, as in 'p : ^Int = nil'",
    },
    [ERROR_CODE_POINTER_EXPECTED] = {
        .title = "'{subject}' is not a pointer, so '^' cannot follow it",
        .label = "not a pointer",
    },
    [ERROR_CODE_POINTER_TARGET_TYPE_MISMATCH] = {
        .title = "the value assigned through '{subject}' is not of the type that it points to",
    },
    [ERROR_CODE_RANGE_TYPE_INVALID] = {
        .title = "'{subject}' is not an Int, so it cannot be the start or the end of a range",
        .label = "not an Int",
    },
    [ERROR_CODE_RESULT_COUNT_MISMATCH] = {
        .title = "the number of names of '{subject}' is not the number of values that the call "
                 "gives",
    },
    [ERROR_CODE_RETURN_COUNT_MISMATCH] = {
        .title = "'{subject}' returns a number of values that is not the number of its return "
                 "types",
    },
    [ERROR_CODE_RETURN_TYPE_MISMATCH] = {
        .title = "'{subject}' returns {other}, but its return type is {detail}",
        .label = "this procedure returns {detail}",
        .second_label = "this is {other}",
    },
    [ERROR_CODE_RETURN_TYPE_MISSING] = {
        .title = "'{subject}' returns a value but has no return type",
        .label = "this has no return type",
    },
    [ERROR_CODE_STRUCT_FIELD_DUPLICATE] = {
        .title = "the field '{subject}' is given a value twice",
        .label = "given again here",
    },
    [ERROR_CODE_STRUCT_FIELD_TYPE_MISMATCH] = {
        .title = "the value of the field '{subject}' is not of the type of the field",
    },
    [ERROR_CODE_STRUCT_FIELD_UNKNOWN] = {
        .title = "the struct has no field called '{subject}'",
        .label = "not a field of the struct",
    },
    [ERROR_CODE_STRUCT_RECURSIVE] = {
        .title = "the struct with the field '{subject}' contains itself",
        .label = "this field makes it infinitely large",
        .note = "a struct that contains itself would never end",
        .help = "give it a pointer to itself instead",
    },
    [ERROR_CODE_TYPE_UNSUPPORTED] = {
        .title = "type '{subject}' is not supported by the {target} target",
        .label = "not supported",
    },
    [ERROR_CODE_UNION_CASE_UNKNOWN] = {
        .title = "the case '{subject}' is not a member of the enum of the field that the union is "
                 "matched against",
        .label = "not a member of the enum",
    },
    [ERROR_CODE_UNION_DISCRIMINANT_INVALID] = {
        .title = "'{subject}' cannot be matched by a union",
        .note = "the field that a union is matched against is a field of the struct, outside of "
                "the cases, and of an enum type",
    },
    [ERROR_CODE_UNION_FIELD_KIND_MISMATCH] = {
        .title = "the field '{subject}' is in a case of a union, but the struct object is given "
                 "another member for the field that the union is matched against",
        .label = "this field does not exist there",
    },
    [ERROR_CODE_UNION_FIELD_UNMATCHED] = {
        .title = "the field '{subject}' is in a case of a union, so it can only be used where the "
                 "field that the union is matched against is known to be the member of the case",
        .label = "used outside of its case",
        .help = "use it inside 'if x.kind == .MEMBER ->', and do not change 'x' or 'x.kind' in it",
    },
    [ERROR_CODE_UNION_KIND_NOT_CONSTANT] = {
        .title = "the field '{subject}' is in a case of a union, so the struct object must give "
                 "the field that the union is matched against a member of the enum that is "
                 "written out",
        .help = "write the member, such as '.MEMBER', and not another value",
    },
    [ERROR_CODE_COMPARISON_ARRAY_LENGTH_MISMATCH] = {
        .title = "two arrays of different lengths cannot be compared: {detail} and {other}",
        .label = "this array has {detail}",
        .second_label = "this array has {other}",
        .note = "arrays are compared element by element, so they must have as many elements, and "
                "the length of an array is known when the program is compiled",
        .help = "compare slices of the parts that have the same length, as in 'a[0:2] == b[0:2]'",
    },
    [ERROR_CODE_MAKE_TYPE_INVALID] = {
        .title = "'make' makes an array slice, and the type at '{subject}' is not the type of one",
        .label = "not the type of an array slice",
        .note = "'make' allocates the elements of an array slice, which 'destroy' gives back",
        .help = "write the type of an array slice, such as '[]Byte'",
    },
    [ERROR_CODE_COMPARISON_ARRAY_KIND_MISMATCH] = {
        .title = "{detail} cannot be compared with {other}",
        .label = "these two are not compared with each other",
        .note = "an array, a dynamic array and an array slice are only compared with their own "
                "kind, even when their elements are the same: the length of an array is known when "
                "the program is compiled, the length of a dynamic array changes, and a slice views "
                "some of the elements of another",
        .help = "compare slices of both, as in 'a[:] == b[:]'",
    },
    [ERROR_CODE_APPEND_TARGET_INVALID] = {
        .title = "'{subject}' can only add to a dynamic array, and its first argument is not the "
                 "address of one",
        .label = "not the address of a dynamic array",
        .note = "write the address of a variable of the type '[dynamic]T', as in "
                "'append(%numbers, 1)': an array, an array slice and any other type cannot be "
                "appended to",
    },
    [ERROR_CODE_VARIABLE_TYPE_MISMATCH] = {
        .title = "variable '{subject}' is given a value that is not of its type",
    },

    // What the x64 target rejects.
    [ERROR_CODE_X64_ADDRESS_INVALID] = {
        .title = "'{subject}' has no address",
        .label = "no address",
        .note = "a variable, an element and what a pointer points to have an address, but not a "
                "parameter, an iterator, a value that is computed, or an array",
    },
    [ERROR_CODE_X64_ARRAY_TYPE_MISPLACED] = {
        .title = "the parameter '{subject}' cannot be an array",
        .label = "an array here",
        .note = "an array is in the frame of its procedure, so a parameter takes an array slice",
        .help = "write the type as '[]Type'",
    },
    [ERROR_CODE_X64_CONSTANT_COUNT_EXCEEDED] = {
        .title = "more than {number} constants, procedures and strings",
        .label = "the limit is passed here",
        .note = "the x64 target keeps at most {number} of them, so that the memory that it needs "
                "is known when it starts",
    },
    [ERROR_CODE_X64_EXPRESSION_TOO_DEEP] = {
        .title = "'{subject}' has an expression nested deeper than {number}",
    },
    [ERROR_CODE_X64_FIELD_BASE_INVALID] = {
        .title = "the field '{subject}' is accessed on something that is not a struct or a "
                 "pointer to one",
        .label = "not a struct",
        .note = "a struct is held by a variable, a parameter, an element or a pointer",
    },
    [ERROR_CODE_X64_FOREIGN_TARGET_UNSUPPORTED] = {
        .title = "the foreign procedure '{subject}' is declared in a 'foreign js' block",
        .label = "the x64 target cannot import from 'foreign js'",
        .note = "the x64 target imports from 'foreign libc' and from the libraries of the system "
                "that an 'import foreign' names",
    },
    [ERROR_CODE_X64_LIBRARY_INVALID] = {
        .title = "the library of the foreign procedure '{subject}' is not written as a library of "
                 "the system",
        .note = "there is room for a few libraries",
        .help = "write it as \"system:Name.lib\" in its import",
    },
    [ERROR_CODE_X64_MAIN_INVALID] = {
        .title = "'{subject}' must be 'main :: proc() Int ->'",
        .label = "not the procedure that starts the program",
        .note = "'main' is a procedure without parameters that returns an Int, which is the exit "
                "code",
    },
    [ERROR_CODE_X64_MAIN_MISSING] = {
        .title = "a program needs a procedure 'main' to start with",
        .help = "define it, as in 'main :: proc() Int ->'",
    },
    [ERROR_CODE_X64_MATCH_NOT_EXHAUSTIVE] = {
        .title = "the match has no case for a member of the enum, and no default case",
        .label = "this match leaves a member out",
        .help = "add a case for each member, or a default case 'case ->', or write 'match "
                "#(partial)' to leave members out",
    },
    [ERROR_CODE_X64_OUTPUT_TOO_LARGE] = {
        .title = "the program is larger than {number} bytes",
        .note = "the compiler allocates the memory for the program once, when it starts",
    },
    [ERROR_CODE_X64_STRUCT_EXPECTED] = {
        .title = "'{subject}' is not a struct that is defined, or the literal without a name is "
                 "not where a struct is wanted",
    },
    [ERROR_CODE_X64_STRUCT_FIELD_MISSING] = {
        .title = "the literal of '{subject}' gives some fields and not all of them",
        .note = "a literal gives either every field or none",
    },
    [ERROR_CODE_X64_TYPE_INFO_MISPLACED] = {
        .title = "the information about '{subject}' is not a value",
        .help = "read a field of it right away, such as '.members' or '.name'",
    },
    [ERROR_CODE_X64_TYPE_INFO_UNSUPPORTED] = {
        .title = "'type_info_of' at '{subject}' is about a type that the x64 target gives no "
                 "information about",
        .note = "the x64 target gives information about an enum only",
    },
    [ERROR_CODE_X64_UNSUPPORTED] = {
        .title = "'{subject}' has something that the x64 target does not build yet",
    },

    // What the wasm target rejects.
    [ERROR_CODE_WASM_ADDRESS_INVALID] = {
        .title = "cannot take the address of '{subject}'",
        .label = "no address",
        .note = "only a variable of a procedure, a field or an element of one, and what a pointer "
                "points to have an address, and not a parameter, an iterator, a global variable, "
                "a value, an array or an array slice",
    },
    [ERROR_CODE_WASM_ARRAY_TYPE_MISPLACED] = {
        .title = "an array of '{subject}' cannot be the type here",
        .label = "an array here",
        .note = "a parameter can be an array slice, as in []{subject}, and a global variable can "
                "be neither",
    },
    [ERROR_CODE_WASM_BUILT_IN_UNSUPPORTED] = {
        .title = "the built-in procedure '{subject}' is not supported by the wasm target",
    },
    [ERROR_CODE_WASM_DEFINITION_NAME_RESERVED] = {
        .title = "'{subject}' is reserved for the memory of the wasm module",
        .label = "this name is reserved",
        .help = "choose another name",
    },
    [ERROR_CODE_WASM_EXPRESSION_TOO_DEEP] = {
        .title = "an expression in '{subject}' has too many nested operators",
    },
    [ERROR_CODE_WASM_FIELD_BASE_INVALID] = {
        .title = "the field '{subject}' is read or assigned through a value that is not a "
                 "variable, a parameter or an element of an array",
    },
    [ERROR_CODE_WASM_FOREIGN_TARGET_UNSUPPORTED] = {
        .title = "the foreign procedure '{subject}' is declared in a 'foreign libc' block or in "
                 "one of a library of the system",
        .label = "the wasm target cannot import from this",
        .note = "the wasm target imports from 'foreign js'",
    },
    [ERROR_CODE_WASM_MATCH_NOT_EXHAUSTIVE] = {
        .title = "the match has no case for the member '{detail}' of the enum",
        .label = "this match leaves '{detail}' out",
        .help = "add a case for it, a default case 'case ->', or write 'match #(partial)' to "
                "leave members out",
    },
    [ERROR_CODE_WASM_OUTPUT_TOO_LARGE] = {
        .title = "the WebAssembly module is larger than {number} bytes",
        .note = "the compiler allocates the memory for the module once, when it starts",
    },
    [ERROR_CODE_WASM_STRUCT_EXPECTED] = {
        .title = "'{subject}' is not a struct or a pointer to one, so it has no fields",
        .label = "not a struct",
    },
    [ERROR_CODE_WASM_STRUCT_FIELD_MISSING] = {
        .title = "the field '{subject}' is not given a value",
        .note = "either every field is given a value, or the braces are empty and every field has "
                "its default value",
    },
    [ERROR_CODE_WASM_TYPE_INFO_MISPLACED] = {
        .title = "type_info_of('{subject}') exists only when the program is compiled",
        .note = "a global variable can be given that and nothing more",
        .help = "write a field after it right away, as in type_info_of({subject}).members",
    },
    [ERROR_CODE_WASM_TYPE_INFO_UNSUPPORTED] = {
        .title = "the wasm target has information about enums only, and '{subject}' is not one",
    },

    // The text of a program.
    [ERROR_CODE_WASM_TEXT_MODULE_MALFORMED] = {
        .title = "internal error: the WebAssembly module is malformed at byte {number}",
        .note = "the formatter reads what the compiler wrote, so what it cannot read is a bug of "
                "the compiler and not of the program",
        .help = "report it as a bug of the compiler, with the program that has it",
    },
    [ERROR_CODE_WASM_TEXT_MODULE_UNSUPPORTED] = {
        .title = "internal error: the WebAssembly module cannot be formatted as text at byte "
                 "{number}",
        .note = "the formatter reads what the compiler wrote, so what it cannot read is a bug of "
                "the compiler and not of the program",
        .help = "report it as a bug of the compiler, with the program that has it",
    },
    [ERROR_CODE_WASM_TEXT_OUTPUT_TOO_LARGE] = {
        .title = "the WebAssembly text is larger than {number} bytes",
        .note = "the compiler allocates the memory for the text once, when it starts",
    },
    [ERROR_CODE_X64_TEXT_EXECUTABLE_MALFORMED] = {
        .title = "internal error: the executable is malformed at byte {number}",
        .note = "the formatter reads what the compiler wrote, so what it cannot read is a bug of "
                "the compiler and not of the program",
        .help = "report it as a bug of the compiler, with the program that has it",
    },
    [ERROR_CODE_X64_TEXT_EXECUTABLE_UNSUPPORTED] = {
        .title = "internal error: the executable cannot be formatted as assembly at byte {number}",
        .note = "the formatter reads what the compiler wrote, so what it cannot read is a bug of "
                "the compiler and not of the program",
        .help = "report it as a bug of the compiler, with the program that has it",
    },
    [ERROR_CODE_X64_TEXT_OUTPUT_TOO_LARGE] = {
        .title = "the assembly is larger than {number} bytes",
        .note = "the compiler allocates the memory for the assembly once, when it starts",
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
    [SEMANTIC_STATUS_TYPE_ARGUMENT_INVALID] = ERROR_CODE_TYPE_ARGUMENT_INVALID,
    [SEMANTIC_STATUS_TYPE_ARGUMENT_MISPLACED] = ERROR_CODE_TYPE_ARGUMENT_MISPLACED,
    [SEMANTIC_STATUS_CONSTANT_CYCLE] = ERROR_CODE_CONSTANT_CYCLE,
    [SEMANTIC_STATUS_CONSTANT_DIVISION_BY_ZERO] = ERROR_CODE_CONSTANT_DIVISION_BY_ZERO,
    [SEMANTIC_STATUS_CONSTANT_NOT_CONSTANT] = ERROR_CODE_CONSTANT_NOT_CONSTANT,
    [SEMANTIC_STATUS_CONSTANT_OPERAND_TYPE_INVALID] = ERROR_CODE_CONSTANT_OPERAND_TYPE_INVALID,
    [SEMANTIC_STATUS_CONSTANT_OVERFLOW] = ERROR_CODE_CONSTANT_OVERFLOW,
    [SEMANTIC_STATUS_CONSTANT_TEXT_TOO_LONG] = ERROR_CODE_CONSTANT_TEXT_TOO_LONG,
    [SEMANTIC_STATUS_CONSTANT_TOO_DEEP] = ERROR_CODE_CONSTANT_TOO_DEEP,
    [SEMANTIC_STATUS_CONSTANT_TYPE_INVALID] = ERROR_CODE_CONSTANT_TYPE_INVALID,
    [SEMANTIC_STATUS_CONSTANT_TYPE_MISMATCH] = ERROR_CODE_CONSTANT_TYPE_MISMATCH,
    [SEMANTIC_STATUS_CONSTANT_VALUE_NOT_REPRESENTABLE] =
        ERROR_CODE_CONSTANT_VALUE_NOT_REPRESENTABLE,
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
    [PARSE_STATUS_RETURN_VALUE_MISSING] = ERROR_CODE_RETURN_VALUE_MISSING,
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
    [TOKENIZE_STATUS_TYPE_PARAMETER_INVALID] = ERROR_CODE_TYPE_PARAMETER_INVALID,
    [TOKENIZE_STATUS_RUNE_LITERAL_EMPTY] = ERROR_CODE_RUNE_LITERAL_EMPTY,
    [TOKENIZE_STATUS_RUNE_LITERAL_ENCODING_INVALID] = ERROR_CODE_RUNE_LITERAL_ENCODING_INVALID,
    [TOKENIZE_STATUS_RUNE_LITERAL_ESCAPE_INVALID] = ERROR_CODE_RUNE_LITERAL_ESCAPE_INVALID,
    [TOKENIZE_STATUS_RUNE_LITERAL_TOO_LONG] = ERROR_CODE_RUNE_LITERAL_TOO_LONG,
    [TOKENIZE_STATUS_RUNE_LITERAL_UNTERMINATED] = ERROR_CODE_RUNE_LITERAL_UNTERMINATED,
    [TOKENIZE_STATUS_STRING_LITERAL_ESCAPE_INVALID] = ERROR_CODE_STRING_LITERAL_ESCAPE_INVALID,
    [TOKENIZE_STATUS_STRING_LITERAL_ENCODING_INVALID] = ERROR_CODE_STRING_LITERAL_ENCODING_INVALID,
    [TOKENIZE_STATUS_STRING_LITERAL_UNTERMINATED] = ERROR_CODE_STRING_LITERAL_UNTERMINATED,
    [TOKENIZE_STATUS_TOKEN_LIMIT] = ERROR_CODE_TOKEN_LIMIT,
    [TOKENIZE_STATUS_UNEXPECTED_CHARACTER] = ERROR_CODE_UNEXPECTED_CHARACTER,
};

// The code of each status of the x64 build.
static Error_Code const REPORT_X64_CODES[] = {
    [X64_BUILD_STATUS_APPEND_TARGET_INVALID] = ERROR_CODE_APPEND_TARGET_INVALID,
    [X64_BUILD_STATUS_COMPARISON_ARRAY_KIND_MISMATCH] = ERROR_CODE_COMPARISON_ARRAY_KIND_MISMATCH,
    [X64_BUILD_STATUS_COMPARISON_ARRAY_LENGTH_MISMATCH] = ERROR_CODE_COMPARISON_ARRAY_LENGTH_MISMATCH,
    [X64_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID] = ERROR_CODE_ARITHMETIC_OPERAND_TYPE_INVALID,
    [X64_BUILD_STATUS_ARRAY_ELEMENT_TYPE_MISMATCH] = ERROR_CODE_ARRAY_ELEMENT_TYPE_MISMATCH,
    [X64_BUILD_STATUS_ARRAY_EXPECTED] = ERROR_CODE_ARRAY_EXPECTED,
    [X64_BUILD_STATUS_ARRAY_INITIALIZER_INVALID] = ERROR_CODE_ARRAY_INITIALIZER_INVALID,
    [X64_BUILD_STATUS_ARRAY_LENGTH_MISMATCH] = ERROR_CODE_ARRAY_LENGTH_MISMATCH,
    [X64_BUILD_STATUS_CALL_ARGUMENT_COUNT_MISMATCH] = ERROR_CODE_CALL_ARGUMENT_COUNT_MISMATCH,
    [X64_BUILD_STATUS_CALL_ARGUMENT_TYPE_MISMATCH] = ERROR_CODE_CALL_ARGUMENT_TYPE_MISMATCH,
    [X64_BUILD_STATUS_CALL_RESULT_MISSING] = ERROR_CODE_CALL_RESULT_MISSING,
    [X64_BUILD_STATUS_CALL_TARGET_NOT_PROCEDURE] = ERROR_CODE_CALL_TARGET_NOT_POINTER,
    [X64_BUILD_STATUS_CAST_INVALID] = ERROR_CODE_CAST_INVALID,
    [X64_BUILD_STATUS_COMPARISON_OPERAND_TYPE_INVALID] = ERROR_CODE_COMPARISON_OPERAND_TYPE_INVALID,
    [X64_BUILD_STATUS_CONDITION_TYPE_INVALID] = ERROR_CODE_CONDITION_TYPE_INVALID,
    [X64_BUILD_STATUS_ENUM_MEMBER_UNKNOWN] = ERROR_CODE_ENUM_MEMBER_UNKNOWN,
    [X64_BUILD_STATUS_ENUM_TYPE_UNKNOWN] = ERROR_CODE_ENUM_TYPE_UNKNOWN,
    [X64_BUILD_STATUS_FIELD_TARGET_READ_ONLY] = ERROR_CODE_FIELD_TARGET_READ_ONLY,
    [X64_BUILD_STATUS_IDENTIFIER_NOT_VALUE] = ERROR_CODE_IDENTIFIER_NOT_VALUE,
    [X64_BUILD_STATUS_INDEX_TYPE_INVALID] = ERROR_CODE_INDEX_TYPE_INVALID,
    [X64_BUILD_STATUS_LOGICAL_OPERAND_TYPE_INVALID] = ERROR_CODE_LOGICAL_OPERAND_TYPE_INVALID,
    [X64_BUILD_STATUS_MAKE_TYPE_INVALID] = ERROR_CODE_MAKE_TYPE_INVALID,
    [X64_BUILD_STATUS_MATCH_CASE_DUPLICATE] = ERROR_CODE_MATCH_CASE_DUPLICATE,
    [X64_BUILD_STATUS_MATCH_PARTIAL_INVALID] = ERROR_CODE_MATCH_PARTIAL_INVALID,
    [X64_BUILD_STATUS_MATCH_PATTERN_NOT_CONSTANT] = ERROR_CODE_MATCH_PATTERN_NOT_CONSTANT,
    [X64_BUILD_STATUS_MATCH_PATTERN_TYPE_MISMATCH] = ERROR_CODE_MATCH_PATTERN_TYPE_MISMATCH,
    [X64_BUILD_STATUS_MATCH_RANGE_EMPTY] = ERROR_CODE_MATCH_RANGE_EMPTY,
    [X64_BUILD_STATUS_MATCH_SUBJECT_TYPE_INVALID] = ERROR_CODE_MATCH_SUBJECT_TYPE_INVALID,
    [X64_BUILD_STATUS_NIL_TYPE_UNKNOWN] = ERROR_CODE_NIL_TYPE_UNKNOWN,
    [X64_BUILD_STATUS_POINTER_EXPECTED] = ERROR_CODE_POINTER_EXPECTED,
    [X64_BUILD_STATUS_POINTER_TARGET_TYPE_MISMATCH] = ERROR_CODE_POINTER_TARGET_TYPE_MISMATCH,
    [X64_BUILD_STATUS_RANGE_TYPE_INVALID] = ERROR_CODE_RANGE_TYPE_INVALID,
    [X64_BUILD_STATUS_RESULT_COUNT_MISMATCH] = ERROR_CODE_RESULT_COUNT_MISMATCH,
    [X64_BUILD_STATUS_RETURN_COUNT_MISMATCH] = ERROR_CODE_RETURN_COUNT_MISMATCH,
    [X64_BUILD_STATUS_RETURN_TYPE_MISMATCH] = ERROR_CODE_RETURN_TYPE_MISMATCH,
    [X64_BUILD_STATUS_RETURN_TYPE_MISSING] = ERROR_CODE_RETURN_TYPE_MISSING,
    [X64_BUILD_STATUS_STRUCT_FIELD_DUPLICATE] = ERROR_CODE_STRUCT_FIELD_DUPLICATE,
    [X64_BUILD_STATUS_STRUCT_FIELD_TYPE_MISMATCH] = ERROR_CODE_STRUCT_FIELD_TYPE_MISMATCH,
    [X64_BUILD_STATUS_STRUCT_FIELD_UNKNOWN] = ERROR_CODE_STRUCT_FIELD_UNKNOWN,
    [X64_BUILD_STATUS_STRUCT_RECURSIVE] = ERROR_CODE_STRUCT_RECURSIVE,
    [X64_BUILD_STATUS_TYPE_UNSUPPORTED] = ERROR_CODE_TYPE_UNSUPPORTED,
    [X64_BUILD_STATUS_UNION_CASE_UNKNOWN] = ERROR_CODE_UNION_CASE_UNKNOWN,
    [X64_BUILD_STATUS_UNION_DISCRIMINANT_INVALID] = ERROR_CODE_UNION_DISCRIMINANT_INVALID,
    [X64_BUILD_STATUS_UNION_FIELD_KIND_MISMATCH] = ERROR_CODE_UNION_FIELD_KIND_MISMATCH,
    [X64_BUILD_STATUS_UNION_FIELD_UNMATCHED] = ERROR_CODE_UNION_FIELD_UNMATCHED,
    [X64_BUILD_STATUS_UNION_KIND_NOT_CONSTANT] = ERROR_CODE_UNION_KIND_NOT_CONSTANT,
    [X64_BUILD_STATUS_VARIABLE_TYPE_MISMATCH] = ERROR_CODE_VARIABLE_TYPE_MISMATCH,
    [X64_BUILD_STATUS_ADDRESS_INVALID] = ERROR_CODE_X64_ADDRESS_INVALID,
    [X64_BUILD_STATUS_ARRAY_TYPE_MISPLACED] = ERROR_CODE_X64_ARRAY_TYPE_MISPLACED,
    [X64_BUILD_STATUS_CONSTANT_COUNT_EXCEEDED] = ERROR_CODE_X64_CONSTANT_COUNT_EXCEEDED,
    [X64_BUILD_STATUS_EXPRESSION_TOO_DEEP] = ERROR_CODE_X64_EXPRESSION_TOO_DEEP,
    [X64_BUILD_STATUS_FIELD_BASE_INVALID] = ERROR_CODE_X64_FIELD_BASE_INVALID,
    [X64_BUILD_STATUS_FOREIGN_TARGET_UNSUPPORTED] = ERROR_CODE_X64_FOREIGN_TARGET_UNSUPPORTED,
    [X64_BUILD_STATUS_LIBRARY_INVALID] = ERROR_CODE_X64_LIBRARY_INVALID,
    [X64_BUILD_STATUS_MAIN_INVALID] = ERROR_CODE_X64_MAIN_INVALID,
    [X64_BUILD_STATUS_MATCH_NOT_EXHAUSTIVE] = ERROR_CODE_X64_MATCH_NOT_EXHAUSTIVE,
    [X64_BUILD_STATUS_OUTPUT_TOO_LARGE] = ERROR_CODE_X64_OUTPUT_TOO_LARGE,
    [X64_BUILD_STATUS_STRUCT_EXPECTED] = ERROR_CODE_X64_STRUCT_EXPECTED,
    [X64_BUILD_STATUS_STRUCT_FIELD_MISSING] = ERROR_CODE_X64_STRUCT_FIELD_MISSING,
    [X64_BUILD_STATUS_TYPE_INFO_MISPLACED] = ERROR_CODE_X64_TYPE_INFO_MISPLACED,
    [X64_BUILD_STATUS_TYPE_INFO_UNSUPPORTED] = ERROR_CODE_X64_TYPE_INFO_UNSUPPORTED,
    [X64_BUILD_STATUS_UNSUPPORTED] = ERROR_CODE_X64_UNSUPPORTED,
};

// The code of each status of the wasm build.
static Error_Code const REPORT_WASM_CODES[] = {
    [WASM_BUILD_STATUS_APPEND_TARGET_INVALID] = ERROR_CODE_APPEND_TARGET_INVALID,
    [WASM_BUILD_STATUS_COMPARISON_ARRAY_KIND_MISMATCH] = ERROR_CODE_COMPARISON_ARRAY_KIND_MISMATCH,
    [WASM_BUILD_STATUS_COMPARISON_ARRAY_LENGTH_MISMATCH] = ERROR_CODE_COMPARISON_ARRAY_LENGTH_MISMATCH,
    [WASM_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID] =
        ERROR_CODE_ARITHMETIC_OPERAND_TYPE_INVALID,
    [WASM_BUILD_STATUS_ARRAY_ELEMENT_TYPE_MISMATCH] = ERROR_CODE_ARRAY_ELEMENT_TYPE_MISMATCH,
    [WASM_BUILD_STATUS_ARRAY_EXPECTED] = ERROR_CODE_ARRAY_EXPECTED,
    [WASM_BUILD_STATUS_ARRAY_INITIALIZER_INVALID] = ERROR_CODE_ARRAY_INITIALIZER_INVALID,
    [WASM_BUILD_STATUS_ARRAY_LENGTH_MISMATCH] = ERROR_CODE_ARRAY_LENGTH_MISMATCH,
    [WASM_BUILD_STATUS_CALL_ARGUMENT_COUNT_MISMATCH] = ERROR_CODE_CALL_ARGUMENT_COUNT_MISMATCH,
    [WASM_BUILD_STATUS_CALL_ARGUMENT_TYPE_MISMATCH] = ERROR_CODE_CALL_ARGUMENT_TYPE_MISMATCH,
    [WASM_BUILD_STATUS_CALL_RESULT_MISSING] = ERROR_CODE_CALL_RESULT_MISSING,
    [WASM_BUILD_STATUS_CALL_TARGET_NOT_PROCEDURE] = ERROR_CODE_CALL_TARGET_NOT_POINTER,
    [WASM_BUILD_STATUS_CAST_INVALID] = ERROR_CODE_CAST_INVALID,
    [WASM_BUILD_STATUS_COMPARISON_OPERAND_TYPE_INVALID] =
        ERROR_CODE_COMPARISON_OPERAND_TYPE_INVALID,
    [WASM_BUILD_STATUS_CONDITION_TYPE_INVALID] = ERROR_CODE_CONDITION_TYPE_INVALID,
    [WASM_BUILD_STATUS_ENUM_MEMBER_UNKNOWN] = ERROR_CODE_ENUM_MEMBER_UNKNOWN,
    [WASM_BUILD_STATUS_ENUM_TYPE_UNKNOWN] = ERROR_CODE_ENUM_TYPE_UNKNOWN,
    [WASM_BUILD_STATUS_FIELD_TARGET_READ_ONLY] = ERROR_CODE_FIELD_TARGET_READ_ONLY,
    [WASM_BUILD_STATUS_IDENTIFIER_NOT_VALUE] = ERROR_CODE_IDENTIFIER_NOT_VALUE,
    [WASM_BUILD_STATUS_INDEX_TYPE_INVALID] = ERROR_CODE_INDEX_TYPE_INVALID,
    [WASM_BUILD_STATUS_LOGICAL_OPERAND_TYPE_INVALID] = ERROR_CODE_LOGICAL_OPERAND_TYPE_INVALID,
    [WASM_BUILD_STATUS_MAKE_TYPE_INVALID] = ERROR_CODE_MAKE_TYPE_INVALID,
    [WASM_BUILD_STATUS_MATCH_CASE_DUPLICATE] = ERROR_CODE_MATCH_CASE_DUPLICATE,
    [WASM_BUILD_STATUS_MATCH_PARTIAL_INVALID] = ERROR_CODE_MATCH_PARTIAL_INVALID,
    [WASM_BUILD_STATUS_MATCH_PATTERN_NOT_CONSTANT] = ERROR_CODE_MATCH_PATTERN_NOT_CONSTANT,
    [WASM_BUILD_STATUS_MATCH_PATTERN_TYPE_MISMATCH] = ERROR_CODE_MATCH_PATTERN_TYPE_MISMATCH,
    [WASM_BUILD_STATUS_MATCH_RANGE_EMPTY] = ERROR_CODE_MATCH_RANGE_EMPTY,
    [WASM_BUILD_STATUS_MATCH_SUBJECT_TYPE_INVALID] = ERROR_CODE_MATCH_SUBJECT_TYPE_INVALID,
    [WASM_BUILD_STATUS_NIL_TYPE_UNKNOWN] = ERROR_CODE_NIL_TYPE_UNKNOWN,
    [WASM_BUILD_STATUS_POINTER_EXPECTED] = ERROR_CODE_POINTER_EXPECTED,
    [WASM_BUILD_STATUS_POINTER_TARGET_TYPE_MISMATCH] = ERROR_CODE_POINTER_TARGET_TYPE_MISMATCH,
    [WASM_BUILD_STATUS_RANGE_TYPE_INVALID] = ERROR_CODE_RANGE_TYPE_INVALID,
    [WASM_BUILD_STATUS_RESULT_COUNT_MISMATCH] = ERROR_CODE_RESULT_COUNT_MISMATCH,
    [WASM_BUILD_STATUS_RETURN_COUNT_MISMATCH] = ERROR_CODE_RETURN_COUNT_MISMATCH,
    [WASM_BUILD_STATUS_RETURN_TYPE_MISMATCH] = ERROR_CODE_RETURN_TYPE_MISMATCH,
    [WASM_BUILD_STATUS_RETURN_TYPE_MISSING] = ERROR_CODE_RETURN_TYPE_MISSING,
    [WASM_BUILD_STATUS_STRUCT_FIELD_DUPLICATE] = ERROR_CODE_STRUCT_FIELD_DUPLICATE,
    [WASM_BUILD_STATUS_STRUCT_FIELD_TYPE_MISMATCH] = ERROR_CODE_STRUCT_FIELD_TYPE_MISMATCH,
    [WASM_BUILD_STATUS_STRUCT_FIELD_UNKNOWN] = ERROR_CODE_STRUCT_FIELD_UNKNOWN,
    [WASM_BUILD_STATUS_STRUCT_RECURSIVE] = ERROR_CODE_STRUCT_RECURSIVE,
    [WASM_BUILD_STATUS_TYPE_UNSUPPORTED] = ERROR_CODE_TYPE_UNSUPPORTED,
    [WASM_BUILD_STATUS_UNION_CASE_UNKNOWN] = ERROR_CODE_UNION_CASE_UNKNOWN,
    [WASM_BUILD_STATUS_UNION_DISCRIMINANT_INVALID] = ERROR_CODE_UNION_DISCRIMINANT_INVALID,
    [WASM_BUILD_STATUS_UNION_FIELD_KIND_MISMATCH] = ERROR_CODE_UNION_FIELD_KIND_MISMATCH,
    [WASM_BUILD_STATUS_UNION_FIELD_UNMATCHED] = ERROR_CODE_UNION_FIELD_UNMATCHED,
    [WASM_BUILD_STATUS_UNION_KIND_NOT_CONSTANT] = ERROR_CODE_UNION_KIND_NOT_CONSTANT,
    [WASM_BUILD_STATUS_VARIABLE_TYPE_MISMATCH] = ERROR_CODE_VARIABLE_TYPE_MISMATCH,
    [WASM_BUILD_STATUS_ADDRESS_INVALID] = ERROR_CODE_WASM_ADDRESS_INVALID,
    [WASM_BUILD_STATUS_ARRAY_TYPE_MISPLACED] = ERROR_CODE_WASM_ARRAY_TYPE_MISPLACED,
    [WASM_BUILD_STATUS_BUILT_IN_UNSUPPORTED] = ERROR_CODE_WASM_BUILT_IN_UNSUPPORTED,
    [WASM_BUILD_STATUS_DEFINITION_NAME_RESERVED] = ERROR_CODE_WASM_DEFINITION_NAME_RESERVED,
    [WASM_BUILD_STATUS_EXPRESSION_TOO_DEEP] = ERROR_CODE_WASM_EXPRESSION_TOO_DEEP,
    [WASM_BUILD_STATUS_FIELD_BASE_INVALID] = ERROR_CODE_WASM_FIELD_BASE_INVALID,
    [WASM_BUILD_STATUS_FOREIGN_TARGET_UNSUPPORTED] = ERROR_CODE_WASM_FOREIGN_TARGET_UNSUPPORTED,
    [WASM_BUILD_STATUS_MATCH_NOT_EXHAUSTIVE] = ERROR_CODE_WASM_MATCH_NOT_EXHAUSTIVE,
    [WASM_BUILD_STATUS_OUTPUT_TOO_LARGE] = ERROR_CODE_WASM_OUTPUT_TOO_LARGE,
    [WASM_BUILD_STATUS_STRUCT_EXPECTED] = ERROR_CODE_WASM_STRUCT_EXPECTED,
    [WASM_BUILD_STATUS_STRUCT_FIELD_MISSING] = ERROR_CODE_WASM_STRUCT_FIELD_MISSING,
    [WASM_BUILD_STATUS_TYPE_INFO_MISPLACED] = ERROR_CODE_WASM_TYPE_INFO_MISPLACED,
    [WASM_BUILD_STATUS_TYPE_INFO_UNSUPPORTED] = ERROR_CODE_WASM_TYPE_INFO_UNSUPPORTED,
};

static void   report_emit(Reporter const *reporter, Report_Error const *error);
static void   report_emit_at_subject(Reporter const *reporter, Error_Code code, char const *path,
                                     Str source, Str subject, Report_Texts texts);
static Str    report_expand(char const *template, Report_Error const *error, char *buffer,
                            size_t buffer_size_bytes);
static bool   report_is_word_byte(char character);
static size_t report_length_character(Str source, size_t offset);
static size_t report_length_literal(Str source, size_t offset);
static size_t report_length_token(Str source, size_t offset, Token_Kind kind);
static size_t report_length_word(Str source, size_t offset);
static void   report_text_append(Report_Text *text, Str part);
static void   report_text_append_subject(Report_Text *text, Str subject);
static bool   report_text_contains(Str text, Str part);

void report_general(Reporter const *reporter, Error_Code code, char const *path,
                    int64_t number) {
    assert(reporter != NULL);
    assert(code > 0);

    Report_Error error = {
        .path = path,
        .source = STR_EMPTY,
        .subject = STR_EMPTY,
        .detail = STR_EMPTY,
        .other = STR_EMPTY,
        .target = STR_EMPTY,
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
    case TOKENIZE_STATUS_TYPE_PARAMETER_INVALID:
        // The colon or the `$`, and the name.
        length = 1 + report_length_word(source, offset + 1);
        detail = str_make(source.bytes + offset + 1, length - 1);
        break;
    case TOKENIZE_STATUS_RUNE_LITERAL_ESCAPE_INVALID:
    case TOKENIZE_STATUS_STRING_LITERAL_ESCAPE_INVALID:
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
        .other = STR_EMPTY,
        .target = STR_EMPTY,
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
    // When the parser wants something about the indentation, that line is what is wrong. When it
    // wants something else, such as an arrow, the line before it ended too soon, and the error is
    // at its end.
    bool is_indentation_wanted =
        result.status == PARSE_STATUS_UNEXPECTED_TOKEN &&
        (report_text_contains(result.expected, STR("indent")) ||
         report_text_contains(result.expected, STR("start of a line")));
    bool is_first_line = token_elements[token_index].source_offset == 0;
    if (token_elements[token_index].kind == TOKEN_KIND_INDENTATION &&
        (is_indentation_wanted || is_first_line) &&
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
        .other = STR_EMPTY,
        .target = STR_EMPTY,
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
                              "at most %d files, %d imports below each other, and %d bytes of "
                              "names",
                              PACKAGE_FILE_COUNT_MAX, PACKAGE_DEPTH_MAX,
                              PACKAGE_NAMES_SIZE_BYTES_MAX);
        assert(length > 0);
        detail = str_make(limits, (size_t)length);
    }
    Report_Texts texts = {.detail = detail, .other = STR_EMPTY, .target = STR_EMPTY, .number = 0,
                          .second_offset = 0, .has_second = false};
    report_emit_at_subject(reporter, code, path, source, result.subject, texts);
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
    Report_Texts texts = {
        .detail = STR_EMPTY,
        .other = STR_EMPTY,
        .target = STR_EMPTY,
        .number = number,
        .second_offset = 0,
        .has_second = false,
    };
    report_emit_at_subject(reporter, code, path, source, result.subject, texts);
}

void report_specialize_error(Reporter const *reporter, char const *path, Str source,
                             Specialize_Result result) {
    assert(reporter != NULL);
    assert(path != NULL);
    assert(result.status != SPECIALIZE_STATUS_OK);
    assert(result.status != SPECIALIZE_STATUS_SEMANTIC);

    Error_Code code = ERROR_CODE_SPECIALIZATION_LIMIT;
    switch (result.status) {
    case SPECIALIZE_STATUS_ARGUMENT_COUNT_MISMATCH:
        code = ERROR_CODE_CALL_ARGUMENT_COUNT_MISMATCH;
        break;
    case SPECIALIZE_STATUS_ARGUMENT_TYPE_MISMATCH:
        code = ERROR_CODE_CALL_ARGUMENT_TYPE_MISMATCH;
        break;
    case SPECIALIZE_STATUS_LIMIT:
        code = ERROR_CODE_SPECIALIZATION_LIMIT;
        break;
    case SPECIALIZE_STATUS_PROCEDURE_NOT_CALLED:
        code = ERROR_CODE_PROCEDURE_NOT_CALLED;
        break;
    case SPECIALIZE_STATUS_STRUCT_NOT_POLYMORPHIC:
        code = ERROR_CODE_STRUCT_NOT_POLYMORPHIC;
        break;
    case SPECIALIZE_STATUS_STRUCT_TYPE_ARGUMENT_COUNT_MISMATCH:
        code = ERROR_CODE_STRUCT_TYPE_ARGUMENT_COUNT_MISMATCH;
        break;
    case SPECIALIZE_STATUS_TYPE_PARAMETER_CONFLICT:
        code = ERROR_CODE_TYPE_PARAMETER_CONFLICT;
        break;
    case SPECIALIZE_STATUS_TYPE_PARAMETER_DUPLICATE:
        code = ERROR_CODE_TYPE_PARAMETER_DUPLICATE;
        break;
    case SPECIALIZE_STATUS_TYPE_PARAMETER_MISPLACED:
        code = ERROR_CODE_TYPE_PARAMETER_MISPLACED;
        break;
    case SPECIALIZE_STATUS_TYPE_PARAMETER_UNDEDUCED:
        code = ERROR_CODE_TYPE_PARAMETER_UNDEDUCED;
        break;
    case SPECIALIZE_STATUS_OK:
    case SPECIALIZE_STATUS_SEMANTIC:
        // Not errors of the specialization itself, see the assertions above.
        assert(false);
        break;
    }
    Report_Texts texts = {
        .detail = result.detail,
        .other = result.other,
        .target = result.target,
        .number = result.number,
        .second_offset = 0,
        .has_second = false,
    };
    report_emit_at_subject(reporter, code, path, source, result.subject, texts);
}

void report_x64_error(Reporter const *reporter, char const *path, Str source,
                      X64_Build_Result result) {
    assert(reporter != NULL);
    assert(path != NULL);
    assert(result.status != X64_BUILD_STATUS_OK);

    Error_Code code = REPORT_X64_CODES[result.status];
    assert(code > 0);

    // These are about the whole program, so they have no place in the source.
    if (result.status == X64_BUILD_STATUS_OUTPUT_TOO_LARGE) {
        report_general(reporter, code, path, OUTPUT_SIZE_BYTES_MAX);
        return;
    }
    if (result.status == X64_BUILD_STATUS_MAIN_INVALID && result.subject.bytes == NULL) {
        report_general(reporter, ERROR_CODE_X64_MAIN_MISSING, path, 0);
        return;
    }

    // The limits that two of the errors tell.
    int64_t number = 0;
    if (result.status == X64_BUILD_STATUS_CONSTANT_COUNT_EXCEEDED) {
        number = X64_CONSTANT_COUNT_MAX;
    }
    if (result.status == X64_BUILD_STATUS_EXPRESSION_TOO_DEEP) {
        number = X64_EXPRESSION_DEPTH_MAX;
    }
    Report_Texts texts = {
        .detail = STR_EMPTY,
        .other = STR_EMPTY,
        .target = STR("x64"),
        .number = number,
        .second_offset = 0,
        .has_second = false,
    };
    if (result.status == X64_BUILD_STATUS_RETURN_TYPE_MISMATCH) {
        texts.detail = result.type_expected;
        texts.other = result.type_found;
        texts.second_offset = result.value_offset;
        texts.has_second = true;
    }
    char first_length[32] = "";
    char second_length[32] = "";
    if (result.status == X64_BUILD_STATUS_COMPARISON_ARRAY_LENGTH_MISMATCH) {
        // The text has the word too, which is "element" for one and "elements" for any other
        // number.
        int first_size = snprintf(first_length, sizeof(first_length), "%" PRId64 " element%s",
                                  result.length_first, result.length_first == 1 ? "" : "s");
        int second_size = snprintf(second_length, sizeof(second_length), "%" PRId64 " element%s",
                                   result.length_second, result.length_second == 1 ? "" : "s");
        texts.detail = str_make(first_length, (size_t)first_size);
        texts.other = str_make(second_length, (size_t)second_size);
        texts.second_offset = result.value_offset;
        texts.has_second = true;
    }
    if (result.status == X64_BUILD_STATUS_COMPARISON_OPERAND_TYPE_INVALID ||
        result.status == X64_BUILD_STATUS_COMPARISON_ARRAY_KIND_MISMATCH) {
        texts.detail = result.type_expected;
        texts.other = result.type_found;
    }
    report_emit_at_subject(reporter, code, path, source, result.subject, texts);
}

void report_wasm_error(Reporter const *reporter, char const *path, Str source,
                       Wasm_Build_Result result) {
    assert(reporter != NULL);
    assert(path != NULL);
    assert(result.status != WASM_BUILD_STATUS_OK);

    Error_Code code = REPORT_WASM_CODES[result.status];
    assert(code > 0);

    // The output being too large is about the whole program, so it has no place in the source.
    if (result.status == WASM_BUILD_STATUS_OUTPUT_TOO_LARGE) {
        report_general(reporter, code, path, OUTPUT_SIZE_BYTES_MAX);
        return;
    }
    Report_Texts texts = {
        .detail = result.detail,
        .other = STR_EMPTY,
        .target = STR("wasm"),
        .number = 0,
        .second_offset = 0,
        .has_second = false,
    };
    if (result.status == WASM_BUILD_STATUS_RETURN_TYPE_MISMATCH) {
        texts.detail = result.type_expected;
        texts.other = result.type_found;
        texts.second_offset = result.value_offset;
        texts.has_second = true;
    }
    char first_length[32] = "";
    char second_length[32] = "";
    if (result.status == WASM_BUILD_STATUS_COMPARISON_ARRAY_LENGTH_MISMATCH) {
        // The text has the word too, which is "element" for one and "elements" for any other
        // number.
        int first_size = snprintf(first_length, sizeof(first_length), "%" PRId64 " element%s",
                                  result.length_first, result.length_first == 1 ? "" : "s");
        int second_size = snprintf(second_length, sizeof(second_length), "%" PRId64 " element%s",
                                   result.length_second, result.length_second == 1 ? "" : "s");
        texts.detail = str_make(first_length, (size_t)first_size);
        texts.other = str_make(second_length, (size_t)second_size);
        texts.second_offset = result.value_offset;
        texts.has_second = true;
    }
    if (result.status == WASM_BUILD_STATUS_COMPARISON_OPERAND_TYPE_INVALID ||
        result.status == WASM_BUILD_STATUS_COMPARISON_ARRAY_KIND_MISMATCH) {
        texts.detail = result.type_expected;
        texts.other = result.type_found;
    }
    report_emit_at_subject(reporter, code, path, source, result.subject, texts);
}

void report_wasm_text_error(Reporter const *reporter, char const *path,
                            Wasm_Text_Result result) {
    assert(reporter != NULL);
    assert(path != NULL);
    assert(result.status != WASM_TEXT_STATUS_OK);

    switch (result.status) {
    case WASM_TEXT_STATUS_MODULE_MALFORMED:
        report_general(reporter, ERROR_CODE_WASM_TEXT_MODULE_MALFORMED, path,
                       (int64_t)result.error_offset);
        break;
    case WASM_TEXT_STATUS_MODULE_UNSUPPORTED:
        report_general(reporter, ERROR_CODE_WASM_TEXT_MODULE_UNSUPPORTED, path,
                       (int64_t)result.error_offset);
        break;
    case WASM_TEXT_STATUS_OK:
        assert(false);
        break;
    case WASM_TEXT_STATUS_OUTPUT_TOO_LARGE:
        report_general(reporter, ERROR_CODE_WASM_TEXT_OUTPUT_TOO_LARGE, path,
                       OUTPUT_TEXT_SIZE_BYTES_MAX);
        break;
    }
}

void report_x64_text_error(Reporter const *reporter, char const *path, X64_Text_Result result) {
    assert(reporter != NULL);
    assert(path != NULL);
    assert(result.status != X64_TEXT_STATUS_OK);

    switch (result.status) {
    case X64_TEXT_STATUS_EXECUTABLE_MALFORMED:
        report_general(reporter, ERROR_CODE_X64_TEXT_EXECUTABLE_MALFORMED, path,
                       (int64_t)result.error_offset);
        break;
    case X64_TEXT_STATUS_EXECUTABLE_UNSUPPORTED:
        report_general(reporter, ERROR_CODE_X64_TEXT_EXECUTABLE_UNSUPPORTED, path,
                       (int64_t)result.error_offset);
        break;
    case X64_TEXT_STATUS_OK:
        assert(false);
        break;
    case X64_TEXT_STATUS_OUTPUT_TOO_LARGE:
        report_general(reporter, ERROR_CODE_X64_TEXT_OUTPUT_TOO_LARGE, path,
                       OUTPUT_TEXT_SIZE_BYTES_MAX);
        break;
    }
}

/**
 * Reports an error that is about `subject`, a view of a source, which is the text that the carets
 * are under. The source is the one that `subject` is part of: the program, or a file that it
 * imports, which `reporter` finds, and then the path and the lines are those of that file. A
 * subject that is part of no source has an error without a place in it.
 */
static void report_emit_at_subject(Reporter const *reporter, Error_Code code, char const *path,
                                   Str source, Str subject, Report_Texts texts) {
    assert(reporter != NULL);
    assert(path != NULL);

    // A text that the specialization wrote is told at the place of the source that it is made of.
    subject = specialize_source_of(reporter->texts, subject);
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
        .detail = texts.detail,
        .other = texts.other,
        .target = texts.target,
        .span_offset = 0,
        .span_length = subject.length,
        .number = texts.number,
        .second_offset = texts.second_offset,
        .code = code,
        .has_second = false,
        .has_span = false,
    };
    // The subject views the source, so its distance from the start is its offset. Something that
    // the compiler adds, such as the built-in struct of `type_info_of`, is not in any source.
    if (subject.bytes != NULL && subject.bytes >= file_source.bytes &&
        subject.bytes <= file_source.bytes + file_source.length) {
        error.has_span = true;
        error.span_offset = (size_t)(subject.bytes - file_source.bytes);
        error.has_second = texts.has_second;
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
    if (kind == TOKEN_KIND_LABEL || kind == TOKEN_KIND_TYPE_PARAMETER) {
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

/**
 * Returns whether `part` is somewhere in `text`.
 */
static bool report_text_contains(Str text, Str part) {
    assert(text.bytes != NULL || text.length == 0);

    if (part.length > text.length) {
        return false;
    }
    for (size_t i = 0; i + part.length <= text.length; i += 1) {
        if (memcmp(text.bytes + i, part.bytes, part.length) == 0) {
            return true;
        }
    }
    return false;
}

static bool report_is_word_byte(char character) {
    bool is_lower = character >= 'a' && character <= 'z';
    bool is_upper = character >= 'A' && character <= 'Z';
    bool is_letter = is_lower || is_upper;
    bool is_digit = character >= '0' && character <= '9';
    return is_letter || is_digit || character == '_';
}

/**
 * Returns the length in bytes of the name that starts at `offset` in `source`, which is the
 * letters, digits and underscores that follow there. It may be zero.
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
 * Returns the length in bytes of the literal that starts at `offset` in `source`. A string or a
 * rune goes to its closing quote, or to the end of its line when it has none. A number is the
 * letters, digits, underscores and dots that follow.
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
    char second[REPORT_PART_SIZE_BYTES] = {0};
    // The second place is underlined as the token that starts there.
    size_t second_length = 1;
    if (error->has_second && error->second_offset < error->source.length) {
        second_length = report_length_token(error->source, error->second_offset,
                                            TOKEN_KIND_IDENTIFIER);
    }
    Diagnostic diagnostic = {
        .path = STR_EMPTY,
        .source = error->source,
        .title = report_expand(entry->title, error, title, sizeof(title)),
        .label = report_expand(entry->label, error, label, sizeof(label)),
        .note = report_expand(entry->note, error, note, sizeof(note)),
        .help = report_expand(entry->help, error, help, sizeof(help)),
        .span_offset = error->span_offset,
        .span_length = error->span_length,
        .second_label = report_expand(entry->second_label, error, second, sizeof(second)),
        .second_offset = error->second_offset,
        .second_length = second_length,
        .code = (uint32_t)error->code,
        .has_second = error->has_second,
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
        if (strncmp(rest, "{{", sizeof("{{") - 1) == 0) {
            // Two braces are one, for text that has a brace in it.
            report_text_append(&text, STR("{"));
            index += sizeof("{{") - 1;
        } else if (strncmp(rest, "{subject}", sizeof("{subject}") - 1) == 0) {
            report_text_append_subject(&text, error->subject);
            index += sizeof("{subject}") - 1;
        } else if (strncmp(rest, "{detail}", sizeof("{detail}") - 1) == 0) {
            report_text_append(&text, error->detail);
            index += sizeof("{detail}") - 1;
        } else if (strncmp(rest, "{other}", sizeof("{other}") - 1) == 0) {
            report_text_append(&text, error->other);
            index += sizeof("{other}") - 1;
        } else if (strncmp(rest, "{target}", sizeof("{target}") - 1) == 0) {
            report_text_append(&text, error->target);
            index += sizeof("{target}") - 1;
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
