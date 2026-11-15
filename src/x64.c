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

    // The registers are numbered like the processor does, and `r8` to `r15` need the extra bit of
    // an instruction prefix. The code uses only the registers that the calling convention of
    // Windows on x64 lets a procedure change without saving them, so that a procedure never has to
    // keep a register of its caller: `rax`, `rcx`, `rdx` and `r8` to `r11`. `rsp` and `rbp` are
    // the stack and the frame.
    X64_REGISTER_RAX = 0,
    X64_REGISTER_RCX = 1,
    X64_REGISTER_RDX = 2,
    X64_REGISTER_RSP = 4,
    X64_REGISTER_RBP = 5,
    X64_REGISTER_R8 = 8,
    X64_REGISTER_R9 = 9,
    X64_REGISTER_R10 = 10,
    X64_REGISTER_R11 = 11,

    // The opcodes of the instructions that have a register and an operand, which is a register or
    // memory. `X64_OPCODE_LOAD` reads the operand into the register, `X64_OPCODE_LEA` puts the
    // address of the operand in the register, and the others write the operand: `mov` stores the
    // register in it, and the arithmetic sets it to itself and the register.
    X64_OPCODE_ADD = 0x01,
    X64_OPCODE_OR = 0x09,
    X64_OPCODE_AND = 0x21,
    X64_OPCODE_SUB = 0x29,
    X64_OPCODE_XOR = 0x31,
    X64_OPCODE_CMP = 0x39,
    X64_OPCODE_TEST = 0x85,
    X64_OPCODE_STORE = 0x89,
    X64_OPCODE_LOAD = 0x8B,
    X64_OPCODE_LEA = 0x8D,
    // The prefix that says that the operands are 64 bits wide. The bits of the extension of the
    // registers are added to it.
    X64_REX_WIDE = 0x48,
    // The prefix of the instructions of two bytes of opcode.
    X64_OPCODE_TWO_BYTE = 0x0F,
    X64_OPCODE_TWO_BYTE_IMUL = 0xAF,
    X64_OPCODE_TWO_BYTE_MOVZX_BYTE = 0xB6,
    X64_OPCODE_TWO_BYTE_SET = 0x90,
    // The numbers of the conditions of `set` and of the conditional jumps, which are added to their
    // opcodes. The signed ones are used for an `Int`, whose order is signed.
    X64_CONDITION_EQUAL = 0x4,
    // Not a condition of the processor: the jump that is always taken, see `x64_jump_write`.
    X64_CONDITION_ALWAYS = 0x10,
    X64_CONDITION_NOT_EQUAL = 0x5,
    X64_CONDITION_LESS = 0xC,
    X64_CONDITION_GREATER_EQUAL = 0xD,
    X64_CONDITION_LESS_EQUAL = 0xE,
    X64_CONDITION_GREATER = 0xF,

    // The space that the caller of a procedure makes below the arguments for the callee to keep the
    // four registers of arguments in, whether it has that many or not.
    X64_SHADOW_SPACE_BYTES = 32,
    // The number of words of the arguments that are passed in registers, see
    // `X64_ARGUMENT_REGISTERS`. The others are passed on the stack, above the shadow space.
    X64_ARGUMENT_REGISTER_COUNT = 4,
    // The size of a word of a value, which every value is made of, see `x64_type_word_count`. It is
    // also the size of a slot of the stack.
    X64_WORD_SIZE_BYTES = 8,
    // The processor pushes the address to return to, and the procedure pushes `rbp`, so the first
    // word of the arguments, which the procedure keeps there, is this far above `rbp`.
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
    X64_DWARF_ENCODING_BOOLEAN = 0x02,
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
    // `rsp`, so the frame of the caller, the CFA, starts 8 bytes above it. `push rbp` makes that
    // 16, and keeps `rbp` 16 bytes below the CFA, and from `mov rbp, rsp` on the CFA is `rbp + 16`.
    // So the arguments that the procedure keeps are at the CFA and above, and the variables are
    // below the 16 bytes of the address and of `rbp`.
    X64_DEBUG_CFA_ENTRY_OFFSET_BYTES = 8,
    X64_DEBUG_CFA_FRAME_OFFSET_BYTES = 16,
    // The start of the program makes 40 bytes of the stack, see `x64_code_write`.
    X64_DEBUG_START_FRAME_BYTES = 40,

    // The stack must stay a multiple of this, so that every call is made with it, see
    // `x64_procedure_write`. The frame of a procedure is rounded up to it.
    X64_STACK_ALIGNMENT_BYTES = 16,
    // The stack of the program, which the system makes for it when it starts. All of it is
    // committed at once, see `x64_headers_write`, so that a frame of any size up to it can be made
    // with one `sub rsp` and used at once, without touching every page on the way down as a
    // program has to when only the first page of the stack is there. 1 MiB is what a program for
    // Windows gets by default, and what the WebAssembly target has for its arrays as well.
    X64_STACK_SIZE_BYTES = 1024 * 1024,
    // The largest frame that a procedure can have, which fits the stack with room left for the
    // frames of its callers.
    X64_FRAME_SIZE_BYTES_MAX = X64_STACK_SIZE_BYTES / 2,
    // The bytes at the top of the frame of every procedure, below `rbp`, that come before the
    // places of its variables: the slot that keeps how much of the temporary storage was in use
    // when the procedure started, see `x64_procedure_write`.
    X64_FRAME_FIXED_SIZE_BYTES = 8,
    // The place of that slot, at `rbp - 8`.
    X64_FRAME_TEMPORARY_OFFSET_BYTES = 8,
    // The most frames that the statements of a procedure need at once: the body, and for each of
    // the blocks that the parser accepts inside each other the statement and the block, see
    // `AST_BLOCK_DEPTH_MAX`, with room for more that later statements need.
    X64_STATEMENT_DEPTH_MAX = 4 * AST_BLOCK_DEPTH_MAX + 4,
};

// The end of a chain of jumps, see `x64_jump_write`. No place in the code is this far.
static uint32_t const X64_JUMP_NONE = UINT32_MAX;

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

// How an expression is wanted by what it is part of. Alphabetical, because nothing depends on the
// order.
typedef enum {
    // The address of the place that the expression is, which is what an assignment writes to.
    X64_MODE_ADDRESS,
    // The value of the expression.
    X64_MODE_VALUE,
} X64_Mode;

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
    uint32_t bool_type;
    uint32_t byte_type;
    uint32_t c_str_type;
    uint32_t int_type;
} X64_Debug_Types;

/**
 * A node of an expression that is being written, see `x64_expression_write`. `wanted` is the type
 * that the place of the node wants, which an untyped constant becomes, and is of the kind
 * `X64_TYPE_KIND_NONE` when the place does not say. `stack_start` is the depth of the stack of
 * values when the node was entered, which is where its value goes, and `index` is the place of the
 * node among the children of its parent.
 */
typedef struct {
    X64_Type wanted;
    Ast_Node_Index node;
    uint32_t stack_start;
    uint32_t index;
    X64_Mode mode;
} X64_Expression_Frame;

/**
 * What the pattern of a case of a match statement matches, see `x64_match_pattern`: the numbers
 * from `low` to `high`, both included. When `status` is not `X64_BUILD_STATUS_OK` the pattern is
 * wrong, and the rest means nothing.
 */
typedef struct {
    int64_t low;
    int64_t high;
    X64_Build_Status status;
} X64_Match_Pattern;

/**
 * A list of statements that is being written, or a statement that has blocks, see
 * `x64_statements_write`. `node` is the procedure for its body, a block, an if statement or a match
 * statement, and `next` is its child that is written next. `end_jumps` are the jumps to the end of
 * an if statement or a match statement, and `false_jumps` the jumps of the condition of its branch
 * that is being written, which go to the next branch when the condition does not hold.
 */
typedef struct {
    Ast_Node_Index node;
    Ast_Node_Index next;
    uint32_t end_jumps;
    uint32_t false_jumps;
} X64_Statement_Frame;

/**
 * The state shared by the building functions. A building function returns false after it recorded
 * an error here, and its caller then returns false too without recording anything more. So only
 * the first error is kept. The code is written twice, see `x64_build`, so everything here that the
 * code changes is reset between the two.
 *
 * The values of an expression are kept on a stack in the frame of the procedure while the
 * expression is worked out, the stack of values, whose depth the build knows at every instruction,
 * because it follows from the expression and not from the run. A value takes some words of 8
 * bytes, see `x64_type_word_count`, which are at increasing addresses, so that a value is in the
 * frame as it is in memory anywhere else. The stack grows down from the places of the variables:
 * the value that takes the words from `start` up to `end` of the stack is at
 * `rbp - (locals_size_bytes + 8 * end)`, see `x64_stack_displacement`. The stack pointer does not
 * move while a procedure runs, so it is a multiple of 16 for every call, and the arguments of a
 * call that do not go in registers are stored at the bottom of the frame, where the callee looks
 * for them.
 */
typedef struct {
    X64_Writer writer;
    Ast_Node const *nodes;
    Semantic_Binding const *bindings;
    // What is known about each node when the program is compiled, see `constant_evaluate`. A node
    // whose entry is `CONSTANT_STATUS_OK` is loaded as its value.
    Constant_Value const *constant_values;
    // What the build works out about each node, see `X64_Node_Layout`.
    X64_Node_Layout *layouts;
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
    // The size of the places of the variables of the procedure that is being written, below `rbp`,
    // which is where its stack of values starts, see `x64_procedure_layout`.
    size_t locals_size_bytes;
    // The depth of the stack of values, in words, and the deepest that it was in the procedure,
    // which says how large the frame is.
    size_t stack_words;
    size_t stack_words_max;
    // The most words of arguments that a call of the procedure passes on the stack, for which the
    // frame has room at its bottom.
    size_t outgoing_words_max;
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

static size_t       x64_align_up                 (size_t value, size_t alignment);
static size_t       x64_argument_word_offset     (X64_Builder const *builder,
                                                  Ast_Node_Index procedure, size_t position);
static bool         x64_assignment_write         (X64_Builder *builder, Ast_Node_Index statement);
static void         x64_block_end                (X64_Builder *builder,
                                                  X64_Statement_Frame *frame,
                                                  Ast_Node_Index block);
static bool         x64_c_str_write              (X64_Builder *builder, Ast_Node_Index literal,
                                                  byte register_number);
static bool         x64_call_is_built_in         (X64_Builder const *builder,
                                                  Ast_Node_Index expression);
static bool         x64_call_is_str_to_c_str     (X64_Builder const *builder,
                                                  Ast_Node_Index expression);
static Ast_Node_Index x64_call_procedure         (X64_Builder const *builder, Ast_Node_Index call);
static bool         x64_call_write               (X64_Builder *builder,
                                                  X64_Expression_Frame const *frame,
                                                  bool is_result_used);
static Ast_Node_Index x64_child_first            (X64_Builder const *builder,
                                                  Ast_Node_Index expression);
static X64_Type     x64_child_wanted             (X64_Builder const *builder,
                                                  X64_Expression_Frame const *parent,
                                                  Ast_Node_Index child, size_t index);
static bool         x64_code_write               (X64_Builder *builder);
static bool         x64_comparison_write         (X64_Builder *builder,
                                                  X64_Expression_Frame const *frame);
static bool         x64_condition_write          (X64_Builder *builder,
                                                  Ast_Node_Index condition,
                                                  uint32_t *false_jumps);
static bool         x64_constant_write           (X64_Builder *builder,
                                                  X64_Expression_Frame const *frame);
static bool         x64_constants_collect        (X64_Builder *builder);
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
static uint32_t     x64_debug_type_reference     (X64_Debug_Types types, X64_Type type);
static void         x64_debug_unwind_write       (X64_Builder *builder, size_t text_size_bytes,
                                                  uint32_t section_rva);
static void         x64_dos_header_write         (byte *header);
static void         x64_epilogue_write           (X64_Builder *builder);
static bool         x64_expression_node_write    (X64_Builder *builder,
                                                  X64_Expression_Frame const *frame,
                                                  bool is_result_used);
static Str          x64_expression_subject       (X64_Builder const *builder,
                                                  Ast_Node_Index expression);
static X64_Type     x64_expression_type          (X64_Builder const *builder,
                                                  Ast_Node_Index expression);
static X64_Type     x64_expression_type_for      (X64_Builder const *builder,
                                                  Ast_Node_Index expression, X64_Type wanted);
static bool         x64_expression_write         (X64_Builder *builder, Ast_Node_Index expression,
                                                  X64_Type wanted, bool is_result_used);
static bool         x64_fail                     (X64_Builder *builder, X64_Build_Status status,
                                                  Str subject);
static bool         x64_fail_unsupported         (X64_Builder *builder);
static void         x64_headers_write            (X64_Builder *builder, size_t text_size_bytes);
static void         x64_image_write              (X64_Builder *builder);
static bool         x64_is_constant              (X64_Builder const *builder,
                                                  Ast_Node_Index expression);
static void         x64_jump_write               (X64_Builder *builder, byte condition,
                                                  uint32_t *jumps);
static void         x64_jumps_resolve            (X64_Builder *builder, uint32_t jumps);
static X64_Layout   x64_layout_make              (X64_Builder const *builder);
static bool         x64_main_check               (X64_Builder *builder);
static Ast_Node_Index x64_match_case_write       (X64_Builder *builder,
                                                  X64_Statement_Frame *frame,
                                                  Ast_Node_Index first);
static X64_Match_Pattern x64_match_pattern       (X64_Builder const *builder, X64_Type subject,
                                                  Ast_Node_Index pattern);
static bool         x64_match_start_write        (X64_Builder *builder, Ast_Node_Index match);
static bool         x64_operation_write          (X64_Builder *builder,
                                                  X64_Expression_Frame const *frame);
static size_t       x64_parameter_count          (X64_Builder const *builder,
                                                  Ast_Node_Index procedure);
static X64_Type     x64_parameter_type           (X64_Builder const *builder,
                                                  Ast_Node_Index procedure, size_t position);
static size_t       x64_parameter_word_count     (X64_Builder const *builder,
                                                  Ast_Node_Index procedure);
static void         x64_patch_u32                (X64_Writer *writer, size_t offset,
                                                  uint32_t number);
static void         x64_procedure_layout         (X64_Builder *builder, Ast_Node_Index procedure);
static bool         x64_procedure_uses_temporary (X64_Builder const *builder,
                                                  Ast_Node_Index procedure);
static bool         x64_procedure_write          (X64_Builder *builder, size_t position);
static void         x64_rdata_write              (X64_Builder *builder);
static X64_Type     x64_return_type              (X64_Builder const *builder,
                                                  Ast_Node_Index procedure);
static bool         x64_return_write             (X64_Builder *builder, Ast_Node_Index procedure,
                                                  Ast_Node_Index statement);
static int32_t      x64_stack_displacement       (X64_Builder const *builder, size_t start,
                                                  size_t word_count);
static void         x64_stack_pop                (X64_Builder *builder, size_t word_count);
static int32_t      x64_stack_push               (X64_Builder *builder, size_t word_count);
static bool         x64_statement_write          (X64_Builder *builder, Ast_Node_Index procedure,
                                                  Ast_Node_Index statement);
static bool         x64_statements_write         (X64_Builder *builder, Ast_Node_Index procedure);
static void         x64_store_u16                (byte *destination, uint16_t number);
static void         x64_store_u32                (byte *destination, uint32_t number);
static void         x64_store_u64                (byte *destination, uint64_t number);
static bool         x64_str_to_c_str_write       (X64_Builder *builder, Ast_Node_Index call);
static Ast_Node_Index x64_string_literal_of      (X64_Builder const *builder,
                                                  Ast_Node_Index expression);
static size_t       x64_string_offset            (X64_Builder const *builder,
                                                  Ast_Node_Index literal);
static bool         x64_type_accepts             (X64_Builder const *builder, X64_Type wanted,
                                                  Ast_Node_Index expression);
static bool         x64_type_equals              (X64_Type first, X64_Type second);
static bool         x64_type_is_scalar           (X64_Type type);
static X64_Type     x64_type_make                (X64_Type_Kind kind);
static X64_Type     x64_type_of_name             (Str name);
static X64_Type     x64_type_of_node             (X64_Builder const *builder, Ast_Node_Index type);
static size_t       x64_type_word_count          (X64_Type type);
static bool         x64_value_store              (X64_Builder *builder, X64_Type type,
                                                  int32_t displacement);
static bool         x64_variable_definition_write(X64_Builder *builder, Ast_Node_Index statement);
static Ast_Node_Index x64_variable_type_node     (X64_Builder const *builder,
                                                  Ast_Node_Index definition);
static void         x64_write_byte               (X64_Writer *writer, byte content);
static void         x64_write_bytes              (X64_Writer *writer, byte const *content,
                                                  size_t count);
static void         x64_write_call_import        (X64_Builder *builder, uint32_t import_slot);
static void         x64_write_call_local         (X64_Builder *builder, size_t target_offset);
static void         x64_write_divide             (X64_Builder *builder, byte divisor);
static void         x64_write_load_integer       (X64_Builder *builder, byte register_number,
                                                  int64_t number);
static void         x64_write_memory             (X64_Builder *builder, byte opcode,
                                                  byte register_number, byte base,
                                                  int32_t displacement);
static void         x64_write_move_from_r10      (X64_Builder *builder, byte register_number);
static void         x64_write_multiply           (X64_Builder *builder, byte target, byte source);
static void         x64_write_registers          (X64_Builder *builder, byte opcode, byte source,
                                                  byte target);
static void         x64_write_rip_displacement   (X64_Builder *builder, uint64_t target_rva);
static void         x64_write_set                (X64_Builder *builder, byte condition);
static void         x64_write_signed_leb128      (X64_Writer *writer, int64_t number);
static void         x64_write_string             (X64_Writer *writer, Str text);
static void         x64_write_u16                (X64_Writer *writer, uint16_t number);
static void         x64_write_u32                (X64_Writer *writer, uint32_t number);
static void         x64_write_u64                (X64_Writer *writer, uint64_t number);
static void         x64_write_unsigned_leb128    (X64_Writer *writer, uint64_t number);
static void         x64_write_zeros              (X64_Writer *writer, size_t count);

// The registers that carry the first words of the arguments, in the order of the words.
static byte const X64_ARGUMENT_REGISTERS[X64_ARGUMENT_REGISTER_COUNT] = {
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
// The fixed part of a frame is whole words, so that the variables after it are aligned.
STATIC_ASSERT(X64_FRAME_FIXED_SIZE_BYTES % X64_WORD_SIZE_BYTES == 0, x64_frame_fixed_is_words);
static char const X64_EXIT_NAME[] = "exit";

X64_Build_Result x64_build(Array_Slice nodes, Array_Slice bindings, Array_Slice constants,
                           Ast_Node_Index root, Array_Slice layout_buffer,
                           Array_Slice output_buffer, Array_Slice symbol_buffer,
                           X64_Debug const *debug) {
    assert(nodes.elements != NULL);
    assert(bindings.elements != NULL);
    assert(constants.elements != NULL);
    assert(constants.length == nodes.length);
    assert(layout_buffer.elements != NULL);
    assert(layout_buffer.length >= nodes.length);
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

    // Every node starts with nothing worked out about it, so that what a procedure does not set is
    // never read as something that it did.
    X64_Node_Layout *layouts = layout_buffer.elements;
    for (size_t i = 0; i < nodes.length; i += 1) {
        layouts[i] = (X64_Node_Layout){
            .type = x64_type_make(X64_TYPE_KIND_NONE),
            .frame_offset = 0,
        };
    }

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
        .layouts = layouts,
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
        .locals_size_bytes = 0,
        .stack_words = 0,
        .stack_words_max = 0,
        .outgoing_words_max = 0,
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
 * depend on the order for: that the module has nothing but procedures and values, that the
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
        Ast_Node_Index parameter = builder->nodes[procedure].first_child;
        for (size_t j = 0; j < parameter_count; j += 1) {
            assert(parameter != AST_NODE_INDEX_NONE);

            Ast_Node_Index type = builder->nodes[parameter].first_child;
            if (x64_type_of_node(builder, type).kind == X64_TYPE_KIND_NONE) {
                Str subject = builder->nodes[parameter].payload.parameter.name;
                if (builder->nodes[type].node_type == AST_NODE_TYPE_TYPE_NAME) {
                    subject = builder->nodes[type].payload.type_name.name;
                }
                return x64_fail(builder, X64_BUILD_STATUS_TYPE_UNSUPPORTED, subject);
            }
            parameter = builder->nodes[parameter].next_sibling;
        }

        Str return_type = ast_procedure_return_type(&builder->nodes[procedure]);
        if (return_type.bytes != NULL &&
            x64_type_of_name(return_type).kind == X64_TYPE_KIND_NONE) {
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
            x64_return_type(builder, procedure).kind != X64_TYPE_KIND_INT) {
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
 * Works out the frame of `procedure` before its code is written: the type of each of its variables
 * and the place of its value, in the order of the source, which `builder->layouts` keeps at the
 * node of the definition. A match statement has a place too, for its subject. A variable takes the
 * words of its type, see `x64_type_word_count`, and has a place of its own also when it is in a block, so that no two variables share memory and a
 * debugger finds each at one place. A variable without a type takes the type of its value, which
 * only uses variables above it, whose types are worked out already. The places start below the
 * fixed part of the frame, see `X64_FRAME_FIXED_SIZE_BYTES`, and their size is
 * `builder->locals_size_bytes`, below which the stack of values starts.
 */
static void x64_procedure_layout(X64_Builder *builder, Ast_Node_Index procedure) {
    assert(builder->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);

    size_t size_bytes = X64_FRAME_FIXED_SIZE_BYTES;
    Ast_Node_Index end = ast_procedure_end(builder->nodes, builder->node_count, procedure);
    for (Ast_Node_Index node = procedure; node < end; node += 1) {
        Ast_Node_Type node_type = builder->nodes[node].node_type;
        X64_Type type = x64_type_make(X64_TYPE_KIND_NONE);
        if (node_type == AST_NODE_TYPE_VARIABLE_DEFINITION) {
            Ast_Node_Index type_node = x64_variable_type_node(builder, node);
            if (type_node != AST_NODE_INDEX_NONE) {
                type = x64_type_of_node(builder, type_node);
            } else {
                type = x64_expression_type(builder, builder->nodes[node].first_child);
            }
        } else if (node_type == AST_NODE_TYPE_MATCH_STATEMENT) {
            // The subject is worked out once, and kept for the cases to compare.
            type = x64_expression_type(builder, ast_match_subject(builder->nodes, node));
        } else {
            continue;
        }
        // A variable of a type that the target does not have is an error that the code finds
        // where it is defined. It takes a word until then, so that the layout is complete.
        size_t word_count = 1;
        if (type.kind != X64_TYPE_KIND_NONE) {
            word_count = x64_type_word_count(type);
        }
        size_bytes += word_count * X64_WORD_SIZE_BYTES;
        assert(size_bytes <= UINT32_MAX);
        builder->layouts[node] = (X64_Node_Layout){
            .type = type,
            .frame_offset = (uint32_t)size_bytes,
        };
    }

    builder->locals_size_bytes = size_bytes;
}

/**
 * Writes the procedure at `position` of the module: it keeps `rbp` and sets it to the stack
 * pointer, so that the arguments, which are kept above it, and the variables, which are kept below
 * it, are found at the same distance from it whatever the procedure does. It copies the registers
 * that carry the first words of the arguments to the space that the caller made for them, right
 * below the words that the caller passed on the stack, so that every word of the arguments is in
 * memory, one after the other, see `x64_argument_word_offset`. Then it makes the frame, whose size
 * is known only after the statements are written, because it holds the stack of values, so the
 * number of `sub rsp` is filled in then. Returns false after it recorded an error.
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
    // that entered the procedure made it 8 past one and `push rbp` is 8 more. The frame is a
    // multiple of 16 too, and nothing is pushed after, so it is also what every call made by the
    // procedure is made with.
    static byte const PROLOGUE[] = {0x55, 0x48, 0x89, 0xE5};
    x64_write_bytes(writer, PROLOGUE, sizeof(PROLOGUE));

    size_t argument_word_count = x64_parameter_word_count(builder, procedure);
    for (size_t i = 0; i < argument_word_count && i < X64_ARGUMENT_REGISTER_COUNT; i += 1) {
        x64_write_memory(builder, X64_OPCODE_STORE, X64_ARGUMENT_REGISTERS[i], X64_REGISTER_RBP,
                         (int32_t)(X64_ARGUMENT_HOME_OFFSET_BYTES + i * X64_WORD_SIZE_BYTES));
    }

    x64_procedure_layout(builder, procedure);
    builder->stack_words = 0;
    builder->stack_words_max = 0;
    builder->outgoing_words_max = 0;
    builder->is_temporary_used = x64_procedure_uses_temporary(builder, procedure);

    // `sub rsp, size`, whose size is written at the end, when the stack of values is known.
    static byte const FRAME_MAKE[] = {0x48, 0x81, 0xEC};
    x64_write_bytes(writer, FRAME_MAKE, sizeof(FRAME_MAKE));
    size_t frame_size_offset = writer->length;
    x64_write_u32(writer, 0);

    // A procedure that makes temporary strings keeps how much of the storage was in use when it
    // started, so that it can give them all back when it ends: `mov rax, [used]` and
    // `mov [rbp - 8], rax`.
    if (builder->is_temporary_used) {
        static byte const USED_LOAD[] = {0x48, 0x8B, 0x05};
        x64_write_bytes(writer, USED_LOAD, sizeof(USED_LOAD));
        x64_write_rip_displacement(builder, builder->data_rva);
        x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP,
                         -(int32_t)X64_FRAME_TEMPORARY_OFFSET_BYTES);
    }

    if (x64_statements_write(builder, procedure) == false) {
        return false;
    }

    // `leave` and `ret` give the stack back and return. Falling off the end of a procedure that
    // has a result is a mistake of the program, so it stops with a breakpoint instead of returning
    // whatever is in `rax`.
    bool has_result = x64_return_type(builder, procedure).kind != X64_TYPE_KIND_NONE;
    Ast_Node_Index last = builder->nodes[procedure].last_child;
    bool is_last_return = builder->nodes[last].node_type == AST_NODE_TYPE_RETURN_STATEMENT;
    if (is_last_return == false) {
        if (has_result) {
            x64_write_byte(writer, 0xCC);
        } else {
            x64_epilogue_write(builder);
        }
    }

    // The frame has the places of the variables, the stack of values below them, and at its
    // bottom the space that a call makes for its callee, with the words of the arguments that go on
    // the stack above it. It is a multiple of 16, so that the stack stays one.
    size_t frame_size_bytes = x64_align_up(
        builder->locals_size_bytes + builder->stack_words_max * X64_WORD_SIZE_BYTES +
            X64_SHADOW_SPACE_BYTES + builder->outgoing_words_max * X64_WORD_SIZE_BYTES,
        X64_STACK_ALIGNMENT_BYTES);
    if (frame_size_bytes > X64_FRAME_SIZE_BYTES_MAX) {
        return x64_fail_unsupported(builder);
    }
    x64_patch_u32(writer, frame_size_offset, (uint32_t)frame_size_bytes);
    assert(builder->stack_words == 0);
    return true;
}

/**
 * Writes the statements of the body of `procedure`, without recursion: a frame is kept for each
 * list of statements that is being written, which is the body or a block, and for each statement
 * that has blocks, an if statement or a match statement. A statement that has no blocks is written
 * at once. A frame is taken off when its list or its statement has nothing more to write, and then
 * the statement that the block belongs to writes what comes between its blocks.
 *
 * A jump goes forward to a place that is not known when it is written, so its distance of 32 bits,
 * which is as long whatever the distance is, is filled in when the place is reached. The jumps that
 * go to one place are a chain, see `x64_jump_write`. Returns false after it recorded an error.
 */
static bool x64_statements_write(X64_Builder *builder, Ast_Node_Index procedure) {
    assert(builder->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);

    // The statements follow the parameters among the children of the procedure.
    size_t parameter_count = x64_parameter_count(builder, procedure);
    Ast_Node_Index first = builder->nodes[procedure].first_child;
    for (size_t i = 0; i < parameter_count; i += 1) {
        first = builder->nodes[first].next_sibling;
    }

    X64_Statement_Frame frames[X64_STATEMENT_DEPTH_MAX];
    size_t frame_count = 1;
    frames[0] = (X64_Statement_Frame){
        .node = procedure,
        .next = first,
        .end_jumps = X64_JUMP_NONE,
        .false_jumps = X64_JUMP_NONE,
    };

    // Every node is entered once and left once, and a frame is made for a node at most once, so
    // twice the number of nodes steps are enough.
    for (size_t step = 0; step < builder->node_count * 2 + 2 && frame_count > 0; step += 1) {
        X64_Statement_Frame *frame = &frames[frame_count - 1];
        Ast_Node_Type node_type = builder->nodes[frame->node].node_type;

        if (frame->next == AST_NODE_INDEX_NONE) {
            // The list or the statement ends here, so the jumps to its end come here, and the
            // statement that a block is of goes on after it.
            x64_jumps_resolve(builder, frame->false_jumps);
            x64_jumps_resolve(builder, frame->end_jumps);
            Ast_Node_Index finished = frame->node;
            frame_count -= 1;
            if (frame_count > 0 && node_type == AST_NODE_TYPE_BLOCK) {
                x64_block_end(builder, &frames[frame_count - 1], finished);
            }
            continue;
        }

        Ast_Node_Index child = frame->next;
        Ast_Node_Index pushed = AST_NODE_INDEX_NONE;
        bool is_ok = true;
        if (node_type == AST_NODE_TYPE_IF_STATEMENT) {
            frame->next = builder->nodes[child].next_sibling;
            if (builder->nodes[child].node_type == AST_NODE_TYPE_BLOCK) {
                pushed = child;
            } else {
                is_ok = x64_condition_write(builder, child, &frame->false_jumps);
            }
        } else if (node_type == AST_NODE_TYPE_MATCH_STATEMENT) {
            pushed = x64_match_case_write(builder, frame, child);
            is_ok = pushed != AST_NODE_INDEX_NONE;
        } else {
            // A list of statements: the body or a block.
            frame->next = builder->nodes[child].next_sibling;
            x64_debug_row_add(builder, X64_DEBUG_ROW_KIND_LINE,
                              builder->nodes[child].source_offset);
            Ast_Node_Type child_type = builder->nodes[child].node_type;
            if (child_type == AST_NODE_TYPE_IF_STATEMENT) {
                pushed = child;
            } else if (child_type == AST_NODE_TYPE_MATCH_STATEMENT) {
                is_ok = x64_match_start_write(builder, child);
                pushed = child;
            } else {
                is_ok = x64_statement_write(builder, procedure, child);
            }
        }
        if (is_ok == false) {
            return false;
        }
        if (pushed == AST_NODE_INDEX_NONE) {
            continue;
        }

        if (frame_count == X64_STATEMENT_DEPTH_MAX) {
            return x64_fail_unsupported(builder);
        }
        Ast_Node_Index pushed_first = builder->nodes[pushed].first_child;
        if (builder->nodes[pushed].node_type == AST_NODE_TYPE_MATCH_STATEMENT) {
            // The cases follow the subject, which was written already.
            pushed_first =
                builder->nodes[ast_match_subject(builder->nodes, pushed)].next_sibling;
        }
        frames[frame_count] = (X64_Statement_Frame){
            .node = pushed,
            .next = pushed_first,
            .end_jumps = X64_JUMP_NONE,
            .false_jumps = X64_JUMP_NONE,
        };
        frame_count += 1;
    }

    assert(frame_count == 0);
    return true;
}

/**
 * Writes what comes after the block `block` of the if statement or the match statement of `frame`,
 * when that has more branches: a jump to its end, over the other branches, and then the place
 * that the jump of the condition of the branch goes to when it does not hold, which is the next
 * branch. A block that ends with a `return` never gets to its end, so it needs no jump.
 */
static void x64_block_end(X64_Builder *builder, X64_Statement_Frame *frame,
                          Ast_Node_Index block) {
    assert(builder->nodes[block].node_type == AST_NODE_TYPE_BLOCK);

    Ast_Node_Type node_type = builder->nodes[frame->node].node_type;
    if (node_type != AST_NODE_TYPE_IF_STATEMENT && node_type != AST_NODE_TYPE_MATCH_STATEMENT) {
        return;
    }
    if (frame->next != AST_NODE_INDEX_NONE) {
        Ast_Node_Index last = builder->nodes[block].last_child;
        if (builder->nodes[last].node_type != AST_NODE_TYPE_RETURN_STATEMENT) {
            x64_jump_write(builder, X64_CONDITION_ALWAYS, &frame->end_jumps);
        }
    }
    x64_jumps_resolve(builder, frame->false_jumps);
    frame->false_jumps = X64_JUMP_NONE;
}

/**
 * Writes the condition `condition` of an if statement, which must be a `Bool`, and a jump that is
 * taken when it is `false`, `test rax, rax` and `je`, which joins the chain `false_jumps`. A
 * condition that is not the first has a line of its own for a debugger, the line of its
 * `else if`. Returns false after it recorded an error.
 */
static bool x64_condition_write(X64_Builder *builder, Ast_Node_Index condition,
                                uint32_t *false_jumps) {
    X64_Type bool_type = x64_type_make(X64_TYPE_KIND_BOOL);
    if (x64_type_accepts(builder, bool_type, condition) == false) {
        return x64_fail(builder, X64_BUILD_STATUS_CONDITION_TYPE_INVALID,
                        x64_expression_subject(builder, condition));
    }
    x64_debug_row_add(builder, X64_DEBUG_ROW_KIND_LINE, builder->nodes[condition].source_offset);
    if (x64_expression_write(builder, condition, bool_type, true) == false) {
        return false;
    }
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP,
                     x64_stack_displacement(builder, builder->stack_words - 1, 1));
    x64_stack_pop(builder, 1);
    x64_write_registers(builder, X64_OPCODE_TEST, X64_REGISTER_RAX, X64_REGISTER_RAX);
    x64_jump_write(builder, X64_CONDITION_EQUAL, false_jumps);
    return true;
}

/**
 * Checks the match statement `match` and writes its start: the subject, which is stored in the
 * place of the statement in the frame, see `x64_procedure_layout`, so that it is worked out once
 * and every case compares the same value. The subject is an `Int`, every pattern is a constant of
 * that type or a range of them, which has a number in it, and no two patterns match a value in
 * common, so the order of the cases does not change what the statement does. Returns false after
 * it recorded an error.
 */
static bool x64_match_start_write(X64_Builder *builder, Ast_Node_Index match) {
    assert(builder->nodes[match].node_type == AST_NODE_TYPE_MATCH_STATEMENT);

    Ast_Node_Index subject = ast_match_subject(builder->nodes, match);
    X64_Type subject_type = builder->layouts[match].type;
    if (subject_type.kind != X64_TYPE_KIND_INT) {
        if (subject_type.kind == X64_TYPE_KIND_NONE &&
            builder->nodes[subject].node_type != AST_NODE_TYPE_CALL) {
            return x64_fail_unsupported(builder);
        }
        return x64_fail(builder, X64_BUILD_STATUS_MATCH_SUBJECT_TYPE_INVALID,
                        x64_expression_subject(builder, subject));
    }

    // The analysis allows `partial` as the only attribute, once, so it is the first child if any,
    // and it is only for a match of an enum.
    Ast_Node_Index attribute = builder->nodes[match].first_child;
    if (builder->nodes[attribute].node_type == AST_NODE_TYPE_ATTRIBUTE) {
        return x64_fail(builder, X64_BUILD_STATUS_MATCH_PARTIAL_INVALID,
                        builder->nodes[attribute].payload.attribute.name);
    }

    Ast_Node_Index first_case = builder->nodes[subject].next_sibling;
    Ast_Node_Index match_case = first_case;
    // Every case is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && match_case != AST_NODE_INDEX_NONE; i += 1) {
        Ast_Node_Index pattern = ast_match_case_pattern(builder->nodes, match_case);
        Str keyword = builder->nodes[match_case].payload.match.keyword;
        if (pattern == AST_NODE_INDEX_NONE) {
            // The parser keeps the default case last.
            break;
        }
        X64_Match_Pattern checked = x64_match_pattern(builder, subject_type, pattern);
        if (checked.status != X64_BUILD_STATUS_OK) {
            return x64_fail(builder, checked.status, keyword);
        }
        Ast_Node_Index earlier = first_case;
        for (size_t j = 0; j < i; j += 1) {
            X64_Match_Pattern other = x64_match_pattern(
                builder, subject_type, ast_match_case_pattern(builder->nodes, earlier));
            assert(other.status == X64_BUILD_STATUS_OK);
            if (other.low <= checked.high && checked.low <= other.high) {
                return x64_fail(builder, X64_BUILD_STATUS_MATCH_CASE_DUPLICATE, keyword);
            }
            earlier = builder->nodes[earlier].next_sibling;
        }
        match_case = builder->nodes[match_case].next_sibling;
    }

    if (x64_expression_write(builder, subject, subject_type, true) == false) {
        return false;
    }
    return x64_value_store(builder, subject_type,
                           -(int32_t)builder->layouts[match].frame_offset);
}

/**
 * Returns what `pattern`, the pattern of a case of a match statement whose subject is of the type
 * `subject`, matches: the numbers from `low` to `high`, both included, which are one number for a
 * pattern that is not a range. A pattern is a constant of the type of the subject, or a range of
 * them, which must have a number in it. The status says what is wrong otherwise.
 */
static X64_Match_Pattern x64_match_pattern(X64_Builder const *builder, X64_Type subject,
                                           Ast_Node_Index pattern) {
    assert(pattern < builder->node_count);
    assert(subject.kind == X64_TYPE_KIND_INT);

    X64_Match_Pattern result = {
        .low = 0,
        .high = 0,
        .status = X64_BUILD_STATUS_OK,
    };
    Ast_Node const *node = &builder->nodes[pattern];
    Ast_Node_Index ends[2] = {pattern, pattern};
    if (node->node_type == AST_NODE_TYPE_RANGE) {
        ends[0] = node->first_child;
        ends[1] = node->last_child;
    }
    for (size_t i = 0; i < 2; i += 1) {
        if (x64_is_constant(builder, ends[i]) == false) {
            result.status = X64_BUILD_STATUS_MATCH_PATTERN_NOT_CONSTANT;
            return result;
        }
        if (x64_type_accepts(builder, subject, ends[i]) == false) {
            result.status = X64_BUILD_STATUS_MATCH_PATTERN_TYPE_MISMATCH;
            return result;
        }
    }
    result.low = builder->constant_values[ends[0]].integer;
    result.high = builder->constant_values[ends[1]].integer;
    if (node->node_type != AST_NODE_TYPE_RANGE) {
        return result;
    }
    // The end of an exclusive range is above its last number, so it is above the first one too,
    // and one less than it does not overflow.
    if (node->payload.range.is_inclusive) {
        if (result.high < result.low) {
            result.status = X64_BUILD_STATUS_MATCH_RANGE_EMPTY;
        }
    } else if (result.high <= result.low) {
        result.status = X64_BUILD_STATUS_MATCH_RANGE_EMPTY;
    } else {
        result.high -= 1;
    }
    return result;
}

/**
 * Writes the case `first` of the match statement of `frame`, and the cases after it that have no
 * block of their own and share the block of the last of them: the subject is compared with the
 * pattern of each, and a pattern that matches jumps to the block, which comes after the
 * comparisons, and when none does the jump over the block goes to the next case. A range compares the subject
 * with its two ends, with the signed order of an `Int`. The default case has no pattern, and its
 * block always runs when it is reached. The next case of the frame is set to the one after the
 * block. Returns the block, which the caller writes next.
 */
static Ast_Node_Index x64_match_case_write(X64_Builder *builder, X64_Statement_Frame *frame,
                                           Ast_Node_Index first) {
    assert(builder->nodes[first].node_type == AST_NODE_TYPE_MATCH_CASE);

    Ast_Node_Index match = frame->node;
    X64_Type subject_type = builder->layouts[match].type;
    int32_t subject = -(int32_t)builder->layouts[match].frame_offset;
    uint32_t block_jumps = X64_JUMP_NONE;
    bool has_default = false;

    Ast_Node_Index match_case = first;
    Ast_Node_Index block = AST_NODE_INDEX_NONE;
    // Every case is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && match_case != AST_NODE_INDEX_NONE; i += 1) {
        x64_debug_row_add(builder, X64_DEBUG_ROW_KIND_LINE,
                          builder->nodes[match_case].source_offset);
        Ast_Node_Index pattern = ast_match_case_pattern(builder->nodes, match_case);
        if (pattern == AST_NODE_INDEX_NONE) {
            has_default = true;
        } else {
            X64_Match_Pattern checked = x64_match_pattern(builder, subject_type, pattern);
            assert(checked.status == X64_BUILD_STATUS_OK);
            x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP,
                             subject);
            if (checked.low == checked.high) {
                x64_write_load_integer(builder, X64_REGISTER_RCX, checked.low);
                x64_write_registers(builder, X64_OPCODE_CMP, X64_REGISTER_RCX, X64_REGISTER_RAX);
                x64_jump_write(builder, X64_CONDITION_EQUAL, &block_jumps);
            } else {
                // Below the first number the next pattern is tried, and up to the last one the
                // pattern matches.
                uint32_t below_jumps = X64_JUMP_NONE;
                x64_write_load_integer(builder, X64_REGISTER_RCX, checked.low);
                x64_write_registers(builder, X64_OPCODE_CMP, X64_REGISTER_RCX, X64_REGISTER_RAX);
                x64_jump_write(builder, X64_CONDITION_LESS, &below_jumps);
                x64_write_load_integer(builder, X64_REGISTER_RCX, checked.high);
                x64_write_registers(builder, X64_OPCODE_CMP, X64_REGISTER_RCX, X64_REGISTER_RAX);
                x64_jump_write(builder, X64_CONDITION_LESS_EQUAL, &block_jumps);
                x64_jumps_resolve(builder, below_jumps);
            }
        }
        block = ast_match_case_block(builder->nodes, match_case);
        if (block != AST_NODE_INDEX_NONE) {
            break;
        }
        match_case = builder->nodes[match_case].next_sibling;
    }
    // The parser makes sure that the last case has a block.
    assert(block != AST_NODE_INDEX_NONE);

    if (has_default == false) {
        x64_jump_write(builder, X64_CONDITION_ALWAYS, &frame->false_jumps);
    }
    x64_jumps_resolve(builder, block_jumps);
    frame->next = builder->nodes[match_case].next_sibling;
    return block;
}

/**
 * Writes a jump that is taken when `condition` holds, or always for `X64_CONDITION_ALWAYS`, with a
 * distance of 32 bits, to a place that is not known yet. The jump joins the chain `jumps`: the
 * distance of each jump of a chain holds the place of the one before it until the place that they
 * go to is known, and `jumps` is the place of the last one, or `X64_JUMP_NONE` for an empty chain.
 * So the jumps to one place need no memory but the code, see `x64_jumps_resolve`.
 */
static void x64_jump_write(X64_Builder *builder, byte condition, uint32_t *jumps) {
    assert(condition <= X64_CONDITION_ALWAYS);

    if (condition == X64_CONDITION_ALWAYS) {
        x64_write_byte(&builder->writer, 0xE9);
    } else {
        x64_write_byte(&builder->writer, X64_OPCODE_TWO_BYTE);
        x64_write_byte(&builder->writer, (byte)(0x80 | condition));
    }
    size_t place = builder->writer.length - builder->text_start;
    assert(place < X64_JUMP_NONE);
    x64_write_u32(&builder->writer, *jumps);
    *jumps = (uint32_t)place;
}

/**
 * Makes every jump of the chain `jumps` go to the place in the code that is written next, see
 * `x64_jump_write`. A writer that overflowed does not have the places, and its file is not handed
 * out, so then the chain is not followed.
 */
static void x64_jumps_resolve(X64_Builder *builder, uint32_t jumps) {
    X64_Writer *writer = &builder->writer;
    size_t target = writer->length - builder->text_start;
    uint32_t place = jumps;
    // Each jump of the chain is in the code before the last one, so there are fewer jumps than
    // bytes of the code.
    for (size_t i = 0; i <= writer->length && place != X64_JUMP_NONE; i += 1) {
        size_t offset = builder->text_start + place;
        if (writer->is_overflowed || offset + 4 > writer->length) {
            return;
        }
        byte const *distance = writer->bytes + offset;
        uint32_t previous = (uint32_t)distance[0] | ((uint32_t)distance[1] << 8) |
                            ((uint32_t)distance[2] << 16) | ((uint32_t)distance[3] << 24);
        assert(previous == X64_JUMP_NONE || previous < place);
        x64_patch_u32(writer, offset, (uint32_t)(target - (place + 4)));
        place = previous;
    }
}

/**
 * Writes the statement `statement` of `procedure`, which has no blocks. Returns false after it
 * recorded an error.
 */
static bool x64_statement_write(X64_Builder *builder, Ast_Node_Index procedure,
                                Ast_Node_Index statement) {
    assert(statement < builder->node_count);
    assert(builder->stack_words == 0);

    Ast_Node const *statement_node = &builder->nodes[statement];
    switch (statement_node->node_type) {
    case AST_NODE_TYPE_ADDITION_ASSIGNMENT:
    case AST_NODE_TYPE_ASSIGNMENT:
    case AST_NODE_TYPE_DIVISION_ASSIGNMENT:
    case AST_NODE_TYPE_MULTIPLICATION_ASSIGNMENT:
    case AST_NODE_TYPE_SUBTRACTION_ASSIGNMENT:
        return x64_assignment_write(builder, statement);
    case AST_NODE_TYPE_CALL_STATEMENT:
        return x64_expression_write(builder, statement_node->first_child,
                                    x64_type_make(X64_TYPE_KIND_NONE), false);
    case AST_NODE_TYPE_RETURN_STATEMENT:
        return x64_return_write(builder, procedure, statement);
    case AST_NODE_TYPE_VARIABLE_DEFINITION:
        return x64_variable_definition_write(builder, statement);
    default:
        break;
    }
    return x64_fail_unsupported(builder);
}

/**
 * Writes the end of a procedure: the temporary strings that it made are given back when it made
 * any, with `mov r11, [rbp - 8]` and `mov [used], r11`, which leave `rax`, the result, alone.
 * Then `leave` and `ret` give the stack back and return.
 */
static void x64_epilogue_write(X64_Builder *builder) {
    if (builder->is_temporary_used) {
        x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_R11, X64_REGISTER_RBP,
                         -(int32_t)X64_FRAME_TEMPORARY_OFFSET_BYTES);
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
 * Writes a `return` statement: the value is worked out on the stack of values and goes to `rax`,
 * which is where the caller looks for it, and the end of the procedure gives the stack back and
 * returns. Returns false after it recorded an error.
 */
static bool x64_return_write(X64_Builder *builder, Ast_Node_Index procedure,
                             Ast_Node_Index statement) {
    assert(builder->nodes[statement].node_type == AST_NODE_TYPE_RETURN_STATEMENT);

    Str name = builder->nodes[builder->definition].payload.constant_definition.name;
    X64_Type return_type = x64_return_type(builder, procedure);
    if (return_type.kind == X64_TYPE_KIND_NONE) {
        return x64_fail(builder, X64_BUILD_STATUS_RETURN_TYPE_MISSING, name);
    }

    Ast_Node_Index value = builder->nodes[statement].first_child;
    X64_Type value_type = x64_expression_type_for(builder, value, return_type);
    if (value_type.kind == X64_TYPE_KIND_NONE &&
        builder->nodes[value].node_type != AST_NODE_TYPE_CALL) {
        // An expression that the target does not build has no type.
        return x64_fail_unsupported(builder);
    }
    if (x64_type_accepts(builder, return_type, value) == false) {
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
    if (return_type.kind == X64_TYPE_KIND_C_STR &&
        builder->bindings[value].kind == SEMANTIC_BINDING_KIND_VARIABLE) {
        return x64_fail_unsupported(builder);
    }

    if (x64_expression_write(builder, value, return_type, true) == false) {
        return false;
    }
    assert(x64_type_is_scalar(return_type));
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP,
                     x64_stack_displacement(builder, 0, 1));
    x64_stack_pop(builder, 1);
    x64_epilogue_write(builder);
    return true;
}

/**
 * Writes the definition of a variable, `name := value`, `name : Type = value` or `name : Type`,
 * which stores the value in the place of the variable. Without a value the variable gets the
 * default value of its type, which is 0 for an `Int`, `false` for a `Bool` and the address 0 for
 * a `C_Str`. Returns false after it recorded an error.
 */
static bool x64_variable_definition_write(X64_Builder *builder, Ast_Node_Index statement) {
    assert(builder->nodes[statement].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION);

    Str name = builder->nodes[statement].payload.variable_definition.name;
    Ast_Node_Index type_node = x64_variable_type_node(builder, statement);
    Ast_Node_Index value = builder->nodes[statement].first_child;
    X64_Type type = builder->layouts[statement].type;
    int32_t displacement = -(int32_t)builder->layouts[statement].frame_offset;
    if (type_node != AST_NODE_INDEX_NONE) {
        if (type.kind == X64_TYPE_KIND_NONE) {
            Str subject = name;
            if (builder->nodes[type_node].node_type == AST_NODE_TYPE_TYPE_NAME) {
                subject = builder->nodes[type_node].payload.type_name.name;
            }
            return x64_fail(builder, X64_BUILD_STATUS_TYPE_UNSUPPORTED, subject);
        }
        value = builder->nodes[type_node].next_sibling;
    }

    if (value == AST_NODE_INDEX_NONE) {
        // The default value of every type of the target is zero in each of its words.
        x64_write_load_integer(builder, X64_REGISTER_RAX, 0);
        for (size_t i = 0; i < x64_type_word_count(type); i += 1) {
            x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP,
                             displacement + (int32_t)(i * X64_WORD_SIZE_BYTES));
        }
        return true;
    }

    if (type.kind == X64_TYPE_KIND_NONE) {
        // A variable without a type takes the type of its value, which the target does not build
        // when it has none. The call of a procedure that returns nothing has none either, which
        // the call reports as a call without a result.
        if (builder->nodes[value].node_type != AST_NODE_TYPE_CALL) {
            return x64_fail_unsupported(builder);
        }
        return x64_expression_write(builder, value, type, true);
    }
    if (type_node != AST_NODE_INDEX_NONE && x64_type_accepts(builder, type, value) == false) {
        return x64_fail(builder, X64_BUILD_STATUS_VARIABLE_TYPE_MISMATCH, name);
    }
    if (x64_expression_write(builder, value, type, true) == false) {
        return false;
    }
    return x64_value_store(builder, type, displacement);
}

/**
 * Writes an assignment: `name = value`, which stores the value in the place of the variable, or
 * one of `+=`, `-=`, `*=` and `/=`, which works out the variable and the value with the operator
 * and stores the result. Only a variable of the procedure is assigned to: a global variable, a
 * field, an element and what a pointer points to are not built. Returns false after it recorded
 * an error.
 */
static bool x64_assignment_write(X64_Builder *builder, Ast_Node_Index statement) {
    assert(ast_node_type_is_assignment(builder->nodes[statement].node_type));

    Ast_Node_Type node_type = builder->nodes[statement].node_type;
    Ast_Node_Index target = builder->nodes[statement].first_child;
    Ast_Node_Index value = builder->nodes[target].next_sibling;
    Semantic_Binding binding = builder->bindings[target];
    if (builder->nodes[target].node_type != AST_NODE_TYPE_IDENTIFIER ||
        binding.kind != SEMANTIC_BINDING_KIND_VARIABLE) {
        return x64_fail_unsupported(builder);
    }

    // The definition of the variable is above, and was checked when it was written.
    X64_Type type = builder->layouts[binding.target].type;
    assert(type.kind != X64_TYPE_KIND_NONE);
    int32_t displacement = -(int32_t)builder->layouts[binding.target].frame_offset;
    Str name = builder->nodes[target].payload.identifier.name;

    if (node_type == AST_NODE_TYPE_ASSIGNMENT) {
        if (x64_type_accepts(builder, type, value) == false) {
            // An expression that the target does not build has no type.
            if (x64_expression_type_for(builder, value, type).kind == X64_TYPE_KIND_NONE &&
                builder->nodes[value].node_type != AST_NODE_TYPE_CALL) {
                return x64_fail_unsupported(builder);
            }
            return x64_fail(builder, X64_BUILD_STATUS_VARIABLE_TYPE_MISMATCH, name);
        }
        if (x64_expression_write(builder, value, type, true) == false) {
            return false;
        }
        return x64_value_store(builder, type, displacement);
    }

    // The operators work on an `Int` only, the variable and the value alike.
    X64_Type int_type = x64_type_make(X64_TYPE_KIND_INT);
    if (type.kind != X64_TYPE_KIND_INT) {
        return x64_fail(builder, X64_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID, name);
    }
    if (x64_type_accepts(builder, int_type, value) == false) {
        return x64_fail(builder, X64_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID,
                        x64_expression_subject(builder, value));
    }
    if (x64_expression_write(builder, value, int_type, true) == false) {
        return false;
    }

    // `mov rax, [variable]` and `mov rcx, value`, the operator, and `mov [variable], rax`.
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP, displacement);
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RCX, X64_REGISTER_RBP,
                     x64_stack_displacement(builder, 0, 1));
    x64_stack_pop(builder, 1);
    switch (node_type) {
    case AST_NODE_TYPE_ADDITION_ASSIGNMENT:
        x64_write_registers(builder, X64_OPCODE_ADD, X64_REGISTER_RCX, X64_REGISTER_RAX);
        break;
    case AST_NODE_TYPE_DIVISION_ASSIGNMENT:
        x64_write_divide(builder, X64_REGISTER_RCX);
        break;
    case AST_NODE_TYPE_MULTIPLICATION_ASSIGNMENT:
        x64_write_multiply(builder, X64_REGISTER_RAX, X64_REGISTER_RCX);
        break;
    case AST_NODE_TYPE_SUBTRACTION_ASSIGNMENT:
        x64_write_registers(builder, X64_OPCODE_SUB, X64_REGISTER_RCX, X64_REGISTER_RAX);
        break;
    default:
        assert(false);
        break;
    }
    x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, displacement);
    return true;
}

/**
 * Moves the value of `type` on the top of the stack of values to the memory at
 * `rbp + displacement`, one word at a time through `rax`, and takes it off the stack. Returns
 * true, so that a caller can return it.
 */
static bool x64_value_store(X64_Builder *builder, X64_Type type, int32_t displacement) {
    size_t word_count = x64_type_word_count(type);
    assert(builder->stack_words >= word_count);

    int32_t source = x64_stack_displacement(builder, builder->stack_words - word_count,
                                            word_count);
    for (size_t i = 0; i < word_count; i += 1) {
        int32_t offset = (int32_t)(i * X64_WORD_SIZE_BYTES);
        x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP,
                         source + offset);
        x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP,
                         displacement + offset);
    }
    x64_stack_pop(builder, word_count);
    return true;
}

/**
 * Writes the expression `expression`, which leaves its value on the top of the stack of values,
 * as a value of `wanted` when it is an untyped constant, or nothing when `is_result_used` is not
 * set and it is a call. The tree is walked without recursion, with the path from the root to the
 * current node in a fixed array: a node is entered, then its children are written one after the
 * other, each of which leaves its value on the stack, and then the node itself, which takes the
 * values of its children off the stack and leaves its own. A constant is its value, so its
 * children are not written. Returns false after it recorded an error.
 */
static bool x64_expression_write(X64_Builder *builder, Ast_Node_Index expression,
                                 X64_Type wanted, bool is_result_used) {
    assert(expression < builder->node_count);
    assert(builder->stack_words <= UINT32_MAX);

    // The nodes on the path from the root to the current node, which is the last of them.
    X64_Expression_Frame frames[X64_EXPRESSION_DEPTH_MAX];
    size_t frame_count = 1;
    frames[0] = (X64_Expression_Frame){
        .wanted = wanted,
        .node = expression,
        .stack_start = (uint32_t)builder->stack_words,
        .index = 0,
        .mode = X64_MODE_VALUE,
    };

    // False once the children of the last node have been written.
    bool is_descending = true;
    // Every node is entered once and left once, so twice the number of nodes steps are enough.
    for (size_t step = 0; step < builder->node_count * 2 + 2; step += 1) {
        X64_Expression_Frame *frame = &frames[frame_count - 1];

        if (is_descending) {
            Ast_Node_Index first_child = x64_child_first(builder, frame->node);
            if (first_child != AST_NODE_INDEX_NONE) {
                if (frame_count == X64_EXPRESSION_DEPTH_MAX) {
                    return x64_fail(builder, X64_BUILD_STATUS_EXPRESSION_TOO_DEEP,
                                    ast_definition_name(&builder->nodes[builder->definition]));
                }
                frames[frame_count] = (X64_Expression_Frame){
                    .wanted = x64_child_wanted(builder, frame, first_child, 0),
                    .node = first_child,
                    .stack_start = (uint32_t)builder->stack_words,
                    .index = 0,
                    .mode = X64_MODE_VALUE,
                };
                frame_count += 1;
                continue;
            }
        }

        // The children of the node are on the stack, so the node itself is written now. The value
        // of the root is dropped when it is not used, which only a call does without one.
        bool is_root = frame_count == 1;
        if (x64_expression_node_write(builder, frame, is_root == false || is_result_used) ==
            false) {
            return false;
        }
        if (is_root) {
            return true;
        }

        // Go on with the next sibling, or finish the parent when there is none.
        Ast_Node_Index next = builder->nodes[frame->node].next_sibling;
        X64_Expression_Frame const *parent = &frames[frame_count - 2];
        if (next != AST_NODE_INDEX_NONE) {
            uint32_t index = frame->index + 1;
            *frame = (X64_Expression_Frame){
                .wanted = x64_child_wanted(builder, parent, next, index),
                .node = next,
                .stack_start = (uint32_t)builder->stack_words,
                .index = index,
                .mode = X64_MODE_VALUE,
            };
            is_descending = true;
        } else {
            frame_count -= 1;
            is_descending = false;
        }
    }

    // Unreachable: the step budget covers every node twice.
    assert(false);
    return false;
}

/**
 * Returns the first child of `expression` that is written before it, or `AST_NODE_INDEX_NONE` when
 * it has none: a constant is its value, and the argument of `str_to_c_str` is copied by the call
 * itself, since it is a string that is known when the program is compiled.
 */
static Ast_Node_Index x64_child_first(X64_Builder const *builder, Ast_Node_Index expression) {
    assert(expression < builder->node_count);

    if (x64_is_constant(builder, expression) || x64_call_is_str_to_c_str(builder, expression)) {
        return AST_NODE_INDEX_NONE;
    }
    switch (builder->nodes[expression].node_type) {
    case AST_NODE_TYPE_ADDITION:
    case AST_NODE_TYPE_AND:
    case AST_NODE_TYPE_CALL:
    case AST_NODE_TYPE_COMPARISON:
    case AST_NODE_TYPE_DIVISION:
    case AST_NODE_TYPE_MULTIPLICATION:
    case AST_NODE_TYPE_NOT:
    case AST_NODE_TYPE_OR:
    case AST_NODE_TYPE_SUBTRACTION:
        return builder->nodes[expression].first_child;
    default:
        break;
    }
    return AST_NODE_INDEX_NONE;
}

/**
 * Returns the type that `parent` wants its child `child` at `index` to have, which an untyped
 * constant becomes: the type of the parameter for an argument, and `Int` and `Bool` for the
 * operands of the operators that take them. The kind is `X64_TYPE_KIND_NONE` where the parent does
 * not say, as for the operands of a comparison, which take the type that they have.
 */
static X64_Type x64_child_wanted(X64_Builder const *builder, X64_Expression_Frame const *parent,
                                 Ast_Node_Index child, size_t index) {
    assert(child < builder->node_count);

    switch (builder->nodes[parent->node].node_type) {
    case AST_NODE_TYPE_ADDITION:
    case AST_NODE_TYPE_DIVISION:
    case AST_NODE_TYPE_MULTIPLICATION:
    case AST_NODE_TYPE_SUBTRACTION:
        return x64_type_make(X64_TYPE_KIND_INT);
    case AST_NODE_TYPE_AND:
    case AST_NODE_TYPE_NOT:
    case AST_NODE_TYPE_OR:
        return x64_type_make(X64_TYPE_KIND_BOOL);
    case AST_NODE_TYPE_CALL: {
        Ast_Node_Index procedure = x64_call_procedure(builder, parent->node);
        if (procedure != AST_NODE_INDEX_NONE && index < x64_parameter_count(builder, procedure)) {
            return x64_parameter_type(builder, procedure, index);
        }
        break;
    }
    default:
        break;
    }
    return x64_type_make(X64_TYPE_KIND_NONE);
}

/**
 * Writes the node of `frame`, whose children are on the top of the stack of values, from
 * `frame->stack_start` on: it takes them off and leaves its own value there. `is_result_used` is
 * not set for a call whose value is dropped, which leaves nothing. Returns false after it recorded
 * an error.
 */
static bool x64_expression_node_write(X64_Builder *builder, X64_Expression_Frame const *frame,
                                      bool is_result_used) {
    Ast_Node_Index node = frame->node;
    Ast_Node const *expression = &builder->nodes[node];

    if (x64_is_constant(builder, node)) {
        return x64_constant_write(builder, frame);
    }

    switch (expression->node_type) {
    case AST_NODE_TYPE_ADDITION:
    case AST_NODE_TYPE_AND:
    case AST_NODE_TYPE_DIVISION:
    case AST_NODE_TYPE_MULTIPLICATION:
    case AST_NODE_TYPE_NOT:
    case AST_NODE_TYPE_OR:
    case AST_NODE_TYPE_SUBTRACTION:
        return x64_operation_write(builder, frame);
    case AST_NODE_TYPE_CALL:
        if (x64_call_is_str_to_c_str(builder, node)) {
            return x64_str_to_c_str_write(builder, node);
        }
        return x64_call_write(builder, frame, is_result_used);
    case AST_NODE_TYPE_COMPARISON:
        return x64_comparison_write(builder, frame);
    case AST_NODE_TYPE_IDENTIFIER: {
        // The value of a parameter is in the words of the arguments, and that of a variable in its
        // place in the frame.
        Semantic_Binding binding = builder->bindings[node];
        int32_t source = 0;
        X64_Type type = x64_expression_type(builder, node);
        if (binding.kind == SEMANTIC_BINDING_KIND_PARAMETER) {
            Ast_Node_Index procedure = builder->nodes[builder->definition].first_child;
            source = (int32_t)(X64_ARGUMENT_HOME_OFFSET_BYTES +
                               x64_argument_word_offset(builder, procedure, binding.position) *
                                   X64_WORD_SIZE_BYTES);
        } else if (binding.kind == SEMANTIC_BINDING_KIND_VARIABLE) {
            source = -(int32_t)builder->layouts[binding.target].frame_offset;
        } else {
            return x64_fail_unsupported(builder);
        }
        if (type.kind == X64_TYPE_KIND_NONE) {
            return x64_fail_unsupported(builder);
        }
        size_t word_count = x64_type_word_count(type);
        int32_t target = x64_stack_push(builder, word_count);
        for (size_t i = 0; i < word_count; i += 1) {
            int32_t offset = (int32_t)(i * X64_WORD_SIZE_BYTES);
            x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP,
                             source + offset);
            x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP,
                             target + offset);
        }
        return true;
    }
    default:
        break;
    }
    return x64_fail_unsupported(builder);
}

/**
 * Writes the value of the constant of `frame`, which the analysis worked out, as a value of the
 * type that its place wants: a number or a truth value is one `mov` of its 64 bits, and a string is
 * a `C_Str`, a copy in the temporary storage, which is the only string that the target has. Returns
 * false after it recorded an error.
 */
static bool x64_constant_write(X64_Builder *builder, X64_Expression_Frame const *frame) {
    Constant_Value constant = builder->constant_values[frame->node];
    assert(constant.status == CONSTANT_STATUS_OK);

    if (constant.string != AST_NODE_INDEX_NONE) {
        if (frame->wanted.kind != X64_TYPE_KIND_C_STR ||
            constant.type != CONSTANT_TYPE_UNTYPED_STRING) {
            return x64_fail_unsupported(builder);
        }
        if (x64_c_str_write(builder, constant.string, X64_REGISTER_RAX) == false) {
            return false;
        }
    } else {
        X64_Type type = x64_expression_type(builder, frame->node);
        if (type.kind == X64_TYPE_KIND_NONE) {
            return x64_fail_unsupported(builder);
        }
        x64_write_load_integer(builder, X64_REGISTER_RAX, constant.integer);
    }
    int32_t target = x64_stack_push(builder, 1);
    x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, target);
    return true;
}

/**
 * Writes an arithmetic or logical operator, whose operands are on the stack of values: `+`, `-`,
 * `*` and `/` of two `Int` values, and `and`, `or` and `not` of `Bool` values. The result is an
 * `Int` or a `Bool` in the place of the first operand. Addition, subtraction and multiplication
 * wrap around, which is what the instructions do, and `idiv` traps when the divisor is zero or the
 * quotient does not fit, which stops the program. Every operand is evaluated, and `and` and `or`
 * are the instructions of the bits, which are right because a `Bool` is always 0 or 1. Returns
 * false after it recorded an error.
 */
static bool x64_operation_write(X64_Builder *builder, X64_Expression_Frame const *frame) {
    Ast_Node_Index node = frame->node;
    Ast_Node_Type node_type = builder->nodes[node].node_type;

    bool is_logical = node_type == AST_NODE_TYPE_AND || node_type == AST_NODE_TYPE_OR ||
                      node_type == AST_NODE_TYPE_NOT;
    X64_Type operand_type = x64_type_make(X64_TYPE_KIND_INT);
    X64_Build_Status status = X64_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID;
    if (is_logical) {
        operand_type = x64_type_make(X64_TYPE_KIND_BOOL);
        status = X64_BUILD_STATUS_LOGICAL_OPERAND_TYPE_INVALID;
    }
    Ast_Node_Index left = builder->nodes[node].first_child;
    Ast_Node_Index right = builder->nodes[left].next_sibling;
    if (x64_type_accepts(builder, operand_type, left) == false) {
        return x64_fail(builder, status, x64_expression_subject(builder, left));
    }
    if (right != AST_NODE_INDEX_NONE && x64_type_accepts(builder, operand_type, right) == false) {
        return x64_fail(builder, status, x64_expression_subject(builder, right));
    }

    // The first operand goes to `rax` and the second to `rcx`, and the result is in `rax`.
    int32_t first = x64_stack_displacement(builder, frame->stack_start, 1);
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP, first);
    if (node_type == AST_NODE_TYPE_NOT) {
        // `not` flips the lowest bit, which is the whole `Bool`.
        x64_write_load_integer(builder, X64_REGISTER_RCX, 1);
    } else {
        x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RCX, X64_REGISTER_RBP,
                         x64_stack_displacement(builder, frame->stack_start + 1, 1));
    }

    switch (node_type) {
    case AST_NODE_TYPE_ADDITION:
        x64_write_registers(builder, X64_OPCODE_ADD, X64_REGISTER_RCX, X64_REGISTER_RAX);
        break;
    case AST_NODE_TYPE_AND:
        x64_write_registers(builder, X64_OPCODE_AND, X64_REGISTER_RCX, X64_REGISTER_RAX);
        break;
    case AST_NODE_TYPE_DIVISION:
        x64_write_divide(builder, X64_REGISTER_RCX);
        break;
    case AST_NODE_TYPE_MULTIPLICATION:
        x64_write_multiply(builder, X64_REGISTER_RAX, X64_REGISTER_RCX);
        break;
    case AST_NODE_TYPE_NOT:
        x64_write_registers(builder, X64_OPCODE_XOR, X64_REGISTER_RCX, X64_REGISTER_RAX);
        break;
    case AST_NODE_TYPE_OR:
        x64_write_registers(builder, X64_OPCODE_OR, X64_REGISTER_RCX, X64_REGISTER_RAX);
        break;
    case AST_NODE_TYPE_SUBTRACTION:
        x64_write_registers(builder, X64_OPCODE_SUB, X64_REGISTER_RCX, X64_REGISTER_RAX);
        break;
    default:
        assert(false);
        break;
    }

    x64_stack_pop(builder, builder->stack_words - frame->stack_start);
    int32_t target = x64_stack_push(builder, 1);
    assert(target == first);
    x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, target);
    return true;
}

/**
 * Writes a comparison, whose operands are on the stack of values: two `Int` values with any of the
 * six operators, whose order is signed, or two `Bool` values with `==` and `not=`. `cmp` compares
 * them and `set` makes the `Bool` of the condition, which `movzx` widens to the 64 bits of the
 * value. Returns false after it recorded an error.
 */
static bool x64_comparison_write(X64_Builder *builder, X64_Expression_Frame const *frame) {
    Ast_Node_Index node = frame->node;
    Ast_Node_Index left = builder->nodes[node].first_child;
    Ast_Node_Index right = builder->nodes[left].next_sibling;
    Ast_Comparison_Operator operator = builder->nodes[node].payload.comparison.operator;

    // An untyped operand takes the type of the other one, so the type of the comparison is the
    // type of the operand that has one.
    X64_Type type = x64_expression_type(builder, left);
    if (x64_is_constant(builder, left) && x64_is_constant(builder, right) == false) {
        type = x64_expression_type(builder, right);
    }
    bool is_equality = operator == AST_COMPARISON_OPERATOR_EQUAL ||
                       operator == AST_COMPARISON_OPERATOR_NOT_EQUAL;
    bool is_valid = type.kind == X64_TYPE_KIND_INT ||
                    (type.kind == X64_TYPE_KIND_BOOL && is_equality);
    if (is_valid == false || x64_type_accepts(builder, type, left) == false ||
        x64_type_accepts(builder, type, right) == false) {
        return x64_fail(builder, X64_BUILD_STATUS_COMPARISON_OPERAND_TYPE_INVALID,
                        x64_expression_subject(builder, node));
    }

    byte condition = X64_CONDITION_EQUAL;
    switch (operator) {
    case AST_COMPARISON_OPERATOR_EQUAL:
        condition = X64_CONDITION_EQUAL;
        break;
    case AST_COMPARISON_OPERATOR_GREATER:
        condition = X64_CONDITION_GREATER;
        break;
    case AST_COMPARISON_OPERATOR_GREATER_EQUAL:
        condition = X64_CONDITION_GREATER_EQUAL;
        break;
    case AST_COMPARISON_OPERATOR_LESS:
        condition = X64_CONDITION_LESS;
        break;
    case AST_COMPARISON_OPERATOR_LESS_EQUAL:
        condition = X64_CONDITION_LESS_EQUAL;
        break;
    case AST_COMPARISON_OPERATOR_NOT_EQUAL:
        condition = X64_CONDITION_NOT_EQUAL;
        break;
    }

    // `mov rax, left`, `mov rcx, right`, `cmp rax, rcx` and the `Bool` of the condition in `rax`.
    int32_t first = x64_stack_displacement(builder, frame->stack_start, 1);
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP, first);
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RCX, X64_REGISTER_RBP,
                     x64_stack_displacement(builder, frame->stack_start + 1, 1));
    x64_write_registers(builder, X64_OPCODE_CMP, X64_REGISTER_RCX, X64_REGISTER_RAX);
    x64_write_set(builder, condition);

    x64_stack_pop(builder, builder->stack_words - frame->stack_start);
    int32_t target = x64_stack_push(builder, 1);
    assert(target == first);
    x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, target);
    return true;
}

/**
 * Writes a call, whose arguments are on the stack of values from `frame->stack_start` on, in the
 * order of the parameters. Each argument is a number of words, and the words of all of them are
 * passed in order: the first four in `rcx`, `rdx`, `r8` and `r9`, and the others on the stack, at
 * the bottom of the frame above the 32 bytes of space that the callee may keep the four in. The
 * stack words are stored first, through `rax`, which no argument is in, and then the registers are
 * loaded. A foreign procedure is called through its place in the table of imports. The arguments
 * are taken off the stack of values, and the result, which is in `rax`, is put on it when
 * `is_result_used` is set. Returns false after it recorded an error.
 */
static bool x64_call_write(X64_Builder *builder, X64_Expression_Frame const *frame,
                           bool is_result_used) {
    Ast_Node_Index call = frame->node;
    assert(builder->nodes[call].node_type == AST_NODE_TYPE_CALL);

    Str name = builder->nodes[call].payload.call.name;
    Semantic_Binding binding = builder->bindings[call];
    // A call through a variable that holds a pointer to a procedure is not built, and neither is a
    // built-in procedure other than `str_to_c_str`.
    if (binding.kind != SEMANTIC_BINDING_KIND_CONSTANT) {
        return x64_fail_unsupported(builder);
    }
    assert(binding.position < builder->constant_count);
    assert(builder->constants[binding.position].definition == binding.target);

    X64_Constant const *callee = &builder->constants[binding.position];
    // The analysis does not bind a call to a value, such as a string, which is not a procedure.
    assert(callee->kind != X64_CONSTANT_KIND_STRING);
    assert(callee->kind != X64_CONSTANT_KIND_VALUE);
    Ast_Node_Index procedure = builder->nodes[callee->definition].first_child;

    size_t parameter_count = x64_parameter_count(builder, procedure);
    Ast_Node_Index argument = builder->nodes[call].first_child;
    for (size_t i = 0; i < parameter_count; i += 1) {
        if (argument == AST_NODE_INDEX_NONE) {
            return x64_fail(builder, X64_BUILD_STATUS_CALL_ARGUMENT_COUNT_MISMATCH, name);
        }
        X64_Type parameter_type = x64_parameter_type(builder, procedure, i);
        if (x64_type_accepts(builder, parameter_type, argument) == false) {
            // An expression that the target does not build has no type.
            if (x64_expression_type_for(builder, argument, parameter_type).kind ==
                    X64_TYPE_KIND_NONE &&
                builder->nodes[argument].node_type != AST_NODE_TYPE_CALL) {
                return x64_fail_unsupported(builder);
            }
            return x64_fail(builder, X64_BUILD_STATUS_CALL_ARGUMENT_TYPE_MISMATCH, name);
        }
        argument = builder->nodes[argument].next_sibling;
    }
    if (argument != AST_NODE_INDEX_NONE) {
        return x64_fail(builder, X64_BUILD_STATUS_CALL_ARGUMENT_COUNT_MISMATCH, name);
    }
    X64_Type return_type = x64_return_type(builder, procedure);
    if (is_result_used && return_type.kind == X64_TYPE_KIND_NONE) {
        return x64_fail(builder, X64_BUILD_STATUS_CALL_RESULT_MISSING, name);
    }

    // The words of the arguments are the words of the stack from where the call started, in the
    // order of the arguments, but the values are at decreasing addresses, so the place of each
    // word is worked out from the place of its argument.
    size_t word_count = x64_parameter_word_count(builder, procedure);
    assert(builder->stack_words - frame->stack_start == word_count);
    if (word_count > X64_ARGUMENT_REGISTER_COUNT &&
        word_count - X64_ARGUMENT_REGISTER_COUNT > builder->outgoing_words_max) {
        builder->outgoing_words_max = word_count - X64_ARGUMENT_REGISTER_COUNT;
    }
    size_t word = 0;
    size_t argument_start = frame->stack_start;
    for (size_t i = 0; i < parameter_count; i += 1) {
        size_t argument_words = x64_type_word_count(x64_parameter_type(builder, procedure, i));
        int32_t source = x64_stack_displacement(builder, argument_start, argument_words);
        for (size_t j = 0; j < argument_words; j += 1) {
            int32_t source_word = source + (int32_t)(j * X64_WORD_SIZE_BYTES);
            if (word < X64_ARGUMENT_REGISTER_COUNT) {
                // The registers are loaded after the stack words, see below.
                word += 1;
                continue;
            }
            x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP,
                             source_word);
            x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RSP,
                             (int32_t)(X64_SHADOW_SPACE_BYTES +
                                       (word - X64_ARGUMENT_REGISTER_COUNT) *
                                           X64_WORD_SIZE_BYTES));
            word += 1;
        }
        argument_start += argument_words;
    }
    word = 0;
    argument_start = frame->stack_start;
    for (size_t i = 0; i < parameter_count && word < X64_ARGUMENT_REGISTER_COUNT; i += 1) {
        size_t argument_words = x64_type_word_count(x64_parameter_type(builder, procedure, i));
        int32_t source = x64_stack_displacement(builder, argument_start, argument_words);
        for (size_t j = 0; j < argument_words && word < X64_ARGUMENT_REGISTER_COUNT; j += 1) {
            x64_write_memory(builder, X64_OPCODE_LOAD, X64_ARGUMENT_REGISTERS[word],
                             X64_REGISTER_RBP, source + (int32_t)(j * X64_WORD_SIZE_BYTES));
            word += 1;
        }
        argument_start += argument_words;
    }

    if (callee->kind == X64_CONSTANT_KIND_FOREIGN) {
        x64_write_call_import(builder, callee->import_slot);
    } else {
        x64_write_call_local(builder, callee->offset);
    }

    x64_stack_pop(builder, word_count);
    if (is_result_used) {
        assert(x64_type_is_scalar(return_type));
        int32_t target = x64_stack_push(builder, 1);
        x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, target);
    }
    return true;
}

/**
 * Writes `str_to_c_str("text")`, which makes a copy of the string with the zero that ends it in the
 * temporary storage, and puts its address on the stack of values. The argument must be a string
 * literal or a constant that is a string, because the target does not have `Str` values, so the
 * string and its size are known when the program is built. Returns false after it recorded an
 * error.
 */
static bool x64_str_to_c_str_write(X64_Builder *builder, Ast_Node_Index call) {
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
    if (x64_c_str_write(builder, literal, X64_REGISTER_RAX) == false) {
        return false;
    }
    int32_t target = x64_stack_push(builder, 1);
    x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, target);
    return true;
}

/**
 * Writes the copy of the string literal `literal` as a `C_Str`, which is what `str_to_c_str` of it
 * and an untyped string where a `C_Str` is wanted both are, and puts its address in
 * `register_number`. The storage is a stack of strings: the number of bytes in use says where the
 * next goes, and it grows by the size of the string, rounded up to 8. The procedure that made the
 * string gives it back when it returns, see `x64_epilogue_write`. The string is known when the
 * program is built, so the copy is a few stores of 8 bytes instead of a loop. Only `rax`, `r10` and
 * `r11` are used. A string that does not fit in what is left stops the program with a breakpoint.
 * Returns false after it recorded an error.
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
 * Returns whether `expression` is written as the value that the analysis worked out for it, see
 * `constant_evaluate`.
 */
static bool x64_is_constant(X64_Builder const *builder, Ast_Node_Index expression) {
    assert(expression < builder->node_count);

    return builder->constant_values[expression].status == CONSTANT_STATUS_OK;
}

/**
 * Returns whether `expression` is a call of a built-in procedure, which the analysis leaves without
 * a binding unless the program defines a procedure of that name, see
 * `semantic_is_built_in_procedure`.
 */
static bool x64_call_is_built_in(X64_Builder const *builder, Ast_Node_Index expression) {
    return builder->nodes[expression].node_type == AST_NODE_TYPE_CALL &&
           builder->bindings[expression].kind == SEMANTIC_BINDING_KIND_NONE &&
           semantic_is_built_in_procedure(builder->nodes[expression].payload.call.name);
}

/**
 * Returns whether `expression` is a call of the built-in procedure `str_to_c_str`.
 */
static bool x64_call_is_str_to_c_str(X64_Builder const *builder, Ast_Node_Index expression) {
    return x64_call_is_built_in(builder, expression) &&
           str_equals(builder->nodes[expression].payload.call.name, STR("str_to_c_str"));
}

/**
 * Returns the `AST_NODE_TYPE_PROCEDURE` that `call` calls, or `AST_NODE_INDEX_NONE` when it is not
 * the call of a procedure of the module, which a built-in procedure is not.
 */
static Ast_Node_Index x64_call_procedure(X64_Builder const *builder, Ast_Node_Index call) {
    assert(builder->nodes[call].node_type == AST_NODE_TYPE_CALL);

    Semantic_Binding binding = builder->bindings[call];
    if (binding.kind != SEMANTIC_BINDING_KIND_CONSTANT) {
        return AST_NODE_INDEX_NONE;
    }
    Ast_Node_Index procedure = builder->nodes[binding.target].first_child;
    if (builder->nodes[procedure].node_type != AST_NODE_TYPE_PROCEDURE) {
        return AST_NODE_INDEX_NONE;
    }
    return procedure;
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
 * Returns the text in the source that an error about `expression` points at: the first name or
 * string in it, which is the leftmost leaf of its tree, or the name of a call. An expression that
 * starts with a number or with `true` or `false` has no such text, so the name of the definition is
 * the closest place that the tree knows.
 */
static Str x64_expression_subject(X64_Builder const *builder, Ast_Node_Index expression) {
    assert(expression < builder->node_count);

    Ast_Node_Index current = expression;
    // Each step goes down one level, so there are fewer steps than nodes.
    for (size_t i = 0; i < builder->node_count; i += 1) {
        Ast_Node const *node = &builder->nodes[current];
        if (node->node_type == AST_NODE_TYPE_CALL || node->first_child == AST_NODE_INDEX_NONE) {
            break;
        }
        current = node->first_child;
    }

    Ast_Node const *leaf = &builder->nodes[current];
    if (leaf->node_type == AST_NODE_TYPE_CALL) {
        return leaf->payload.call.name;
    }
    if (leaf->node_type == AST_NODE_TYPE_IDENTIFIER) {
        return leaf->payload.identifier.name;
    }
    if (leaf->node_type == AST_NODE_TYPE_STRING_LITERAL) {
        return leaf->payload.string_literal.text;
    }
    return ast_definition_name(&builder->nodes[builder->definition]);
}

/**
 * Returns whether a value of the type `wanted` can be given `expression`: the expression has that
 * type, or it is an untyped constant that becomes it there.
 */
static bool x64_type_accepts(X64_Builder const *builder, X64_Type wanted,
                             Ast_Node_Index expression) {
    if (wanted.kind == X64_TYPE_KIND_NONE) {
        return false;
    }
    return x64_type_equals(x64_expression_type_for(builder, expression, wanted), wanted);
}

/**
 * Returns the type that `expression` has where a value of `wanted` is wanted, or
 * `X64_TYPE_KIND_NONE` for no type in particular. It is the type of the expression, except that an
 * untyped string is a `C_Str` where one is wanted: the address of a copy of its bytes with a zero
 * after them. The caller compares the type with `wanted`, so an untyped constant that does not
 * become it keeps the type that it has where nothing else says.
 */
static X64_Type x64_expression_type_for(X64_Builder const *builder, Ast_Node_Index expression,
                                        X64_Type wanted) {
    Constant_Value constant = builder->constant_values[expression];
    if (constant.status == CONSTANT_STATUS_OK && constant.type == CONSTANT_TYPE_UNTYPED_STRING &&
        wanted.kind == X64_TYPE_KIND_C_STR) {
        return wanted;
    }
    return x64_expression_type(builder, expression);
}

/**
 * Returns the type of `expression`, or a type of the kind `X64_TYPE_KIND_NONE` if it has none,
 * which is the case for a call of a procedure that returns nothing and for everything that the
 * target does not build. A constant has the type that it has where nothing else says, see
 * `constant_type_default`, which is `Int` for an untyped integer and `Bool` for an untyped truth
 * value, and the target has no `Byte` or `Str` yet. A variable has the type that the layout of its
 * procedure worked out, see `x64_procedure_layout`.
 */
static X64_Type x64_expression_type(X64_Builder const *builder, Ast_Node_Index expression) {
    assert(expression < builder->node_count);

    Ast_Node const *node = &builder->nodes[expression];
    Constant_Value constant = builder->constant_values[expression];
    if (constant.status == CONSTANT_STATUS_OK) {
        switch (constant_type_default(constant.type)) {
        case CONSTANT_TYPE_BOOL:
            return x64_type_make(X64_TYPE_KIND_BOOL);
        case CONSTANT_TYPE_INT:
            return x64_type_make(X64_TYPE_KIND_INT);
        case CONSTANT_TYPE_BYTE:
        case CONSTANT_TYPE_STR:
        case CONSTANT_TYPE_UNTYPED_BOOL:
        case CONSTANT_TYPE_UNTYPED_INTEGER:
        case CONSTANT_TYPE_UNTYPED_STRING:
            break;
        }
        return x64_type_make(X64_TYPE_KIND_NONE);
    }

    switch (node->node_type) {
    case AST_NODE_TYPE_ADDITION:
    case AST_NODE_TYPE_DIVISION:
    case AST_NODE_TYPE_MULTIPLICATION:
    case AST_NODE_TYPE_SUBTRACTION:
        return x64_type_make(X64_TYPE_KIND_INT);
    case AST_NODE_TYPE_AND:
    case AST_NODE_TYPE_COMPARISON:
    case AST_NODE_TYPE_NOT:
    case AST_NODE_TYPE_OR:
        return x64_type_make(X64_TYPE_KIND_BOOL);
    case AST_NODE_TYPE_CALL: {
        if (x64_call_is_str_to_c_str(builder, expression)) {
            return x64_type_make(X64_TYPE_KIND_C_STR);
        }
        Ast_Node_Index procedure = x64_call_procedure(builder, expression);
        if (procedure == AST_NODE_INDEX_NONE) {
            return x64_type_make(X64_TYPE_KIND_NONE);
        }
        return x64_return_type(builder, procedure);
    }
    case AST_NODE_TYPE_IDENTIFIER: {
        Semantic_Binding binding = builder->bindings[expression];
        if (binding.kind == SEMANTIC_BINDING_KIND_PARAMETER) {
            return x64_type_of_node(builder, builder->nodes[binding.target].first_child);
        }
        if (binding.kind == SEMANTIC_BINDING_KIND_VARIABLE) {
            return builder->layouts[binding.target].type;
        }
        break;
    }
    default:
        break;
    }

    return x64_type_make(X64_TYPE_KIND_NONE);
}

/**
 * Returns the type that a procedure returns, or a type of the kind `X64_TYPE_KIND_NONE` when it
 * returns nothing. `procedure` is an `AST_NODE_TYPE_PROCEDURE`.
 */
static X64_Type x64_return_type(X64_Builder const *builder, Ast_Node_Index procedure) {
    assert(builder->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);

    Str name = ast_procedure_return_type(&builder->nodes[procedure]);
    if (name.bytes == NULL) {
        return x64_type_make(X64_TYPE_KIND_NONE);
    }
    return x64_type_of_name(name);
}

/**
 * Returns a type of `kind` that has nothing more to it.
 */
static X64_Type x64_type_make(X64_Type_Kind kind) {
    return (X64_Type){
        .kind = kind,
    };
}

/**
 * Returns whether two types are the same.
 */
static bool x64_type_equals(X64_Type first, X64_Type second) {
    return first.kind == second.kind;
}

/**
 * Returns whether a value of `type` is one word, which the instructions handle in a register.
 */
static bool x64_type_is_scalar(X64_Type type) {
    return x64_type_word_count(type) == 1;
}

/**
 * Returns the number of words of 8 bytes that a value of `type` takes: one for an `Int`, which is
 * a 64-bit integer, one for a `Bool`, which is 0 or 1 in a word, so that it is passed to and from
 * the C library like an integer, and one for a `C_Str`, which is an address.
 */
static size_t x64_type_word_count(X64_Type type) {
    switch (type.kind) {
    case X64_TYPE_KIND_BOOL:
    case X64_TYPE_KIND_C_STR:
    case X64_TYPE_KIND_INT:
        return 1;
    case X64_TYPE_KIND_NONE:
        break;
    }

    // Unreachable: a type that the target does not have takes no place anywhere.
    assert(false);
    return 1;
}

/**
 * Returns the type that the name `name` stands for, or a type of the kind `X64_TYPE_KIND_NONE` if
 * the target does not have a type of that name.
 */
static X64_Type x64_type_of_name(Str name) {
    if (str_equals(name, STR("Bool"))) {
        return x64_type_make(X64_TYPE_KIND_BOOL);
    }
    if (str_equals(name, STR("C_Str"))) {
        return x64_type_make(X64_TYPE_KIND_C_STR);
    }
    if (str_equals(name, STR("Int"))) {
        return x64_type_make(X64_TYPE_KIND_INT);
    }
    return x64_type_make(X64_TYPE_KIND_NONE);
}

/**
 * Returns the type that the type node `type` stands for, or a type of the kind
 * `X64_TYPE_KIND_NONE` if the target does not have it, which is any type that is not a name yet.
 */
static X64_Type x64_type_of_node(X64_Builder const *builder, Ast_Node_Index type) {
    assert(type < builder->node_count);

    if (builder->nodes[type].node_type != AST_NODE_TYPE_TYPE_NAME) {
        return x64_type_make(X64_TYPE_KIND_NONE);
    }
    return x64_type_of_name(builder->nodes[type].payload.type_name.name);
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

    return x64_type_of_node(builder, builder->nodes[parameter].first_child);
}

/**
 * Returns the number of words of the arguments of `procedure`, which are the words of its
 * parameters, one after the other.
 */
static size_t x64_parameter_word_count(X64_Builder const *builder, Ast_Node_Index procedure) {
    size_t parameter_count = x64_parameter_count(builder, procedure);
    return x64_argument_word_offset(builder, procedure, parameter_count);
}

/**
 * Returns the place of the first word of the parameter at `position` among the words of the
 * arguments of `procedure`: the words of the parameters before it. A procedure keeps the words in
 * memory, from `rbp + 16` on, so the parameter is at `rbp + 16 + 8 * offset`. `position` can be
 * the number of parameters, for the number of words of all of them.
 */
static size_t x64_argument_word_offset(X64_Builder const *builder, Ast_Node_Index procedure,
                                       size_t position) {
    assert(position <= x64_parameter_count(builder, procedure));

    size_t offset = 0;
    for (size_t i = 0; i < position; i += 1) {
        X64_Type type = x64_parameter_type(builder, procedure, i);
        assert(type.kind != X64_TYPE_KIND_NONE);
        offset += x64_type_word_count(type);
    }
    return offset;
}

/**
 * Returns the distance from `rbp` of the value that takes the words of the stack of values from
 * `start` on, `word_count` of them. The stack grows down, so the value is below the words before
 * it, and its own words go up from its first, as in any other memory.
 */
static int32_t x64_stack_displacement(X64_Builder const *builder, size_t start,
                                      size_t word_count) {
    assert(start + word_count <= builder->stack_words_max);

    size_t distance = builder->locals_size_bytes + (start + word_count) * X64_WORD_SIZE_BYTES;
    assert(distance <= INT32_MAX);
    return -(int32_t)distance;
}

/**
 * Puts a value of `word_count` words on the stack of values, and returns its distance from `rbp`.
 * Nothing is written: the caller stores the words.
 */
static int32_t x64_stack_push(X64_Builder *builder, size_t word_count) {
    size_t start = builder->stack_words;
    builder->stack_words += word_count;
    if (builder->stack_words > builder->stack_words_max) {
        builder->stack_words_max = builder->stack_words;
    }
    return x64_stack_displacement(builder, start, word_count);
}

/**
 * Takes `word_count` words off the stack of values.
 */
static void x64_stack_pop(X64_Builder *builder, size_t word_count) {
    assert(word_count <= builder->stack_words);

    builder->stack_words -= word_count;
}

/**
 * Writes `mov register, r10`.
 */
static void x64_write_move_from_r10(X64_Builder *builder, byte register_number) {
    assert(register_number < 16);

    x64_write_registers(builder, X64_OPCODE_STORE, X64_REGISTER_R10, register_number);
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
    x64_write_byte(&builder->writer, (byte)(X64_REX_WIDE | (register_number >> 3)));
    x64_write_byte(&builder->writer, (byte)(0xB8 | (register_number & 7)));
    x64_write_u64(&builder->writer, (uint64_t)number);
}

/**
 * Writes an instruction of 64 bits with `opcode` between the register `register_number` and the
 * memory at `base + displacement`, such as `mov register, [base + displacement]` for
 * `X64_OPCODE_LOAD`. The displacement takes no byte when it is zero, which `rbp` cannot have,
 * because that form of it means an address relative to the instruction pointer, one byte when it
 * fits in one and four otherwise. Both writes of the code make the same choice, because the
 * displacement depends only on the procedure. A base of `rsp` needs the byte that adds an index,
 * which says that there is none.
 */
static void x64_write_memory(X64_Builder *builder, byte opcode, byte register_number, byte base,
                             int32_t displacement) {
    assert(register_number < 16);
    assert(base < 16);

    // The prefix says that the operands are 64-bit, and has the extensions of the register and of
    // the base when they are one of `r8` to `r15`.
    x64_write_byte(&builder->writer,
                   (byte)(X64_REX_WIDE | ((register_number >> 3) << 2) | (base >> 3)));
    x64_write_byte(&builder->writer, opcode);
    byte modrm = (byte)(((register_number & 7) << 3) | (base & 7));
    bool is_short = displacement >= INT8_MIN && displacement <= INT8_MAX;
    if (displacement == 0 && (base & 7) != X64_REGISTER_RBP) {
        x64_write_byte(&builder->writer, modrm);
    } else if (is_short) {
        x64_write_byte(&builder->writer, (byte)(0x40 | modrm));
    } else {
        x64_write_byte(&builder->writer, (byte)(0x80 | modrm));
    }
    if ((base & 7) == X64_REGISTER_RSP) {
        x64_write_byte(&builder->writer, 0x24);
    }
    if (displacement == 0 && (base & 7) != X64_REGISTER_RBP) {
        return;
    }
    if (is_short) {
        x64_write_byte(&builder->writer, (byte)(int8_t)displacement);
    } else {
        x64_write_u32(&builder->writer, (uint32_t)displacement);
    }
}

/**
 * Writes an instruction of 64 bits with `opcode` between two registers, which writes `target` with
 * `source`, such as `add target, source` for `X64_OPCODE_ADD` and `mov target, source` for
 * `X64_OPCODE_STORE`.
 */
static void x64_write_registers(X64_Builder *builder, byte opcode, byte source, byte target) {
    assert(source < 16);
    assert(target < 16);

    x64_write_byte(&builder->writer,
                   (byte)(X64_REX_WIDE | ((source >> 3) << 2) | (target >> 3)));
    x64_write_byte(&builder->writer, opcode);
    x64_write_byte(&builder->writer, (byte)(0xC0 | ((source & 7) << 3) | (target & 7)));
}

/**
 * Writes `imul target, source`, which multiplies `target` by `source`. The low 64 bits of the
 * product are the same for signed and unsigned numbers, so it wraps around like the language says.
 */
static void x64_write_multiply(X64_Builder *builder, byte target, byte source) {
    assert(source < 16);
    assert(target < 16);

    x64_write_byte(&builder->writer,
                   (byte)(X64_REX_WIDE | ((target >> 3) << 2) | (source >> 3)));
    x64_write_byte(&builder->writer, X64_OPCODE_TWO_BYTE);
    x64_write_byte(&builder->writer, X64_OPCODE_TWO_BYTE_IMUL);
    x64_write_byte(&builder->writer, (byte)(0xC0 | ((target & 7) << 3) | (source & 7)));
}

/**
 * Writes the division of `rax` by `divisor`: `cqo` widens `rax` with its sign to `rdx`, and `idiv`
 * leaves the quotient, truncated toward zero, in `rax`. It traps when the divisor is zero, or when
 * the smallest `Int` is divided by -1, because the quotient does not fit.
 */
static void x64_write_divide(X64_Builder *builder, byte divisor) {
    assert(divisor < 16);
    assert(divisor != X64_REGISTER_RAX);
    assert(divisor != X64_REGISTER_RDX);

    static byte const SIGN_EXTEND[] = {0x48, 0x99};
    x64_write_bytes(&builder->writer, SIGN_EXTEND, sizeof(SIGN_EXTEND));
    x64_write_byte(&builder->writer, (byte)(X64_REX_WIDE | (divisor >> 3)));
    x64_write_byte(&builder->writer, 0xF7);
    // The middle bits are 7, which say that the instruction is `idiv`.
    x64_write_byte(&builder->writer, (byte)(0xC0 | (7 << 3) | (divisor & 7)));
}

/**
 * Writes `set` of `condition` to `al`, which is 1 when the flags of the last comparison hold it
 * and 0 otherwise, and `movzx rax, al`, so that `rax` is that `Bool`.
 */
static void x64_write_set(X64_Builder *builder, byte condition) {
    assert(condition < 16);

    x64_write_byte(&builder->writer, X64_OPCODE_TWO_BYTE);
    x64_write_byte(&builder->writer, (byte)(X64_OPCODE_TWO_BYTE_SET | condition));
    x64_write_byte(&builder->writer, 0xC0);
    x64_write_byte(&builder->writer, X64_REX_WIDE);
    x64_write_byte(&builder->writer, X64_OPCODE_TWO_BYTE);
    x64_write_byte(&builder->writer, X64_OPCODE_TWO_BYTE_MOVZX_BYTE);
    x64_write_byte(&builder->writer, 0xC0);
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
    // The stack: all of it is there from the start, so that a frame of any size can be used at
    // once, see `X64_STACK_SIZE_BYTES`.
    x64_store_u64(optional + 72, X64_STACK_SIZE_BYTES);
    x64_store_u64(optional + 80, X64_STACK_SIZE_BYTES);
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
 * of the prologue of a procedure, so that a breakpoint on a procedure is at its first statement.
 * The table has an entry for the start of the program and one for each procedure, in the order of
 * the code, and the unwind information of each follows the table. `section_rva` is where the
 * section is in memory.
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

    // An `Int` is a signed number of 8 bytes, a `Bool` is a truth value of 8 bytes, and a `C_Str`
    // is a pointer to bytes, which gdb shows as the text that they are, up to the zero.
    X64_Debug_Types types = {0};
    types.int_type = (uint32_t)(writer->length - unit_start);
    x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_BASE_TYPE);
    x64_write_string(writer, STR("Int"));
    x64_write_byte(writer, X64_DWARF_ENCODING_SIGNED);
    x64_write_byte(writer, X64_WORD_SIZE_BYTES);
    types.bool_type = (uint32_t)(writer->length - unit_start);
    x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_BASE_TYPE);
    x64_write_string(writer, STR("Bool"));
    x64_write_byte(writer, X64_DWARF_ENCODING_BOOLEAN);
    x64_write_byte(writer, X64_WORD_SIZE_BYTES);
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
        X64_Type return_type = x64_return_type(builder, procedure);
        cursor = x64_debug_cursor_move(debug->source, cursor,
                                       builder->nodes[definition].source_offset);

        if (return_type.kind == X64_TYPE_KIND_NONE) {
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
        if (return_type.kind != X64_TYPE_KIND_NONE) {
            x64_write_u32(writer, x64_debug_type_reference(types, return_type));
        }

        // The words of the arguments are kept in the space that the caller made, at the CFA and
        // above, see `x64_argument_word_offset`.
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
            x64_debug_location_write(
                writer, (int64_t)(x64_argument_word_offset(builder, procedure, j) *
                                  X64_WORD_SIZE_BYTES));
            parameter = builder->nodes[parameter].next_sibling;
        }

        // A variable is at its place below `rbp`, which the layout of the procedure worked out,
        // see `x64_procedure_layout`, and `rbp` is 16 bytes below the CFA.
        Ast_Node_Index procedure_end = ast_procedure_end(builder->nodes, builder->node_count,
                                                         procedure);
        for (Ast_Node_Index node = procedure; node < procedure_end; node += 1) {
            if (builder->nodes[node].node_type != AST_NODE_TYPE_VARIABLE_DEFINITION) {
                continue;
            }
            // The build checked the type of every variable, so it is one that the target has.
            X64_Type type = builder->layouts[node].type;
            assert(type.kind != X64_TYPE_KIND_NONE);
            cursor = x64_debug_cursor_move(debug->source, cursor,
                                           builder->nodes[node].source_offset);
            x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_VARIABLE);
            x64_write_string(writer, builder->nodes[node].payload.variable_definition.name);
            x64_write_byte(writer, 1);
            x64_write_u32(writer, cursor.line);
            x64_write_u32(writer, x64_debug_type_reference(types, type));
            x64_debug_location_write(writer, -(int64_t)builder->layouts[node].frame_offset -
                                                 X64_DEBUG_CFA_FRAME_OFFSET_BYTES);
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
    x64_write_signed_leb128(writer, -X64_WORD_SIZE_BYTES);
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
    switch (type.kind) {
    case X64_TYPE_KIND_BOOL:
        return types.bool_type;
    case X64_TYPE_KIND_C_STR:
        return types.c_str_type;
    case X64_TYPE_KIND_INT:
        return types.int_type;
    case X64_TYPE_KIND_NONE:
        break;
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
