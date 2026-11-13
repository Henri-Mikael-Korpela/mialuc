#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <mialu/semantic.h>
#include <mialu/version.h>
#include <mialu/constant.h>
#include <mialu/x64.h>

// The layout of the file follows the PE format, see
// https://learn.microsoft.com/en-us/windows/win32/debug/pe-format. A file has the headers, and then
// the sections: `.rdata` holds what the program reads, which is the table of imports and the
// strings, and `.text` holds the code. `.rdata` comes first, so that its address is known before
// any code is written, and the code can refer to a string or to an import without being patched.
enum {
    // The sections and the headers start at multiples of this in the file. It is the smallest value
    // that the format allows, which keeps the file small.
    X64_FILE_ALIGNMENT_BYTES = 0x200,
    // The sections start at multiples of this in memory, the size of a page.
    X64_SECTION_ALIGNMENT_BYTES = 0x1000,
    // The sections that every file has: `.rdata`, `.data` and `.text`, and the sections that a file
    // has when it is built with debug information, see `X64_DEBUG_SECTION_NAMES`.
    X64_SECTION_COUNT = 3,
    X64_DEBUG_SECTION_COUNT = 5,
    // The address in memory, relative to the image, of the first section.
    X64_RDATA_RVA = 0x1000,

    X64_DOS_HEADER_SIZE_BYTES = 0x40,
    // The signature "PE" with two zeros, and the header of the file.
    X64_FILE_HEADER_SIZE_BYTES = 24,
    X64_OPTIONAL_HEADER_SIZE_BYTES = 240,
    X64_SECTION_HEADER_SIZE_BYTES = 40,
    X64_IMPORT_DESCRIPTOR_SIZE_BYTES = 20,
    // An entry of the table of imports is the 64-bit address of the name of an import, and the
    // table ends with a zero entry.
    X64_IMPORT_ENTRY_SIZE_BYTES = 8,

    // The registers that the calling convention of Windows on x64 uses are numbered like the
    // processor does, and `r8` and `r9` need the extra bit of an instruction prefix.
    X64_REGISTER_RAX = 0,
    X64_REGISTER_RCX = 1,
    X64_REGISTER_RDX = 2,
    X64_REGISTER_R8 = 8,
    X64_REGISTER_R9 = 9,
    X64_REGISTER_R11 = 11,

    // `mov` from memory to a register, and from a register to memory.
    X64_OPCODE_LOAD = 0x8B,
    X64_OPCODE_STORE = 0x89,

    // The space that the caller of a procedure makes below the arguments for the callee to keep the
    // four registers of arguments in, whether it has that many or not.
    X64_SHADOW_SPACE_BYTES = 32,
    // The size of a value of the target, which is also the size of a slot of the stack.
    X64_SLOT_SIZE_BYTES = 8,
    // The processor pushes the address to return to, and the procedure pushes `rbp`, so the first
    // argument that was kept for it is this far above `rbp`.
    X64_ARGUMENT_HOME_OFFSET_BYTES = 16,

    // The temporary storage that `str_to_c_str` makes its strings in. It is a section of its own
    // that the system gives to the program zeroed and that is not in the file, so it costs no space
    // there. 64 KiB hold a great many strings of a procedure that has them, which are all given
    // back when it returns. A string that does not fit stops the program.
    X64_TEMPORARY_STORAGE_SIZE_BYTES = 64 * 1024,
    // The first 8 bytes of the section are the number of bytes of the storage that are in use, and
    // the storage follows. Strings are made at multiples of 8 so that they are aligned.
    X64_TEMPORARY_USED_SIZE_BYTES = 8,
    X64_TEMPORARY_ALIGNMENT_BYTES = 8,
    // A section of the debug information is not needed to run the program, so the system may drop
    // it: it is data that is initialized, read and discardable. The table of unwinding is read by
    // the system when it unwinds the stack, so it stays.
    X64_DEBUG_SECTION_CHARACTERISTICS = 0x42000040,
    X64_UNWIND_SECTION_CHARACTERISTICS = 0x40000040,
    // An entry of the table of unwinding: where a procedure starts and ends, and where its unwind
    // information is. The information of every procedure takes 8 bytes: 4 of header and two codes
    // of 2 bytes, see `x64_debug_unwind_write`.
    X64_UNWIND_ENTRY_SIZE_BYTES = 12,
    X64_UNWIND_INFO_SIZE_BYTES = 8,
    // The entry of the data directories that is the table of unwinding, which Windows calls the
    // exception table.
    X64_DIRECTORY_EXCEPTION = 3,
    // The address of the image when it is loaded, which the debug information has its addresses
    // relative to, see `X64_IMAGE_BASE`.
    X64_DEBUG_ADDRESS_SIZE_BYTES = 8,

    // The numbers of the DWARF format that the debug information uses, see
    // https://dwarfstd.org/doc/DWARF4.pdf. The registers are numbered as the System V ABI for x64
    // numbers them for DWARF, which gdb uses for Windows on x64 too.
    X64_DWARF_VERSION = 4,
    X64_DWARF_FRAME_VERSION = 1,
    X64_DWARF_LANGUAGE_C99 = 0x0C,
    X64_DWARF_REGISTER_RBP = 6,
    X64_DWARF_REGISTER_RSP = 7,
    X64_DWARF_REGISTER_RIP = 16,
    X64_DWARF_ENCODING_SIGNED = 0x05,
    X64_DWARF_ENCODING_UNSIGNED_CHAR = 0x08,
    X64_DWARF_OP_ADDR = 0x03,
    X64_DWARF_OP_FBREG = 0x91,
    X64_DWARF_OP_CALL_FRAME_CFA = 0x9C,
    X64_DWARF_LNS_COPY = 0x01,
    X64_DWARF_LNS_ADVANCE_PC = 0x02,
    X64_DWARF_LNS_ADVANCE_LINE = 0x03,
    X64_DWARF_LNS_SET_COLUMN = 0x05,
    X64_DWARF_LNE_END_SEQUENCE = 0x01,
    X64_DWARF_LNE_SET_ADDRESS = 0x02,
    X64_DWARF_CFA_ADVANCE_LOC4 = 0x04,
    X64_DWARF_CFA_DEF_CFA = 0x0C,
    X64_DWARF_CFA_DEF_CFA_REGISTER = 0x0D,
    X64_DWARF_CFA_DEF_CFA_OFFSET = 0x0E,
    X64_DWARF_CFA_OFFSET = 0x80,
    X64_DWARF_CFA_RESTORE = 0xC0,
    X64_DWARF_CFA_NOP = 0x00,
    // The abbreviations of `X64_DEBUG_ABBREVIATIONS`, which say what each entry of `.debug_info`
    // is.
    X64_DWARF_ABBREVIATION_COMPILE_UNIT = 1,
    X64_DWARF_ABBREVIATION_BASE_TYPE = 2,
    X64_DWARF_ABBREVIATION_POINTER_TYPE = 3,
    X64_DWARF_ABBREVIATION_TYPEDEF = 4,
    X64_DWARF_ABBREVIATION_SUBPROGRAM = 5,
    X64_DWARF_ABBREVIATION_SUBPROGRAM_VOID = 6,
    X64_DWARF_ABBREVIATION_PARAMETER = 7,
    X64_DWARF_ABBREVIATION_VARIABLE = 8,
    X64_DWARF_ABBREVIATION_ARRAY_TYPE = 9,
    X64_DWARF_ABBREVIATION_SUBRANGE_TYPE = 10,
    X64_DWARF_ABBREVIATION_START = 11,
    // The frame of a procedure, see `x64_procedure_write`: on entry the address to return to is at
    // `rsp`, so the frame of the caller, the CFA, starts 8 bytes above it. `push rbp` makes that 16,
    // and keeps `rbp` 16 bytes below the CFA, and from `mov rbp, rsp` on the CFA is `rbp + 16`. So
    // the arguments that the procedure keeps are at the CFA and above, and the variables are below
    // the 16 bytes of the address and of `rbp`.
    X64_DEBUG_CFA_ENTRY_OFFSET_BYTES = 8,
    X64_DEBUG_CFA_FRAME_OFFSET_BYTES = 16,
    // The start of the program makes 40 bytes of the stack, see `x64_code_write`.
    X64_DEBUG_START_FRAME_BYTES = 40,

    // The stack must stay a multiple of this, so that every call is made with it, see
    // `x64_procedure_write`. The frame of a procedure is rounded up to it.
    X64_STACK_ALIGNMENT_BYTES = 16,
    // The most variables that a procedure can have. Each takes a slot of the frame below `rbp`,
    // and the frame is made with one `sub rsp` of a 32-bit number, which this keeps far below.
    X64_VARIABLE_COUNT_MAX = 64 * 1024,
};

// What a constant of the module is, which decides what the build makes of it. Alphabetical, because
// nothing depends on the order.
typedef enum {
    // A procedure of a `foreign libc ->` block, which is imported and has no code.
    X64_CONSTANT_KIND_FOREIGN,
    // A procedure with a body, which has code.
    X64_CONSTANT_KIND_PROCEDURE,
    // A string, such as `greeting :: "Hello"`, whose bytes are in `.rdata` with the other string
    // literals. It has no code, and it is used where a `C_Str` is wanted, or as the argument of
    // `str_to_c_str`, like a string literal.
    X64_CONSTANT_KIND_STRING,
    // A number or a truth value, such as `LIMIT :: 100`. It has no code either: a use of it is its
    // value, like a literal.
    X64_CONSTANT_KIND_VALUE,
} X64_Constant_Kind;

// Alphabetical, because nothing depends on the order. `X64_TYPE_NONE` is the type of a call of a
// procedure that returns nothing, and of an expression that the target does not build.
typedef enum {
    X64_TYPE_C_STR,
    X64_TYPE_INT,
    X64_TYPE_NONE,
} X64_Type;

/**
 * The bytes of the file that is being built. A write past the end is not made, and sets
 * `is_overflowed`, which is checked once at the end, so that the code that writes need not check
 * every byte.
 */
typedef struct {
    byte *bytes;
    size_t capacity;
    size_t length;
    bool is_overflowed;
} X64_Writer;

/**
 * What the build knows about a constant of the module, kept at the position of the constant among
 * the constant definitions, which is what the analysis binds a name or a call to.
 */
typedef struct {
    Ast_Node_Index definition;
    // Where the code of the procedure starts, counted from the start of the code. Only set for a
    // procedure with a body, because nothing else has code.
    size_t offset;
    // The place of the import of a foreign procedure in the table of imports, see
    // `x64_layout_make`. Not set for any other constant.
    uint32_t import_slot;
    X64_Constant_Kind kind;
} X64_Constant;

/**
 * Where the parts of `.rdata` are, counted from its start, and how big the sections are. It is made
 * from the syntax tree alone, before any code is written.
 */
typedef struct {
    size_t import_table_offset;
    size_t lookup_table_offset;
    size_t address_table_offset;
    size_t names_offset;
    size_t library_name_offset;
    size_t strings_offset;
    size_t rdata_size_bytes;
    // The number of imports, which is one more than the number of foreign procedures, because the
    // first is `exit`, which the program ends with.
    size_t import_count;
} X64_Layout;

/**
 * Where a section of the debug information is in the file and in memory.
 */
typedef struct {
    size_t file_offset;
    size_t size_bytes;
    uint32_t rva;
} X64_Debug_Section;

/**
 * Where the reading of the source to count lines got to. The source is read forward once for all
 * the places that are in the order of the source, see `x64_debug_cursor_move`. `line` is counted
 * from 1, and `line_start` is the offset of its first byte.
 */
typedef struct {
    size_t offset;
    size_t line_start;
    uint32_t line;
} X64_Debug_Cursor;

/**
 * Where the entries of the types are in `.debug_info`, counted from the start of its unit, which
 * is how one entry refers to another.
 */
typedef struct {
    uint32_t byte_type;
    uint32_t c_str_type;
    uint32_t int_type;
} X64_Debug_Types;

/**
 * The state shared by the building functions. A building function returns false after it recorded
 * an error here, and its caller then returns false too without recording anything more. So only
 * the first error is kept. The code is written twice, see `x64_build`, so everything here that the
 * code changes is reset between the two.
 */
typedef struct {
    X64_Writer writer;
    Ast_Node const *nodes;
    Semantic_Binding const *bindings;
    // What is known about each node when the program is compiled, see `constant_evaluate`. A node
    // whose entry is `CONSTANT_STATUS_OK` is loaded as its value.
    Constant_Value const *constant_values;
    Str subject;
    // What the debug information needs, or NULL when the file has none.
    X64_Debug const *debug;
    X64_Debug_Row *rows;
    X64_Layout layout;
    size_t node_count;
    // The size of the headers, which is one or two units of file alignment, as the sections need.
    size_t headers_size_bytes;
    // The place in the file of the start of the code, and of the data that precedes it.
    size_t text_start;
    // The rows that the code has noted, and how many it can note, see `x64_debug_row_add`.
    size_t row_count;
    size_t row_capacity;
    // The place in the file of the table of strings, which has the names of the sections of the
    // debug information, which are longer than the 8 bytes of a name in a header.
    size_t string_table_offset;
    X64_Debug_Section debug_sections[X64_DEBUG_SECTION_COUNT];
    size_t constant_count;
    // The number of variables of the procedure that is being written, which says where the slot of
    // the temporary storage is in its frame, see `x64_frame_offset`.
    size_t variable_count;
    // The number of variables of that procedure whose definitions were written, which is the
    // position of the next one. The analysis gives a position to the uses of a variable and not to
    // its definition, and the statements are written in the order of the source, which is the order
    // of the positions, because the target builds no blocks, which have variables of their own.
    size_t variable_defined_count;
    uint32_t text_rva;
    // Where the section of the temporary storage is in memory, see
    // `X64_TEMPORARY_STORAGE_SIZE_BYTES`.
    uint32_t data_rva;
    Ast_Node_Index module;
    // The definition of the procedure that is being written, which a message about it names.
    Ast_Node_Index definition;
    X64_Build_Status status;
    // Whether the procedure that is being written makes temporary strings, which it gives back
    // when it returns.
    bool is_temporary_used;
    X64_Constant constants[X64_CONSTANT_COUNT_MAX];
} X64_Builder;

static bool         x64_arguments_write          (X64_Builder *builder, Ast_Node_Index call,
                                                  Ast_Node_Index callee);
static bool         x64_call_is_str_to_c_str     (X64_Builder const *builder,
                                                  Ast_Node_Index expression);
static bool         x64_call_write               (X64_Builder *builder, Ast_Node_Index call,
                                                  bool is_result_used);
static bool         x64_assignment_write         (X64_Builder *builder, Ast_Node_Index statement);
static uint32_t     x64_debug_address_rva        (X64_Builder const *builder, size_t code_offset);
static X64_Debug_Cursor x64_debug_cursor_move    (Str source, X64_Debug_Cursor cursor,
                                                  size_t offset);
static void         x64_debug_entry_close        (X64_Builder *builder, size_t entry_start);
static void         x64_debug_frame_write        (X64_Builder *builder, size_t text_size_bytes);
static void         x64_debug_info_write         (X64_Builder *builder, size_t text_size_bytes);
static void         x64_debug_line_write         (X64_Builder *builder, size_t text_size_bytes);
static void         x64_debug_location_write     (X64_Writer *writer, int64_t cfa_offset);
static size_t       x64_debug_procedure_end      (X64_Builder const *builder, size_t position,
                                                  size_t text_size_bytes);
static void         x64_debug_row_add            (X64_Builder *builder, X64_Debug_Row_Kind kind,
                                                  uint32_t source_offset);
static void         x64_debug_sections_write     (X64_Builder *builder, size_t text_size_bytes);
static void         x64_debug_unwind_write       (X64_Builder *builder, size_t text_size_bytes,
                                                  uint32_t section_rva);
static uint32_t     x64_debug_type_reference     (X64_Debug_Types types, X64_Type type);
static void         x64_patch_u32                (X64_Writer *writer, size_t offset,
                                                  uint32_t number);
static size_t       x64_string_offset            (X64_Builder const *builder,
                                                  Ast_Node_Index literal);
static void         x64_write_signed_leb128      (X64_Writer *writer, int64_t number);
static void         x64_write_string             (X64_Writer *writer, Str text);
static void         x64_write_unsigned_leb128    (X64_Writer *writer, uint64_t number);
static void         x64_epilogue_write           (X64_Builder *builder);
static int32_t      x64_frame_offset             (size_t position);
static size_t       x64_procedure_variable_count (X64_Builder const *builder,
                                                  Ast_Node_Index procedure);
static bool         x64_value_write              (X64_Builder *builder, Ast_Node_Index value,
                                                  X64_Type variable_type, Str name);
static bool         x64_variable_definition_write(X64_Builder *builder, Ast_Node_Index statement);
static X64_Type     x64_variable_type            (X64_Builder const *builder,
                                                  Ast_Node_Index definition);
static Ast_Node_Index x64_variable_type_node     (X64_Builder const *builder,
                                                  Ast_Node_Index definition);
static void         x64_write_frame_move         (X64_Builder *builder, byte opcode,
                                                  byte register_number, int32_t displacement);
static bool         x64_procedure_uses_temporary (X64_Builder const *builder,
                                                  Ast_Node_Index procedure);
static bool         x64_c_str_write              (X64_Builder *builder, Ast_Node_Index literal,
                                                  byte register_number);
static bool         x64_str_to_c_str_write       (X64_Builder *builder, Ast_Node_Index call,
                                                  byte register_number);
static Ast_Node_Index x64_string_literal_of      (X64_Builder const *builder,
                                                  Ast_Node_Index expression);
static void         x64_write_move_from_r10      (X64_Builder *builder, byte register_number);
static void         x64_write_rip_displacement   (X64_Builder *builder, uint64_t target_rva);
static bool         x64_code_write               (X64_Builder *builder);
static void         x64_dos_header_write         (byte *header);
static bool         x64_expression_load          (X64_Builder *builder, Ast_Node_Index expression,
                                                  byte register_number);
static X64_Type     x64_expression_type          (X64_Builder const *builder,
                                                  Ast_Node_Index expression);
static X64_Type     x64_expression_type_for      (X64_Builder const *builder,
                                                  Ast_Node_Index expression, X64_Type wanted);
static bool         x64_fail                     (X64_Builder *builder, X64_Build_Status status,
                                                  Str subject);
static bool         x64_fail_unsupported         (X64_Builder *builder);
static void         x64_headers_write            (X64_Builder *builder, size_t text_size_bytes);
static void         x64_image_write              (X64_Builder *builder);
static X64_Layout   x64_layout_make              (X64_Builder const *builder);
static bool         x64_main_check               (X64_Builder *builder);
static size_t       x64_align_up                 (size_t value, size_t alignment);
static size_t       x64_parameter_count          (X64_Builder const *builder,
                                                  Ast_Node_Index procedure);
static X64_Type     x64_parameter_type           (X64_Builder const *builder,
                                                  Ast_Node_Index procedure, size_t position);
static bool         x64_procedure_write          (X64_Builder *builder, size_t position);
static bool         x64_constants_collect       (X64_Builder *builder);
static void         x64_rdata_write              (X64_Builder *builder);
static X64_Type     x64_return_type              (Ast_Node const *procedure);
static bool         x64_return_write             (X64_Builder *builder, Ast_Node_Index procedure,
                                                  Ast_Node_Index statement);
static X64_Type     x64_type_of_name             (Str name);
static void         x64_write_byte               (X64_Writer *writer, byte content);
static void         x64_write_bytes              (X64_Writer *writer, byte const *content,
                                                  size_t count);
static void         x64_write_u16                (X64_Writer *writer, uint16_t number);
static void         x64_write_u32                (X64_Writer *writer, uint32_t number);
static void         x64_write_u64                (X64_Writer *writer, uint64_t number);
static void         x64_write_zeros              (X64_Writer *writer, size_t count);
static void         x64_write_call_import        (X64_Builder *builder, uint32_t import_slot);
static void         x64_write_call_local         (X64_Builder *builder, size_t target_offset);
static void         x64_write_load_integer       (X64_Builder *builder, byte register_number,
                                                  int64_t number);
static void         x64_write_load_slot          (X64_Builder *builder, byte register_number,
                                                  size_t position);
static void         x64_write_store_slot         (X64_Builder *builder, byte register_number,
                                                  size_t position);
static void         x64_store_u16                (byte *destination, uint16_t number);
static void         x64_store_u32                (byte *destination, uint32_t number);
static void         x64_store_u64                (byte *destination, uint64_t number);

// The registers that carry the arguments, in the order of the arguments.
static byte const X64_ARGUMENT_REGISTERS[X64_PARAMETER_COUNT_MAX] = {
    X64_REGISTER_RCX,
    X64_REGISTER_RDX,
    X64_REGISTER_R8,
    X64_REGISTER_R9,
};

// Where the program is loaded, if the system can. The code does not depend on it, because it only
// uses addresses relative to itself, so the file needs no relocations. It is the address that the
// linkers use for a program for x64, and it is far above the 4 GiB that a 32-bit program has.
static uint64_t const X64_IMAGE_BASE = 0x140000000ull;

// The library that the C library is imported from. It is part of every installation of Windows, and
// it exports `printf` and `exit` by name, unlike the universal C runtime that the headers of the
// compilers redirect to.
static char const X64_LIBRARY_NAME[] = "msvcrt.dll";

// The sections of a build with debug information, in the order of the file. The first is the table
// of unwinding that Windows describes for x64, and the others are the DWARF of gdb, which finds
// them by these names. A name that is longer than the 8 bytes of a header is in the table of
// strings.
static Str const X64_DEBUG_SECTION_NAMES[X64_DEBUG_SECTION_COUNT] = {
    {.bytes = ".pdata", .length = 6},
    {.bytes = ".debug_abbrev", .length = 13},
    {.bytes = ".debug_info", .length = 11},
    {.bytes = ".debug_line", .length = 11},
    {.bytes = ".debug_frame", .length = 12},
};
enum {
    X64_DEBUG_SECTION_UNWIND = 0,
    X64_DEBUG_SECTION_ABBREV = 1,
    X64_DEBUG_SECTION_INFO = 2,
    X64_DEBUG_SECTION_LINE = 3,
    X64_DEBUG_SECTION_FRAME = 4,
    // The longest name that a header has room for.
    X64_SECTION_NAME_SIZE_BYTES = 8,
};

// The abbreviations of `.debug_info`: the number of each, its tag, whether it has children, and
// the pairs of attribute and form of its entries, which end with two zeros. The list ends with a
// zero. The forms are the simplest that the values need: strings in place, numbers of fixed size,
// references of 4 bytes and expressions with their length before them.
static byte const X64_DEBUG_ABBREVIATIONS[] = {
    // The unit: producer, language, name, directory, low_pc, high_pc as a size, stmt_list.
    X64_DWARF_ABBREVIATION_COMPILE_UNIT, 0x11, 1,
    0x25, 0x08, 0x13, 0x05, 0x03, 0x08, 0x1B, 0x08, 0x11, 0x01, 0x12, 0x07, 0x10, 0x17, 0, 0,
    // A type of the processor: name, encoding, size.
    X64_DWARF_ABBREVIATION_BASE_TYPE, 0x24, 0,
    0x03, 0x08, 0x3E, 0x0B, 0x0B, 0x0B, 0, 0,
    // A pointer: size, type.
    X64_DWARF_ABBREVIATION_POINTER_TYPE, 0x0F, 0,
    0x0B, 0x0B, 0x49, 0x13, 0, 0,
    // Another name of a type: name, type.
    X64_DWARF_ABBREVIATION_TYPEDEF, 0x16, 0,
    0x03, 0x08, 0x49, 0x13, 0, 0,
    // A procedure with a result: name, file, line, low_pc, high_pc as a size, frame base,
    // external, type.
    X64_DWARF_ABBREVIATION_SUBPROGRAM, 0x2E, 1,
    0x03, 0x08, 0x3A, 0x0B, 0x3B, 0x06, 0x11, 0x01, 0x12, 0x07, 0x40, 0x18, 0x3F, 0x19,
    0x49, 0x13, 0, 0,
    // A procedure without a result: the same without the type.
    X64_DWARF_ABBREVIATION_SUBPROGRAM_VOID, 0x2E, 1,
    0x03, 0x08, 0x3A, 0x0B, 0x3B, 0x06, 0x11, 0x01, 0x12, 0x07, 0x40, 0x18, 0x3F, 0x19, 0, 0,
    // A parameter: name, file, line, type, location.
    X64_DWARF_ABBREVIATION_PARAMETER, 0x05, 0,
    0x03, 0x08, 0x3A, 0x0B, 0x3B, 0x06, 0x49, 0x13, 0x02, 0x18, 0, 0,
    // A variable, or a constant, which is a variable that the program does not change: name,
    // file, line, type, location.
    X64_DWARF_ABBREVIATION_VARIABLE, 0x34, 0,
    0x03, 0x08, 0x3A, 0x0B, 0x3B, 0x06, 0x49, 0x13, 0x02, 0x18, 0, 0,
    // An array, the bytes of a constant: type of the elements, and the range below it.
    X64_DWARF_ABBREVIATION_ARRAY_TYPE, 0x01, 1,
    0x49, 0x13, 0, 0,
    // The range of an array: the last index.
    X64_DWARF_ABBREVIATION_SUBRANGE_TYPE, 0x21, 0,
    0x2F, 0x06, 0, 0,
    // The start of the program, which is code of the compiler and has no source: name, low_pc,
    // high_pc as a size.
    X64_DWARF_ABBREVIATION_START, 0x2E, 0,
    0x03, 0x08, 0x11, 0x01, 0x12, 0x07, 0, 0,
    0,
};

// The padding of an entry of `.debug_frame` is zeros, see `x64_debug_entry_close`.
STATIC_ASSERT(X64_DWARF_CFA_NOP == 0, x64_dwarf_cfa_nop_is_zero);
static char const X64_EXIT_NAME[] = "exit";

X64_Build_Result x64_build(Array_Slice nodes, Array_Slice bindings, Array_Slice constants,
                           Ast_Node_Index root, Array_Slice output_buffer,
                           Array_Slice symbol_buffer, X64_Debug const *debug) {
    assert(nodes.elements != NULL);
    assert(bindings.elements != NULL);
    assert(constants.elements != NULL);
    assert(constants.length == nodes.length);
    assert(output_buffer.elements != NULL);
    assert(symbol_buffer.elements != NULL);
    // One symbol for each procedure fits, because the build has room for no more procedures.
    assert(symbol_buffer.length >= X64_CONSTANT_COUNT_MAX);
    assert(root < nodes.length);
    // The bindings are of this tree, one for each node.
    assert(bindings.length == nodes.length);

    Ast_Node const *node_elements = nodes.elements;
    assert(node_elements[root].node_type == AST_NODE_TYPE_MODULE);

    // The headers of the sections follow the optional header, and the headers take whole units of
    // file alignment. The debug information has four sections more, which need a second unit.
    size_t section_count = X64_SECTION_COUNT;
    X64_Debug_Row *rows = NULL;
    size_t row_capacity = 0;
    if (debug != NULL) {
        assert(debug->source.bytes != NULL);
        assert(debug->source_path.bytes != NULL);
        assert(debug->row_buffer.elements != NULL);
        assert(debug->row_buffer.length >= X64_DEBUG_ROWS_PER_NODE * nodes.length);
        section_count += X64_DEBUG_SECTION_COUNT;
        rows = debug->row_buffer.elements;
        row_capacity = debug->row_buffer.length;
    }
    size_t headers_size_bytes =
        x64_align_up(X64_DOS_HEADER_SIZE_BYTES + X64_FILE_HEADER_SIZE_BYTES +
                         X64_OPTIONAL_HEADER_SIZE_BYTES +
                         section_count * X64_SECTION_HEADER_SIZE_BYTES,
                     X64_FILE_ALIGNMENT_BYTES);
    // The headers are below the first section in memory.
    assert(headers_size_bytes <= X64_RDATA_RVA);

    X64_Builder builder = {
        .writer =
            {
                .bytes = output_buffer.elements,
                .capacity = output_buffer.length,
                .length = 0,
                .is_overflowed = false,
            },
        .nodes = node_elements,
        .bindings = bindings.elements,
        .constant_values = constants.elements,
        .subject = STR_EMPTY,
        .debug = debug,
        .rows = rows,
        .layout = {0},
        .node_count = nodes.length,
        .headers_size_bytes = headers_size_bytes,
        .text_start = 0,
        .row_count = 0,
        .row_capacity = row_capacity,
        .string_table_offset = 0,
        .debug_sections = {{0}},
        .constant_count = 0,
        .variable_count = 0,
        .variable_defined_count = 0,
        .text_rva = 0,
        .data_rva = 0,
        .module = root,
        .definition = AST_NODE_INDEX_NONE,
        .status = X64_BUILD_STATUS_OK,
        .is_temporary_used = false,
        .constants = {{0}},
    };

    bool is_ok = x64_constants_collect(&builder);
    if (is_ok) {
        is_ok = x64_main_check(&builder);
    }
    if (is_ok) {
        builder.layout = x64_layout_make(&builder);
        x64_image_write(&builder);

        // The code is written twice. A call of a procedure further down has the distance to it in
        // its instruction, which is not known before that procedure is written, but the instruction
        // is as long with any distance. So the first time finds where every procedure starts, and
        // the second time writes the code with the distances.
        is_ok = x64_code_write(&builder);
        if (is_ok) {
            builder.writer.length = builder.text_start;
            is_ok = x64_code_write(&builder);
        }
        if (is_ok) {
            size_t text_size_bytes = builder.writer.length - builder.text_start;
            // The file is padded to a whole unit of file alignment, as the format asks.
            x64_write_zeros(&builder.writer,
                            x64_align_up(text_size_bytes, X64_FILE_ALIGNMENT_BYTES) -
                                text_size_bytes);
            // The debug information is about the code, so it comes after it, and it is not needed
            // to run the program, so the code does not depend on where it is.
            if (debug != NULL) {
                x64_debug_sections_write(&builder, text_size_bytes);
            }
            x64_headers_write(&builder, text_size_bytes);
        }
    }

    // A real error is more useful than the overflow that may follow from it, so the overflow is
    // only reported when nothing else went wrong.
    if (is_ok && builder.writer.is_overflowed) {
        is_ok = x64_fail(&builder, X64_BUILD_STATUS_OUTPUT_TOO_LARGE, STR_EMPTY);
    }

    // Nothing was built when there was an error, so no bytes are handed out.
    size_t length_bytes = 0;
    size_t symbol_count = 0;
    if (is_ok) {
        length_bytes = builder.writer.length;

        X64_Symbol *symbol_elements = symbol_buffer.elements;
        for (size_t i = 0; i < builder.constant_count; i += 1) {
            if (builder.constants[i].kind != X64_CONSTANT_KIND_PROCEDURE) {
                continue;
            }
            symbol_elements[symbol_count] = (X64_Symbol){
                .name = builder.nodes[builder.constants[i].definition]
                            .payload.constant_definition.name,
                .offset = builder.constants[i].offset,
            };
            symbol_count += 1;
        }
    }

    assert(is_ok == (builder.status == X64_BUILD_STATUS_OK));
    assert(builder.writer.length <= output_buffer.length);
    return (X64_Build_Result){
        .bytes = array_slice_make(output_buffer.elements, length_bytes),
        .symbols = array_slice_make(symbol_buffer.elements, symbol_count),
        .subject = builder.subject,
        .status = builder.status,
    };
}

/**
 * Finds the constants of the module, and checks everything about them that the code does not
 * depend on the order for: that the module has nothing but procedures and strings, that the
 * procedures are for this target, and that their parameters and results have types that the
 * target has. Fills `builder->constants`, in the order of the definitions, which is the position
 * that the analysis binds a name or a call by. Returns false after it recorded an error.
 */
static bool x64_constants_collect(X64_Builder *builder) {
    assert(builder->constant_count == 0);

    // The first place of the table of imports is for `exit`.
    uint32_t import_slot = 1;
    Ast_Node_Index definition = builder->nodes[builder->module].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && definition != AST_NODE_INDEX_NONE; i += 1) {
        Ast_Node const *definition_node = &builder->nodes[definition];
        // An `import foreign` has no name, and says nothing that the target does not know from the
        // foreign procedures themselves, which each have their target.
        if (definition_node->node_type == AST_NODE_TYPE_FOREIGN_IMPORT) {
            definition = definition_node->next_sibling;
            continue;
        }

        Str name = ast_definition_name(definition_node);
        builder->definition = definition;
        if (definition_node->node_type != AST_NODE_TYPE_CONSTANT_DEFINITION) {
            return x64_fail(builder, X64_BUILD_STATUS_UNSUPPORTED, name);
        }

        Ast_Node_Index procedure = definition_node->first_child;
        if (builder->constant_count == X64_CONSTANT_COUNT_MAX) {
            return x64_fail(builder, X64_BUILD_STATUS_CONSTANT_COUNT_EXCEEDED, name);
        }
        // A value needs nothing more: its uses are its value, and the bytes of a string are written
        // with the other string literals.
        if (constant_definition_is_value(builder->nodes, definition)) {
            X64_Constant_Kind kind = X64_CONSTANT_KIND_VALUE;
            if (builder->constant_values[definition].string != AST_NODE_INDEX_NONE) {
                kind = X64_CONSTANT_KIND_STRING;
            }
            builder->constants[builder->constant_count] = (X64_Constant){
                .definition = definition,
                .offset = 0,
                .import_slot = 0,
                .kind = kind,
            };
            builder->constant_count += 1;
            definition = definition_node->next_sibling;
            continue;
        }
        if (builder->nodes[procedure].node_type != AST_NODE_TYPE_PROCEDURE) {
            // A struct, an enum or the member of one is not built.
            return x64_fail(builder, X64_BUILD_STATUS_UNSUPPORTED, name);
        }

        bool is_foreign = ast_procedure_is_foreign(builder->nodes, procedure);
        if (is_foreign &&
            ast_procedure_foreign_target(builder->nodes, procedure) != AST_FOREIGN_TARGET_LIBC) {
            return x64_fail(builder, X64_BUILD_STATUS_FOREIGN_TARGET_UNSUPPORTED, name);
        }

        size_t parameter_count = x64_parameter_count(builder, procedure);
        if (parameter_count > X64_PARAMETER_COUNT_MAX) {
            return x64_fail(builder, X64_BUILD_STATUS_PARAMETER_COUNT_UNSUPPORTED, name);
        }
        Ast_Node_Index parameter = builder->nodes[procedure].first_child;
        for (size_t j = 0; j < parameter_count; j += 1) {
            assert(parameter != AST_NODE_INDEX_NONE);

            Ast_Node const *type = &builder->nodes[builder->nodes[parameter].first_child];
            if (type->node_type != AST_NODE_TYPE_TYPE_NAME ||
                x64_type_of_name(type->payload.type_name.name) == X64_TYPE_NONE) {
                Str subject = builder->nodes[parameter].payload.parameter.name;
                if (type->node_type == AST_NODE_TYPE_TYPE_NAME) {
                    subject = type->payload.type_name.name;
                }
                return x64_fail(builder, X64_BUILD_STATUS_TYPE_UNSUPPORTED, subject);
            }
            parameter = builder->nodes[parameter].next_sibling;
        }

        Str return_type = ast_procedure_return_type(&builder->nodes[procedure]);
        if (return_type.bytes != NULL && x64_type_of_name(return_type) == X64_TYPE_NONE) {
            return x64_fail(builder, X64_BUILD_STATUS_TYPE_UNSUPPORTED, return_type);
        }

        builder->constants[builder->constant_count] = (X64_Constant){
            .definition = definition,
            .offset = 0,
            .import_slot = 0,
            .kind = X64_CONSTANT_KIND_PROCEDURE,
        };
        if (is_foreign) {
            builder->constants[builder->constant_count].kind = X64_CONSTANT_KIND_FOREIGN;
            builder->constants[builder->constant_count].import_slot = import_slot;
            import_slot += 1;
        }
        builder->constant_count += 1;
        definition = definition_node->next_sibling;
    }

    builder->definition = AST_NODE_INDEX_NONE;
    return true;
}

/**
 * Checks that the module has the procedure that the program starts with: `main :: proc() Int ->`,
 * which has a body. Returns false after it recorded an error.
 */
static bool x64_main_check(X64_Builder *builder) {
    for (size_t i = 0; i < builder->constant_count; i += 1) {
        Ast_Node_Index definition = builder->constants[i].definition;
        Str name = builder->nodes[definition].payload.constant_definition.name;
        if (str_equals(name, STR("main")) == false) {
            continue;
        }

        Ast_Node_Index procedure = builder->nodes[definition].first_child;
        if (builder->constants[i].kind != X64_CONSTANT_KIND_PROCEDURE) {
            return x64_fail(builder, X64_BUILD_STATUS_MAIN_INVALID, name);
        }
        if (x64_parameter_count(builder, procedure) != 0 ||
            x64_return_type(&builder->nodes[procedure]) != X64_TYPE_INT) {
            return x64_fail(builder, X64_BUILD_STATUS_MAIN_INVALID, name);
        }
        return true;
    }

    return x64_fail(builder, X64_BUILD_STATUS_MAIN_INVALID, STR_EMPTY);
}

/**
 * Works out where everything in `.rdata` is. First comes the table of imports of the library: its
 * descriptor and the one that ends the list, then the lookup table and the address table, which
 * start out the same, the loader fills the second with the addresses, and then the names. After
 * them come the name of the library, and the string literals.
 */
static X64_Layout x64_layout_make(X64_Builder const *builder) {
    X64_Layout layout = {0};

    layout.import_count = 1;
    for (size_t i = 0; i < builder->constant_count; i += 1) {
        if (builder->constants[i].kind == X64_CONSTANT_KIND_FOREIGN) {
            layout.import_count += 1;
        }
    }

    layout.import_table_offset = 0;
    layout.lookup_table_offset = 2 * X64_IMPORT_DESCRIPTOR_SIZE_BYTES;
    // Each table ends with a zero entry.
    layout.address_table_offset =
        layout.lookup_table_offset + (layout.import_count + 1) * X64_IMPORT_ENTRY_SIZE_BYTES;
    layout.names_offset =
        layout.address_table_offset + (layout.import_count + 1) * X64_IMPORT_ENTRY_SIZE_BYTES;

    // A name is preceded by a hint of two bytes, which the loader uses to search faster, and ends
    // with a zero. It starts at an even address, so the size is made even.
    size_t names_bytes = x64_align_up(2 + sizeof(X64_EXIT_NAME), 2);
    for (size_t i = 0; i < builder->constant_count; i += 1) {
        if (builder->constants[i].kind == X64_CONSTANT_KIND_FOREIGN) {
            Str name = builder->nodes[builder->constants[i].definition]
                           .payload.constant_definition.name;
            names_bytes += x64_align_up(2 + name.length + 1, 2);
        }
    }
    layout.library_name_offset = layout.names_offset + names_bytes;
    layout.strings_offset = layout.library_name_offset + sizeof(X64_LIBRARY_NAME);

    size_t strings_bytes = 0;
    for (size_t i = 0; i < builder->node_count; i += 1) {
        if (builder->nodes[i].node_type == AST_NODE_TYPE_STRING_LITERAL) {
            // The zero that ends the string for C comes after the bytes.
            strings_bytes += builder->nodes[i].payload.string_literal.text.length + 1;
        }
    }
    layout.rdata_size_bytes = layout.strings_offset + strings_bytes;
    return layout;
}

/**
 * Writes the part of the file that comes before the code, which is the space for the headers and
 * `.rdata`, and makes the writer ready for the code. The headers are written last, by
 * `x64_headers_write`, because they hold the size of the code.
 */
static void x64_image_write(X64_Builder *builder) {
    assert(builder->writer.length == 0);

    x64_write_zeros(&builder->writer, builder->headers_size_bytes);
    x64_rdata_write(builder);
    size_t rdata_end = builder->writer.length;
    x64_write_zeros(&builder->writer,
                    x64_align_up(rdata_end, X64_FILE_ALIGNMENT_BYTES) - rdata_end);

    builder->text_start = builder->writer.length;
    builder->data_rva = (uint32_t)(X64_RDATA_RVA + x64_align_up(builder->layout.rdata_size_bytes,
                                                                 X64_SECTION_ALIGNMENT_BYTES));
    builder->text_rva = (uint32_t)(builder->data_rva +
                                   x64_align_up(X64_TEMPORARY_USED_SIZE_BYTES +
                                                    X64_TEMPORARY_STORAGE_SIZE_BYTES,
                                                X64_SECTION_ALIGNMENT_BYTES));
}

/**
 * Writes `.rdata`. The sizes and the places are in `builder->layout`.
 */
static void x64_rdata_write(X64_Builder *builder) {
    X64_Layout const *layout = &builder->layout;
    X64_Writer *writer = &builder->writer;

    // The descriptor of the library: where its lookup table and its address table are, and its
    // name. The date and the forwarder are not used. A zero descriptor ends the list.
    x64_write_u32(writer, (uint32_t)(X64_RDATA_RVA + layout->lookup_table_offset));
    x64_write_u32(writer, 0);
    x64_write_u32(writer, 0);
    x64_write_u32(writer, (uint32_t)(X64_RDATA_RVA + layout->library_name_offset));
    x64_write_u32(writer, (uint32_t)(X64_RDATA_RVA + layout->address_table_offset));
    x64_write_zeros(writer, X64_IMPORT_DESCRIPTOR_SIZE_BYTES);

    // The two tables are the same until the program is loaded: the address of the name of each
    // import. The loader replaces the second with the addresses of the imports.
    for (size_t table = 0; table < 2; table += 1) {
        size_t name_offset = layout->names_offset;
        x64_write_u64(writer, X64_RDATA_RVA + name_offset);
        name_offset += x64_align_up(2 + sizeof(X64_EXIT_NAME), 2);
        for (size_t i = 0; i < builder->constant_count; i += 1) {
            if (builder->constants[i].kind == X64_CONSTANT_KIND_FOREIGN) {
                Str name = builder->nodes[builder->constants[i].definition]
                               .payload.constant_definition.name;
                x64_write_u64(writer, X64_RDATA_RVA + name_offset);
                name_offset += x64_align_up(2 + name.length + 1, 2);
            }
        }
        x64_write_u64(writer, 0);
    }

    x64_write_u16(writer, 0);
    x64_write_bytes(writer, (byte const *)X64_EXIT_NAME, sizeof(X64_EXIT_NAME));
    if ((2 + sizeof(X64_EXIT_NAME)) % 2 != 0) {
        x64_write_byte(writer, 0);
    }
    for (size_t i = 0; i < builder->constant_count; i += 1) {
        if (builder->constants[i].kind == X64_CONSTANT_KIND_FOREIGN) {
            Str name = builder->nodes[builder->constants[i].definition]
                           .payload.constant_definition.name;
            x64_write_u16(writer, 0);
            x64_write_bytes(writer, (byte const *)name.bytes, name.length);
            x64_write_byte(writer, 0);
            if ((2 + name.length + 1) % 2 != 0) {
                x64_write_byte(writer, 0);
            }
        }
    }

    x64_write_bytes(writer, (byte const *)X64_LIBRARY_NAME, sizeof(X64_LIBRARY_NAME));

    for (size_t i = 0; i < builder->node_count; i += 1) {
        if (builder->nodes[i].node_type == AST_NODE_TYPE_STRING_LITERAL) {
            Str text = builder->nodes[i].payload.string_literal.text;
            x64_write_bytes(writer, (byte const *)text.bytes, text.length);
            x64_write_byte(writer, 0);
        }
    }

    assert(writer->is_overflowed ||
           writer->length == builder->headers_size_bytes + layout->rdata_size_bytes);
}

/**
 * Writes the code: the few instructions that the program starts with, and then every procedure
 * that has a body, in the order of the definitions. Returns false after it recorded an error.
 */
static bool x64_code_write(X64_Builder *builder) {
    assert(builder->writer.length == builder->text_start);

    X64_Writer *writer = &builder->writer;
    // The rows are noted again with the code, so only the ones of the last write are kept.
    builder->row_count = 0;

    // The program starts here. The system enters it with the stack 8 bytes past a multiple of 16,
    // because it was entered like a procedure is, and a call must be made with a multiple of 16.
    // So 8 bytes make it one, and 32 more are the space that `main` expects to be able to keep its
    // arguments in, which is `sub rsp, 40`.
    static byte const START_PROLOGUE[] = {0x48, 0x83, 0xEC, 0x28};
    x64_write_bytes(writer, START_PROLOGUE, sizeof(START_PROLOGUE));

    size_t main_position = builder->constant_count;
    for (size_t i = 0; i < builder->constant_count; i += 1) {
        Str name = builder->nodes[builder->constants[i].definition]
                       .payload.constant_definition.name;
        if (str_equals(name, STR("main"))) {
            main_position = i;
        }
    }
    assert(main_position < builder->constant_count);
    x64_write_call_local(builder, builder->constants[main_position].offset);

    // The result of `main` is the exit code. `exit` ends the process after it flushed what the
    // program printed with the C library, which ending it directly would not do. It does not
    // return, so the breakpoint after it is never reached; it is there so that a stray jump here
    // stops instead of running into the next procedure.
    static byte const START_EXIT[] = {0x89, 0xC1};
    x64_write_bytes(writer, START_EXIT, sizeof(START_EXIT));
    x64_write_call_import(builder, 0);
    x64_write_byte(writer, 0xCC);

    for (size_t i = 0; i < builder->constant_count; i += 1) {
        if (builder->constants[i].kind != X64_CONSTANT_KIND_PROCEDURE) {
            continue;
        }
        builder->constants[i].offset = writer->length - builder->text_start;
        if (x64_procedure_write(builder, i) == false) {
            return false;
        }
    }

    return true;
}

/**
 * Writes the procedure at `position` of the module: it keeps `rbp` and sets it to the stack
 * pointer, so that the arguments, which are kept above it, and the variables, which are kept below
 * it, are found at the same distance from it whatever the procedure pushes. It copies the
 * registers that carry the arguments to the space that the caller made for them, and makes the
 * frame, see `x64_frame_offset`. Then come the statements, which are calls, definitions of
 * variables, assignments to them and `return`. Returns false after it recorded an error.
 */
static bool x64_procedure_write(X64_Builder *builder, size_t position) {
    assert(position < builder->constant_count);
    assert(builder->constants[position].kind == X64_CONSTANT_KIND_PROCEDURE);

    X64_Writer *writer = &builder->writer;
    Ast_Node_Index definition = builder->constants[position].definition;
    Ast_Node_Index procedure = builder->nodes[definition].first_child;
    builder->definition = definition;

    // The line of the procedure is the line of its name, which a debugger shows while the frame is
    // made, before the first statement.
    x64_debug_row_add(builder, X64_DEBUG_ROW_KIND_LINE, builder->nodes[definition].source_offset);

    // `push rbp` and `mov rbp, rsp`. The stack is a multiple of 16 after this, because the call
    // that entered the procedure made it 8 past one and `push rbp` is 8 more. Nothing is pushed
    // after, so it is also what every call made by the procedure is made with.
    static byte const PROLOGUE[] = {0x55, 0x48, 0x89, 0xE5};
    x64_write_bytes(writer, PROLOGUE, sizeof(PROLOGUE));

    size_t parameter_count = x64_parameter_count(builder, procedure);
    for (size_t i = 0; i < parameter_count; i += 1) {
        x64_write_store_slot(builder, X64_ARGUMENT_REGISTERS[i], i);
    }

    builder->is_temporary_used = x64_procedure_uses_temporary(builder, procedure);
    builder->variable_count = x64_procedure_variable_count(builder, procedure);
    builder->variable_defined_count = 0;
    if (builder->variable_count > X64_VARIABLE_COUNT_MAX) {
        return x64_fail_unsupported(builder);
    }

    // The frame has a slot for each variable, and one for the temporary storage when the procedure
    // uses it: `sub rsp, size`, rounded up so that the stack is still a multiple of 16. The
    // variables are not set here, because each gets its value where it is defined, and nothing can
    // read it before.
    size_t slot_count = builder->variable_count;
    if (builder->is_temporary_used) {
        slot_count += 1;
    }
    if (slot_count > 0) {
        size_t frame_size_bytes =
            x64_align_up(slot_count * X64_SLOT_SIZE_BYTES, X64_STACK_ALIGNMENT_BYTES);
        static byte const FRAME_MAKE[] = {0x48, 0x81, 0xEC};
        x64_write_bytes(writer, FRAME_MAKE, sizeof(FRAME_MAKE));
        x64_write_u32(writer, (uint32_t)frame_size_bytes);
    }

    // A procedure that makes temporary strings keeps how much of the storage was in use when it
    // started, so that it can give them all back when it ends: `mov rax, [used]` and
    // `mov [rbp - offset], rax`.
    if (builder->is_temporary_used) {
        static byte const USED_LOAD[] = {0x48, 0x8B, 0x05};
        x64_write_bytes(writer, USED_LOAD, sizeof(USED_LOAD));
        x64_write_rip_displacement(builder, builder->data_rva);
        x64_write_frame_move(builder, X64_OPCODE_STORE, X64_REGISTER_RAX,
                             x64_frame_offset(builder->variable_count));
    }

    // The statements follow the parameters among the children of the procedure.
    Ast_Node_Index statement = builder->nodes[procedure].first_child;
    for (size_t i = 0; i < parameter_count; i += 1) {
        statement = builder->nodes[statement].next_sibling;
    }

    bool has_result = x64_return_type(&builder->nodes[procedure]) != X64_TYPE_NONE;
    bool is_last_return = false;
    // Every statement is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && statement != AST_NODE_INDEX_NONE; i += 1) {
        Ast_Node const *statement_node = &builder->nodes[statement];
        is_last_return = false;
        x64_debug_row_add(builder, X64_DEBUG_ROW_KIND_LINE, statement_node->source_offset);
        if (statement_node->node_type == AST_NODE_TYPE_CALL_STATEMENT) {
            if (x64_call_write(builder, statement_node->first_child, false) == false) {
                return false;
            }
        } else if (statement_node->node_type == AST_NODE_TYPE_RETURN_STATEMENT) {
            if (x64_return_write(builder, procedure, statement) == false) {
                return false;
            }
            is_last_return = true;
        } else if (statement_node->node_type == AST_NODE_TYPE_VARIABLE_DEFINITION) {
            if (x64_variable_definition_write(builder, statement) == false) {
                return false;
            }
        } else if (statement_node->node_type == AST_NODE_TYPE_ASSIGNMENT) {
            if (x64_assignment_write(builder, statement) == false) {
                return false;
            }
        } else {
            return x64_fail_unsupported(builder);
        }
        statement = statement_node->next_sibling;
    }

    // `leave` and `ret` give the stack back and return. Falling off the end of a procedure that
    // has a result is a mistake of the program, so it stops with a breakpoint instead of returning
    // whatever is in `rax`.
    if (is_last_return == false) {
        if (has_result) {
            x64_write_byte(writer, 0xCC);
        } else {
            x64_epilogue_write(builder);
        }
    }
    return true;
}

/**
 * Writes the end of a procedure: the temporary strings that it made are given back when it made
 * any, with `mov r11, [rbp - offset]` and `mov [used], r11`, which leave `rax`, the result, alone.
 * Then `leave` and `ret` give the stack back and return.
 */
static void x64_epilogue_write(X64_Builder *builder) {
    if (builder->is_temporary_used) {
        x64_write_frame_move(builder, X64_OPCODE_LOAD, X64_REGISTER_R11,
                             x64_frame_offset(builder->variable_count));
        static byte const USED_STORE[] = {0x4C, 0x89, 0x1D};
        x64_write_bytes(&builder->writer, USED_STORE, sizeof(USED_STORE));
        x64_write_rip_displacement(builder, builder->data_rva);
    }
    x64_debug_row_add(builder, X64_DEBUG_ROW_KIND_LEAVE, 0);
    static byte const EPILOGUE[] = {0xC9, 0xC3};
    x64_write_bytes(&builder->writer, EPILOGUE, sizeof(EPILOGUE));
}

/**
 * Returns whether the procedure makes temporary strings, which is when it calls `str_to_c_str`.
 * The nodes of a procedure are the ones from its own up to the end of it.
 */
static bool x64_procedure_uses_temporary(X64_Builder const *builder, Ast_Node_Index procedure) {
    Ast_Node_Index end = ast_procedure_end(builder->nodes, builder->node_count, procedure);
    for (Ast_Node_Index node = procedure; node < end; node += 1) {
        if (x64_call_is_str_to_c_str(builder, node)) {
            return true;
        }
        // A string that is a `C_Str` is copied to the temporary storage, as `str_to_c_str` copies
        // it, and the target has no `Str` that a string could be instead.
        if (builder->constant_values[node].status == CONSTANT_STATUS_OK &&
            builder->constant_values[node].string != AST_NODE_INDEX_NONE) {
            return true;
        }
    }
    return false;
}

/**
 * Returns whether `expression` is a call of the built-in procedure `str_to_c_str`, which the
 * analysis leaves without a binding unless the program defines a procedure of that name.
 */
static bool x64_call_is_str_to_c_str(X64_Builder const *builder, Ast_Node_Index expression) {
    return builder->nodes[expression].node_type == AST_NODE_TYPE_CALL &&
           builder->bindings[expression].kind == SEMANTIC_BINDING_KIND_NONE &&
           str_equals(builder->nodes[expression].payload.call.name, STR("str_to_c_str"));
}

/**
 * Writes a `return` statement: the value goes to `rax`, which is where the caller looks for it, and
 * `leave` and `ret` give the stack back and return. Returns false after it recorded an error.
 */
static bool x64_return_write(X64_Builder *builder, Ast_Node_Index procedure,
                             Ast_Node_Index statement) {
    assert(builder->nodes[statement].node_type == AST_NODE_TYPE_RETURN_STATEMENT);

    Str name = builder->nodes[builder->definition].payload.constant_definition.name;
    X64_Type return_type = x64_return_type(&builder->nodes[procedure]);
    if (return_type == X64_TYPE_NONE) {
        return x64_fail(builder, X64_BUILD_STATUS_RETURN_TYPE_MISSING, name);
    }

    Ast_Node_Index value = builder->nodes[statement].first_child;
    X64_Type value_type = x64_expression_type_for(builder, value, return_type);
    if (value_type == X64_TYPE_NONE && builder->nodes[value].node_type != AST_NODE_TYPE_CALL) {
        // An expression that the target does not build has no type.
        return x64_fail_unsupported(builder);
    }
    if (value_type != return_type) {
        // The call of a procedure that returns nothing is in here too.
        return x64_fail(builder, X64_BUILD_STATUS_RETURN_TYPE_MISMATCH, name);
    }
    // The temporary strings are given back by the return, so a pointer to one must not leave. A
    // string that is a `C_Str` is one, and a `C_Str` variable may hold one, so neither can be
    // returned either.
    if (x64_call_is_str_to_c_str(builder, value) ||
        x64_string_literal_of(builder, value) != AST_NODE_INDEX_NONE) {
        return x64_fail_unsupported(builder);
    }
    if (value_type == X64_TYPE_C_STR &&
        builder->bindings[value].kind == SEMANTIC_BINDING_KIND_VARIABLE) {
        return x64_fail_unsupported(builder);
    }
    if (x64_expression_load(builder, value, X64_REGISTER_RAX) == false) {
        return false;
    }

    x64_epilogue_write(builder);
    return true;
}

/**
 * Writes the definition of a variable, `name := value`, `name : Type = value` or `name : Type`,
 * which stores the value in the slot of the variable. Without a value the variable gets the
 * default value of its type, which is 0 for an `Int` and the address 0 for a `C_Str`. Returns false
 * after it recorded an error.
 */
static bool x64_variable_definition_write(X64_Builder *builder, Ast_Node_Index statement) {
    assert(builder->nodes[statement].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION);
    assert(builder->variable_defined_count < builder->variable_count);

    Str name = builder->nodes[statement].payload.variable_definition.name;
    Ast_Node_Index type = x64_variable_type_node(builder, statement);
    Ast_Node_Index value = builder->nodes[statement].first_child;
    if (type != AST_NODE_INDEX_NONE) {
        Ast_Node const *type_node = &builder->nodes[type];
        if (type_node->node_type != AST_NODE_TYPE_TYPE_NAME) {
            return x64_fail(builder, X64_BUILD_STATUS_TYPE_UNSUPPORTED, name);
        }
        if (x64_type_of_name(type_node->payload.type_name.name) == X64_TYPE_NONE) {
            return x64_fail(builder, X64_BUILD_STATUS_TYPE_UNSUPPORTED,
                            type_node->payload.type_name.name);
        }
        value = type_node->next_sibling;
    }

    if (value == AST_NODE_INDEX_NONE) {
        x64_write_load_integer(builder, X64_REGISTER_RAX, 0);
    } else {
        // A variable without a type takes the type of its value, so the value is not checked.
        X64_Type variable_type = X64_TYPE_NONE;
        if (type != AST_NODE_INDEX_NONE) {
            variable_type = x64_variable_type(builder, statement);
        }
        if (x64_value_write(builder, value, variable_type, name) == false) {
            return false;
        }
    }

    x64_write_frame_move(builder, X64_OPCODE_STORE, X64_REGISTER_RAX,
                         x64_frame_offset(builder->variable_defined_count));
    builder->variable_defined_count += 1;
    return true;
}

/**
 * Writes an assignment, `name = value`, which stores the value in the slot of the variable. Only a
 * variable of the procedure is assigned to: a global variable, a field, an element and what a
 * pointer points to are not built, and neither are the assignments such as `x += 1`, which need
 * arithmetic. Returns false after it recorded an error.
 */
static bool x64_assignment_write(X64_Builder *builder, Ast_Node_Index statement) {
    assert(builder->nodes[statement].node_type == AST_NODE_TYPE_ASSIGNMENT);

    Ast_Node_Index target = builder->nodes[statement].first_child;
    Ast_Node_Index value = builder->nodes[target].next_sibling;
    Semantic_Binding binding = builder->bindings[target];
    if (builder->nodes[target].node_type != AST_NODE_TYPE_IDENTIFIER ||
        binding.kind != SEMANTIC_BINDING_KIND_VARIABLE) {
        return x64_fail_unsupported(builder);
    }
    assert(binding.position < builder->variable_defined_count);

    // The definition of the variable is above, and was checked when it was written.
    X64_Type variable_type = x64_variable_type(builder, binding.target);
    assert(variable_type != X64_TYPE_NONE);
    Str name = builder->nodes[target].payload.identifier.name;
    if (x64_value_write(builder, value, variable_type, name) == false) {
        return false;
    }
    x64_write_frame_move(builder, X64_OPCODE_STORE, X64_REGISTER_RAX,
                         x64_frame_offset(binding.position));
    return true;
}

/**
 * Writes the instructions that put `value` in `rax`, after it checked that it is of the type of
 * the variable `name` that it is given to, which is `variable_type`, or `X64_TYPE_NONE` when the
 * variable has no type of its own and takes the type of the value. Returns false after it
 * recorded an error.
 */
static bool x64_value_write(X64_Builder *builder, Ast_Node_Index value, X64_Type variable_type,
                            Str name) {
    X64_Type value_type = x64_expression_type_for(builder, value, variable_type);
    if (value_type == X64_TYPE_NONE && builder->nodes[value].node_type != AST_NODE_TYPE_CALL) {
        // An expression that the target does not build has no type.
        return x64_fail_unsupported(builder);
    }
    // The call of a procedure that returns nothing has no type either. When the variable takes the
    // type of the value, `x64_call_write` reports that the call has no result.
    if (variable_type != X64_TYPE_NONE && value_type != variable_type) {
        return x64_fail(builder, X64_BUILD_STATUS_VARIABLE_TYPE_MISMATCH, name);
    }
    return x64_expression_load(builder, value, X64_REGISTER_RAX);
}

/**
 * Returns the number of variables of `procedure`, which the analysis gives the positions of in the
 * order of the source, see `Semantic_Binding`. The iterator of a for statement counts as one, but
 * the target does not build those, so it only takes a slot that is never used.
 */
static size_t x64_procedure_variable_count(X64_Builder const *builder, Ast_Node_Index procedure) {
    assert(builder->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);

    size_t count = 0;
    Ast_Node_Index end = ast_procedure_end(builder->nodes, builder->node_count, procedure);
    for (Ast_Node_Index node = procedure; node < end; node += 1) {
        Ast_Node_Type node_type = builder->nodes[node].node_type;
        if (node_type == AST_NODE_TYPE_VARIABLE_DEFINITION ||
            node_type == AST_NODE_TYPE_FOR_STATEMENT) {
            count += 1;
        }
    }
    return count;
}

/**
 * Returns the type that is written in the definition of a variable, which is its first child, or
 * `AST_NODE_INDEX_NONE` when the variable takes the type of its value.
 */
static Ast_Node_Index x64_variable_type_node(X64_Builder const *builder,
                                             Ast_Node_Index definition) {
    assert(builder->nodes[definition].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION);

    Ast_Node_Index first = builder->nodes[definition].first_child;
    assert(first != AST_NODE_INDEX_NONE);
    Ast_Node_Type node_type = builder->nodes[first].node_type;
    if (node_type == AST_NODE_TYPE_TYPE_NAME || node_type == AST_NODE_TYPE_POINTER_TYPE ||
        node_type == AST_NODE_TYPE_ARRAY_TYPE || node_type == AST_NODE_TYPE_PROCEDURE_TYPE) {
        return first;
    }
    return AST_NODE_INDEX_NONE;
}

/**
 * Returns the type of the variable that `definition` defines: the type that is written, or the type
 * of its value. A value that is another variable without a type is followed to the value of that
 * one, and so on, so that the type is found without recursion. Returns `X64_TYPE_NONE` for a type
 * that the target does not have, and for a value that it does not build.
 */
static X64_Type x64_variable_type(X64_Builder const *builder, Ast_Node_Index definition) {
    Ast_Node_Index current = definition;
    // Each step goes to a variable that is defined above, so there cannot be more than there are
    // nodes.
    for (size_t i = 0; i < builder->node_count; i += 1) {
        assert(builder->nodes[current].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION);

        Ast_Node_Index type = x64_variable_type_node(builder, current);
        if (type != AST_NODE_INDEX_NONE) {
            if (builder->nodes[type].node_type != AST_NODE_TYPE_TYPE_NAME) {
                return X64_TYPE_NONE;
            }
            return x64_type_of_name(builder->nodes[type].payload.type_name.name);
        }

        Ast_Node_Index value = builder->nodes[current].first_child;
        if (builder->nodes[value].node_type == AST_NODE_TYPE_IDENTIFIER &&
            builder->bindings[value].kind == SEMANTIC_BINDING_KIND_VARIABLE) {
            current = builder->bindings[value].target;
            continue;
        }
        // The value is not a variable, so this does not come back here.
        return x64_expression_type(builder, value);
    }

    assert(false);
    return X64_TYPE_NONE;
}

/**
 * Returns the distance from `rbp` of the slot at `position` of the frame: the variables have the
 * slots in the order of their positions, see `x64_procedure_variable_count`, and the one after the
 * last variable keeps how much of the temporary storage was in use, see `x64_procedure_write`. The
 * frame is below `rbp`, so the distance is negative.
 */
static int32_t x64_frame_offset(size_t position) {
    assert(position <= X64_VARIABLE_COUNT_MAX);

    return -(int32_t)((position + 1) * X64_SLOT_SIZE_BYTES);
}

/**
 * Writes a call: the arguments go to the registers, the space for the callee to keep them in is
 * made, and the procedure is called, either at its place in the code or through its place in the
 * table of imports. `is_result_used` is set when the call is a value, which needs a result. The
 * result is in `rax` afterwards. Returns false after it recorded an error.
 */
static bool x64_call_write(X64_Builder *builder, Ast_Node_Index call, bool is_result_used) {
    assert(builder->nodes[call].node_type == AST_NODE_TYPE_CALL);

    Str name = builder->nodes[call].payload.call.name;
    Semantic_Binding binding = builder->bindings[call];
    // A call through a variable that holds a pointer to a procedure is not built, and neither is a
    // built-in procedure as a statement: `str_to_c_str` is a value, see `x64_expression_load`.
    if (binding.kind != SEMANTIC_BINDING_KIND_CONSTANT) {
        return x64_fail_unsupported(builder);
    }
    assert(binding.position < builder->constant_count);
    assert(builder->constants[binding.position].definition == binding.target);

    X64_Constant const *callee = &builder->constants[binding.position];
    Ast_Node_Index procedure = builder->nodes[callee->definition].first_child;
    if (x64_arguments_write(builder, call, procedure) == false) {
        return false;
    }
    if (is_result_used && x64_return_type(&builder->nodes[procedure]) == X64_TYPE_NONE) {
        return x64_fail(builder, X64_BUILD_STATUS_CALL_RESULT_MISSING, name);
    }

    // `sub rsp, 32` makes the space that the callee can keep the four registers in, and
    // `add rsp, 32` gives it back. The stack is a multiple of 16 before it, see
    // `x64_procedure_write`, and 32 keeps it so.
    static byte const SHADOW_SPACE_MAKE[] = {0x48, 0x83, 0xEC, X64_SHADOW_SPACE_BYTES};
    static byte const SHADOW_SPACE_FREE[] = {0x48, 0x83, 0xC4, X64_SHADOW_SPACE_BYTES};
    x64_write_bytes(&builder->writer, SHADOW_SPACE_MAKE, sizeof(SHADOW_SPACE_MAKE));
    // The analysis does not bind a call to a value, such as a string, which is not a procedure.
    assert(callee->kind != X64_CONSTANT_KIND_STRING);
    assert(callee->kind != X64_CONSTANT_KIND_VALUE);
    if (callee->kind == X64_CONSTANT_KIND_FOREIGN) {
        x64_write_call_import(builder, callee->import_slot);
    } else {
        x64_write_call_local(builder, callee->offset);
    }
    x64_write_bytes(&builder->writer, SHADOW_SPACE_FREE, sizeof(SHADOW_SPACE_FREE));
    return true;
}

/**
 * Checks the arguments of `call` against the parameters of `callee`, the procedure that it calls,
 * and writes the instructions that put each in its register. An argument is a literal or a
 * parameter, which need no register but their own, so that none is overwritten while the others
 * are written. Returns false after it recorded an error.
 */
static bool x64_arguments_write(X64_Builder *builder, Ast_Node_Index call,
                                Ast_Node_Index callee) {
    Str name = builder->nodes[call].payload.call.name;

    size_t parameter_count = x64_parameter_count(builder, callee);
    Ast_Node_Index argument = builder->nodes[call].first_child;
    for (size_t i = 0; i < parameter_count; i += 1) {
        if (argument == AST_NODE_INDEX_NONE) {
            return x64_fail(builder, X64_BUILD_STATUS_CALL_ARGUMENT_COUNT_MISMATCH, name);
        }
        // A call is not an argument yet, except `str_to_c_str`, which only uses registers that no
        // argument is in: the result of another call would have to be kept while the others are
        // written, and its own call would overwrite the registers of the ones before it.
        if (builder->nodes[argument].node_type == AST_NODE_TYPE_CALL &&
            x64_call_is_str_to_c_str(builder, argument) == false) {
            return x64_fail_unsupported(builder);
        }
        X64_Type parameter_type = x64_parameter_type(builder, callee, i);
        X64_Type type = x64_expression_type_for(builder, argument, parameter_type);
        if (type == X64_TYPE_NONE) {
            return x64_fail_unsupported(builder);
        }
        if (type != parameter_type) {
            return x64_fail(builder, X64_BUILD_STATUS_CALL_ARGUMENT_TYPE_MISMATCH, name);
        }
        if (x64_expression_load(builder, argument, X64_ARGUMENT_REGISTERS[i]) == false) {
            return false;
        }
        argument = builder->nodes[argument].next_sibling;
    }

    if (argument != AST_NODE_INDEX_NONE) {
        return x64_fail(builder, X64_BUILD_STATUS_CALL_ARGUMENT_COUNT_MISMATCH, name);
    }
    return true;
}

/**
 * Writes the instruction that puts the value of `expression` in the register `register_number`.
 * The expression is a constant, a parameter, a variable, `str_to_c_str` or a call, whose result is
 * in `rax`, so a call is only loaded to `rax`. A constant that is a number is its value, whatever
 * operators and constants it was worked out from, and a string is a `C_Str`, because the type of
 * the place was checked, and the target has no `Str`. Returns false after it recorded an error.
 */
static bool x64_expression_load(X64_Builder *builder, Ast_Node_Index expression,
                                byte register_number) {
    Ast_Node const *node = &builder->nodes[expression];
    Constant_Value constant = builder->constant_values[expression];
    if (constant.status == CONSTANT_STATUS_OK && constant.string != AST_NODE_INDEX_NONE) {
        return x64_c_str_write(builder, constant.string, register_number);
    }
    if (constant.status == CONSTANT_STATUS_OK) {
        x64_write_load_integer(builder, register_number, constant.integer);
        return true;
    }
    if (node->node_type == AST_NODE_TYPE_IDENTIFIER &&
        builder->bindings[expression].kind == SEMANTIC_BINDING_KIND_PARAMETER) {
        x64_write_load_slot(builder, register_number, builder->bindings[expression].position);
        return true;
    }
    if (node->node_type == AST_NODE_TYPE_IDENTIFIER &&
        builder->bindings[expression].kind == SEMANTIC_BINDING_KIND_VARIABLE) {
        assert(builder->bindings[expression].position < builder->variable_defined_count);
        x64_write_frame_move(builder, X64_OPCODE_LOAD, register_number,
                             x64_frame_offset(builder->bindings[expression].position));
        return true;
    }
    if (x64_call_is_str_to_c_str(builder, expression)) {
        return x64_str_to_c_str_write(builder, expression, register_number);
    }
    if (node->node_type == AST_NODE_TYPE_CALL && register_number == X64_REGISTER_RAX) {
        return x64_call_write(builder, expression, true);
    }

    return x64_fail_unsupported(builder);
}

/**
 * Returns the type that `expression` has where a value of `wanted` is wanted, or `X64_TYPE_NONE` for
 * no type in particular. It is the type of the expression, except that an untyped string is a
 * `C_Str` where one is wanted: the address of a copy of its bytes with a zero after them. The
 * caller compares the type with `wanted`, so an untyped constant that does not become it keeps the
 * type that it has where nothing else says.
 */
static X64_Type x64_expression_type_for(X64_Builder const *builder, Ast_Node_Index expression,
                                        X64_Type wanted) {
    Constant_Value constant = builder->constant_values[expression];
    if (constant.status == CONSTANT_STATUS_OK && constant.type == CONSTANT_TYPE_UNTYPED_STRING &&
        wanted == X64_TYPE_C_STR) {
        return X64_TYPE_C_STR;
    }
    return x64_expression_type(builder, expression);
}

/**
 * Returns the type of `expression`, or `X64_TYPE_NONE` if it has none, which is the case for a
 * call of a procedure that returns nothing and for everything that the target does not build. A
 * constant has the type that it has where nothing else says, see `constant_type_default`, which is
 * `Int` for an untyped integer, and the target has no `Byte`, `Bool` or `Str`.
 */
static X64_Type x64_expression_type(X64_Builder const *builder, Ast_Node_Index expression) {
    Ast_Node const *node = &builder->nodes[expression];
    Constant_Value constant = builder->constant_values[expression];
    if (constant.status == CONSTANT_STATUS_OK) {
        if (constant_type_default(constant.type) == CONSTANT_TYPE_INT) {
            return X64_TYPE_INT;
        }
        return X64_TYPE_NONE;
    }
    if (node->node_type == AST_NODE_TYPE_IDENTIFIER &&
        builder->bindings[expression].kind == SEMANTIC_BINDING_KIND_PARAMETER) {
        Ast_Node_Index parameter = builder->bindings[expression].target;
        return x64_type_of_name(
            builder->nodes[builder->nodes[parameter].first_child].payload.type_name.name);
    }
    if (node->node_type == AST_NODE_TYPE_IDENTIFIER &&
        builder->bindings[expression].kind == SEMANTIC_BINDING_KIND_VARIABLE) {
        return x64_variable_type(builder, builder->bindings[expression].target);
    }
    if (x64_call_is_str_to_c_str(builder, expression)) {
        return X64_TYPE_C_STR;
    }
    if (node->node_type == AST_NODE_TYPE_CALL &&
        builder->bindings[expression].kind == SEMANTIC_BINDING_KIND_CONSTANT) {
        Ast_Node_Index callee = builder->nodes[builder->bindings[expression].target].first_child;
        return x64_return_type(&builder->nodes[callee]);
    }

    return X64_TYPE_NONE;
}

/**
 * Returns the type that a procedure returns, or `X64_TYPE_NONE` when it returns nothing.
 * `procedure` is an `AST_NODE_TYPE_PROCEDURE`, which `x64_constants_collect` checked.
 */
static X64_Type x64_return_type(Ast_Node const *procedure) {
    assert(procedure->node_type == AST_NODE_TYPE_PROCEDURE);

    Str name = ast_procedure_return_type(procedure);
    if (name.bytes == NULL) {
        return X64_TYPE_NONE;
    }
    return x64_type_of_name(name);
}

/**
 * Returns the type that the name `name` stands for, or `X64_TYPE_NONE` if the target does not have
 * a type of that name.
 */
static X64_Type x64_type_of_name(Str name) {
    if (str_equals(name, STR("Int"))) {
        return X64_TYPE_INT;
    }
    if (str_equals(name, STR("C_Str"))) {
        return X64_TYPE_C_STR;
    }
    return X64_TYPE_NONE;
}

/**
 * Returns the number of parameters of `procedure`, which are its first children.
 */
static size_t x64_parameter_count(X64_Builder const *builder, Ast_Node_Index procedure) {
    assert(builder->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);

    size_t count = 0;
    Ast_Node_Index child = builder->nodes[procedure].first_child;
    // Every child is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && child != AST_NODE_INDEX_NONE; i += 1) {
        if (builder->nodes[child].node_type != AST_NODE_TYPE_PARAMETER) {
            break;
        }
        count += 1;
        child = builder->nodes[child].next_sibling;
    }

    return count;
}

/**
 * Returns the type of the parameter at `position` of `procedure`, which the collecting of the
 * procedures checked is one that the target has.
 */
static X64_Type x64_parameter_type(X64_Builder const *builder, Ast_Node_Index procedure,
                                   size_t position) {
    assert(position < x64_parameter_count(builder, procedure));

    Ast_Node_Index parameter = builder->nodes[procedure].first_child;
    for (size_t i = 0; i < position; i += 1) {
        parameter = builder->nodes[parameter].next_sibling;
    }

    return x64_type_of_name(
        builder->nodes[builder->nodes[parameter].first_child].payload.type_name.name);
}

/**
 * Writes `str_to_c_str("text")`, which makes a copy of the string with the zero that ends it in the
 * temporary storage, and puts its address in `register_number`. The storage is a stack of strings:
 * the number of bytes in use says where the next goes, and it grows by the size of the string,
 * rounded up to 8. The procedure that made the string gives it back when it returns, see
 * `x64_epilogue_write`. The argument must be a string literal or a constant that is a string,
 * because the target does not have `Str` values, so the string and its size are known when the
 * program is built, and the copy is a
 * few stores of 8 bytes instead of a loop. Only `rax`, `r10` and `r11` are used, which no argument
 * is in. A string that does not fit in what is left stops the program with a breakpoint. Returns
 * false after it recorded an error.
 */
static bool x64_str_to_c_str_write(X64_Builder *builder, Ast_Node_Index call,
                                   byte register_number) {
    assert(x64_call_is_str_to_c_str(builder, call));

    Str name = builder->nodes[call].payload.call.name;
    Ast_Node_Index argument = builder->nodes[call].first_child;
    if (argument == AST_NODE_INDEX_NONE ||
        builder->nodes[argument].next_sibling != AST_NODE_INDEX_NONE) {
        return x64_fail(builder, X64_BUILD_STATUS_CALL_ARGUMENT_COUNT_MISMATCH, name);
    }
    Ast_Node_Index literal = x64_string_literal_of(builder, argument);
    if (literal == AST_NODE_INDEX_NONE) {
        return x64_fail_unsupported(builder);
    }
    return x64_c_str_write(builder, literal, register_number);
}

/**
 * Writes the copy of the string literal `literal` as a `C_Str`, which is what `str_to_c_str` of it
 * and an untyped string where a `C_Str` is wanted both are, see `x64_str_to_c_str_write`, and puts
 * its address in `register_number`. Returns false after it recorded an error.
 */
static bool x64_c_str_write(X64_Builder *builder, Ast_Node_Index literal, byte register_number) {
    assert(builder->nodes[literal].node_type == AST_NODE_TYPE_STRING_LITERAL);
    assert(register_number < 16);

    Str text = builder->nodes[literal].payload.string_literal.text;
    size_t size_bytes = x64_align_up(text.length + 1, X64_TEMPORARY_ALIGNMENT_BYTES);
    if (size_bytes > X64_TEMPORARY_STORAGE_SIZE_BYTES) {
        return x64_fail_unsupported(builder);
    }
    X64_Writer *writer = &builder->writer;

    // `mov rax, [used]`, then `cmp rax, storage - size` and `jbe` over a breakpoint: the string
    // fits when what is in use is at most that.
    static byte const USED_LOAD[] = {0x48, 0x8B, 0x05};
    x64_write_bytes(writer, USED_LOAD, sizeof(USED_LOAD));
    x64_write_rip_displacement(builder, builder->data_rva);
    x64_write_byte(writer, 0x48);
    x64_write_byte(writer, 0x3D);
    x64_write_u32(writer, (uint32_t)(X64_TEMPORARY_STORAGE_SIZE_BYTES - size_bytes));
    static byte const FIT_CHECK[] = {0x76, 0x01, 0xCC};
    x64_write_bytes(writer, FIT_CHECK, sizeof(FIT_CHECK));

    // `lea r10, [storage]` and `add r10, rax` make the address of the string.
    static byte const STORAGE_LOAD[] = {0x4C, 0x8D, 0x15};
    x64_write_bytes(writer, STORAGE_LOAD, sizeof(STORAGE_LOAD));
    x64_write_rip_displacement(builder, builder->data_rva + X64_TEMPORARY_USED_SIZE_BYTES);
    static byte const ADDRESS_MAKE[] = {0x49, 0x01, 0xC2};
    x64_write_bytes(writer, ADDRESS_MAKE, sizeof(ADDRESS_MAKE));

    // The bytes, and the zero after them, 8 at a time: `mov r11, chunk` and `mov [r10 + k], r11`.
    for (size_t offset = 0; offset < size_bytes; offset += X64_TEMPORARY_ALIGNMENT_BYTES) {
        uint64_t chunk = 0;
        for (size_t i = 0; i < X64_TEMPORARY_ALIGNMENT_BYTES; i += 1) {
            if (offset + i < text.length) {
                chunk |= (uint64_t)(byte)text.bytes[offset + i] << (8 * i);
            }
        }
        x64_write_byte(writer, 0x49);
        x64_write_byte(writer, 0xBB);
        x64_write_u64(writer, chunk);
        static byte const CHUNK_STORE[] = {0x4D, 0x89, 0x9A};
        x64_write_bytes(writer, CHUNK_STORE, sizeof(CHUNK_STORE));
        x64_write_u32(writer, (uint32_t)offset);
    }

    // `lea r11, [rax + size]` and `mov [used], r11` take the string into use.
    static byte const USED_GROW[] = {0x4C, 0x8D, 0x98};
    x64_write_bytes(writer, USED_GROW, sizeof(USED_GROW));
    x64_write_u32(writer, (uint32_t)size_bytes);
    static byte const USED_STORE[] = {0x4C, 0x89, 0x1D};
    x64_write_bytes(writer, USED_STORE, sizeof(USED_STORE));
    x64_write_rip_displacement(builder, builder->data_rva);

    x64_write_move_from_r10(builder, register_number);
    return true;
}

/**
 * Returns the string literal that `expression` is, or that it stands for when it is a constant that
 * is a string, such as `greeting` of `greeting :: "Hello"`. Returns `AST_NODE_INDEX_NONE` for
 * anything else.
 */
static Ast_Node_Index x64_string_literal_of(X64_Builder const *builder,
                                            Ast_Node_Index expression) {
    assert(expression < builder->node_count);

    Constant_Value constant = builder->constant_values[expression];
    if (constant.status != CONSTANT_STATUS_OK) {
        return AST_NODE_INDEX_NONE;
    }
    return constant.string;
}

/**
 * Writes `mov register, r10`.
 */
static void x64_write_move_from_r10(X64_Builder *builder, byte register_number) {
    assert(register_number < 16);

    // The prefix has the extension of the source register, `r10`, and of the destination if it is
    // one of `r8` to `r15`.
    x64_write_byte(&builder->writer, (byte)(0x4C | (register_number >> 3)));
    x64_write_byte(&builder->writer, 0x89);
    x64_write_byte(&builder->writer, (byte)(0xC0 | (2 << 3) | (register_number & 7)));
}

/**
 * Writes the 32-bit distance from the end of the instruction that is being written, which is where
 * this ends, to `target_rva`. It is for an instruction that has an address relative to itself as
 * its last part.
 */
static void x64_write_rip_displacement(X64_Builder *builder, uint64_t target_rva) {
    uint64_t end_rva = (uint64_t)builder->text_rva +
                       (builder->writer.length + 4 - builder->text_start);
    x64_write_u32(&builder->writer, (uint32_t)(target_rva - end_rva));
}

/**
 * Records that the program has something the target does not build. The message names the
 * definition that is being written, because most nodes have no text of their own that it could
 * point at. Returns false, like every function that records an error.
 */
static bool x64_fail_unsupported(X64_Builder *builder) {
    assert(builder->definition != AST_NODE_INDEX_NONE);

    return x64_fail(builder, X64_BUILD_STATUS_UNSUPPORTED,
                    ast_definition_name(&builder->nodes[builder->definition]));
}

/**
 * Records the first error: its status and the text in the source that it is about. A later error is
 * a result of the first one, so it is not kept. Returns false, so that a caller can return it.
 */
static bool x64_fail(X64_Builder *builder, X64_Build_Status status, Str subject) {
    assert(status != X64_BUILD_STATUS_OK);

    if (builder->status == X64_BUILD_STATUS_OK) {
        builder->status = status;
        builder->subject = subject;
    }
    return false;
}

/**
 * Writes `mov register, number`, with the 64-bit constant in the instruction. A shorter one exists
 * for a small number, but one form keeps the code simple.
 */
static void x64_write_load_integer(X64_Builder *builder, byte register_number,
                                   int64_t number) {
    assert(register_number < 16);

    // The prefix says that the operands are 64-bit, and that the register is one of `r8` to `r15`
    // when it is.
    x64_write_byte(&builder->writer, (byte)(0x48 | (register_number >> 3)));
    x64_write_byte(&builder->writer, (byte)(0xB8 | (register_number & 7)));
    x64_write_u64(&builder->writer, (uint64_t)number);
}

/**
 * Writes `mov register, [rbp + offset]` for the argument at `position`, which the procedure kept in
 * the space that its caller made.
 */
static void x64_write_load_slot(X64_Builder *builder, byte register_number, size_t position) {
    assert(position < X64_PARAMETER_COUNT_MAX);

    x64_write_frame_move(
        builder, X64_OPCODE_LOAD, register_number,
        (int32_t)(X64_ARGUMENT_HOME_OFFSET_BYTES + position * X64_SLOT_SIZE_BYTES));
}

/**
 * Writes `mov [rbp + offset], register`, which keeps the argument at `position` in the space that
 * the caller made for it.
 */
static void x64_write_store_slot(X64_Builder *builder, byte register_number, size_t position) {
    assert(position < X64_PARAMETER_COUNT_MAX);

    x64_write_frame_move(
        builder, X64_OPCODE_STORE, register_number,
        (int32_t)(X64_ARGUMENT_HOME_OFFSET_BYTES + position * X64_SLOT_SIZE_BYTES));
}

/**
 * Writes `mov register, [rbp + displacement]` for `X64_OPCODE_LOAD`, or
 * `mov [rbp + displacement], register` for `X64_OPCODE_STORE`, which is how the frame of a
 * procedure is read and written. The displacement takes one byte when it fits in one, which it
 * does for the arguments and the first variables, and four bytes otherwise. Both writes of the code
 * make the same choice, because the displacement depends only on the procedure.
 */
static void x64_write_frame_move(X64_Builder *builder, byte opcode, byte register_number,
                                 int32_t displacement) {
    assert(register_number < 16);
    assert(opcode == X64_OPCODE_LOAD || opcode == X64_OPCODE_STORE);

    // The prefix says that the operands are 64-bit, and has the extension of the register when it
    // is one of `r8` to `r15`.
    x64_write_byte(&builder->writer, (byte)(0x48 | ((register_number >> 3) << 2)));
    x64_write_byte(&builder->writer, opcode);
    // The base is `rbp`, which is 5 in the low bits, the register is in the middle bits, and the
    // mode in the top bits says how long the displacement is.
    byte modrm = (byte)(0x05 | ((register_number & 7) << 3));
    if (displacement >= INT8_MIN && displacement <= INT8_MAX) {
        x64_write_byte(&builder->writer, (byte)(0x40 | modrm));
        x64_write_byte(&builder->writer, (byte)(int8_t)displacement);
    } else {
        x64_write_byte(&builder->writer, (byte)(0x80 | modrm));
        x64_write_u32(&builder->writer, (uint32_t)displacement);
    }
}

/**
 * Writes `call` of the code that starts at `target_offset` from the start of the code. The
 * distance is counted from the end of the instruction, which is 4 bytes after the place where it
 * is written. It can be negative, so it is a 32-bit integer in two's complement.
 */
static void x64_write_call_local(X64_Builder *builder, size_t target_offset) {
    x64_write_byte(&builder->writer, 0xE8);
    size_t end_offset = builder->writer.length + 4 - builder->text_start;
    x64_write_u32(&builder->writer, (uint32_t)(target_offset - end_offset));
}

/**
 * Writes `call [rip + distance]` of the import in the place `import_slot` of the address table,
 * which the loader fills with the address of the import.
 */
static void x64_write_call_import(X64_Builder *builder, uint32_t import_slot) {
    assert(import_slot < builder->layout.import_count);

    x64_write_byte(&builder->writer, 0xFF);
    x64_write_byte(&builder->writer, 0x15);
    uint64_t end_rva = (uint64_t)builder->text_rva +
                       (builder->writer.length + 4 - builder->text_start);
    uint64_t target_rva = X64_RDATA_RVA + builder->layout.address_table_offset +
                          (uint64_t)import_slot * X64_IMPORT_ENTRY_SIZE_BYTES;
    x64_write_u32(&builder->writer, (uint32_t)(target_rva - end_rva));
}

/**
 * Writes the headers into the space that `x64_image_write` left for them: the header of DOS, which
 * only says where the next one is, the signature and the header of the file, the optional header,
 * which is not optional for a program, and the headers of the two sections. `text_size_bytes` is
 * the size of the code without the padding after it, which the sections need.
 */
static void x64_headers_write(X64_Builder *builder, size_t text_size_bytes) {
    size_t text_raw_bytes = x64_align_up(text_size_bytes, X64_FILE_ALIGNMENT_BYTES);
    size_t rdata_raw_bytes =
        x64_align_up(builder->layout.rdata_size_bytes, X64_FILE_ALIGNMENT_BYTES);
    size_t rdata_virtual_bytes = builder->layout.rdata_size_bytes;
    size_t image_bytes =
        builder->text_rva + x64_align_up(text_size_bytes, X64_SECTION_ALIGNMENT_BYTES);
    size_t section_count = X64_SECTION_COUNT;
    if (builder->debug != NULL) {
        section_count += X64_DEBUG_SECTION_COUNT;
        X64_Debug_Section const *last = &builder->debug_sections[X64_DEBUG_SECTION_COUNT - 1];
        image_bytes = last->rva + x64_align_up(last->size_bytes, X64_SECTION_ALIGNMENT_BYTES);
    }

    if (builder->writer.is_overflowed) {
        return;
    }
    byte *header = builder->writer.bytes;
    x64_dos_header_write(header);

    // The signature "PE" and two zeros, and then the header of the file.
    byte *file = header + X64_DOS_HEADER_SIZE_BYTES;
    memcpy(file, "PE\0\0", 4);
    file += 4;
    x64_store_u16(file + 0, 0x8664);                      // The processor is x64.
    // The sections: .rdata, .data, .text, and the ones of the debug information.
    x64_store_u16(file + 2, (uint16_t)section_count);
    x64_store_u32(file + 4, 0);                           // No date, so the file is reproducible.
    // No symbols, but the table of strings that follows where they would be has the names of the
    // sections of the debug information.
    x64_store_u32(file + 8, (uint32_t)builder->string_table_offset);
    x64_store_u32(file + 12, 0);
    x64_store_u16(file + 16, X64_OPTIONAL_HEADER_SIZE_BYTES);
    // An executable file, and one that may be loaded above 2 GiB.
    x64_store_u16(file + 18, 0x0002 | 0x0020);
    file += 20;

    byte *optional = file;
    x64_store_u16(optional + 0, 0x20B);                   // The magic number of PE32+.
    optional[2] = 1;                                      // The version of the linker.
    optional[3] = 0;
    x64_store_u32(optional + 4, (uint32_t)text_raw_bytes);   // The size of the code.
    x64_store_u32(optional + 8, (uint32_t)rdata_raw_bytes);  // The size of the data.
    x64_store_u32(optional + 12,
                  X64_TEMPORARY_USED_SIZE_BYTES + X64_TEMPORARY_STORAGE_SIZE_BYTES);
    x64_store_u32(optional + 16, builder->text_rva);         // Where the program starts.
    x64_store_u32(optional + 20, builder->text_rva);         // Where the code starts.
    x64_store_u64(optional + 24, X64_IMAGE_BASE);
    x64_store_u32(optional + 32, X64_SECTION_ALIGNMENT_BYTES);
    x64_store_u32(optional + 36, X64_FILE_ALIGNMENT_BYTES);
    // Windows 6.0 (Vista) is the oldest that loads a program for x64 with the system that is
    // asked for, and nothing here needs more.
    x64_store_u16(optional + 40, 6);
    x64_store_u16(optional + 42, 0);
    x64_store_u16(optional + 44, 0);
    x64_store_u16(optional + 46, 0);
    x64_store_u16(optional + 48, 6);
    x64_store_u16(optional + 50, 0);
    x64_store_u32(optional + 52, 0);
    x64_store_u32(optional + 56, (uint32_t)image_bytes);
    x64_store_u32(optional + 60, (uint32_t)builder->headers_size_bytes);
    x64_store_u32(optional + 64, 0);                      // The checksum is not checked.
    x64_store_u16(optional + 68, 3);                      // A program for the console.
    // The data cannot be executed. The program is not moved, because it has no relocations.
    x64_store_u16(optional + 70, 0x0100);
    x64_store_u64(optional + 72, 0x100000);               // The stack: 1 MiB, of which
    x64_store_u64(optional + 80, 0x1000);                 // one page is there from the start.
    x64_store_u64(optional + 88, 0x100000);               // The heap, likewise.
    x64_store_u64(optional + 96, 0x1000);
    x64_store_u32(optional + 104, 0);
    x64_store_u32(optional + 108, 16);                    // The number of entries that follow.
    // Entry 1 is the table of imports and entry 12 is the address table, which the loader reads.
    byte *directories = optional + 112;
    x64_store_u32(directories + 1 * 8,
                  (uint32_t)(X64_RDATA_RVA + builder->layout.import_table_offset));
    x64_store_u32(directories + 1 * 8 + 4, 2 * X64_IMPORT_DESCRIPTOR_SIZE_BYTES);
    x64_store_u32(directories + 12 * 8,
                  (uint32_t)(X64_RDATA_RVA + builder->layout.address_table_offset));
    x64_store_u32(directories + 12 * 8 + 4,
                  (uint32_t)((builder->layout.import_count + 1) * X64_IMPORT_ENTRY_SIZE_BYTES));

    // The headers of the sections: the name, the size in memory, where it is in memory, the size
    // in the file, where it is in the file, the four fields for relocations and line numbers that
    // a program does not use, and what the section may do.
    byte *section = optional + X64_OPTIONAL_HEADER_SIZE_BYTES;
    memcpy(section, ".rdata\0\0", 8);
    x64_store_u32(section + 8, (uint32_t)rdata_virtual_bytes);
    x64_store_u32(section + 12, X64_RDATA_RVA);
    x64_store_u32(section + 16, (uint32_t)rdata_raw_bytes);
    x64_store_u32(section + 20, (uint32_t)builder->headers_size_bytes);
    x64_store_u32(section + 36, 0x40000040);              // Data that is read and was initialized.
    section += X64_SECTION_HEADER_SIZE_BYTES;
    // The temporary storage: it has no bytes in the file, and the system gives it zeroed.
    memcpy(section, ".data\0\0\0", 8);
    x64_store_u32(section + 8, X64_TEMPORARY_USED_SIZE_BYTES + X64_TEMPORARY_STORAGE_SIZE_BYTES);
    x64_store_u32(section + 12, builder->data_rva);
    x64_store_u32(section + 16, 0);
    x64_store_u32(section + 20, 0);
    x64_store_u32(section + 36, 0xC0000080);              // Data that is read and written.
    section += X64_SECTION_HEADER_SIZE_BYTES;
    memcpy(section, ".text\0\0\0", 8);
    x64_store_u32(section + 8, (uint32_t)text_size_bytes);
    x64_store_u32(section + 12, builder->text_rva);
    x64_store_u32(section + 16, (uint32_t)text_raw_bytes);
    x64_store_u32(section + 20, (uint32_t)builder->text_start);
    x64_store_u32(section + 36, 0x60000020);              // Code that is read and executed.
    if (builder->debug == NULL) {
        return;
    }

    X64_Debug_Section const *unwind = &builder->debug_sections[X64_DEBUG_SECTION_UNWIND];
    x64_store_u32(directories + X64_DIRECTORY_EXCEPTION * 8, unwind->rva);
    // The table is the entries without the unwind information after them, which is as large for
    // every entry, see `x64_debug_unwind_write`.
    size_t unwind_entry_count =
        unwind->size_bytes / (X64_UNWIND_ENTRY_SIZE_BYTES + X64_UNWIND_INFO_SIZE_BYTES);
    x64_store_u32(directories + X64_DIRECTORY_EXCEPTION * 8 + 4,
                  (uint32_t)(unwind_entry_count * X64_UNWIND_ENTRY_SIZE_BYTES));

    // A name that is longer than 8 bytes is a slash and the place of the name in the table of
    // strings, in decimal, which is how the linkers of GNU write the names of the debug sections.
    size_t name_offset = 4;
    for (size_t i = 0; i < X64_DEBUG_SECTION_COUNT; i += 1) {
        section += X64_SECTION_HEADER_SIZE_BYTES;
        X64_Debug_Section const *debug_section = &builder->debug_sections[i];
        Str name = X64_DEBUG_SECTION_NAMES[i];
        if (name.length <= X64_SECTION_NAME_SIZE_BYTES) {
            memcpy(section, name.bytes, name.length);
        } else {
            char digits[X64_SECTION_NAME_SIZE_BYTES] = {0};
            size_t digit_count = 0;
            size_t rest = name_offset;
            do {
                digits[digit_count] = (char)('0' + rest % 10);
                rest /= 10;
                digit_count += 1;
            } while (rest != 0 && digit_count < sizeof(digits) - 1);
            assert(rest == 0);
            section[0] = '/';
            for (size_t j = 0; j < digit_count; j += 1) {
                section[1 + j] = (byte)digits[digit_count - 1 - j];
            }
            name_offset += name.length + 1;
        }
        x64_store_u32(section + 8, (uint32_t)debug_section->size_bytes);
        x64_store_u32(section + 12, debug_section->rva);
        x64_store_u32(section + 16, (uint32_t)x64_align_up(debug_section->size_bytes,
                                                           X64_FILE_ALIGNMENT_BYTES));
        x64_store_u32(section + 20, (uint32_t)debug_section->file_offset);
        if (i == X64_DEBUG_SECTION_UNWIND) {
            x64_store_u32(section + 36, X64_UNWIND_SECTION_CHARACTERISTICS);
        } else {
            x64_store_u32(section + 36, X64_DEBUG_SECTION_CHARACTERISTICS);
        }
    }
}

/**
 * Notes that the code that is written next is a place of `kind`, see `X64_Debug_Row`. It does
 * nothing for a file without debug information.
 */
static void x64_debug_row_add(X64_Builder *builder, X64_Debug_Row_Kind kind,
                              uint32_t source_offset) {
    if (builder->debug == NULL) {
        return;
    }
    // There are at most `X64_DEBUG_ROWS_PER_NODE` rows for each node, see `X64_Debug_Row`.
    assert(builder->row_count < builder->row_capacity);
    assert(builder->writer.length >= builder->text_start);

    builder->rows[builder->row_count] = (X64_Debug_Row){
        .code_offset = (uint32_t)(builder->writer.length - builder->text_start),
        .source_offset = source_offset,
        .kind = kind,
    };
    builder->row_count += 1;
}

/**
 * Writes the sections of the debug information after the code, each at a whole unit of file
 * alignment and at a page of its own in memory, and the table of strings with their names after
 * them. `text_size_bytes` is the size of the code without its padding.
 */
static void x64_debug_sections_write(X64_Builder *builder, size_t text_size_bytes) {
    assert(builder->debug != NULL);
    assert(builder->writer.is_overflowed ||
           builder->writer.length % X64_FILE_ALIGNMENT_BYTES == 0);

    X64_Writer *writer = &builder->writer;
    uint32_t rva = (uint32_t)(builder->text_rva +
                              x64_align_up(text_size_bytes, X64_SECTION_ALIGNMENT_BYTES));
    for (size_t i = 0; i < X64_DEBUG_SECTION_COUNT; i += 1) {
        size_t start = writer->length;
        switch (i) {
        case X64_DEBUG_SECTION_ABBREV:
            x64_write_bytes(writer, X64_DEBUG_ABBREVIATIONS, sizeof(X64_DEBUG_ABBREVIATIONS));
            break;
        case X64_DEBUG_SECTION_FRAME:
            x64_debug_frame_write(builder, text_size_bytes);
            break;
        case X64_DEBUG_SECTION_INFO:
            x64_debug_info_write(builder, text_size_bytes);
            break;
        case X64_DEBUG_SECTION_LINE:
            x64_debug_line_write(builder, text_size_bytes);
            break;
        case X64_DEBUG_SECTION_UNWIND:
            x64_debug_unwind_write(builder, text_size_bytes, rva);
            break;
        default:
            assert(false);
            break;
        }
        size_t size_bytes = writer->length - start;
        builder->debug_sections[i] = (X64_Debug_Section){
            .file_offset = start,
            .size_bytes = size_bytes,
            .rva = rva,
        };
        x64_write_zeros(writer, x64_align_up(size_bytes, X64_FILE_ALIGNMENT_BYTES) - size_bytes);
        rva += (uint32_t)x64_align_up(size_bytes, X64_SECTION_ALIGNMENT_BYTES);
    }

    // The table of strings starts with its size, which counts the 4 bytes of the size, and then
    // has the names, each with a zero after it.
    builder->string_table_offset = writer->length;
    size_t table_size_bytes = 4;
    for (size_t i = 0; i < X64_DEBUG_SECTION_COUNT; i += 1) {
        if (X64_DEBUG_SECTION_NAMES[i].length > X64_SECTION_NAME_SIZE_BYTES) {
            table_size_bytes += X64_DEBUG_SECTION_NAMES[i].length + 1;
        }
    }
    x64_write_u32(writer, (uint32_t)table_size_bytes);
    for (size_t i = 0; i < X64_DEBUG_SECTION_COUNT; i += 1) {
        if (X64_DEBUG_SECTION_NAMES[i].length > X64_SECTION_NAME_SIZE_BYTES) {
            x64_write_string(writer, X64_DEBUG_SECTION_NAMES[i]);
        }
    }
}

/**
 * Writes the table of unwinding of Windows for x64, which the system and its debuggers use to
 * find the frame of the caller of a procedure, see
 * https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64. gdb reads it to find the end
 * of the prologue of a procedure, so that a breakpoint on a procedure is at its first statement. The
 * table has an entry for the start of the program and one for each procedure, in the order of the
 * code, and the unwind information of each follows the table. `section_rva` is where the section
 * is in memory.
 */
static void x64_debug_unwind_write(X64_Builder *builder, size_t text_size_bytes,
                                   uint32_t section_rva) {
    X64_Writer *writer = &builder->writer;

    size_t entry_count = 1;
    size_t first_procedure = text_size_bytes;
    for (size_t i = 0; i < builder->constant_count; i += 1) {
        if (builder->constants[i].kind == X64_CONSTANT_KIND_PROCEDURE) {
            if (entry_count == 1) {
                first_procedure = builder->constants[i].offset;
            }
            entry_count += 1;
        }
    }
    uint32_t info_rva = (uint32_t)(section_rva + entry_count * X64_UNWIND_ENTRY_SIZE_BYTES);

    x64_write_u32(writer, x64_debug_address_rva(builder, 0));
    x64_write_u32(writer, x64_debug_address_rva(builder, first_procedure));
    x64_write_u32(writer, info_rva);
    for (size_t i = 0; i < builder->constant_count; i += 1) {
        if (builder->constants[i].kind != X64_CONSTANT_KIND_PROCEDURE) {
            continue;
        }
        info_rva += X64_UNWIND_INFO_SIZE_BYTES;
        x64_write_u32(writer, x64_debug_address_rva(builder, builder->constants[i].offset));
        x64_write_u32(writer, x64_debug_address_rva(
                                  builder, x64_debug_procedure_end(builder, i, text_size_bytes)));
        x64_write_u32(writer, info_rva);
    }

    // The start of the program: version 1, a prologue of 4 bytes, and one code, which says that
    // it made 40 bytes of the stack, in units of 8 after the first 8. The codes are padded to an
    // even number.
    static byte const START_INFO[X64_UNWIND_INFO_SIZE_BYTES] = {
        0x01, 4, 1, 0x00, 4, (byte)(0x02 | (((X64_DEBUG_START_FRAME_BYTES - 8) / 8) << 4)), 0, 0,
    };
    x64_write_bytes(writer, START_INFO, sizeof(START_INFO));
    // A procedure: version 1, a prologue of 4 bytes, two codes, and `rbp` as the register of the
    // frame, with no offset. The codes are in the reverse order of the prologue: at 4 `rbp` was
    // set to the stack pointer, and at 1 `rbp` was pushed. The frame that is made after the
    // prologue needs no code, because `rbp` gives the stack pointer back.
    static byte const PROCEDURE_INFO[X64_UNWIND_INFO_SIZE_BYTES] = {
        0x01, 4, 2, 0x05, 4, 0x03, 1, 0x50,
    };
    for (size_t i = 1; i < entry_count; i += 1) {
        x64_write_bytes(writer, PROCEDURE_INFO, sizeof(PROCEDURE_INFO));
    }
}

/**
 * Writes `.debug_info`: one unit, which is the source file, with the types, the constants that are
 * strings, and the procedures with their parameters and variables. A location is where the value
 * is relative to the CFA of the procedure, see `X64_DEBUG_CFA_FRAME_OFFSET_BYTES`, which gdb works
 * out from `.debug_frame`, and the procedures say that with `DW_OP_call_frame_cfa`. The language is
 * C, because gdb has no Mialu, and the C of gdb shows an `Int` and a `C_Str` as they are.
 */
static void x64_debug_info_write(X64_Builder *builder, size_t text_size_bytes) {
    X64_Writer *writer = &builder->writer;
    X64_Debug const *debug = builder->debug;
    size_t unit_start = writer->length;

    // The size of the unit is known at the end.
    x64_write_u32(writer, 0);
    x64_write_u16(writer, X64_DWARF_VERSION);
    x64_write_u32(writer, 0);                             // The abbreviations start the section.
    x64_write_byte(writer, X64_DEBUG_ADDRESS_SIZE_BYTES);

    // The directory of the source file is the path up to its last separator.
    Str directory = STR(".");
    for (size_t i = debug->source_path.length; i > 0; i -= 1) {
        char character = debug->source_path.bytes[i - 1];
        if (character == '/' || character == '\\') {
            directory = str_make(debug->source_path.bytes, i - 1);
            break;
        }
    }

    x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_COMPILE_UNIT);
    x64_write_string(writer, STR("mialuc " MIALU_VERSION_STRING));
    x64_write_u16(writer, X64_DWARF_LANGUAGE_C99);
    x64_write_string(writer, debug->source_path);
    x64_write_string(writer, directory);
    x64_write_u64(writer, X64_IMAGE_BASE + builder->text_rva);
    x64_write_u64(writer, text_size_bytes);
    x64_write_u32(writer, 0);                             // The lines start their section.

    // An `Int` is a signed number of 8 bytes, and a `C_Str` is a pointer to bytes, which gdb shows
    // as the text that they are, up to the zero.
    X64_Debug_Types types = {0};
    types.int_type = (uint32_t)(writer->length - unit_start);
    x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_BASE_TYPE);
    x64_write_string(writer, STR("Int"));
    x64_write_byte(writer, X64_DWARF_ENCODING_SIGNED);
    x64_write_byte(writer, X64_SLOT_SIZE_BYTES);
    types.byte_type = (uint32_t)(writer->length - unit_start);
    x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_BASE_TYPE);
    x64_write_string(writer, STR("Byte"));
    x64_write_byte(writer, X64_DWARF_ENCODING_UNSIGNED_CHAR);
    x64_write_byte(writer, 1);
    uint32_t pointer_type = (uint32_t)(writer->length - unit_start);
    x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_POINTER_TYPE);
    x64_write_byte(writer, X64_DEBUG_ADDRESS_SIZE_BYTES);
    x64_write_u32(writer, types.byte_type);
    types.c_str_type = (uint32_t)(writer->length - unit_start);
    x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_TYPEDEF);
    x64_write_string(writer, STR("C_Str"));
    x64_write_u32(writer, pointer_type);

    // A constant that is a string is an array of its bytes and the zero after them, at its place
    // in `.rdata`.
    X64_Debug_Cursor cursor = {.offset = 0, .line_start = 0, .line = 1};
    for (size_t i = 0; i < builder->constant_count; i += 1) {
        if (builder->constants[i].kind != X64_CONSTANT_KIND_STRING) {
            continue;
        }
        Ast_Node_Index definition = builder->constants[i].definition;
        // The literal of the string, which is the value of the definition or of the constant
        // that it is defined as.
        Ast_Node_Index literal = builder->constant_values[definition].string;
        Str text = builder->nodes[literal].payload.string_literal.text;
        cursor = x64_debug_cursor_move(debug->source, cursor,
                                       builder->nodes[definition].source_offset);

        uint32_t array_type = (uint32_t)(writer->length - unit_start);
        x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_ARRAY_TYPE);
        x64_write_u32(writer, types.byte_type);
        x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_SUBRANGE_TYPE);
        x64_write_u32(writer, (uint32_t)text.length);
        x64_write_byte(writer, 0);

        x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_VARIABLE);
        x64_write_string(writer, builder->nodes[definition].payload.constant_definition.name);
        x64_write_byte(writer, 1);
        x64_write_u32(writer, cursor.line);
        x64_write_u32(writer, array_type);
        x64_write_unsigned_leb128(writer, 1 + X64_DEBUG_ADDRESS_SIZE_BYTES);
        x64_write_byte(writer, X64_DWARF_OP_ADDR);
        x64_write_u64(writer, X64_IMAGE_BASE + X64_RDATA_RVA + builder->layout.strings_offset +
                                  x64_string_offset(builder, literal));
    }

    // The start of the program has a name, so that a debugger that shows the frames below `main`
    // can name its frame. It is the code before the first procedure.
    size_t first_procedure = text_size_bytes;
    for (size_t i = 0; i < builder->constant_count; i += 1) {
        if (builder->constants[i].kind == X64_CONSTANT_KIND_PROCEDURE) {
            first_procedure = builder->constants[i].offset;
            break;
        }
    }
    x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_START);
    x64_write_string(writer, STR("start"));
    x64_write_u64(writer, X64_IMAGE_BASE + builder->text_rva);
    x64_write_u64(writer, first_procedure);

    // The procedures are in the order of the source, and so are their parameters and variables,
    // so the source is read once more from its start for them.
    cursor = (X64_Debug_Cursor){.offset = 0, .line_start = 0, .line = 1};
    for (size_t i = 0; i < builder->constant_count; i += 1) {
        if (builder->constants[i].kind != X64_CONSTANT_KIND_PROCEDURE) {
            continue;
        }
        Ast_Node_Index definition = builder->constants[i].definition;
        Ast_Node_Index procedure = builder->nodes[definition].first_child;
        size_t start = builder->constants[i].offset;
        size_t end = x64_debug_procedure_end(builder, i, text_size_bytes);
        X64_Type return_type = x64_return_type(&builder->nodes[procedure]);
        cursor = x64_debug_cursor_move(debug->source, cursor,
                                       builder->nodes[definition].source_offset);

        if (return_type == X64_TYPE_NONE) {
            x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_SUBPROGRAM_VOID);
        } else {
            x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_SUBPROGRAM);
        }
        x64_write_string(writer, builder->nodes[definition].payload.constant_definition.name);
        x64_write_byte(writer, 1);
        x64_write_u32(writer, cursor.line);
        x64_write_u64(writer, X64_IMAGE_BASE + x64_debug_address_rva(builder, start));
        x64_write_u64(writer, end - start);
        x64_write_unsigned_leb128(writer, 1);
        x64_write_byte(writer, X64_DWARF_OP_CALL_FRAME_CFA);
        if (return_type != X64_TYPE_NONE) {
            x64_write_u32(writer, x64_debug_type_reference(types, return_type));
        }

        // The argument at `j` is kept in the space that the caller made, at the CFA and above.
        size_t parameter_count = x64_parameter_count(builder, procedure);
        Ast_Node_Index parameter = builder->nodes[procedure].first_child;
        for (size_t j = 0; j < parameter_count; j += 1) {
            cursor = x64_debug_cursor_move(debug->source, cursor,
                                           builder->nodes[parameter].source_offset);
            x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_PARAMETER);
            x64_write_string(writer, builder->nodes[parameter].payload.parameter.name);
            x64_write_byte(writer, 1);
            x64_write_u32(writer, cursor.line);
            x64_write_u32(writer, x64_debug_type_reference(types,
                                                           x64_parameter_type(builder, procedure,
                                                                              j)));
            x64_debug_location_write(writer, (int64_t)(j * X64_SLOT_SIZE_BYTES));
            parameter = builder->nodes[parameter].next_sibling;
        }

        // The variable at `position` is in its slot below `rbp`, see `x64_frame_offset`.
        Ast_Node_Index procedure_end = ast_procedure_end(builder->nodes, builder->node_count,
                                                         procedure);
        size_t position = 0;
        for (Ast_Node_Index node = procedure; node < procedure_end; node += 1) {
            Ast_Node_Type node_type = builder->nodes[node].node_type;
            if (node_type == AST_NODE_TYPE_FOR_STATEMENT) {
                position += 1;
                continue;
            }
            if (node_type != AST_NODE_TYPE_VARIABLE_DEFINITION) {
                continue;
            }
            // The build checked the type of every variable, so it is one that the target has.
            X64_Type type = x64_variable_type(builder, node);
            assert(type != X64_TYPE_NONE);
            cursor = x64_debug_cursor_move(debug->source, cursor,
                                           builder->nodes[node].source_offset);
            x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_VARIABLE);
            x64_write_string(writer, builder->nodes[node].payload.variable_definition.name);
            x64_write_byte(writer, 1);
            x64_write_u32(writer, cursor.line);
            x64_write_u32(writer, x64_debug_type_reference(types, type));
            x64_debug_location_write(writer, (int64_t)x64_frame_offset(position) -
                                                 X64_DEBUG_CFA_FRAME_OFFSET_BYTES);
            position += 1;
        }

        x64_write_byte(writer, 0);                        // The end of the children.
    }

    x64_write_byte(writer, 0);                            // The end of the children of the unit.
    x64_patch_u32(writer, unit_start, (uint32_t)(writer->length - unit_start - 4));
}

/**
 * Writes `.debug_line`: the program of DWARF that gives the line and the column of every row of
 * `X64_DEBUG_ROW_KIND_LINE`, in the order of the code. It only uses the simplest instructions, one
 * row at a time, because the size of the section does not matter.
 */
static void x64_debug_line_write(X64_Builder *builder, size_t text_size_bytes) {
    X64_Writer *writer = &builder->writer;
    X64_Debug const *debug = builder->debug;
    size_t unit_start = writer->length;

    x64_write_u32(writer, 0);                             // The size, known at the end.
    x64_write_u16(writer, X64_DWARF_VERSION);
    size_t header_size_offset = writer->length;
    x64_write_u32(writer, 0);                             // The size of the rest of the header.
    size_t header_start = writer->length;
    // The smallest instruction is a byte, every row is the start of a statement, and the numbers of
    // the special opcodes, which are not used, are the usual ones: lines from -5 on, 14 of them,
    // after the 12 standard opcodes, whose numbers of operands follow.
    static byte const HEADER[] = {1, 1, 1, 0xFB, 14, 13, 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1};
    x64_write_bytes(writer, HEADER, sizeof(HEADER));
    x64_write_byte(writer, 0);                            // No directories besides the unit's.
    // The one file: its path, in the directory of the unit, with no time and size.
    x64_write_string(writer, debug->source_path);
    x64_write_unsigned_leb128(writer, 0);
    x64_write_unsigned_leb128(writer, 0);
    x64_write_unsigned_leb128(writer, 0);
    x64_write_byte(writer, 0);
    x64_patch_u32(writer, header_size_offset, (uint32_t)(writer->length - header_start));

    x64_write_byte(writer, 0);
    x64_write_unsigned_leb128(writer, 1 + X64_DEBUG_ADDRESS_SIZE_BYTES);
    x64_write_byte(writer, X64_DWARF_LNE_SET_ADDRESS);
    x64_write_u64(writer, X64_IMAGE_BASE + builder->text_rva);

    size_t address = 0;
    uint32_t line = 1;
    X64_Debug_Cursor cursor = {.offset = 0, .line_start = 0, .line = 1};
    for (size_t i = 0; i < builder->row_count; i += 1) {
        X64_Debug_Row row = builder->rows[i];
        if (row.kind != X64_DEBUG_ROW_KIND_LINE) {
            continue;
        }
        assert(row.code_offset >= address);
        cursor = x64_debug_cursor_move(debug->source, cursor, row.source_offset);
        if (row.code_offset > address) {
            x64_write_byte(writer, X64_DWARF_LNS_ADVANCE_PC);
            x64_write_unsigned_leb128(writer, row.code_offset - address);
            address = row.code_offset;
        }
        if (cursor.line != line) {
            x64_write_byte(writer, X64_DWARF_LNS_ADVANCE_LINE);
            x64_write_signed_leb128(writer, (int64_t)cursor.line - (int64_t)line);
            line = cursor.line;
        }
        x64_write_byte(writer, X64_DWARF_LNS_SET_COLUMN);
        x64_write_unsigned_leb128(writer, cursor.offset - cursor.line_start + 1);
        x64_write_byte(writer, X64_DWARF_LNS_COPY);
    }

    // The sequence ends at the end of the code.
    if (text_size_bytes > address) {
        x64_write_byte(writer, X64_DWARF_LNS_ADVANCE_PC);
        x64_write_unsigned_leb128(writer, text_size_bytes - address);
    }
    x64_write_byte(writer, 0);
    x64_write_unsigned_leb128(writer, 1);
    x64_write_byte(writer, X64_DWARF_LNE_END_SEQUENCE);
    x64_patch_u32(writer, unit_start, (uint32_t)(writer->length - unit_start - 4));
}

/**
 * Writes `.debug_frame`, which tells a debugger how to find the frame of the caller, the CFA, and
 * where `rbp` and the address to return to are kept, at every place of the code, so that it can
 * show the procedures that were called and step over a call. The common entry says what holds on
 * entry to a procedure, and then there is an entry for the start of the program and one for each
 * procedure, see `X64_DEBUG_CFA_FRAME_OFFSET_BYTES`.
 */
static void x64_debug_frame_write(X64_Builder *builder, size_t text_size_bytes) {
    X64_Writer *writer = &builder->writer;

    // The common entry: no augmentation, instructions of one byte, slots of 8 bytes that go down,
    // the address to return to is the register of `rip`, the CFA is `rsp + 8` and the address to
    // return to is in the slot right below it.
    size_t common_start = writer->length;
    x64_write_u32(writer, 0);
    x64_write_u32(writer, UINT32_MAX);
    x64_write_byte(writer, X64_DWARF_FRAME_VERSION);
    x64_write_byte(writer, 0);
    x64_write_unsigned_leb128(writer, 1);
    x64_write_signed_leb128(writer, -X64_SLOT_SIZE_BYTES);
    x64_write_byte(writer, X64_DWARF_REGISTER_RIP);
    x64_write_byte(writer, X64_DWARF_CFA_DEF_CFA);
    x64_write_unsigned_leb128(writer, X64_DWARF_REGISTER_RSP);
    x64_write_unsigned_leb128(writer, X64_DEBUG_CFA_ENTRY_OFFSET_BYTES);
    x64_write_byte(writer, X64_DWARF_CFA_OFFSET | X64_DWARF_REGISTER_RIP);
    x64_write_unsigned_leb128(writer, 1);
    x64_debug_entry_close(builder, common_start);

    // The start of the program makes 40 bytes of the stack with its first instruction of 4 bytes,
    // and never returns.
    size_t first_procedure = text_size_bytes;
    for (size_t i = 0; i < builder->constant_count; i += 1) {
        if (builder->constants[i].kind == X64_CONSTANT_KIND_PROCEDURE) {
            first_procedure = builder->constants[i].offset;
            break;
        }
    }
    size_t start_entry = writer->length;
    x64_write_u32(writer, 0);
    x64_write_u32(writer, 0);                             // The common entry is at the start.
    x64_write_u64(writer, X64_IMAGE_BASE + builder->text_rva);
    x64_write_u64(writer, first_procedure);
    x64_write_byte(writer, X64_DWARF_CFA_ADVANCE_LOC4);
    x64_write_u32(writer, 4);
    x64_write_byte(writer, X64_DWARF_CFA_DEF_CFA_OFFSET);
    x64_write_unsigned_leb128(writer,
                              X64_DEBUG_CFA_ENTRY_OFFSET_BYTES + X64_DEBUG_START_FRAME_BYTES);
    x64_debug_entry_close(builder, start_entry);

    // The rows are in the order of the code, and so are the procedures, so the rows of `leave` of
    // each are found by going on from where the last one ended.
    size_t row_index = 0;
    for (size_t i = 0; i < builder->constant_count; i += 1) {
        if (builder->constants[i].kind != X64_CONSTANT_KIND_PROCEDURE) {
            continue;
        }
        size_t start = builder->constants[i].offset;
        size_t end = x64_debug_procedure_end(builder, i, text_size_bytes);
        size_t entry = writer->length;
        x64_write_u32(writer, 0);
        x64_write_u32(writer, 0);
        x64_write_u64(writer, X64_IMAGE_BASE + x64_debug_address_rva(builder, start));
        x64_write_u64(writer, end - start);

        // After `push rbp`, which is 1 byte, the CFA is `rsp + 16` and `rbp` is kept right below
        // the address to return to. After `mov rbp, rsp`, which is 3 bytes, the CFA is `rbp + 16`
        // for the rest of the procedure, whatever it pushes.
        x64_write_byte(writer, X64_DWARF_CFA_ADVANCE_LOC4);
        x64_write_u32(writer, 1);
        x64_write_byte(writer, X64_DWARF_CFA_DEF_CFA_OFFSET);
        x64_write_unsigned_leb128(writer, X64_DEBUG_CFA_FRAME_OFFSET_BYTES);
        x64_write_byte(writer, X64_DWARF_CFA_OFFSET | X64_DWARF_REGISTER_RBP);
        x64_write_unsigned_leb128(writer, 2);
        x64_write_byte(writer, X64_DWARF_CFA_ADVANCE_LOC4);
        x64_write_u32(writer, 3);
        x64_write_byte(writer, X64_DWARF_CFA_DEF_CFA_REGISTER);
        x64_write_unsigned_leb128(writer, X64_DWARF_REGISTER_RBP);
        size_t location = start + 4;

        // After a `leave`, which is 1 byte, `rbp` is the one of the caller again and the CFA is
        // `rsp + 8`, until the `ret` after it. A `return` that is not the end of the procedure has
        // more code after its `ret`, where the frame is as it was before the `leave`.
        for (; row_index < builder->row_count; row_index += 1) {
            X64_Debug_Row row = builder->rows[row_index];
            if (row.code_offset >= end) {
                break;
            }
            if (row.kind != X64_DEBUG_ROW_KIND_LEAVE) {
                continue;
            }
            assert(row.code_offset + 1 >= location);
            x64_write_byte(writer, X64_DWARF_CFA_ADVANCE_LOC4);
            x64_write_u32(writer, (uint32_t)(row.code_offset + 1 - location));
            x64_write_byte(writer, X64_DWARF_CFA_DEF_CFA);
            x64_write_unsigned_leb128(writer, X64_DWARF_REGISTER_RSP);
            x64_write_unsigned_leb128(writer, X64_DEBUG_CFA_ENTRY_OFFSET_BYTES);
            x64_write_byte(writer, X64_DWARF_CFA_RESTORE | X64_DWARF_REGISTER_RBP);
            location = row.code_offset + 1;
            if (row.code_offset + 2 < end) {
                x64_write_byte(writer, X64_DWARF_CFA_ADVANCE_LOC4);
                x64_write_u32(writer, 1);
                x64_write_byte(writer, X64_DWARF_CFA_DEF_CFA);
                x64_write_unsigned_leb128(writer, X64_DWARF_REGISTER_RBP);
                x64_write_unsigned_leb128(writer, X64_DEBUG_CFA_FRAME_OFFSET_BYTES);
                x64_write_byte(writer, X64_DWARF_CFA_OFFSET | X64_DWARF_REGISTER_RBP);
                x64_write_unsigned_leb128(writer, 2);
                location += 1;
            }
        }
        x64_debug_entry_close(builder, entry);
    }
}

/**
 * Ends the entry of `.debug_frame` that starts at `entry_start`: it is padded with instructions
 * that do nothing to a multiple of the size of an address, as the format asks, and its size, which
 * does not count the 4 bytes of the size, is written at its start.
 */
static void x64_debug_entry_close(X64_Builder *builder, size_t entry_start) {
    X64_Writer *writer = &builder->writer;
    size_t size_bytes = writer->length - entry_start;
    // The instruction that does nothing is a zero.
    x64_write_zeros(writer, x64_align_up(size_bytes, X64_DEBUG_ADDRESS_SIZE_BYTES) - size_bytes);
    x64_patch_u32(writer, entry_start, (uint32_t)(writer->length - entry_start - 4));
}

/**
 * Writes a location of DWARF that is `cfa_offset` bytes from the CFA of the procedure, which is
 * its frame base, with the size of the expression before it.
 */
static void x64_debug_location_write(X64_Writer *writer, int64_t cfa_offset) {
    // The number takes one byte for each 7 bits of it, with the sign.
    size_t number_size_bytes = 1;
    for (int64_t rest = cfa_offset >> 6; rest != 0 && rest != -1; rest >>= 7) {
        number_size_bytes += 1;
    }
    x64_write_unsigned_leb128(writer, 1 + number_size_bytes);
    x64_write_byte(writer, X64_DWARF_OP_FBREG);
    x64_write_signed_leb128(writer, cfa_offset);
}

/**
 * Returns the offset in the code of the end of the procedure at `position` of the constants, which
 * is where the next procedure with code starts, or the end of the code for the last one.
 */
static size_t x64_debug_procedure_end(X64_Builder const *builder, size_t position,
                                      size_t text_size_bytes) {
    assert(position < builder->constant_count);
    assert(builder->constants[position].kind == X64_CONSTANT_KIND_PROCEDURE);

    for (size_t i = position + 1; i < builder->constant_count; i += 1) {
        if (builder->constants[i].kind == X64_CONSTANT_KIND_PROCEDURE) {
            return builder->constants[i].offset;
        }
    }
    return text_size_bytes;
}

/**
 * Returns the address in memory, relative to the image, of the code at `code_offset`.
 */
static uint32_t x64_debug_address_rva(X64_Builder const *builder, size_t code_offset) {
    return (uint32_t)(builder->text_rva + code_offset);
}

/**
 * Returns where the entry of `type` is in `.debug_info`, which `type` must have.
 */
static uint32_t x64_debug_type_reference(X64_Debug_Types types, X64_Type type) {
    switch (type) {
    case X64_TYPE_C_STR:
        return types.c_str_type;
    case X64_TYPE_INT:
        return types.int_type;
    case X64_TYPE_NONE:
        assert(false);
        return 0;
    }
    assert(false);
    return 0;
}

/**
 * Returns the cursor at `offset` in `source`, read on from `cursor`, or from the start when the
 * offset is before it, so that places in the order of the source are counted in one pass.
 */
static X64_Debug_Cursor x64_debug_cursor_move(Str source, X64_Debug_Cursor cursor,
                                              size_t offset) {
    assert(offset <= source.length);

    if (offset < cursor.offset) {
        cursor = (X64_Debug_Cursor){.offset = 0, .line_start = 0, .line = 1};
    }
    for (size_t i = cursor.offset; i < offset; i += 1) {
        if (source.bytes[i] == '\n') {
            cursor.line += 1;
            cursor.line_start = i + 1;
        }
    }
    cursor.offset = offset;
    return cursor;
}

/**
 * Returns where the bytes of the string literal `literal` are in `.rdata`, counted from the start
 * of the strings, which are in the order of the nodes, see `x64_rdata_write`.
 */
static size_t x64_string_offset(X64_Builder const *builder, Ast_Node_Index literal) {
    assert(builder->nodes[literal].node_type == AST_NODE_TYPE_STRING_LITERAL);

    size_t offset = 0;
    for (Ast_Node_Index node = 0; node < literal; node += 1) {
        if (builder->nodes[node].node_type == AST_NODE_TYPE_STRING_LITERAL) {
            offset += builder->nodes[node].payload.string_literal.text.length + 1;
        }
    }
    return offset;
}

/**
 * Writes `number` at `offset` of what was written already, which is how a size that is known only
 * at the end is written at the start. A writer that overflowed may not have the place, and its
 * file is not handed out, so nothing is written then.
 */
static void x64_patch_u32(X64_Writer *writer, size_t offset, uint32_t number) {
    if (offset + 4 > writer->length) {
        assert(writer->is_overflowed);
        return;
    }
    x64_store_u32(writer->bytes + offset, number);
}

/**
 * Writes `text` and a zero after it, which is how DWARF and the table of strings have a string.
 */
static void x64_write_string(X64_Writer *writer, Str text) {
    x64_write_bytes(writer, (byte const *)text.bytes, text.length);
    x64_write_byte(writer, 0);
}

/**
 * Writes `number` in the LEB128 format of DWARF: 7 bits in each byte, the lowest first, with the
 * top bit set in every byte but the last.
 */
static void x64_write_unsigned_leb128(X64_Writer *writer, uint64_t number) {
    // A 64-bit number has at most 10 groups of 7 bits.
    for (size_t i = 0; i < 10; i += 1) {
        byte low = (byte)(number & 0x7F);
        number >>= 7;
        if (number == 0) {
            x64_write_byte(writer, low);
            return;
        }
        x64_write_byte(writer, (byte)(low | 0x80));
    }
    assert(false);
}

/**
 * Writes `number` in the signed LEB128 format of DWARF, which ends when the rest is only the sign,
 * and the sign bit of the last byte agrees with it.
 */
static void x64_write_signed_leb128(X64_Writer *writer, int64_t number) {
    // A 64-bit number has at most 10 groups of 7 bits. The shift of a negative number is
    // arithmetic on every compiler that the project is built with, and the arithmetic is done on
    // the bits of an unsigned number so that it has no undefined behavior.
    uint64_t bits = (uint64_t)number;
    bool is_negative = number < 0;
    for (size_t i = 0; i < 10; i += 1) {
        byte low = (byte)(bits & 0x7F);
        bits >>= 7;
        if (is_negative) {
            bits |= ~(UINT64_MAX >> 7);
        }
        bool is_done = (bits == 0 && (low & 0x40) == 0) ||
                       (bits == UINT64_MAX && (low & 0x40) != 0);
        if (is_done) {
            x64_write_byte(writer, low);
            return;
        }
        x64_write_byte(writer, (byte)(low | 0x80));
    }
    assert(false);
}

/**
 * Writes the header of DOS, which every file of this format starts with. It is the two letters
 * "MZ" and, 60 bytes in, the place of the header that follows, which is right after it.
 */
static void x64_dos_header_write(byte *header) {
    header[0] = 'M';
    header[1] = 'Z';
    x64_store_u32(header + 0x3C, X64_DOS_HEADER_SIZE_BYTES);
}

/**
 * Returns `value` rounded up to a multiple of `alignment`, which is not zero.
 */
static size_t x64_align_up(size_t value, size_t alignment) {
    assert(alignment > 0);

    return (value + alignment - 1) / alignment * alignment;
}

static void x64_write_byte(X64_Writer *writer, byte content) {
    if (writer->length >= writer->capacity) {
        writer->is_overflowed = true;
        return;
    }
    writer->bytes[writer->length] = content;
    writer->length += 1;
}

static void x64_write_bytes(X64_Writer *writer, byte const *content, size_t count) {
    for (size_t i = 0; i < count; i += 1) {
        x64_write_byte(writer, content[i]);
    }
}

static void x64_write_zeros(X64_Writer *writer, size_t count) {
    for (size_t i = 0; i < count; i += 1) {
        x64_write_byte(writer, 0);
    }
}

/**
 * The numbers of the format are little-endian, whatever the machine that builds them is, so they
 * are written one byte at a time.
 */
static void x64_write_u16(X64_Writer *writer, uint16_t number) {
    x64_write_byte(writer, (byte)(number & 0xFF));
    x64_write_byte(writer, (byte)(number >> 8));
}

static void x64_write_u32(X64_Writer *writer, uint32_t number) {
    x64_write_u16(writer, (uint16_t)(number & 0xFFFF));
    x64_write_u16(writer, (uint16_t)(number >> 16));
}

static void x64_write_u64(X64_Writer *writer, uint64_t number) {
    x64_write_u32(writer, (uint32_t)(number & 0xFFFFFFFF));
    x64_write_u32(writer, (uint32_t)(number >> 32));
}

static void x64_store_u16(byte *destination, uint16_t number) {
    destination[0] = (byte)(number & 0xFF);
    destination[1] = (byte)(number >> 8);
}

static void x64_store_u32(byte *destination, uint32_t number) {
    x64_store_u16(destination, (uint16_t)(number & 0xFFFF));
    x64_store_u16(destination + 2, (uint16_t)(number >> 16));
}

static void x64_store_u64(byte *destination, uint64_t number) {
    x64_store_u32(destination, (uint32_t)(number & 0xFFFFFFFF));
    x64_store_u32(destination + 4, (uint32_t)(number >> 32));
}
