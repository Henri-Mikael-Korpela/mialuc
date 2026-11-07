#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <mialu/common_types.h>
#include <mialu/wasm-text.h>
#include <mialu/wasm.h>

// The text follows the layout of `wasm2wat` from the WebAssembly Binary Toolkit, which is what
// people who read WebAssembly text are used to: types first, then functions, tables, memories,
// globals, exports, elements and data, with the index of each in a `(;index;)` comment.

enum {
    // The longest decimal number that is written: `UINT64_MAX` has 20 digits.
    WASM_TEXT_DECIMAL_DIGITS_MAX = 20,
    // The natural alignment of a 64-bit load or store, as a power of two: 8 bytes.
    WASM_TEXT_ALIGNMENT_I64 = 3,
    // The same for a 32-bit load or store: 4 bytes.
    WASM_TEXT_ALIGNMENT_I32 = 2,
};

/**
 * Where the text is written. A write that does not fit sets `is_overflowed` and the rest of the
 * writes are ignored, so the code that writes does not check after every write. The flag is
 * checked once, when the text is finished.
 */
typedef struct {
    char *bytes;
    size_t capacity;
    size_t length;
    // How many structured instructions are open where the next instruction is written, which is
    // how far it is indented.
    size_t block_depth;
    bool is_overflowed;
} Wasm_Text_Writer;

/**
 * Where the content of a section is in the module, after its id and size. A section that the
 * module does not have is not present, and reads as one with no entries.
 */
typedef struct {
    size_t start;
    size_t end;
    bool is_present;
} Wasm_Text_Section;

/**
 * The sections that the formatter knows. The text shows them in a different order than the
 * binary format has them, so they are found first and formatted afterwards.
 */
typedef struct {
    Wasm_Text_Section code;
    Wasm_Text_Section data;
    Wasm_Text_Section element;
    Wasm_Text_Section export;
    Wasm_Text_Section function;
    Wasm_Text_Section global;
    Wasm_Text_Section import;
    Wasm_Text_Section memory;
    Wasm_Text_Section table;
    Wasm_Text_Section type;
} Wasm_Text_Sections;

/**
 * The state shared by the formatting functions. Reads take bytes from `offset` up to `limit`,
 * which is the end of the part of the module that is being read, so that a read cannot run into
 * the next part. A read that fails records an error and returns zero, and so does every read
 * after it, so the code that reads checks `status` only where a value decides something. Only
 * the first error is kept.
 */
typedef struct {
    Wasm_Text_Writer writer;
    byte const *module;
    size_t module_length;
    size_t offset;
    size_t limit;
    // The entries of the type section, to find the type of a function from its index.
    size_t type_entries_start;
    size_t type_entries_end;
    // The type that was found last, see `wasm_text_type_find`.
    size_t type_cursor_offset;
    size_t error_offset;
    uint32_t type_count;
    uint32_t type_cursor_index;
    // The number of imported functions, which come first in the function index space, so the
    // first function of the function section has this index.
    uint32_t import_count;
    Wasm_Text_Status status;
} Wasm_Text_Formatter;

static bool               wasm_text_fail                              (Wasm_Text_Formatter *formatter,
                                                                       Wasm_Text_Status status,
                                                                       size_t error_offset);
static bool               wasm_text_format_data                       (Wasm_Text_Formatter *formatter,
                                                                       Wasm_Text_Section section);
static bool               wasm_text_format_elements                   (Wasm_Text_Formatter *formatter,
                                                                       Wasm_Text_Section section);
static bool               wasm_text_format_exports                    (Wasm_Text_Formatter *formatter,
                                                                       Wasm_Text_Section section);
static bool               wasm_text_format_function_type              (Wasm_Text_Formatter *formatter);
static bool               wasm_text_format_imports                    (Wasm_Text_Formatter *formatter,
                                                                       Wasm_Text_Section section);
static bool               wasm_text_format_functions                  (Wasm_Text_Formatter *formatter,
                                                                       Wasm_Text_Section function,
                                                                       Wasm_Text_Section code);
static bool               wasm_text_format_functions_body             (Wasm_Text_Formatter *formatter);
static bool               wasm_text_format_functions_body_instructions(Wasm_Text_Formatter *formatter);
static bool               wasm_text_format_functions_body_locals      (Wasm_Text_Formatter *formatter);
static bool               wasm_text_format_globals                    (Wasm_Text_Formatter *formatter,
                                                                       Wasm_Text_Section section);
static bool               wasm_text_format_memories                   (Wasm_Text_Formatter *formatter,
                                                                       Wasm_Text_Section section);
static bool               wasm_text_format_tables                     (Wasm_Text_Formatter *formatter,
                                                                       Wasm_Text_Section section);
static bool               wasm_text_format_types                      (Wasm_Text_Formatter *formatter,
                                                                       Wasm_Text_Section section);
static bool               wasm_text_format_value_type                 (Wasm_Text_Formatter *formatter);
static byte               wasm_text_read_byte                         (Wasm_Text_Formatter *formatter);
static int64_t            wasm_text_read_signed_64                    (Wasm_Text_Formatter *formatter);
static uint32_t           wasm_text_read_unsigned_32                  (Wasm_Text_Formatter *formatter);
static uint32_t           wasm_text_section_count_read                (Wasm_Text_Formatter *formatter,
                                                                       Wasm_Text_Section section);
static bool               wasm_text_section_end                       (Wasm_Text_Formatter *formatter);
static Wasm_Text_Sections wasm_text_sections_find                     (Wasm_Text_Formatter *formatter);
static size_t             wasm_text_type_find                         (Wasm_Text_Formatter *formatter,
                                                                       uint32_t type_index);
static Str                wasm_text_value_type_name                   (byte value_type);
static void               wasm_text_write                             (Wasm_Text_Writer *writer,
                                                                       Str text);
static void               wasm_text_write_instruction                 (Wasm_Text_Writer *writer,
                                                                       Str name);
static void               wasm_text_write_signed                      (Wasm_Text_Writer *writer,
                                                                       int64_t number);
static void               wasm_text_write_string                      (Wasm_Text_Writer *writer,
                                                                       byte const *bytes,
                                                                       size_t length);
static void               wasm_text_write_unsigned                    (Wasm_Text_Writer *writer,
                                                                       uint64_t number);

Wasm_Text_Result wasm_text_format(Array_Slice module, Array_Slice output_buffer) {
    assert(module.elements != NULL);
    assert(output_buffer.elements != NULL);

    Wasm_Text_Formatter formatter = {
        .writer =
            {
                .bytes = output_buffer.elements,
                .capacity = output_buffer.length,
                .length = 0,
                .block_depth = 0,
                .is_overflowed = false,
            },
        .module = module.elements,
        .module_length = module.length,
        .offset = 0,
        .limit = module.length,
        .type_entries_start = 0,
        .type_entries_end = 0,
        .type_cursor_offset = 0,
        .error_offset = 0,
        .type_count = 0,
        .type_cursor_index = 0,
        .import_count = 0,
        .status = WASM_TEXT_STATUS_OK,
    };

    Wasm_Text_Sections sections = wasm_text_sections_find(&formatter);
    bool is_ok = formatter.status == WASM_TEXT_STATUS_OK;

    // The types come first, because formatting them checks them, and the functions rely on that.
    if (is_ok) {
        wasm_text_write(&formatter.writer, STR("(module"));
        is_ok = wasm_text_format_types(&formatter, sections.type);
    }
    if (is_ok) {
        is_ok = wasm_text_format_imports(&formatter, sections.import);
    }
    if (is_ok) {
        is_ok = wasm_text_format_functions(&formatter, sections.function, sections.code);
    }
    if (is_ok) {
        is_ok = wasm_text_format_tables(&formatter, sections.table);
    }
    if (is_ok) {
        is_ok = wasm_text_format_memories(&formatter, sections.memory);
    }
    if (is_ok) {
        is_ok = wasm_text_format_globals(&formatter, sections.global);
    }
    if (is_ok) {
        is_ok = wasm_text_format_exports(&formatter, sections.export);
    }
    if (is_ok) {
        is_ok = wasm_text_format_elements(&formatter, sections.element);
    }
    if (is_ok) {
        is_ok = wasm_text_format_data(&formatter, sections.data);
    }
    if (is_ok) {
        wasm_text_write(&formatter.writer, STR(")\n"));

        // A malformed module is more useful to know about than the overflow that may follow from
        // it, so the overflow is only reported when nothing else went wrong.
        if (formatter.writer.is_overflowed) {
            is_ok = wasm_text_fail(&formatter, WASM_TEXT_STATUS_OUTPUT_TOO_LARGE, 0);
        }
    }

    // Nothing was formatted when there was an error, so no text is handed out.
    size_t length_bytes = 0;
    if (is_ok) {
        length_bytes = formatter.writer.length;
    }

    assert(is_ok == (formatter.status == WASM_TEXT_STATUS_OK));
    assert(formatter.writer.length <= output_buffer.length);
    return (Wasm_Text_Result){
        .text = str_make(output_buffer.elements, length_bytes),
        .error_offset = formatter.error_offset,
        .status = formatter.status,
    };
}

/**
 * Checks the header of the module and finds where its sections are. Records an error in
 * `formatter` when the module is malformed, or has a section that the formatter does not know.
 */
static Wasm_Text_Sections wasm_text_sections_find(Wasm_Text_Formatter *formatter) {
    assert(formatter->offset == 0);
    assert(formatter->limit == formatter->module_length);

    Wasm_Text_Section const absent = {
        .start = 0,
        .end = 0,
        .is_present = false,
    };
    Wasm_Text_Sections sections = {
        .code = absent,
        .data = absent,
        .element = absent,
        .export = absent,
        .function = absent,
        .global = absent,
        .import = absent,
        .memory = absent,
        .table = absent,
        .type = absent,
    };

    for (size_t i = 0; i < sizeof(WASM_MODULE_HEADER); i += 1) {
        byte header_byte = wasm_text_read_byte(formatter);
        if (formatter->status != WASM_TEXT_STATUS_OK) {
            return sections;
        }
        if (header_byte != WASM_MODULE_HEADER[i]) {
            wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_MALFORMED, i);
            return sections;
        }
    }

    // Every section is at least two bytes, its id and its size, so there are fewer sections than
    // bytes in the module.
    for (size_t i = 0; i < formatter->module_length && formatter->offset < formatter->limit;
         i += 1) {
        size_t id_offset = formatter->offset;
        byte id = wasm_text_read_byte(formatter);
        uint32_t size_bytes = wasm_text_read_unsigned_32(formatter);
        if (formatter->status != WASM_TEXT_STATUS_OK) {
            return sections;
        }
        if (size_bytes > formatter->limit - formatter->offset) {
            wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_MALFORMED, id_offset);
            return sections;
        }

        Wasm_Text_Section section = {
            .start = formatter->offset,
            .end = formatter->offset + size_bytes,
            .is_present = true,
        };

        // The id is a byte of the module rather than an enumeration, so any value can come, and
        // the default case is the ones that the formatter does not know.
        Wasm_Text_Section *known = NULL;
        switch (id) {
        case WASM_SECTION_ID_CODE:
            known = &sections.code;
            break;
        case WASM_SECTION_ID_CUSTOM:
            // Custom sections hold extra information, such as names for debuggers, which the
            // text does not show.
            break;
        case WASM_SECTION_ID_DATA:
            known = &sections.data;
            break;
        case WASM_SECTION_ID_ELEMENT:
            known = &sections.element;
            break;
        case WASM_SECTION_ID_EXPORT:
            known = &sections.export;
            break;
        case WASM_SECTION_ID_FUNCTION:
            known = &sections.function;
            break;
        case WASM_SECTION_ID_GLOBAL:
            known = &sections.global;
            break;
        case WASM_SECTION_ID_IMPORT:
            known = &sections.import;
            break;
        case WASM_SECTION_ID_MEMORY:
            known = &sections.memory;
            break;
        case WASM_SECTION_ID_TABLE:
            known = &sections.table;
            break;
        case WASM_SECTION_ID_TYPE:
            known = &sections.type;
            break;
        default:
            wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED, id_offset);
            return sections;
        }

        if (known != NULL) {
            // A module has at most one section of each kind, other than custom sections.
            if (known->is_present) {
                wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_MALFORMED, id_offset);
                return sections;
            }
            *known = section;
        }

        formatter->offset = section.end;
    }

    assert(formatter->offset == formatter->module_length);
    return sections;
}

/**
 * Writes one `(type ...)` per entry of the type section, and remembers where the entries are, so
 * that the functions can show the type that they have.
 */
static bool wasm_text_format_types(Wasm_Text_Formatter *formatter, Wasm_Text_Section section) {
    assert(formatter->status == WASM_TEXT_STATUS_OK);
    assert(section.start <= section.end);

    uint32_t count = wasm_text_section_count_read(formatter, section);
    formatter->type_entries_start = formatter->offset;
    formatter->type_entries_end = formatter->limit;
    formatter->type_cursor_offset = formatter->offset;
    formatter->type_cursor_index = 0;
    formatter->type_count = count;

    // Every entry is at least one byte, and a read past the end of the section fails, so the
    // loop ends within as many steps as the section has bytes, even if `count` is wrong.
    for (uint32_t i = 0; i < count && formatter->status == WASM_TEXT_STATUS_OK; i += 1) {
        wasm_text_write(&formatter->writer, STR("\n  (type (;"));
        wasm_text_write_unsigned(&formatter->writer, i);
        wasm_text_write(&formatter->writer, STR(";) (func"));
        wasm_text_format_function_type(formatter);
        wasm_text_write(&formatter->writer, STR("))"));
    }

    return wasm_text_section_end(formatter);
}

/**
 * Reads one function type at `offset`, and writes its parameters and results, each list only
 * when it is not empty, as the text format allows.
 */
static bool wasm_text_format_function_type(Wasm_Text_Formatter *formatter) {
    assert(formatter->offset <= formatter->limit);

    size_t form_offset = formatter->offset;
    byte form = wasm_text_read_byte(formatter);
    if (formatter->status != WASM_TEXT_STATUS_OK) {
        return false;
    }
    // Newer versions of the format have other kinds of type here, such as structures.
    if (form != WASM_TYPE_FUNCTION) {
        return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED, form_offset);
    }

    uint32_t parameter_count = wasm_text_read_unsigned_32(formatter);
    if (parameter_count > 0) {
        wasm_text_write(&formatter->writer, STR(" (param"));
        for (uint32_t i = 0; i < parameter_count && formatter->status == WASM_TEXT_STATUS_OK;
             i += 1) {
            wasm_text_write(&formatter->writer, STR(" "));
            wasm_text_format_value_type(formatter);
        }
        wasm_text_write(&formatter->writer, STR(")"));
    }

    uint32_t result_count = wasm_text_read_unsigned_32(formatter);
    if (result_count > 0) {
        wasm_text_write(&formatter->writer, STR(" (result"));
        for (uint32_t i = 0; i < result_count && formatter->status == WASM_TEXT_STATUS_OK;
             i += 1) {
            wasm_text_write(&formatter->writer, STR(" "));
            wasm_text_format_value_type(formatter);
        }
        wasm_text_write(&formatter->writer, STR(")"));
    }

    return formatter->status == WASM_TEXT_STATUS_OK;
}

/**
 * Writes one `(import ...)` per entry of the import section, with the signature repeated from its
 * type, as for a function, because the instructions refer to the parameters by position and the
 * reader should not have to look up the type. The module only imports functions. The imported
 * functions come before the ones of the function section in the function index space, so their
 * number is remembered for the index that the functions are shown with.
 */
static bool wasm_text_format_imports(Wasm_Text_Formatter *formatter, Wasm_Text_Section section) {
    assert(formatter->status == WASM_TEXT_STATUS_OK);
    assert(section.start <= section.end);

    uint32_t count = wasm_text_section_count_read(formatter, section);
    size_t cursor = formatter->offset;
    // Bounded like the loop over the types: every entry takes at least one byte, and a read past
    // the end of the section fails.
    for (uint32_t i = 0; i < count && formatter->status == WASM_TEXT_STATUS_OK; i += 1) {
        formatter->offset = cursor;
        formatter->limit = section.end;

        // The module name and then the name of the function, each with its length before it.
        byte const *names[2] = {NULL, NULL};
        uint32_t name_lengths[2] = {0, 0};
        for (size_t j = 0; j < 2; j += 1) {
            size_t name_offset = formatter->offset;
            name_lengths[j] = wasm_text_read_unsigned_32(formatter);
            if (formatter->status != WASM_TEXT_STATUS_OK) {
                return false;
            }
            if (name_lengths[j] > formatter->limit - formatter->offset) {
                return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_MALFORMED, name_offset);
            }
            names[j] = formatter->module + formatter->offset;
            formatter->offset += name_lengths[j];
        }

        size_t kind_offset = formatter->offset;
        byte kind = wasm_text_read_byte(formatter);
        size_t type_index_offset = formatter->offset;
        uint32_t type_index = wasm_text_read_unsigned_32(formatter);
        cursor = formatter->offset;
        if (formatter->status != WASM_TEXT_STATUS_OK) {
            return false;
        }
        // Tables, memories and globals can be imported too, but the module imports functions only.
        if (kind != WASM_IMPORT_KIND_FUNCTION) {
            return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED, kind_offset);
        }
        if (type_index >= formatter->type_count) {
            return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_MALFORMED, type_index_offset);
        }

        wasm_text_write(&formatter->writer, STR("\n  (import "));
        wasm_text_write_string(&formatter->writer, names[0], name_lengths[0]);
        wasm_text_write(&formatter->writer, STR(" "));
        wasm_text_write_string(&formatter->writer, names[1], name_lengths[1]);
        wasm_text_write(&formatter->writer, STR(" (func (;"));
        wasm_text_write_unsigned(&formatter->writer, i);
        wasm_text_write(&formatter->writer, STR(";) (type "));
        wasm_text_write_unsigned(&formatter->writer, type_index);
        wasm_text_write(&formatter->writer, STR(")"));

        formatter->offset = wasm_text_type_find(formatter, type_index);
        formatter->limit = formatter->type_entries_end;
        bool is_type_ok = wasm_text_format_function_type(formatter);
        // The types were formatted already, so formatting one again cannot fail.
        assert(is_type_ok);
        (void)is_type_ok;

        wasm_text_write(&formatter->writer, STR("))"));
    }

    if (formatter->status != WASM_TEXT_STATUS_OK) {
        return false;
    }

    formatter->offset = cursor;
    formatter->limit = section.end;
    formatter->import_count = count;
    return wasm_text_section_end(formatter);
}

/**
 * Reads one value type and writes its name.
 */
static bool wasm_text_format_value_type(Wasm_Text_Formatter *formatter) {
    assert(formatter->offset <= formatter->limit);

    size_t type_offset = formatter->offset;
    byte value_type = wasm_text_read_byte(formatter);
    if (formatter->status != WASM_TEXT_STATUS_OK) {
        return false;
    }

    Str name = wasm_text_value_type_name(value_type);
    if (name.bytes == NULL) {
        return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED, type_offset);
    }
    wasm_text_write(&formatter->writer, name);
    return true;
}

/**
 * Writes one `(func ...)` per function. The type of function `i` is entry `i` of the function
 * section, and its body is entry `i` of the code section, so the two are read side by side, each
 * from its own position.
 */
static bool wasm_text_format_functions(Wasm_Text_Formatter *formatter, Wasm_Text_Section function,
                                       Wasm_Text_Section code) {
    assert(formatter->status == WASM_TEXT_STATUS_OK);
    assert(function.start <= function.end);
    assert(code.start <= code.end);

    uint32_t function_count = wasm_text_section_count_read(formatter, function);
    size_t function_cursor = formatter->offset;
    uint32_t code_count = wasm_text_section_count_read(formatter, code);
    size_t code_cursor = formatter->offset;
    if (formatter->status != WASM_TEXT_STATUS_OK) {
        return false;
    }
    if (function_count != code_count) {
        return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_MALFORMED, code.start);
    }

    // Bounded like the loop over the types: every entry takes at least one byte.
    for (uint32_t i = 0; i < function_count && formatter->status == WASM_TEXT_STATUS_OK; i += 1) {
        formatter->offset = function_cursor;
        formatter->limit = function.end;
        size_t type_index_offset = formatter->offset;
        uint32_t type_index = wasm_text_read_unsigned_32(formatter);
        function_cursor = formatter->offset;
        if (formatter->status != WASM_TEXT_STATUS_OK) {
            return false;
        }
        if (type_index >= formatter->type_count) {
            return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_MALFORMED, type_index_offset);
        }

        wasm_text_write(&formatter->writer, STR("\n  (func (;"));
        wasm_text_write_unsigned(&formatter->writer, formatter->import_count + i);
        wasm_text_write(&formatter->writer, STR(";) (type "));
        wasm_text_write_unsigned(&formatter->writer, type_index);
        wasm_text_write(&formatter->writer, STR(")"));

        // The parameters are repeated from the type, because the instructions refer to them by
        // index, and the reader should not have to look up the type to see what they are.
        formatter->offset = wasm_text_type_find(formatter, type_index);
        formatter->limit = formatter->type_entries_end;
        bool is_type_ok = wasm_text_format_function_type(formatter);
        // The types were formatted already, so formatting one again cannot fail.
        assert(is_type_ok);

        formatter->offset = code_cursor;
        formatter->limit = code.end;
        wasm_text_format_functions_body(formatter);
        code_cursor = formatter->offset;

        wasm_text_write(&formatter->writer, STR(")"));
    }

    if (formatter->status != WASM_TEXT_STATUS_OK) {
        return false;
    }

    // Both sections must end right after their last entry.
    formatter->offset = function_cursor;
    formatter->limit = function.end;
    if (wasm_text_section_end(formatter) == false) {
        return false;
    }
    formatter->offset = code_cursor;
    formatter->limit = code.end;
    return wasm_text_section_end(formatter);
}

/**
 * Reads one entry of the code section, the size of a body and then the body, and writes its
 * locals and instructions.
 */
static bool wasm_text_format_functions_body(Wasm_Text_Formatter *formatter) {
    assert(formatter->status == WASM_TEXT_STATUS_OK);
    assert(formatter->offset <= formatter->limit);

    size_t size_offset = formatter->offset;
    uint32_t size_bytes = wasm_text_read_unsigned_32(formatter);
    if (formatter->status != WASM_TEXT_STATUS_OK) {
        return false;
    }
    if (size_bytes > formatter->limit - formatter->offset) {
        return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_MALFORMED, size_offset);
    }

    // The body is read with its own end as the limit, and the limit of the section is restored
    // afterwards, so that an instruction cannot run into the next body.
    size_t section_limit = formatter->limit;
    formatter->limit = formatter->offset + size_bytes;

    bool is_ok = wasm_text_format_functions_body_locals(formatter);
    if (is_ok) {
        is_ok = wasm_text_format_functions_body_instructions(formatter);
    }
    if (is_ok) {
        is_ok = wasm_text_section_end(formatter);
    }

    formatter->limit = section_limit;
    return is_ok;
}

/**
 * Reads the locals of a body, which come in groups of locals of one type, and writes them as one
 * `(local ...)` per group.
 */
static bool wasm_text_format_functions_body_locals(Wasm_Text_Formatter *formatter) {
    assert(formatter->status == WASM_TEXT_STATUS_OK);
    assert(formatter->offset <= formatter->limit);

    uint32_t group_count = wasm_text_read_unsigned_32(formatter);
    // Every group is at least two bytes, so the loop is bounded by the size of the body.
    for (uint32_t i = 0; i < group_count && formatter->status == WASM_TEXT_STATUS_OK; i += 1) {
        uint32_t local_count = wasm_text_read_unsigned_32(formatter);
        size_t type_offset = formatter->offset;
        byte value_type = wasm_text_read_byte(formatter);
        if (formatter->status != WASM_TEXT_STATUS_OK) {
            return false;
        }

        Str name = wasm_text_value_type_name(value_type);
        if (name.bytes == NULL) {
            return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED, type_offset);
        }

        wasm_text_write(&formatter->writer, STR(" (local"));
        // Two bytes can declare billions of locals, so the loop also stops when the output is
        // full, which is reported afterwards.
        for (uint32_t j = 0; j < local_count && formatter->writer.is_overflowed == false; j += 1) {
            wasm_text_write(&formatter->writer, STR(" "));
            wasm_text_write(&formatter->writer, name);
        }
        wasm_text_write(&formatter->writer, STR(")"));
    }

    return formatter->status == WASM_TEXT_STATUS_OK;
}

/**
 * Reads the instructions of a body up to the `end` that closes it, and writes one per line.
 */
static bool wasm_text_format_functions_body_instructions(Wasm_Text_Formatter *formatter) {
    assert(formatter->status == WASM_TEXT_STATUS_OK);
    assert(formatter->offset <= formatter->limit);

    bool is_ended = false;
    // Every instruction is at least one byte, so a body has no more instructions than bytes.
    size_t instruction_count_max = formatter->limit - formatter->offset;
    for (size_t i = 0; i < instruction_count_max && is_ended == false &&
                       formatter->status == WASM_TEXT_STATUS_OK;
         i += 1) {
        size_t opcode_offset = formatter->offset;
        byte opcode = wasm_text_read_byte(formatter);
        if (formatter->status != WASM_TEXT_STATUS_OK) {
            return false;
        }

        // The opcode is a byte of the module rather than an enumeration, so any value can come,
        // and the default case is the ones that the formatter does not know.
        switch (opcode) {
        case WASM_OPCODE_BLOCK:
        case WASM_OPCODE_LOOP: {
            // Like `if`, only the block type that takes and leaves nothing is written.
            size_t block_type_offset = formatter->offset;
            byte block_type = wasm_text_read_byte(formatter);
            if (formatter->status != WASM_TEXT_STATUS_OK) {
                return false;
            }
            if (block_type != WASM_BLOCK_TYPE_EMPTY) {
                return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED,
                                      block_type_offset);
            }
            if (opcode == WASM_OPCODE_BLOCK) {
                wasm_text_write_instruction(&formatter->writer, STR("block"));
            } else {
                wasm_text_write_instruction(&formatter->writer, STR("loop"));
            }
            formatter->writer.block_depth += 1;
            break;
        }
        case WASM_OPCODE_BR:
        case WASM_OPCODE_BR_IF: {
            uint32_t label_index = wasm_text_read_unsigned_32(formatter);
            if (opcode == WASM_OPCODE_BR) {
                wasm_text_write_instruction(&formatter->writer, STR("br "));
            } else {
                wasm_text_write_instruction(&formatter->writer, STR("br_if "));
            }
            wasm_text_write_unsigned(&formatter->writer, label_index);
            break;
        }
        case WASM_OPCODE_CALL: {
            uint32_t function_index = wasm_text_read_unsigned_32(formatter);
            wasm_text_write_instruction(&formatter->writer, STR("call "));
            wasm_text_write_unsigned(&formatter->writer, function_index);
            break;
        }
        case WASM_OPCODE_CALL_INDIRECT: {
            // The type that the function in the table must have, and the table, which is always
            // table 0, so the text leaves it out as the text format allows.
            size_t table_offset = 0;
            uint32_t type_index = wasm_text_read_unsigned_32(formatter);
            table_offset = formatter->offset;
            uint32_t table_index = wasm_text_read_unsigned_32(formatter);
            if (formatter->status != WASM_TEXT_STATUS_OK) {
                return false;
            }
            if (table_index != 0) {
                return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED, table_offset);
            }
            wasm_text_write_instruction(&formatter->writer, STR("call_indirect (type "));
            wasm_text_write_unsigned(&formatter->writer, type_index);
            wasm_text_write(&formatter->writer, STR(")"));
            break;
        }
        case WASM_OPCODE_DROP:
            wasm_text_write_instruction(&formatter->writer, STR("drop"));
            break;
        case WASM_OPCODE_ELSE:
            // An `else` belongs to an `if` that is open, and is written like the `if`.
            if (formatter->writer.block_depth == 0) {
                return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_MALFORMED, opcode_offset);
            }
            formatter->writer.block_depth -= 1;
            wasm_text_write_instruction(&formatter->writer, STR("else"));
            formatter->writer.block_depth += 1;
            break;
        case WASM_OPCODE_END:
            if (formatter->writer.block_depth > 0) {
                // It closes an `if`, and is written like it.
                formatter->writer.block_depth -= 1;
                wasm_text_write_instruction(&formatter->writer, STR("end"));
                break;
            }
            // Otherwise it closes the body, which must be the last instruction.
            if (formatter->offset != formatter->limit) {
                return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED,
                                      opcode_offset);
            }
            // The text format closes the body with the parenthesis of the function instead.
            is_ended = true;
            break;
        case WASM_OPCODE_GLOBAL_GET: {
            uint32_t global_index = wasm_text_read_unsigned_32(formatter);
            wasm_text_write_instruction(&formatter->writer, STR("global.get "));
            wasm_text_write_unsigned(&formatter->writer, global_index);
            break;
        }
        case WASM_OPCODE_GLOBAL_SET: {
            uint32_t global_index = wasm_text_read_unsigned_32(formatter);
            wasm_text_write_instruction(&formatter->writer, STR("global.set "));
            wasm_text_write_unsigned(&formatter->writer, global_index);
            break;
        }
        case WASM_OPCODE_I32_AND:
            wasm_text_write_instruction(&formatter->writer, STR("i32.and"));
            break;
        case WASM_OPCODE_I32_EQZ:
            wasm_text_write_instruction(&formatter->writer, STR("i32.eqz"));
            break;
        case WASM_OPCODE_I32_OR:
            wasm_text_write_instruction(&formatter->writer, STR("i32.or"));
            break;
        case WASM_OPCODE_I32_EQ:
            wasm_text_write_instruction(&formatter->writer, STR("i32.eq"));
            break;
        case WASM_OPCODE_I32_NE:
            wasm_text_write_instruction(&formatter->writer, STR("i32.ne"));
            break;
        case WASM_OPCODE_I64_EQ:
            wasm_text_write_instruction(&formatter->writer, STR("i64.eq"));
            break;
        case WASM_OPCODE_I64_GE_S:
            wasm_text_write_instruction(&formatter->writer, STR("i64.ge_s"));
            break;
        case WASM_OPCODE_I64_GT_S:
            wasm_text_write_instruction(&formatter->writer, STR("i64.gt_s"));
            break;
        case WASM_OPCODE_I64_LE_S:
            wasm_text_write_instruction(&formatter->writer, STR("i64.le_s"));
            break;
        case WASM_OPCODE_I64_LT_S:
            wasm_text_write_instruction(&formatter->writer, STR("i64.lt_s"));
            break;
        case WASM_OPCODE_I64_NE:
            wasm_text_write_instruction(&formatter->writer, STR("i64.ne"));
            break;
        case WASM_OPCODE_IF: {
            // Only the block type that takes and leaves nothing is written by the compiler.
            size_t block_type_offset = formatter->offset;
            byte block_type = wasm_text_read_byte(formatter);
            if (formatter->status != WASM_TEXT_STATUS_OK) {
                return false;
            }
            if (block_type != WASM_BLOCK_TYPE_EMPTY) {
                return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED,
                                      block_type_offset);
            }
            wasm_text_write_instruction(&formatter->writer, STR("if"));
            formatter->writer.block_depth += 1;
            break;
        }
        case WASM_OPCODE_I32_CONST: {
            // The immediate is a signed 32-bit number, which the 64-bit reader also reads.
            int64_t number = wasm_text_read_signed_64(formatter);
            wasm_text_write_instruction(&formatter->writer, STR("i32.const "));
            wasm_text_write_signed(&formatter->writer, number);
            break;
        }
        case WASM_OPCODE_I64_ADD:
            wasm_text_write_instruction(&formatter->writer, STR("i64.add"));
            break;
        case WASM_OPCODE_I64_CONST: {
            int64_t number = wasm_text_read_signed_64(formatter);
            wasm_text_write_instruction(&formatter->writer, STR("i64.const "));
            wasm_text_write_signed(&formatter->writer, number);
            break;
        }
        case WASM_OPCODE_I64_DIV_S:
            wasm_text_write_instruction(&formatter->writer, STR("i64.div_s"));
            break;
        case WASM_OPCODE_I64_MUL:
            wasm_text_write_instruction(&formatter->writer, STR("i64.mul"));
            break;
        case WASM_OPCODE_I64_SUB:
            wasm_text_write_instruction(&formatter->writer, STR("i64.sub"));
            break;
        case WASM_OPCODE_LOCAL_GET: {
            uint32_t local_index = wasm_text_read_unsigned_32(formatter);
            wasm_text_write_instruction(&formatter->writer, STR("local.get "));
            wasm_text_write_unsigned(&formatter->writer, local_index);
            break;
        }
        case WASM_OPCODE_LOCAL_SET: {
            uint32_t local_index = wasm_text_read_unsigned_32(formatter);
            wasm_text_write_instruction(&formatter->writer, STR("local.set "));
            wasm_text_write_unsigned(&formatter->writer, local_index);
            break;
        }
        case WASM_OPCODE_I32_ADD:
            wasm_text_write_instruction(&formatter->writer, STR("i32.add"));
            break;
        case WASM_OPCODE_I32_GE_U:
            wasm_text_write_instruction(&formatter->writer, STR("i32.ge_u"));
            break;
        case WASM_OPCODE_I32_LT_S:
            wasm_text_write_instruction(&formatter->writer, STR("i32.lt_s"));
            break;
        case WASM_OPCODE_I32_MUL:
            wasm_text_write_instruction(&formatter->writer, STR("i32.mul"));
            break;
        case WASM_OPCODE_I32_SHL:
            wasm_text_write_instruction(&formatter->writer, STR("i32.shl"));
            break;
        case WASM_OPCODE_I32_SUB:
            wasm_text_write_instruction(&formatter->writer, STR("i32.sub"));
            break;
        case WASM_OPCODE_I32_WRAP_I64:
            wasm_text_write_instruction(&formatter->writer, STR("i32.wrap_i64"));
            break;
        case WASM_OPCODE_I64_EXTEND_I32_U:
            wasm_text_write_instruction(&formatter->writer, STR("i64.extend_i32_u"));
            break;
        case WASM_OPCODE_I64_GT_U:
            wasm_text_write_instruction(&formatter->writer, STR("i64.gt_u"));
            break;
        case WASM_OPCODE_I64_LE_U:
            wasm_text_write_instruction(&formatter->writer, STR("i64.le_u"));
            break;
        case WASM_OPCODE_I32_GT_U:
            wasm_text_write_instruction(&formatter->writer, STR("i32.gt_u"));
            break;
        case WASM_OPCODE_I32_LE_U:
            wasm_text_write_instruction(&formatter->writer, STR("i32.le_u"));
            break;
        case WASM_OPCODE_I32_LT_U:
            wasm_text_write_instruction(&formatter->writer, STR("i32.lt_u"));
            break;
        case WASM_OPCODE_I32_LOAD:
        case WASM_OPCODE_I32_LOAD8_U:
        case WASM_OPCODE_I32_STORE:
        case WASM_OPCODE_I32_STORE8:
        case WASM_OPCODE_I64_LOAD:
        case WASM_OPCODE_I64_STORE: {
            // The immediates are the alignment, as a power of two, and the offset. Like
            // `wasm2wat`, the text leaves out the offset when it is zero and the alignment when it
            // is the size of the value, which is 8 bytes, 4 bytes or 1 byte.
            uint32_t alignment = wasm_text_read_unsigned_32(formatter);
            uint32_t offset_bytes = wasm_text_read_unsigned_32(formatter);
            uint32_t alignment_natural = WASM_TEXT_ALIGNMENT_I64;
            if (opcode == WASM_OPCODE_I32_LOAD) {
                wasm_text_write_instruction(&formatter->writer, STR("i32.load"));
                alignment_natural = WASM_TEXT_ALIGNMENT_I32;
            } else if (opcode == WASM_OPCODE_I32_STORE) {
                wasm_text_write_instruction(&formatter->writer, STR("i32.store"));
                alignment_natural = WASM_TEXT_ALIGNMENT_I32;
            } else if (opcode == WASM_OPCODE_I32_LOAD8_U) {
                wasm_text_write_instruction(&formatter->writer, STR("i32.load8_u"));
                alignment_natural = 0;
            } else if (opcode == WASM_OPCODE_I32_STORE8) {
                wasm_text_write_instruction(&formatter->writer, STR("i32.store8"));
                alignment_natural = 0;
            } else if (opcode == WASM_OPCODE_I64_LOAD) {
                wasm_text_write_instruction(&formatter->writer, STR("i64.load"));
            } else {
                wasm_text_write_instruction(&formatter->writer, STR("i64.store"));
            }
            if (offset_bytes != 0) {
                wasm_text_write(&formatter->writer, STR(" offset="));
                wasm_text_write_unsigned(&formatter->writer, offset_bytes);
            }
            if (alignment != alignment_natural) {
                // An alignment above the size of the value is not valid.
                if (alignment > alignment_natural) {
                    return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_MALFORMED,
                                          opcode_offset);
                }
                wasm_text_write(&formatter->writer, STR(" align="));
                wasm_text_write_unsigned(&formatter->writer, (uint64_t)1 << alignment);
            }
            break;
        }
        case WASM_OPCODE_LOCAL_TEE: {
            uint32_t local_index = wasm_text_read_unsigned_32(formatter);
            wasm_text_write_instruction(&formatter->writer, STR("local.tee "));
            wasm_text_write_unsigned(&formatter->writer, local_index);
            break;
        }
        case WASM_OPCODE_PREFIX_MISC: {
            // Only `memory.copy` is written, from memory 0 to memory 0.
            size_t sub_opcode_offset = formatter->offset;
            uint32_t sub_opcode = wasm_text_read_unsigned_32(formatter);
            byte destination = wasm_text_read_byte(formatter);
            byte source = wasm_text_read_byte(formatter);
            if (formatter->status != WASM_TEXT_STATUS_OK) {
                return false;
            }
            if (sub_opcode != WASM_OPCODE_MISC_MEMORY_COPY || destination != 0 || source != 0) {
                return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED,
                                      sub_opcode_offset);
            }
            wasm_text_write_instruction(&formatter->writer, STR("memory.copy"));
            break;
        }
        case WASM_OPCODE_RETURN:
            wasm_text_write_instruction(&formatter->writer, STR("return"));
            break;
        case WASM_OPCODE_UNREACHABLE:
            wasm_text_write_instruction(&formatter->writer, STR("unreachable"));
            break;
        default:
            return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED, opcode_offset);
        }
    }

    if (formatter->status != WASM_TEXT_STATUS_OK) {
        return false;
    }
    if (is_ended == false) {
        return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_MALFORMED, formatter->offset);
    }
    return true;
}

/**
 * Writes one `(export ...)` per entry of the export section.
 */
static bool wasm_text_format_exports(Wasm_Text_Formatter *formatter, Wasm_Text_Section section) {
    assert(formatter->status == WASM_TEXT_STATUS_OK);
    assert(section.start <= section.end);

    uint32_t count = wasm_text_section_count_read(formatter, section);
    // Bounded like the loop over the types: every entry takes at least one byte.
    for (uint32_t i = 0; i < count && formatter->status == WASM_TEXT_STATUS_OK; i += 1) {
        size_t name_offset = formatter->offset;
        uint32_t name_length = wasm_text_read_unsigned_32(formatter);
        if (formatter->status != WASM_TEXT_STATUS_OK) {
            return false;
        }
        if (name_length > formatter->limit - formatter->offset) {
            return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_MALFORMED, name_offset);
        }
        byte const *name = formatter->module + formatter->offset;
        formatter->offset += name_length;

        size_t kind_offset = formatter->offset;
        byte kind = wasm_text_read_byte(formatter);
        uint32_t index = wasm_text_read_unsigned_32(formatter);
        if (formatter->status != WASM_TEXT_STATUS_OK) {
            return false;
        }

        // Only functions and memories are supported, because the module exports no tables or
        // globals.
        Str kind_name = STR_EMPTY;
        if (kind == WASM_EXPORT_KIND_FUNCTION) {
            kind_name = STR(" (func ");
        } else if (kind == WASM_EXPORT_KIND_MEMORY) {
            kind_name = STR(" (memory ");
        } else {
            return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED, kind_offset);
        }

        wasm_text_write(&formatter->writer, STR("\n  (export "));
        wasm_text_write_string(&formatter->writer, name, name_length);
        wasm_text_write(&formatter->writer, kind_name);
        wasm_text_write_unsigned(&formatter->writer, index);
        wasm_text_write(&formatter->writer, STR("))"));
    }

    return wasm_text_section_end(formatter);
}

/**
 * Writes one `(global ...)` per entry of the global section, with its type, which says that it is
 * mutable when it is, and its initial value, which is a constant expression of one instruction.
 */
static bool wasm_text_format_globals(Wasm_Text_Formatter *formatter, Wasm_Text_Section section) {
    assert(formatter->status == WASM_TEXT_STATUS_OK);
    assert(section.start <= section.end);

    uint32_t count = wasm_text_section_count_read(formatter, section);
    // Bounded like the loop over the types: every entry takes at least one byte.
    for (uint32_t i = 0; i < count && formatter->status == WASM_TEXT_STATUS_OK; i += 1) {
        size_t type_offset = formatter->offset;
        byte value_type = wasm_text_read_byte(formatter);
        size_t mutability_offset = formatter->offset;
        byte mutability = wasm_text_read_byte(formatter);
        size_t initial_offset = formatter->offset;
        byte initial_opcode = wasm_text_read_byte(formatter);
        int64_t initial_value = wasm_text_read_signed_64(formatter);
        byte end_opcode = wasm_text_read_byte(formatter);
        if (formatter->status != WASM_TEXT_STATUS_OK) {
            return false;
        }

        Str type_name = wasm_text_value_type_name(value_type);
        if (type_name.bytes == NULL) {
            return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED, type_offset);
        }
        // A global can also be fixed, which `wasm_build` never writes.
        if (mutability != WASM_GLOBAL_MUTABLE) {
            return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED,
                                  mutability_offset);
        }
        // Other initial values are computed, for example from another global.
        Str initial_name = STR_EMPTY;
        if (initial_opcode == WASM_OPCODE_I32_CONST) {
            initial_name = STR("i32.const ");
        } else if (initial_opcode == WASM_OPCODE_I64_CONST) {
            initial_name = STR("i64.const ");
        }
        if (initial_name.bytes == NULL || end_opcode != WASM_OPCODE_END) {
            return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED, initial_offset);
        }

        wasm_text_write(&formatter->writer, STR("\n  (global (;"));
        wasm_text_write_unsigned(&formatter->writer, i);
        wasm_text_write(&formatter->writer, STR(";) (mut "));
        wasm_text_write(&formatter->writer, type_name);
        wasm_text_write(&formatter->writer, STR(") ("));
        wasm_text_write(&formatter->writer, initial_name);
        wasm_text_write_signed(&formatter->writer, initial_value);
        wasm_text_write(&formatter->writer, STR("))"));
    }

    return wasm_text_section_end(formatter);
}

/**
 * Writes one `(table ...)` per entry of the table section, with its size and the type of its
 * elements, which is a reference to a function.
 */
static bool wasm_text_format_tables(Wasm_Text_Formatter *formatter, Wasm_Text_Section section) {
    assert(formatter->status == WASM_TEXT_STATUS_OK);
    assert(section.start <= section.end);

    uint32_t count = wasm_text_section_count_read(formatter, section);
    // Bounded like the loop over the types: every entry takes at least one byte.
    for (uint32_t i = 0; i < count && formatter->status == WASM_TEXT_STATUS_OK; i += 1) {
        size_t element_type_offset = formatter->offset;
        byte element_type = wasm_text_read_byte(formatter);
        size_t flag_offset = formatter->offset;
        byte flag = wasm_text_read_byte(formatter);
        uint32_t size_min = wasm_text_read_unsigned_32(formatter);
        if (formatter->status != WASM_TEXT_STATUS_OK) {
            return false;
        }
        // A table can hold other references, and can have a maximum, which `wasm_build` never
        // writes.
        if (element_type != WASM_TYPE_FUNCREF) {
            return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED,
                                  element_type_offset);
        }
        if (flag != WASM_TABLE_LIMITS_MIN_ONLY) {
            return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED, flag_offset);
        }

        wasm_text_write(&formatter->writer, STR("\n  (table (;"));
        wasm_text_write_unsigned(&formatter->writer, i);
        wasm_text_write(&formatter->writer, STR(";) "));
        wasm_text_write_unsigned(&formatter->writer, size_min);
        wasm_text_write(&formatter->writer, STR(" funcref)"));
    }

    return wasm_text_section_end(formatter);
}

/**
 * Writes one `(elem ...)` per segment of the element section, with the index in the table that its
 * functions are put at, and the indexes of the functions. The module only has segments that are put
 * in table 0 when it is instantiated.
 */
static bool wasm_text_format_elements(Wasm_Text_Formatter *formatter, Wasm_Text_Section section) {
    assert(formatter->status == WASM_TEXT_STATUS_OK);
    assert(section.start <= section.end);

    uint32_t count = wasm_text_section_count_read(formatter, section);
    // Bounded like the loop over the types: every segment takes at least one byte.
    for (uint32_t i = 0; i < count && formatter->status == WASM_TEXT_STATUS_OK; i += 1) {
        size_t flag_offset = formatter->offset;
        byte flag = wasm_text_read_byte(formatter);
        size_t start_offset = formatter->offset;
        byte start_opcode = wasm_text_read_byte(formatter);
        int64_t start = wasm_text_read_signed_64(formatter);
        byte end_opcode = wasm_text_read_byte(formatter);
        uint32_t function_count = wasm_text_read_unsigned_32(formatter);
        if (formatter->status != WASM_TEXT_STATUS_OK) {
            return false;
        }

        // Other segments are passive or declarative, or for another table, or have expressions
        // as their elements, and other starts are computed.
        if (flag != WASM_ELEMENT_SEGMENT_ACTIVE) {
            return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED, flag_offset);
        }
        if (start_opcode != WASM_OPCODE_I32_CONST || end_opcode != WASM_OPCODE_END) {
            return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED, start_offset);
        }

        wasm_text_write(&formatter->writer, STR("\n  (elem (;"));
        wasm_text_write_unsigned(&formatter->writer, i);
        wasm_text_write(&formatter->writer, STR(";) (i32.const "));
        wasm_text_write_signed(&formatter->writer, start);
        wasm_text_write(&formatter->writer, STR(") func"));
        // Every function index takes at least one byte, and a read past the end of the section
        // fails, so the loop ends within as many steps as the section has bytes.
        for (uint32_t j = 0; j < function_count && formatter->status == WASM_TEXT_STATUS_OK;
             j += 1) {
            uint32_t function_index = wasm_text_read_unsigned_32(formatter);
            wasm_text_write(&formatter->writer, STR(" "));
            wasm_text_write_unsigned(&formatter->writer, function_index);
        }
        wasm_text_write(&formatter->writer, STR(")"));
    }

    return wasm_text_section_end(formatter);
}

/**
 * Writes one `(memory ...)` per entry of the memory section, with its size in pages.
 */
static bool wasm_text_format_memories(Wasm_Text_Formatter *formatter, Wasm_Text_Section section) {
    assert(formatter->status == WASM_TEXT_STATUS_OK);
    assert(section.start <= section.end);

    uint32_t count = wasm_text_section_count_read(formatter, section);
    // Bounded like the loop over the types: every entry takes at least one byte.
    for (uint32_t i = 0; i < count && formatter->status == WASM_TEXT_STATUS_OK; i += 1) {
        size_t flag_offset = formatter->offset;
        byte flag = wasm_text_read_byte(formatter);
        uint32_t page_count_min = wasm_text_read_unsigned_32(formatter);
        if (formatter->status != WASM_TEXT_STATUS_OK) {
            return false;
        }
        // A memory can also have a maximum, which `wasm_build` never writes.
        if (flag != WASM_MEMORY_LIMITS_MIN_ONLY) {
            return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED, flag_offset);
        }

        wasm_text_write(&formatter->writer, STR("\n  (memory (;"));
        wasm_text_write_unsigned(&formatter->writer, i);
        wasm_text_write(&formatter->writer, STR(";) "));
        wasm_text_write_unsigned(&formatter->writer, page_count_min);
        wasm_text_write(&formatter->writer, STR(")"));
    }

    return wasm_text_section_end(formatter);
}

/**
 * Writes one `(data ...)` per segment of the data section, with the address that it is copied to
 * and its bytes as a string.
 */
static bool wasm_text_format_data(Wasm_Text_Formatter *formatter, Wasm_Text_Section section) {
    assert(formatter->status == WASM_TEXT_STATUS_OK);
    assert(section.start <= section.end);

    uint32_t count = wasm_text_section_count_read(formatter, section);
    // Bounded like the loop over the types: every segment takes at least one byte.
    for (uint32_t i = 0; i < count && formatter->status == WASM_TEXT_STATUS_OK; i += 1) {
        size_t flag_offset = formatter->offset;
        byte flag = wasm_text_read_byte(formatter);
        size_t address_offset = formatter->offset;
        byte address_opcode = wasm_text_read_byte(formatter);
        int64_t address = wasm_text_read_signed_64(formatter);
        byte end_opcode = wasm_text_read_byte(formatter);
        size_t size_offset = formatter->offset;
        uint32_t size_bytes = wasm_text_read_unsigned_32(formatter);
        if (formatter->status != WASM_TEXT_STATUS_OK) {
            return false;
        }

        // Other segments are passive, or for another memory, and other addresses are computed.
        if (flag != WASM_DATA_SEGMENT_ACTIVE) {
            return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED, flag_offset);
        }
        if (address_opcode != WASM_OPCODE_I32_CONST || end_opcode != WASM_OPCODE_END) {
            return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_UNSUPPORTED, address_offset);
        }
        if (size_bytes > formatter->limit - formatter->offset) {
            return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_MALFORMED, size_offset);
        }

        wasm_text_write(&formatter->writer, STR("\n  (data (;"));
        wasm_text_write_unsigned(&formatter->writer, i);
        wasm_text_write(&formatter->writer, STR(";) (i32.const "));
        wasm_text_write_signed(&formatter->writer, address);
        wasm_text_write(&formatter->writer, STR(") "));
        wasm_text_write_string(&formatter->writer, formatter->module + formatter->offset,
                               size_bytes);
        wasm_text_write(&formatter->writer, STR(")"));
        formatter->offset += size_bytes;
    }

    return wasm_text_section_end(formatter);
}

/**
 * Returns the offset of entry `type_index` of the type section. The entries have different sizes,
 * so they are skipped one by one. The entry found last is remembered, and the search goes on from
 * there when the index is not smaller, so that the usual module, where function `i` has type `i`,
 * takes one pass over the types in total instead of one pass per function. That needs no memory
 * for a table of the entries, whose size the formatter cannot bound.
 */
static size_t wasm_text_type_find(Wasm_Text_Formatter *formatter, uint32_t type_index) {
    assert(formatter->status == WASM_TEXT_STATUS_OK);
    assert(type_index < formatter->type_count);

    if (type_index < formatter->type_cursor_index) {
        formatter->type_cursor_index = 0;
        formatter->type_cursor_offset = formatter->type_entries_start;
    }

    formatter->offset = formatter->type_cursor_offset;
    formatter->limit = formatter->type_entries_end;
    // The types were formatted already, so every entry is known to be a function type whose
    // value types are one byte each, and the reads cannot fail.
    for (uint32_t i = formatter->type_cursor_index; i < type_index; i += 1) {
        byte form = wasm_text_read_byte(formatter);
        assert(form == WASM_TYPE_FUNCTION);
        uint32_t parameter_count = wasm_text_read_unsigned_32(formatter);
        formatter->offset += parameter_count;
        uint32_t result_count = wasm_text_read_unsigned_32(formatter);
        formatter->offset += result_count;
        assert(formatter->offset <= formatter->limit);
    }

    assert(formatter->status == WASM_TEXT_STATUS_OK);
    formatter->type_cursor_index = type_index;
    formatter->type_cursor_offset = formatter->offset;
    return formatter->offset;
}

/**
 * Reads the number of entries at the start of `section`, and leaves `offset` at the first entry,
 * with `limit` at the end of the section. A section that the module does not have reads as one
 * with no entries.
 */
static uint32_t wasm_text_section_count_read(Wasm_Text_Formatter *formatter,
                                             Wasm_Text_Section section) {
    assert(section.start <= section.end);
    assert(section.end <= formatter->module_length);

    formatter->offset = section.start;
    formatter->limit = section.end;
    if (section.is_present == false) {
        return 0;
    }
    return wasm_text_read_unsigned_32(formatter);
}

/**
 * Returns whether everything went well and the part that is being read was read to its end.
 * Records an error when bytes are left over, because then the count of entries was wrong.
 */
static bool wasm_text_section_end(Wasm_Text_Formatter *formatter) {
    assert(formatter->offset <= formatter->limit);

    if (formatter->status != WASM_TEXT_STATUS_OK) {
        return false;
    }
    if (formatter->offset != formatter->limit) {
        return wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_MALFORMED, formatter->offset);
    }
    return true;
}

/**
 * Records the first error and returns false, so that a caller can report failure in one line.
 * Later errors are ignored, because they usually follow from the first one.
 */
static bool wasm_text_fail(Wasm_Text_Formatter *formatter, Wasm_Text_Status status,
                           size_t error_offset) {
    assert(status != WASM_TEXT_STATUS_OK);
    assert(error_offset <= formatter->module_length);

    if (formatter->status == WASM_TEXT_STATUS_OK) {
        formatter->status = status;
        formatter->error_offset = error_offset;
    }
    return false;
}

/**
 * Returns the next byte, or zero after it recorded an error when there is none before `limit`.
 */
static byte wasm_text_read_byte(Wasm_Text_Formatter *formatter) {
    assert(formatter->limit <= formatter->module_length);
    assert(formatter->offset <= formatter->limit);

    if (formatter->status != WASM_TEXT_STATUS_OK) {
        return 0;
    }
    if (formatter->offset == formatter->limit) {
        wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_MALFORMED, formatter->offset);
        return 0;
    }

    byte content = formatter->module[formatter->offset];
    formatter->offset += 1;
    return content;
}

/**
 * Reads a number in unsigned LEB128 that must fit in 32 bits, as counts, sizes and indexes do.
 * Returns zero after it recorded an error when it does not.
 */
static uint32_t wasm_text_read_unsigned_32(Wasm_Text_Formatter *formatter) {
    assert(formatter->offset <= formatter->limit);

    size_t start = formatter->offset;
    uint64_t number = 0;
    for (size_t i = 0; i < WASM_LEB128_32_BYTES_MAX; i += 1) {
        byte group = wasm_text_read_byte(formatter);
        if (formatter->status != WASM_TEXT_STATUS_OK) {
            return 0;
        }

        // At most 5 groups of 7 bits, so the number fits in 64 bits before it is checked.
        number |= (uint64_t)(group & 0x7F) << (7 * i);
        if ((group & 0x80) == 0) {
            if (number > UINT32_MAX) {
                wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_MALFORMED, start);
                return 0;
            }
            return (uint32_t)number;
        }
    }

    wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_MALFORMED, start);
    return 0;
}

/**
 * Reads a number in signed LEB128 that must fit in 64 bits, as the immediate of `i64.const` does.
 * Returns zero after it recorded an error when it does not. The unused bits of the last group are
 * not checked, because the formatter is not a validator.
 */
static int64_t wasm_text_read_signed_64(Wasm_Text_Formatter *formatter) {
    assert(formatter->offset <= formatter->limit);

    size_t start = formatter->offset;
    uint64_t bits = 0;
    for (size_t i = 0; i < WASM_LEB128_64_BYTES_MAX; i += 1) {
        byte group = wasm_text_read_byte(formatter);
        if (formatter->status != WASM_TEXT_STATUS_OK) {
            return 0;
        }

        // The shift is at most 63, and bits shifted out of an unsigned number are just lost.
        size_t shift = 7 * i;
        bits |= (uint64_t)(group & 0x7F) << shift;
        if ((group & 0x80) == 0) {
            // Copies the sign bit of the last group into the bits above it.
            if (shift + 7 < 64) {
                if ((group & 0x40) != 0) {
                    bits |= UINT64_MAX << (shift + 7);
                }
            }

            // Converting a number above `INT64_MAX` to `int64_t` is implementation-defined in C,
            // so the negative number is computed: the bits of `~bits` are its magnitude minus one.
            if (bits <= INT64_MAX) {
                return (int64_t)bits;
            }
            return -(int64_t)(~bits) - 1;
        }
    }

    wasm_text_fail(formatter, WASM_TEXT_STATUS_MODULE_MALFORMED, start);
    return 0;
}

/**
 * Returns the name of `value_type` in the text format, or an empty `Str` (`bytes == NULL`) when
 * it is a type that the formatter does not know.
 */
static Str wasm_text_value_type_name(byte value_type) {
    if (value_type == WASM_TYPE_I32) {
        return STR("i32");
    }
    if (value_type == WASM_TYPE_I64) {
        return STR("i64");
    }
    return STR_EMPTY;
}

/**
 * Writes `text`, or sets the overflow flag when it does not fit.
 */
static void wasm_text_write(Wasm_Text_Writer *writer, Str text) {
    assert(writer->length <= writer->capacity);
    assert(text.bytes != NULL);

    if (writer->is_overflowed) {
        return;
    }
    if (text.length > writer->capacity - writer->length) {
        writer->is_overflowed = true;
        return;
    }

    memcpy(writer->bytes + writer->length, text.bytes, text.length);
    writer->length += text.length;
}

/**
 * Starts a new line for an instruction, indented to be inside its function, and writes `name`.
 */
static void wasm_text_write_instruction(Wasm_Text_Writer *writer, Str name) {
    assert(name.bytes != NULL);
    assert(name.length > 0);

    wasm_text_write(writer, STR("\n    "));
    // Two more spaces for each structured instruction that is open.
    for (size_t i = 0; i < writer->block_depth; i += 1) {
        wasm_text_write(writer, STR("  "));
    }
    wasm_text_write(writer, name);
}

/**
 * Writes bytes, such as a name or the content of a data segment, as a string of the text format.
 * Printable ASCII is written as it is, except for the quote and the backslash, which end or
 * escape the string. Every other byte is written as a backslash and two hexadecimal digits, so
 * that the string can hold any bytes, and the text stays on one line.
 */
static void wasm_text_write_string(Wasm_Text_Writer *writer, byte const *bytes, size_t length) {
    assert(bytes != NULL);
    assert(writer->length <= writer->capacity);

    static char const hexadecimal_digits[] = "0123456789abcdef";

    wasm_text_write(writer, STR("\""));
    for (size_t i = 0; i < length; i += 1) {
        byte character = bytes[i];
        bool is_printable = character >= 0x20 && character < 0x7F;
        if (is_printable && character != '"' && character != '\\') {
            char plain[1] = {(char)character};
            wasm_text_write(writer, str_make(plain, 1));
        } else {
            char escaped[3] = {
                '\\',
                hexadecimal_digits[character >> 4],
                hexadecimal_digits[character & 0x0F],
            };
            wasm_text_write(writer, str_make(escaped, 3));
        }
    }
    wasm_text_write(writer, STR("\""));
}

static void wasm_text_write_signed(Wasm_Text_Writer *writer, int64_t number) {
    assert(writer->length <= writer->capacity);

    if (number < 0) {
        wasm_text_write(writer, STR("-"));
        // Negating `INT64_MIN` overflows, so the magnitude is computed in unsigned arithmetic,
        // which wraps around as needed.
        wasm_text_write_unsigned(writer, (uint64_t)0 - (uint64_t)number);
    } else {
        wasm_text_write_unsigned(writer, (uint64_t)number);
    }
}

static void wasm_text_write_unsigned(Wasm_Text_Writer *writer, uint64_t number) {
    assert(writer->length <= writer->capacity);

    // The digits come out least significant first, so they are filled in from the end.
    char digits[WASM_TEXT_DECIMAL_DIGITS_MAX] = {0};
    size_t start = WASM_TEXT_DECIMAL_DIGITS_MAX;
    uint64_t rest = number;
    for (size_t i = 0; i < WASM_TEXT_DECIMAL_DIGITS_MAX; i += 1) {
        start -= 1;
        digits[start] = (char)('0' + rest % 10);
        rest /= 10;
        if (rest == 0) {
            break;
        }
    }

    assert(rest == 0);
    assert(start < WASM_TEXT_DECIMAL_DIGITS_MAX);
    wasm_text_write(writer, str_make(digits + start, WASM_TEXT_DECIMAL_DIGITS_MAX - start));
}
