#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <mialu/array_slice.h>
#include <mialu/common_types.h>
#include <mialu/str.h>
#include <mialu/x64-text.h>
#include <mialu/x64.h>

// The text follows the inline assembly of the Odin programming language, see
// https://odin-lang.org/docs/inline-asm/: the operand that is written to comes first, a register is
// written with a `%` in front of it, a memory operand is `[base + displacement]`, a label is
// `.name:` and is used as `.name`, and a comment starts with `//`. Odin writes the instructions of
// a template inside of a block, and here they are the whole file. A memory operand that is
// relative to the instruction pointer, which Odin does not describe, is `[%rip + displacement]`,
// and a comment says what it is that it points at.

enum {
    // The structures of the file, see `x64_headers_write` in x64.c.
    X64_TEXT_DOS_POINTER_OFFSET = 0x3C,
    X64_TEXT_FILE_HEADER_SIZE_BYTES = 24,
    X64_TEXT_SECTION_HEADER_SIZE_BYTES = 40,
    X64_TEXT_IMPORT_DESCRIPTOR_SIZE_BYTES = 20,
    // An entry of the table of imports is the address of the name of an import.
    X64_TEXT_IMPORT_ENTRY_SIZE_BYTES = 8,
    // A name of an import is preceded by a hint of two bytes.
    X64_TEXT_IMPORT_HINT_SIZE_BYTES = 2,
    X64_TEXT_DIRECTORY_OFFSET = 112,
    X64_TEXT_DIRECTORY_IMPORT = 1,
    X64_TEXT_DIRECTORY_ADDRESS = 12,
    X64_TEXT_DIRECTORY_SIZE_BYTES = 8,
    // The names of the imports and of the library are short, and a name that does not end within
    // this many bytes is not read, so that a damaged file cannot make the loop that reads it run
    // long.
    X64_TEXT_NAME_LENGTH_MAX = 256,
    X64_TEXT_REGISTER_COUNT = 16,
    // The longest decimal number that is written: `UINT64_MAX` has 20 digits.
    X64_TEXT_DECIMAL_DIGITS_MAX = 20,
    X64_TEXT_HEXADECIMAL_DIGITS_MAX = 16,
    // The first 8 bytes of the section for the temporary storage are the number of bytes in use,
    // and the storage follows, see `X64_TEMPORARY_USED_SIZE_BYTES` in x64.c.
    X64_TEXT_TEMPORARY_USED_SIZE_BYTES = 8,
    // A number is written in decimal if it is smaller than this, and otherwise in hexadecimal,
    // because a number that is large is mostly a pattern of bits, such as the bytes of a string.
    X64_TEXT_DECIMAL_LIMIT = 65536,
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
    bool is_overflowed;
} X64_Text_Writer;

/**
 * Where a section is in the file and in memory.
 */
typedef struct {
    uint32_t rva;
    uint32_t size_bytes;
    uint32_t file_offset;
    bool is_present;
} X64_Text_Section;

/**
 * The bytes of the code, which are read from `position` on. A read past the end sets `is_failed`
 * and gives zero, and so does every read after it, so the code that reads checks the flag once,
 * after the instruction.
 */
typedef struct {
    byte const *bytes;
    size_t length;
    size_t position;
    bool is_failed;
} X64_Text_Cursor;

/**
 * The operands that the byte after the opcode, the ModRM byte, describes. `register_number` is the
 * register of the middle bits, and `field` is the same bits without the extension of the prefix,
 * which some opcodes use to say which instruction it is. The other operand is a register, which is
 * `base`, when `is_register` is set. Otherwise it is the memory at `base` plus `displacement`, or
 * at the instruction pointer plus it when `is_rip` is set.
 */
typedef struct {
    int32_t displacement;
    byte base;
    byte field;
    byte register_number;
    bool is_register;
    bool is_rip;
    bool is_supported;
} X64_Text_Operand;

/**
 * The state shared by the formatting functions. Only the first error is kept.
 */
typedef struct {
    X64_Text_Writer writer;
    byte const *executable;
    size_t executable_length;
    X64_Symbol const *symbols;
    size_t symbol_count;
    X64_Text_Section data;
    X64_Text_Section rdata;
    X64_Text_Section text;
    // Where the tables of the one library that the program imports from are in memory. The table of
    // names holds the place of the name of each import, in the order of the address table.
    uint32_t address_table_rva;
    uint32_t address_table_size_bytes;
    uint32_t library_name_rva;
    uint32_t name_table_rva;
    // The place in the code of the end of a jump over a breakpoint, which gets a label.
    size_t label_offset;
    size_t error_offset;
    X64_Text_Status status;
    bool has_label;
} X64_Text_Formatter;

static bool             x64_text_fail               (X64_Text_Formatter *formatter,
                                                     X64_Text_Status status, size_t error_offset);
static bool             x64_text_format_code        (X64_Text_Formatter *formatter);
static size_t           x64_text_format_instruction (X64_Text_Formatter *formatter,
                                                     X64_Text_Cursor *cursor);
static size_t           x64_text_format_jump        (X64_Text_Formatter *formatter,
                                                     X64_Text_Cursor *cursor);
static size_t           x64_text_format_operands    (X64_Text_Formatter *formatter,
                                                     X64_Text_Cursor *cursor, byte opcode,
                                                     byte rex);
static bool             x64_text_headers_read       (X64_Text_Formatter *formatter);
static uint16_t         x64_text_load_u16           (byte const *source);
static uint32_t         x64_text_load_u32           (byte const *source);
static uint64_t         x64_text_load_u64           (byte const *source);
static byte const      *x64_text_memory_find        (X64_Text_Formatter const *formatter,
                                                     uint32_t rva, size_t length);
static Str              x64_text_name_find          (X64_Text_Formatter const *formatter,
                                                     uint32_t rva);
static X64_Text_Operand x64_text_operand_read       (X64_Text_Cursor *cursor, byte rex);
static byte             x64_text_read_byte          (X64_Text_Cursor *cursor);
static int32_t          x64_text_read_signed_32     (X64_Text_Cursor *cursor);
static uint64_t         x64_text_read_unsigned_64   (X64_Text_Cursor *cursor);
static void             x64_text_write              (X64_Text_Writer *writer, Str text);
static void             x64_text_write_comment      (X64_Text_Formatter *formatter,
                                                     X64_Text_Operand operand, size_t end_offset);
static void             x64_text_write_hexadecimal  (X64_Text_Writer *writer, uint64_t number);
static void             x64_text_write_immediate    (X64_Text_Writer *writer, int64_t number);
static void             x64_text_write_label_target (X64_Text_Formatter *formatter,
                                                     int64_t target_offset);
static void             x64_text_write_memory       (X64_Text_Writer *writer,
                                                     X64_Text_Operand operand);
static void             x64_text_write_operand      (X64_Text_Writer *writer,
                                                     X64_Text_Operand operand, bool is_wide);
static void             x64_text_write_register     (X64_Text_Writer *writer,
                                                     byte register_number, bool is_wide);
static void             x64_text_write_signed       (X64_Text_Writer *writer, int64_t number);
static void             x64_text_write_unsigned     (X64_Text_Writer *writer, uint64_t number);

static Str const X64_TEXT_REGISTERS_WIDE[X64_TEXT_REGISTER_COUNT] = {
    {.bytes = "rax", .length = 3}, {.bytes = "rcx", .length = 3}, {.bytes = "rdx", .length = 3},
    {.bytes = "rbx", .length = 3}, {.bytes = "rsp", .length = 3}, {.bytes = "rbp", .length = 3},
    {.bytes = "rsi", .length = 3}, {.bytes = "rdi", .length = 3}, {.bytes = "r8", .length = 2},
    {.bytes = "r9", .length = 2},  {.bytes = "r10", .length = 3}, {.bytes = "r11", .length = 3},
    {.bytes = "r12", .length = 3}, {.bytes = "r13", .length = 3}, {.bytes = "r14", .length = 3},
    {.bytes = "r15", .length = 3},
};

static Str const X64_TEXT_REGISTERS_NARROW[X64_TEXT_REGISTER_COUNT] = {
    {.bytes = "eax", .length = 3},  {.bytes = "ecx", .length = 3},  {.bytes = "edx", .length = 3},
    {.bytes = "ebx", .length = 3},  {.bytes = "esp", .length = 3},  {.bytes = "ebp", .length = 3},
    {.bytes = "esi", .length = 3},  {.bytes = "edi", .length = 3},  {.bytes = "r8d", .length = 3},
    {.bytes = "r9d", .length = 3},  {.bytes = "r10d", .length = 4}, {.bytes = "r11d", .length = 4},
    {.bytes = "r12d", .length = 4}, {.bytes = "r13d", .length = 4}, {.bytes = "r14d", .length = 4},
    {.bytes = "r15d", .length = 4},
};

X64_Text_Result x64_text_format(Array_Slice executable, Array_Slice symbols,
                                Array_Slice output_buffer) {
    assert(executable.elements != NULL);
    assert(output_buffer.elements != NULL);
    assert(symbols.elements != NULL || symbols.length == 0);

    X64_Text_Formatter formatter = {
        .writer =
            {
                .bytes = output_buffer.elements,
                .capacity = output_buffer.length,
                .length = 0,
                .is_overflowed = false,
            },
        .executable = executable.elements,
        .executable_length = executable.length,
        .symbols = symbols.elements,
        .symbol_count = symbols.length,
        .data = {0},
        .rdata = {0},
        .text = {0},
        .address_table_rva = 0,
        .address_table_size_bytes = 0,
        .library_name_rva = 0,
        .name_table_rva = 0,
        .label_offset = 0,
        .error_offset = 0,
        .status = X64_TEXT_STATUS_OK,
        .has_label = false,
    };

    bool is_ok = x64_text_headers_read(&formatter);
    if (is_ok) {
        x64_text_write(&formatter.writer, STR("// x64-win32\n"));
        is_ok = x64_text_format_code(&formatter);
    }

    // A real error is more useful than the overflow that may follow from it.
    if (is_ok && formatter.writer.is_overflowed) {
        is_ok = x64_text_fail(&formatter, X64_TEXT_STATUS_OUTPUT_TOO_LARGE, 0);
    }

    assert(is_ok == (formatter.status == X64_TEXT_STATUS_OK));
    assert(formatter.writer.length <= output_buffer.length);
    Str text = STR_EMPTY;
    if (is_ok) {
        text = str_make(output_buffer.elements, formatter.writer.length);
    }
    return (X64_Text_Result){
        .text = text,
        .error_offset = formatter.error_offset,
        .status = formatter.status,
    };
}

/**
 * Records the first error. Returns false, so that a caller can return it.
 */
static bool x64_text_fail(X64_Text_Formatter *formatter, X64_Text_Status status,
                          size_t error_offset) {
    assert(status != X64_TEXT_STATUS_OK);

    if (formatter->status == X64_TEXT_STATUS_OK) {
        formatter->status = status;
        formatter->error_offset = error_offset;
    }
    return false;
}

static uint16_t x64_text_load_u16(byte const *source) {
    return (uint16_t)(source[0] | (source[1] << 8));
}

static uint32_t x64_text_load_u32(byte const *source) {
    return (uint32_t)x64_text_load_u16(source) | ((uint32_t)x64_text_load_u16(source + 2) << 16);
}

static uint64_t x64_text_load_u64(byte const *source) {
    return (uint64_t)x64_text_load_u32(source) | ((uint64_t)x64_text_load_u32(source + 4) << 32);
}

/**
 * Finds the sections of the file, and where the tables of imports are, which name the imports that
 * the code calls. Returns false after it recorded an error.
 */
static bool x64_text_headers_read(X64_Text_Formatter *formatter) {
    byte const *file = formatter->executable;
    size_t length = formatter->executable_length;

    if (length < X64_TEXT_DOS_POINTER_OFFSET + 4 || file[0] != 'M' || file[1] != 'Z') {
        return x64_text_fail(formatter, X64_TEXT_STATUS_EXECUTABLE_MALFORMED, 0);
    }
    size_t header = x64_text_load_u32(file + X64_TEXT_DOS_POINTER_OFFSET);
    if (header > length || length - header < X64_TEXT_FILE_HEADER_SIZE_BYTES ||
        memcmp(file + header, "PE\0\0", 4) != 0) {
        return x64_text_fail(formatter, X64_TEXT_STATUS_EXECUTABLE_MALFORMED, header);
    }
    // The processor is x64.
    if (x64_text_load_u16(file + header + 4) != 0x8664) {
        return x64_text_fail(formatter, X64_TEXT_STATUS_EXECUTABLE_UNSUPPORTED, header + 4);
    }

    size_t section_count = x64_text_load_u16(file + header + 6);
    size_t optional_size = x64_text_load_u16(file + header + 20);
    size_t optional = header + X64_TEXT_FILE_HEADER_SIZE_BYTES;
    size_t sections = optional + optional_size;
    size_t directories = optional + X64_TEXT_DIRECTORY_OFFSET;
    size_t directories_end = directories +
                             (X64_TEXT_DIRECTORY_ADDRESS + 1) * X64_TEXT_DIRECTORY_SIZE_BYTES;
    if (optional_size < X64_TEXT_DIRECTORY_OFFSET || directories_end > sections ||
        sections > length ||
        (length - sections) / X64_TEXT_SECTION_HEADER_SIZE_BYTES < section_count) {
        return x64_text_fail(formatter, X64_TEXT_STATUS_EXECUTABLE_MALFORMED, optional);
    }

    // Every section has a name of 8 bytes, its size and place in memory, and its size and place in
    // the file. The size in memory of the code is the size of the code without the padding after
    // it.
    for (size_t i = 0; i < section_count; i += 1) {
        byte const *section = file + sections + i * X64_TEXT_SECTION_HEADER_SIZE_BYTES;
        X64_Text_Section found = {
            .rva = x64_text_load_u32(section + 12),
            .size_bytes = x64_text_load_u32(section + 8),
            .file_offset = x64_text_load_u32(section + 20),
            .is_present = true,
        };
        if (memcmp(section, ".text\0\0\0", 8) == 0) {
            formatter->text = found;
        } else if (memcmp(section, ".rdata\0\0", 8) == 0) {
            formatter->rdata = found;
        } else if (memcmp(section, ".data\0\0\0", 8) == 0) {
            formatter->data = found;
        }
    }
    if (formatter->text.is_present == false || formatter->rdata.is_present == false ||
        formatter->data.is_present == false) {
        return x64_text_fail(formatter, X64_TEXT_STATUS_EXECUTABLE_MALFORMED, sections);
    }
    if (formatter->text.file_offset > length ||
        length - formatter->text.file_offset < formatter->text.size_bytes) {
        return x64_text_fail(formatter, X64_TEXT_STATUS_EXECUTABLE_MALFORMED,
                             formatter->text.file_offset);
    }

    byte const *import_directory =
        file + directories + X64_TEXT_DIRECTORY_IMPORT * X64_TEXT_DIRECTORY_SIZE_BYTES;
    byte const *address_directory =
        file + directories + X64_TEXT_DIRECTORY_ADDRESS * X64_TEXT_DIRECTORY_SIZE_BYTES;
    // The first descriptor of the table of imports is the one library that the program imports
    // from: where the names of its imports are, and what its name is.
    byte const *descriptor = x64_text_memory_find(formatter, x64_text_load_u32(import_directory),
                                                  X64_TEXT_IMPORT_DESCRIPTOR_SIZE_BYTES);
    if (descriptor == NULL) {
        return x64_text_fail(formatter, X64_TEXT_STATUS_EXECUTABLE_MALFORMED, directories);
    }
    formatter->name_table_rva = x64_text_load_u32(descriptor);
    formatter->library_name_rva = x64_text_load_u32(descriptor + 12);
    formatter->address_table_rva = x64_text_load_u32(address_directory);
    formatter->address_table_size_bytes = x64_text_load_u32(address_directory + 4);
    return true;
}

/**
 * Returns where `length` bytes at `rva` are in the file, if they are in a section that has bytes
 * in the file. Returns NULL when they are not, so that a damaged file is not read past its end.
 */
static byte const *x64_text_memory_find(X64_Text_Formatter const *formatter, uint32_t rva,
                                        size_t length) {
    X64_Text_Section const *sections[2] = {&formatter->rdata, &formatter->text};
    for (size_t i = 0; i < 2; i += 1) {
        X64_Text_Section const *section = sections[i];
        if (rva < section->rva) {
            continue;
        }
        size_t within = rva - section->rva;
        if (within > section->size_bytes || section->size_bytes - within < length) {
            continue;
        }
        size_t file_offset = (size_t)section->file_offset + within;
        if (file_offset > formatter->executable_length ||
            formatter->executable_length - file_offset < length) {
            continue;
        }
        return formatter->executable + file_offset;
    }
    return NULL;
}

/**
 * Returns the string that ends with a zero at `rva`, or an empty `Str` if it is not in the file or
 * does not end soon enough.
 */
static Str x64_text_name_find(X64_Text_Formatter const *formatter, uint32_t rva) {
    byte const *start = x64_text_memory_find(formatter, rva, 1);
    if (start == NULL) {
        return STR_EMPTY;
    }

    for (size_t length = 0; length < X64_TEXT_NAME_LENGTH_MAX; length += 1) {
        byte const *character = x64_text_memory_find(formatter, rva + (uint32_t)length, 1);
        if (character == NULL) {
            return STR_EMPTY;
        }
        if (*character == 0) {
            return str_make((char const *)start, length);
        }
    }
    return STR_EMPTY;
}

/**
 * Formats the instructions from the start of the code to its end, with a label where a procedure
 * starts. Returns false after it recorded an error.
 */
static bool x64_text_format_code(X64_Text_Formatter *formatter) {
    X64_Text_Cursor cursor = {
        .bytes = formatter->executable + formatter->text.file_offset,
        .length = formatter->text.size_bytes,
        .position = 0,
        .is_failed = false,
    };
    X64_Text_Writer *writer = &formatter->writer;

    x64_text_write(writer, STR("\n.start:\n"));

    size_t symbol_index = 0;
    // An instruction is at least one byte, so this bounds the loop.
    for (size_t i = 0; i < cursor.length && cursor.position < cursor.length; i += 1) {
        size_t offset = cursor.position;

        if (symbol_index < formatter->symbol_count &&
            formatter->symbols[symbol_index].offset == offset) {
            x64_text_write(writer, STR("\n."));
            x64_text_write(writer, formatter->symbols[symbol_index].name);
            x64_text_write(writer, STR(":\n"));
            symbol_index += 1;
        }
        if (formatter->has_label && formatter->label_offset == offset) {
            x64_text_write_label_target(formatter, (int64_t)offset);
            x64_text_write(writer, STR(":\n"));
            formatter->has_label = false;
        }

        size_t length = x64_text_format_instruction(formatter, &cursor);
        if (length == 0) {
            return false;
        }
        assert(cursor.position == offset + length);
    }

    // A jump whose target is past the end of the code means that the code ends in the middle of
    // it.
    if (formatter->has_label) {
        return x64_text_fail(formatter, X64_TEXT_STATUS_EXECUTABLE_MALFORMED,
                             formatter->text.file_offset + formatter->label_offset);
    }
    return true;
}

/**
 * Formats the instruction at the cursor on a line, and moves the cursor past it. Returns its
 * length in bytes, or zero after it recorded an error.
 */
static size_t x64_text_format_instruction(X64_Text_Formatter *formatter,
                                          X64_Text_Cursor *cursor) {
    assert(cursor->position < cursor->length);

    X64_Text_Writer *writer = &formatter->writer;
    size_t start = cursor->position;
    size_t error_offset = formatter->text.file_offset + start;

    // The prefix says that the operands are 64-bit, and that the registers are one of `r8` to
    // `r15`. It comes before the opcode.
    byte opcode = x64_text_read_byte(cursor);
    byte rex = 0;
    if ((opcode & 0xF0) == 0x40) {
        rex = opcode;
        opcode = x64_text_read_byte(cursor);
    }
    if (cursor->is_failed) {
        x64_text_fail(formatter, X64_TEXT_STATUS_EXECUTABLE_MALFORMED, error_offset);
        return 0;
    }
    // The bit for the index of a memory operand is never set, because the code has none.
    if ((rex & 0x02) != 0) {
        x64_text_fail(formatter, X64_TEXT_STATUS_EXECUTABLE_UNSUPPORTED, error_offset);
        return 0;
    }

    x64_text_write(writer, STR("    "));
    bool is_wide = (rex & 0x08) != 0;
    byte base_extension = (rex & 0x01) != 0 ? 8 : 0;

    size_t length = 0;
    if (opcode >= 0x50 && opcode <= 0x5F && rex == 0) {
        x64_text_write(writer, opcode < 0x58 ? STR("push ") : STR("pop "));
        x64_text_write_register(writer, (byte)(opcode & 7), true);
        length = cursor->position - start;
    } else if (opcode >= 0xB8 && opcode <= 0xBF && is_wide) {
        x64_text_write(writer, STR("mov "));
        x64_text_write_register(writer, (byte)((opcode & 7) | base_extension), true);
        x64_text_write(writer, STR(", "));
        x64_text_write_immediate(writer, (int64_t)x64_text_read_unsigned_64(cursor));
        length = cursor->position - start;
    } else {
        switch (opcode) {
        case 0x01:
        case 0x3D:
        case 0x83:
        case 0x89:
        case 0x8B:
        case 0x8D:
        case 0xFF:
            length = x64_text_format_operands(formatter, cursor, opcode, rex);
            if (length != 0) {
                length = cursor->position - start;
            }
            break;
        case 0x76:
        case 0xE8:
            length = x64_text_format_jump(formatter, cursor);
            if (length != 0) {
                length = cursor->position - start;
            }
            break;
        case 0xC3:
            x64_text_write(writer, STR("ret"));
            length = cursor->position - start;
            break;
        case 0xC9:
            x64_text_write(writer, STR("leave"));
            length = cursor->position - start;
            break;
        case 0xCC:
            x64_text_write(writer, STR("int3"));
            length = cursor->position - start;
            break;
        default:
            x64_text_fail(formatter, X64_TEXT_STATUS_EXECUTABLE_UNSUPPORTED, error_offset);
            return 0;
        }
    }

    if (cursor->is_failed) {
        x64_text_fail(formatter, X64_TEXT_STATUS_EXECUTABLE_MALFORMED, error_offset);
        return 0;
    }
    if (formatter->status != X64_TEXT_STATUS_OK) {
        return 0;
    }
    x64_text_write(writer, STR("\n"));
    assert(length == cursor->position - start);
    return length;
}

/**
 * Formats the instructions that have a register and an operand, or an operand and a number, which
 * is what the byte after the opcode says. Returns a value that is not zero, or zero after it
 * recorded an error.
 */
static size_t x64_text_format_operands(X64_Text_Formatter *formatter, X64_Text_Cursor *cursor,
                                       byte opcode, byte rex) {
    X64_Text_Writer *writer = &formatter->writer;
    bool is_wide = (rex & 0x08) != 0;
    size_t error_offset = formatter->text.file_offset + cursor->position;

    if (opcode == 0x3D) {
        // A comparison of `rax` with a number of 32 bits, which the processor widens.
        if (is_wide == false) {
            x64_text_fail(formatter, X64_TEXT_STATUS_EXECUTABLE_UNSUPPORTED, error_offset);
            return 0;
        }
        x64_text_write(writer, STR("cmp %rax, "));
        x64_text_write_immediate(writer, x64_text_read_signed_32(cursor));
        return 1;
    }

    X64_Text_Operand operand = x64_text_operand_read(cursor, rex);
    if (operand.is_supported == false) {
        x64_text_fail(formatter, X64_TEXT_STATUS_EXECUTABLE_UNSUPPORTED, error_offset);
        return 0;
    }

    switch (opcode) {
    case 0x01:
        // The operand that is written to is the one of the byte, and the register is added to it.
        x64_text_write(writer, STR("add "));
        x64_text_write_operand(writer, operand, is_wide);
        x64_text_write(writer, STR(", "));
        x64_text_write_register(writer, operand.register_number, is_wide);
        break;
    case 0x83: {
        // The three instructions that have a number of 8 bits, which the processor widens, and
        // are told apart by the middle bits instead of a register. The operand is a register.
        Str mnemonic = STR_EMPTY;
        if (operand.field == 0) {
            mnemonic = STR("add ");
        } else if (operand.field == 5) {
            mnemonic = STR("sub ");
        } else if (operand.field == 7) {
            mnemonic = STR("cmp ");
        }
        int8_t number = (int8_t)x64_text_read_byte(cursor);
        if (mnemonic.bytes == NULL || operand.is_register == false || is_wide == false) {
            x64_text_fail(formatter, X64_TEXT_STATUS_EXECUTABLE_UNSUPPORTED, error_offset);
            return 0;
        }
        x64_text_write(writer, mnemonic);
        x64_text_write_operand(writer, operand, true);
        x64_text_write(writer, STR(", "));
        x64_text_write_immediate(writer, number);
        break;
    }
    case 0x89:
        x64_text_write(writer, STR("mov "));
        x64_text_write_operand(writer, operand, is_wide);
        x64_text_write(writer, STR(", "));
        x64_text_write_register(writer, operand.register_number, is_wide);
        break;
    case 0x8B:
        x64_text_write(writer, STR("mov "));
        x64_text_write_register(writer, operand.register_number, is_wide);
        x64_text_write(writer, STR(", "));
        x64_text_write_operand(writer, operand, is_wide);
        break;
    case 0x8D:
        if (operand.is_register) {
            x64_text_fail(formatter, X64_TEXT_STATUS_EXECUTABLE_UNSUPPORTED, error_offset);
            return 0;
        }
        x64_text_write(writer, STR("lea "));
        x64_text_write_register(writer, operand.register_number, is_wide);
        x64_text_write(writer, STR(", "));
        x64_text_write_operand(writer, operand, is_wide);
        break;
    case 0xFF:
        // A call through memory, which is how an import is called. The size is the one of an
        // address, and says that a memory operand is read as one.
        if (operand.field != 2 || operand.is_register) {
            x64_text_fail(formatter, X64_TEXT_STATUS_EXECUTABLE_UNSUPPORTED, error_offset);
            return 0;
        }
        x64_text_write(writer, STR("call "));
        x64_text_write_operand(writer, operand, true);
        x64_text_write(writer, STR(":u64"));
        break;
    default:
        assert(false);
        return 0;
    }

    if (operand.is_rip) {
        x64_text_write_comment(formatter, operand, cursor->position);
    }
    return 1;
}

/**
 * Formats a call of a procedure of the code, or the jump over a breakpoint that checks whether a
 * string fits. Both have the distance to go in the instruction. Returns a value that is not
 * zero, or zero after it recorded an error.
 */
static size_t x64_text_format_jump(X64_Text_Formatter *formatter, X64_Text_Cursor *cursor) {
    X64_Text_Writer *writer = &formatter->writer;
    size_t error_offset = formatter->text.file_offset + cursor->position - 1;

    // The opcode is the byte before the distance, which is at the start of the instruction.
    byte opcode = cursor->bytes[cursor->position - 1];
    if (opcode == 0x76) {
        int8_t distance = (int8_t)x64_text_read_byte(cursor);
        size_t target = cursor->position + (size_t)(int64_t)distance;
        // The jump goes forward, and the code has one place to go to at a time, because the only
        // jump is over the breakpoint that follows it.
        if (distance <= 0 || formatter->has_label) {
            x64_text_fail(formatter, X64_TEXT_STATUS_EXECUTABLE_UNSUPPORTED, error_offset);
            return 0;
        }
        formatter->has_label = true;
        formatter->label_offset = target;
        x64_text_write(writer, STR("jbe "));
        x64_text_write_label_target(formatter, (int64_t)target);
        return 1;
    }

    assert(opcode == 0xE8);
    int32_t distance = x64_text_read_signed_32(cursor);
    int64_t target = (int64_t)cursor->position + distance;
    if (target < 0 || (size_t)target >= cursor->length) {
        x64_text_fail(formatter, X64_TEXT_STATUS_EXECUTABLE_MALFORMED, error_offset);
        return 0;
    }
    x64_text_write(writer, STR("call "));
    x64_text_write_label_target(formatter, target);
    return 1;
}

/**
 * Writes the label of the code that starts at `target_offset`: `.start` for the start of the
 * program, `.name` for a procedure, and for a place inside a procedure, which only a jump has,
 * `.skip_` and the offset. A label that ends a line is followed by a colon by the caller.
 */
static void x64_text_write_label_target(X64_Text_Formatter *formatter, int64_t target_offset) {
    X64_Text_Writer *writer = &formatter->writer;

    x64_text_write(writer, STR("."));
    if (target_offset == 0) {
        x64_text_write(writer, STR("start"));
        return;
    }
    for (size_t i = 0; i < formatter->symbol_count; i += 1) {
        if ((int64_t)formatter->symbols[i].offset == target_offset) {
            x64_text_write(writer, formatter->symbols[i].name);
            return;
        }
    }
    x64_text_write(writer, STR("skip_"));
    x64_text_write_unsigned(writer, (uint64_t)target_offset);
}

/**
 * Writes a comment that says what the memory that is relative to the instruction pointer is. It
 * knows the imports and the temporary storage, and says nothing about another place.
 * `end_offset` is the offset in the code of the end of the instruction, which the distance is
 * counted from.
 */
static void x64_text_write_comment(X64_Text_Formatter *formatter, X64_Text_Operand operand,
                                   size_t end_offset) {
    assert(operand.is_rip);

    X64_Text_Writer *writer = &formatter->writer;
    int64_t target = (int64_t)formatter->text.rva + (int64_t)end_offset + operand.displacement;
    if (target < 0 || target > (int64_t)UINT32_MAX) {
        return;
    }
    uint32_t rva = (uint32_t)target;

    if (rva >= formatter->address_table_rva &&
        rva - formatter->address_table_rva < formatter->address_table_size_bytes) {
        // The address of an import, which is the place in the address table that has the same
        // number as the name in the table of names.
        uint32_t index = (rva - formatter->address_table_rva) / X64_TEXT_IMPORT_ENTRY_SIZE_BYTES;
        byte const *entry = x64_text_memory_find(
            formatter, formatter->name_table_rva + index * X64_TEXT_IMPORT_ENTRY_SIZE_BYTES,
            X64_TEXT_IMPORT_ENTRY_SIZE_BYTES);
        if (entry == NULL) {
            return;
        }
        uint32_t name_rva = (uint32_t)x64_text_load_u64(entry);
        Str name = x64_text_name_find(formatter, name_rva + X64_TEXT_IMPORT_HINT_SIZE_BYTES);
        Str library = x64_text_name_find(formatter, formatter->library_name_rva);
        if (name.bytes != NULL && library.bytes != NULL) {
            x64_text_write(writer, STR("  // "));
            x64_text_write(writer, library);
            x64_text_write(writer, STR(": "));
            x64_text_write(writer, name);
        }
        return;
    }
    if (rva == formatter->data.rva) {
        x64_text_write(writer, STR("  // the number of bytes of the temporary storage in use"));
    } else if (rva == formatter->data.rva + X64_TEXT_TEMPORARY_USED_SIZE_BYTES) {
        x64_text_write(writer, STR("  // the start of the temporary storage"));
    }
}

/**
 * Reads the byte that says what the operands are, and the displacement that follows it. The
 * operand is not supported when it has a byte that adds an index, which the code does not use.
 */
static X64_Text_Operand x64_text_operand_read(X64_Text_Cursor *cursor, byte rex) {
    byte modrm = x64_text_read_byte(cursor);
    byte mode = (byte)(modrm >> 6);
    byte field = (byte)((modrm >> 3) & 7);
    byte other = (byte)(modrm & 7);

    X64_Text_Operand operand = {
        .displacement = 0,
        .base = (byte)(other | ((rex & 0x01) != 0 ? 8 : 0)),
        .field = field,
        .register_number = (byte)(field | ((rex & 0x04) != 0 ? 8 : 0)),
        .is_register = mode == 3,
        .is_rip = false,
        .is_supported = true,
    };
    if (operand.is_register) {
        return operand;
    }

    // Memory. The other is 4 when a byte follows that adds an index, and 5 with mode 0 when the
    // address is relative to the instruction pointer.
    if (other == 4) {
        operand.is_supported = false;
    } else if (mode == 0 && other == 5) {
        operand.is_rip = true;
        operand.displacement = x64_text_read_signed_32(cursor);
    } else if (mode == 1) {
        operand.displacement = (int8_t)x64_text_read_byte(cursor);
    } else if (mode == 2) {
        operand.displacement = x64_text_read_signed_32(cursor);
    }
    return operand;
}

static void x64_text_write_operand(X64_Text_Writer *writer, X64_Text_Operand operand,
                                   bool is_wide) {
    if (operand.is_register) {
        x64_text_write_register(writer, operand.base, is_wide);
    } else {
        x64_text_write_memory(writer, operand);
    }
}

/**
 * Writes `[base + displacement]`. The displacement is left out when it is zero, and a negative one
 * is subtracted. The base is a 64-bit register, because an address is.
 */
static void x64_text_write_memory(X64_Text_Writer *writer, X64_Text_Operand operand) {
    assert(operand.is_register == false);

    x64_text_write(writer, STR("["));
    if (operand.is_rip) {
        x64_text_write(writer, STR("%rip"));
    } else {
        x64_text_write_register(writer, operand.base, true);
    }
    if (operand.displacement > 0) {
        x64_text_write(writer, STR(" + "));
        x64_text_write_signed(writer, operand.displacement);
    } else if (operand.displacement < 0) {
        x64_text_write(writer, STR(" - "));
        x64_text_write_signed(writer, -(int64_t)operand.displacement);
    }
    x64_text_write(writer, STR("]"));
}

static void x64_text_write_register(X64_Text_Writer *writer, byte register_number,
                                    bool is_wide) {
    assert(register_number < X64_TEXT_REGISTER_COUNT);

    x64_text_write(writer, STR("%"));
    if (is_wide) {
        x64_text_write(writer, X64_TEXT_REGISTERS_WIDE[register_number]);
    } else {
        x64_text_write(writer, X64_TEXT_REGISTERS_NARROW[register_number]);
    }
}

/**
 * Writes a number that an instruction holds: in decimal if it is small, whether it is negative or
 * not, and in hexadecimal otherwise.
 */
static void x64_text_write_immediate(X64_Text_Writer *writer, int64_t number) {
    if (number > -X64_TEXT_DECIMAL_LIMIT && number < X64_TEXT_DECIMAL_LIMIT) {
        x64_text_write_signed(writer, number);
    } else {
        x64_text_write_hexadecimal(writer, (uint64_t)number);
    }
}

static void x64_text_write_signed(X64_Text_Writer *writer, int64_t number) {
    if (number < 0) {
        x64_text_write(writer, STR("-"));
        // The magnitude is worked out as unsigned, because negating the lowest `int64_t` overflows.
        x64_text_write_unsigned(writer, (uint64_t)0 - (uint64_t)number);
    } else {
        x64_text_write_unsigned(writer, (uint64_t)number);
    }
}

static void x64_text_write_unsigned(X64_Text_Writer *writer, uint64_t number) {
    char digits[X64_TEXT_DECIMAL_DIGITS_MAX];
    size_t count = 0;
    do {
        digits[X64_TEXT_DECIMAL_DIGITS_MAX - 1 - count] = (char)('0' + number % 10);
        number /= 10;
        count += 1;
    } while (number != 0);

    assert(count <= X64_TEXT_DECIMAL_DIGITS_MAX);
    x64_text_write(writer, str_make(digits + X64_TEXT_DECIMAL_DIGITS_MAX - count, count));
}

static void x64_text_write_hexadecimal(X64_Text_Writer *writer, uint64_t number) {
    static char const DIGITS[] = "0123456789abcdef";

    char text[X64_TEXT_HEXADECIMAL_DIGITS_MAX];
    size_t count = 0;
    do {
        text[X64_TEXT_HEXADECIMAL_DIGITS_MAX - 1 - count] = DIGITS[number & 0xF];
        number >>= 4;
        count += 1;
    } while (number != 0);

    assert(count <= X64_TEXT_HEXADECIMAL_DIGITS_MAX);
    x64_text_write(writer, STR("0x"));
    x64_text_write(writer, str_make(text + X64_TEXT_HEXADECIMAL_DIGITS_MAX - count, count));
}

static void x64_text_write(X64_Text_Writer *writer, Str text) {
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

static byte x64_text_read_byte(X64_Text_Cursor *cursor) {
    if (cursor->is_failed || cursor->position >= cursor->length) {
        cursor->is_failed = true;
        return 0;
    }
    byte content = cursor->bytes[cursor->position];
    cursor->position += 1;
    return content;
}

static int32_t x64_text_read_signed_32(X64_Text_Cursor *cursor) {
    uint32_t number = 0;
    for (size_t i = 0; i < 4; i += 1) {
        number |= (uint32_t)x64_text_read_byte(cursor) << (8 * i);
    }
    // The bytes are two's complement, so the sign is in the top bit. Going through the range below
    // avoids converting an unsigned number that does not fit into a signed one.
    if (number > INT32_MAX) {
        return (int32_t)((int64_t)number - ((int64_t)1 << 32));
    }
    return (int32_t)number;
}

static uint64_t x64_text_read_unsigned_64(X64_Text_Cursor *cursor) {
    uint64_t number = 0;
    for (size_t i = 0; i < 8; i += 1) {
        number |= (uint64_t)x64_text_read_byte(cursor) << (8 * i);
    }
    return number;
}
