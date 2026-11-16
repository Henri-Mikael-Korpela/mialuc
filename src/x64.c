#include <assert.h>
#include <iso646.h>
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
    // `mov` of the lowest byte of the register to memory.
    X64_OPCODE_STORE_BYTE = 0x88,
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
    X64_CONDITION_BELOW = 0x2,
    X64_CONDITION_ABOVE_EQUAL = 0x3,
    X64_CONDITION_EQUAL = 0x4,
    // Not a condition of the processor: the jump that is always taken, see `x64_jump_write`.
    X64_CONDITION_ALWAYS = 0x10,
    X64_CONDITION_NOT_EQUAL = 0x5,
    // The unsigned conditions, which an index and an address are compared with, so that a negative
    // index is above every length.
    X64_CONDITION_BELOW_EQUAL = 0x6,
    X64_CONDITION_ABOVE = 0x7,
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
    // The largest number that a `Byte` holds.
    X64_BYTE_MAX = 255,
    // An `Enum_Member_Type_Info` of the table of an enum, which is the `Str` of the name of the
    // member and its number, see `x64_data_layout`.
    X64_TYPE_INFO_MEMBER_SIZE_BYTES = 3 * X64_WORD_SIZE_BYTES,
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
    X64_DWARF_ABBREVIATION_STRUCTURE_TYPE = 12,
    X64_DWARF_ABBREVIATION_STRUCTURE_TYPE_ANONYMOUS = 13,
    X64_DWARF_ABBREVIATION_MEMBER = 14,
    X64_DWARF_ABBREVIATION_ENUMERATION_TYPE = 15,
    X64_DWARF_ABBREVIATION_ENUMERATOR = 16,
    X64_DWARF_ABBREVIATION_SUBROUTINE_TYPE = 17,
    X64_DWARF_ABBREVIATION_SUBROUTINE_TYPE_VOID = 18,
    X64_DWARF_ABBREVIATION_LEXICAL_BLOCK = 19,
    X64_DWARF_ABBREVIATION_LABEL = 20,
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
    // The most structs that are open at once while the fields of a struct are walked, which is how
    // far structs can be inside each other, see `x64_struct_word_count`. A struct that has itself
    // as a field is infinitely large, and is found by going deeper than this.
    X64_STRUCT_DEPTH_MAX = 64,
    // The most fields that a walk over a struct visits. Structs whose fields are structs with
    // several fields of a struct each have more words than any program can use, and the walk must
    // end.
    X64_STRUCT_STEP_MAX = 1 << 20,
    // The most frames that the statements of a procedure need at once: the body, and for each of
    // the blocks that the parser accepts inside each other the statement and the block, see
    // `AST_BLOCK_DEPTH_MAX`, with room for more that later statements need.
    X64_STATEMENT_DEPTH_MAX = 4 * AST_BLOCK_DEPTH_MAX + 4,
};

// The end of a chain of jumps, see `x64_jump_write`. No place in the code is this far.
static uint32_t const X64_JUMP_NONE = UINT32_MAX;
// What the walk over the words of a struct answers when it has to stop, see
// `x64_struct_word_count`.
static size_t const X64_WORD_COUNT_INVALID = SIZE_MAX;

// What a constant of the module is, which decides what the build makes of it. Alphabetical, because
// nothing depends on the order.
typedef enum {
    // A procedure of a `foreign libc ->` block, which is imported and has no code.
    X64_CONSTANT_KIND_FOREIGN,
    // The member of an enum, such as `DEFAULT :: Color.RED`, which has no code: a use of it is the
    // number of the member.
    X64_CONSTANT_KIND_MEMBER,
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
    // The address of the place that the expression is, which is what `%` takes.
    X64_MODE_ADDRESS,
    // The address of a place that is only read, which can also be a parameter or an iterator: the
    // struct whose field is read.
    X64_MODE_PLACE,
    // The address of the place that an assignment writes to, which can also be a global variable.
    X64_MODE_TARGET,
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
    // The names of the enums and of their members that `type_info_of` gives, after the strings.
    size_t type_names_offset;
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
    uint32_t str_type;
    // The start of the unit in `.debug_info`, which the places of the entries are counted from.
    size_t unit_start;
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
 * from `low` to `high`, both included, for an `Int`, a `Byte` and an enum, whose members are their
 * numbers, and the bytes of `string` for a `Str`. When `status` is not `X64_BUILD_STATUS_OK` the
 * pattern is wrong, and the rest means nothing.
 */
typedef struct {
    int64_t low;
    int64_t high;
    // The string literal that a `Str` is compared with, or `AST_NODE_INDEX_NONE` for the others.
    Ast_Node_Index string;
    X64_Build_Status status;
} X64_Match_Pattern;

/**
 * A list of statements that is being written, or a statement that has blocks, see
 * `x64_statements_write`. `node` is the procedure for its body, a block, a `defer`, whose one
 * statement is its list, a statement that has a block, or a jump that writes the deferred
 * statements of the blocks that it leaves, and `next` is its child that is written next.
 * `end_jumps` are the jumps to the end of an if statement or a match statement, and `false_jumps`
 * the jumps of the condition of its branch that is being written, which go to the next branch when
 * the condition does not hold.
 */
typedef struct {
    Ast_Node_Index node;
    Ast_Node_Index next;
    // The deferred statements of a list that are still to be written when it is left are its
    // `defer`s before this node, the last one first, which is `AST_NODE_INDEX_NONE`, after every
    // node, until the first one is written. For a jump it is about the list at `exit_level`, and
    // starts at the jump, which comes after every `defer` that it has passed.
    Ast_Node_Index defer_bound;
    uint32_t end_jumps;
    uint32_t false_jumps;
    // The jumps of the `continue`s of a loop, which go to where it goes on with its next pass.
    uint32_t continue_jumps;
    // The place in the code of the start of a loop, which each pass goes back to.
    uint32_t loop_start;
    // For a jump, the frame of the list whose deferred statements are written, which goes down
    // from the list that the jump is in to the one that it leaves.
    uint32_t exit_level;
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
    // The size of `.data`, see `x64_data_layout`.
    size_t data_size_bytes;
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
    // The place of the start of the loop that was started last, and the jump that leaves it from
    // its test, which its frame takes over, see `x64_for_start_write`.
    uint32_t loop_start;
    uint32_t loop_exit_jumps;
    // Whether the procedure that is being written makes temporary strings, which it gives back
    // when it returns.
    bool is_temporary_used;
    X64_Constant constants[X64_CONSTANT_COUNT_MAX];
} X64_Builder;

static bool         x64_address_write            (X64_Builder *builder,
                                                  X64_Expression_Frame const *frame);
static size_t       x64_align_up                 (size_t value, size_t alignment);
static size_t       x64_argument_word_offset     (X64_Builder const *builder,
                                                  Ast_Node_Index procedure, size_t position);
static bool         x64_assignment_write         (X64_Builder *builder, Ast_Node_Index statement);
static void         x64_block_end                (X64_Builder *builder,
                                                  X64_Statement_Frame *frame,
                                                  Ast_Node_Index block);
static bool         x64_built_in_write           (X64_Builder *builder,
                                                  X64_Expression_Frame const *frame);
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
static bool         x64_child_done               (X64_Builder *builder,
                                                  X64_Expression_Frame const *parent,
                                                  X64_Expression_Frame const *child);
static Ast_Node_Index x64_child_first            (X64_Builder const *builder,
                                                  Ast_Node_Index expression);
static X64_Mode     x64_child_mode               (X64_Builder const *builder,
                                                  X64_Expression_Frame const *parent,
                                                  Ast_Node_Index child);
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
static void         x64_conversion_write         (X64_Builder *builder,
                                                  X64_Expression_Frame const *frame);
static void         x64_copy_write               (X64_Builder *builder);
static bool         x64_data_layout              (X64_Builder *builder);
static void         x64_data_start_write         (X64_Builder *builder);
static uint32_t     x64_debug_address_rva        (X64_Builder const *builder, size_t code_offset);
static void         x64_debug_blocks_write       (X64_Builder *builder, size_t start,
                                                  size_t end);
static X64_Debug_Cursor x64_debug_cursor_move    (Str source, X64_Debug_Cursor cursor,
                                                  size_t offset);
static void         x64_debug_entry_close        (X64_Builder *builder, size_t entry_start);
static void         x64_debug_enums_write        (X64_Builder *builder, X64_Debug_Types types);
static void         x64_debug_frame_write        (X64_Builder *builder, size_t text_size_bytes);
static void         x64_debug_info_write         (X64_Builder *builder, size_t text_size_bytes);
static void         x64_debug_line_write         (X64_Builder *builder, size_t text_size_bytes);
static void         x64_debug_location_write     (X64_Writer *writer, int64_t cfa_offset);
static void         x64_debug_member_write       (X64_Writer *writer, Str name, uint32_t type,
                                                  size_t offset);
static uint32_t     x64_debug_pointers_write     (X64_Builder *builder, X64_Debug_Types types,
                                                  uint32_t target, uint32_t depth);
static size_t       x64_debug_procedure_end      (X64_Builder const *builder, size_t position,
                                                  size_t text_size_bytes);
static void         x64_debug_row_add            (X64_Builder *builder, X64_Debug_Row_Kind kind,
                                                  uint32_t source_offset);
static uint32_t     x64_debug_scalar_type_write  (X64_Builder *builder, X64_Debug_Types types,
                                                  X64_Type type);
static void         x64_debug_sections_write     (X64_Builder *builder, size_t text_size_bytes);
static void         x64_debug_structs_write      (X64_Builder *builder, X64_Debug_Types types);
static uint32_t     x64_debug_subroutine_write   (X64_Builder *builder, X64_Debug_Types types,
                                                  Ast_Node_Index signature);
static uint32_t     x64_debug_type_reference     (X64_Builder const *builder,
                                                  X64_Debug_Types types, X64_Type_Kind kind,
                                                  Ast_Node_Index definition);
static uint32_t     x64_debug_type_write         (X64_Builder *builder, X64_Debug_Types types,
                                                  X64_Type type);
static void         x64_debug_unit_write         (X64_Builder *builder, size_t text_size_bytes);
static void         x64_debug_unwind_write       (X64_Builder *builder, size_t text_size_bytes,
                                                  uint32_t section_rva);
static Ast_Node_Index x64_defer_last_before      (X64_Builder const *builder, Ast_Node_Index list,
                                                  Ast_Node_Index bound);
static Ast_Node_Index x64_defer_next             (X64_Builder *builder,
                                                  X64_Statement_Frame const *frames,
                                                  X64_Statement_Frame *frame);
static void         x64_dos_header_write         (byte *header);
static Ast_Node_Index x64_enum_find              (X64_Builder const *builder, Str name);
static bool         x64_enum_member_find         (X64_Builder const *builder,
                                                  Ast_Node_Index enum_node, Str member,
                                                  int64_t *number);
static Str          x64_enum_member_name_of      (X64_Builder const *builder,
                                                  Ast_Node_Index expression,
                                                  Ast_Node_Index enum_node);
static Ast_Node_Index x64_enum_of_access         (X64_Builder const *builder,
                                                  Ast_Node_Index access);
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
                                                  X64_Type wanted, X64_Mode mode,
                                                  bool is_result_used);
static bool         x64_fail                     (X64_Builder *builder, X64_Build_Status status,
                                                  Str subject);
static bool         x64_fail_unsupported         (X64_Builder *builder);
static bool         x64_field_address_write      (X64_Builder *builder,
                                                  X64_Expression_Frame const *frame);
static bool         x64_field_target_is_read_only(X64_Builder const *builder,
                                                  Ast_Node_Index target);
static Ast_Node_Index x64_field_union_match      (X64_Builder const *builder,
                                                  Ast_Node_Index field);
static bool         x64_for_each_start_write     (X64_Builder *builder, Ast_Node_Index statement);
static bool         x64_for_start_write          (X64_Builder *builder, Ast_Node_Index statement);
static void         x64_headers_write            (X64_Builder *builder, size_t text_size_bytes);
static bool         x64_identifier_address_load  (X64_Builder *builder, Ast_Node_Index identifier,
                                                  byte register_number);
static bool         x64_identifier_place         (X64_Builder const *builder,
                                                  Ast_Node_Index identifier, int32_t *place);
static void         x64_image_write              (X64_Builder *builder);
static bool         x64_is_constant              (X64_Builder const *builder,
                                                  Ast_Node_Index expression);
static void         x64_jump_back_write          (X64_Builder *builder, uint32_t target);
static void         x64_jump_end_write           (X64_Builder *builder,
                                                  X64_Statement_Frame *frames, size_t frame_count,
                                                  Ast_Node_Index jump);
static void         x64_jump_write               (X64_Builder *builder, byte condition,
                                                  uint32_t *jumps);
static void         x64_jumps_resolve            (X64_Builder *builder, uint32_t jumps);
static X64_Layout   x64_layout_make              (X64_Builder const *builder);
static X64_Type     x64_leaf_type                (X64_Builder const *builder,
                                                  Ast_Node_Index expression);
static bool         x64_list_statement_write     (X64_Builder *builder, Ast_Node_Index procedure,
                                                  Ast_Node_Index statement,
                                                  Ast_Node_Index *pushed);
static bool         x64_main_check               (X64_Builder *builder);
static Ast_Node_Index x64_match_case_write       (X64_Builder *builder,
                                                  X64_Statement_Frame *frame,
                                                  Ast_Node_Index first);
static X64_Match_Pattern x64_match_pattern       (X64_Builder const *builder, X64_Type subject,
                                                  Ast_Node_Index pattern);
static bool         x64_match_patterns_overlap   (X64_Builder const *builder, X64_Type subject,
                                                  X64_Match_Pattern first,
                                                  X64_Match_Pattern second);
static bool         x64_match_start_write        (X64_Builder *builder, Ast_Node_Index match);
static void         x64_match_string_write       (X64_Builder *builder, int32_t subject,
                                                  Ast_Node_Index literal, uint32_t *block_jumps);
static bool         x64_member_write             (X64_Builder *builder, Ast_Node_Index enum_node,
                                                  Str member);
static void         x64_memory_load              (X64_Builder *builder, X64_Type type, byte base);
static void         x64_memory_load_words        (X64_Builder *builder, X64_Type type, byte base);
static void         x64_memory_store             (X64_Builder *builder, X64_Type type, byte base);
static bool         x64_node_enter               (X64_Builder *builder,
                                                  X64_Expression_Frame const *frame);
static bool         x64_node_type_is_jump        (Ast_Node_Type node_type);
static bool         x64_cast_write               (X64_Builder *builder,
                                                  X64_Expression_Frame const *frame);
static bool         x64_operation_write          (X64_Builder *builder,
                                                  X64_Expression_Frame const *frame);
static size_t       x64_parameter_count          (X64_Builder const *builder,
                                                  Ast_Node_Index procedure);
static X64_Type     x64_parameter_type           (X64_Builder const *builder,
                                                  Ast_Node_Index procedure, size_t position);
static Ast_Node_Index x64_parameter_type_node    (X64_Builder const *builder,
                                                  Ast_Node_Index signature, size_t position);
static size_t       x64_parameter_word_count     (X64_Builder const *builder,
                                                  Ast_Node_Index procedure);
static Ast_Node_Index x64_parent_find            (X64_Builder const *builder,
                                                  Ast_Node_Index child);
static void         x64_patch_u32                (X64_Writer *writer, size_t offset,
                                                  uint32_t number);
static bool         x64_path_equals              (X64_Builder const *builder, Ast_Node_Index first,
                                                  Ast_Node_Index second);
static bool         x64_procedure_address_write  (X64_Builder *builder, size_t position);
static void         x64_procedure_layout         (X64_Builder *builder, Ast_Node_Index procedure);
static bool         x64_procedure_uses_temporary (X64_Builder const *builder,
                                                  Ast_Node_Index procedure);
static bool         x64_procedure_write          (X64_Builder *builder, size_t position);
static bool         x64_range_assignment_write   (X64_Builder *builder, Ast_Node_Index statement);
static void         x64_rdata_write              (X64_Builder *builder);
static bool         x64_result_is_hidden         (X64_Builder const *builder,
                                                  Ast_Node_Index procedure);
static X64_Type     x64_return_type              (X64_Builder const *builder,
                                                  Ast_Node_Index procedure);
static bool         x64_return_value_write       (X64_Builder *builder, Ast_Node_Index procedure,
                                                  Ast_Node_Index statement);
static void         x64_sequence_length_load     (X64_Builder *builder, X64_Type type,
                                                  byte register_number, int32_t place);
static Ast_Node_Index x64_signature_canonical    (X64_Builder const *builder,
                                                  Ast_Node_Index signature);
static bool         x64_signatures_check         (X64_Builder *builder);
static bool         x64_slice_write              (X64_Builder *builder,
                                                  X64_Expression_Frame const *frame);
static int32_t      x64_stack_displacement       (X64_Builder const *builder, size_t start,
                                                  size_t word_count);
static void         x64_stack_pop                (X64_Builder *builder, size_t word_count);
static int32_t      x64_stack_push               (X64_Builder *builder, size_t word_count);
static X64_Statement_Frame x64_statement_frame_make(Ast_Node_Index node, Ast_Node_Index next);
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
static uint64_t     x64_string_rva               (X64_Builder const *builder,
                                                  Ast_Node_Index literal);
static bool         x64_struct_field_find        (X64_Builder const *builder,
                                                  Ast_Node_Index struct_node, Str name,
                                                  size_t *word_offset, X64_Type *type);
static Ast_Node_Index x64_struct_field_node_find (X64_Builder const *builder,
                                                  Ast_Node_Index struct_node, Str name);
static Ast_Node_Index x64_struct_find            (X64_Builder const *builder, Str name);
static size_t       x64_struct_word_count        (X64_Builder const *builder,
                                                  Ast_Node_Index struct_node);
static bool         x64_structs_check            (X64_Builder *builder);
static bool         x64_type_accepts             (X64_Builder const *builder, X64_Type wanted,
                                                  Ast_Node_Index expression);
static X64_Type     x64_type_element             (X64_Type sequence);
static bool         x64_type_equals              (X64_Type first, X64_Type second);
static bool         x64_type_info_access_is      (X64_Builder const *builder,
                                                  Ast_Node_Index access);
static Ast_Node_Index x64_type_info_enum         (X64_Builder const *builder, Ast_Node_Index info);
static X64_Type     x64_type_info_type           (X64_Builder const *builder,
                                                  Ast_Node_Index access);
static bool         x64_type_info_write          (X64_Builder *builder, Ast_Node_Index access);
static bool         x64_type_is_scalar           (X64_Builder const *builder, X64_Type type);
static X64_Type     x64_type_make                (X64_Type_Kind kind);
static void         x64_type_name_address_write  (X64_Builder *builder, Ast_Node_Index info,
                                                  byte register_number);
static size_t       x64_type_names_offset        (X64_Builder const *builder,
                                                  Ast_Node_Index before);
static bool         x64_type_nodes_equal         (X64_Builder const *builder, Ast_Node_Index first,
                                                  Ast_Node_Index second);
static X64_Type     x64_type_of_element_node     (X64_Builder const *builder, Ast_Node_Index type);
static X64_Type     x64_type_of_name             (X64_Builder const *builder, Str name);
static X64_Type     x64_type_of_node             (X64_Builder const *builder, Ast_Node_Index type);
static X64_Type     x64_type_pointee             (X64_Type pointer);
static X64_Type     x64_type_pointer_to          (X64_Type target);
static X64_Type     x64_type_sequence_of         (X64_Type element, X64_Type_Kind kind,
                                                  int64_t length);
static size_t       x64_type_stride_bytes        (X64_Builder const *builder,
                                                  X64_Type type);
static Str          x64_type_subject             (X64_Builder const *builder, Ast_Node_Index type);
static size_t       x64_type_word_count          (X64_Builder const *builder, X64_Type type);
static bool         x64_union_block_changes      (X64_Builder const *builder, Ast_Node_Index block,
                                                  Ast_Node_Index container, Str discriminant);
static bool         x64_union_condition_matches  (X64_Builder const *builder,
                                                  Ast_Node_Index condition,
                                                  Ast_Node_Index container,
                                                  Ast_Node_Index union_match,
                                                  Ast_Node_Index enum_node);
static Ast_Node_Index x64_union_enum             (X64_Builder const *builder,
                                                  Ast_Node_Index struct_node,
                                                  Ast_Node_Index union_match);
static bool         x64_union_is_matched         (X64_Builder const *builder, Ast_Node_Index access,
                                                  Ast_Node_Index struct_node,
                                                  Ast_Node_Index union_match);
static Str          x64_union_literal_member     (X64_Builder const *builder,
                                                  Ast_Node_Index literal,
                                                  Ast_Node_Index union_match,
                                                  Ast_Node_Index enum_node);
static bool         x64_unions_check             (X64_Builder *builder);
static bool         x64_value_store              (X64_Builder *builder, X64_Type type,
                                                  int32_t displacement);
static bool         x64_variable_definition_write(X64_Builder *builder, Ast_Node_Index statement);
static Ast_Node_Index x64_variable_type_node     (X64_Builder const *builder,
                                                  Ast_Node_Index definition);
static void         x64_write_add_immediate      (X64_Builder *builder, byte register_number,
                                                  int8_t number);
static void         x64_write_and_immediate      (X64_Builder *builder, byte register_number,
                                                  int8_t number);
static void         x64_write_byte               (X64_Writer *writer, byte content);
static void         x64_write_bytes              (X64_Writer *writer, byte const *content,
                                                  size_t count);
static void         x64_write_call_import        (X64_Builder *builder, uint32_t import_slot);
static void         x64_write_call_local         (X64_Builder *builder, size_t target_offset);
static void         x64_write_call_register      (X64_Builder *builder, byte register_number);
static void         x64_write_divide             (X64_Builder *builder, byte divisor);
static void         x64_write_load_integer       (X64_Builder *builder, byte register_number,
                                                  int64_t number);
static void         x64_write_memory             (X64_Builder *builder, byte opcode,
                                                  byte register_number, byte base,
                                                  int32_t displacement);
static void         x64_write_memory_operand     (X64_Builder *builder, byte register_number,
                                                  byte base, int32_t displacement);
static void         x64_write_memory_two_byte    (X64_Builder *builder, byte opcode,
                                                  byte register_number, byte base,
                                                  int32_t displacement);
static void         x64_write_move_from_r10      (X64_Builder *builder, byte register_number);
static void         x64_write_multiply           (X64_Builder *builder, byte target, byte source);
static void         x64_write_registers          (X64_Builder *builder, byte opcode, byte source,
                                                  byte target);
static void         x64_write_rip_displacement   (X64_Builder *builder, uint64_t target_rva);
static void         x64_write_rip_memory         (X64_Builder *builder, byte opcode,
                                                  byte register_number, uint64_t target_rva);
static void         x64_write_set                (X64_Builder *builder, byte condition);
static void         x64_write_signed_leb128      (X64_Writer *writer, int64_t number);
static void         x64_write_string             (X64_Writer *writer, Str text);
static void         x64_write_trap_unless        (X64_Builder *builder, byte condition);
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
    // A struct, with its fields as its children: name, size.
    X64_DWARF_ABBREVIATION_STRUCTURE_TYPE, 0x13, 1,
    0x03, 0x08, 0x0B, 0x0F, 0, 0,
    // A struct without a name, which is the type of a field or an array slice: size.
    X64_DWARF_ABBREVIATION_STRUCTURE_TYPE_ANONYMOUS, 0x13, 1,
    0x0B, 0x0F, 0, 0,
    // A field of a struct: name, type, the place in the struct.
    X64_DWARF_ABBREVIATION_MEMBER, 0x0D, 0,
    0x03, 0x08, 0x49, 0x13, 0x38, 0x0F, 0, 0,
    // An enum, with its members as its children: name, size, the type of the numbers.
    X64_DWARF_ABBREVIATION_ENUMERATION_TYPE, 0x04, 1,
    0x03, 0x08, 0x0B, 0x0B, 0x49, 0x13, 0, 0,
    // A member of an enum: name, number.
    X64_DWARF_ABBREVIATION_ENUMERATOR, 0x28, 0,
    0x03, 0x08, 0x1C, 0x0D, 0, 0,
    // The procedure that a pointer to a procedure points to: the type of its result.
    X64_DWARF_ABBREVIATION_SUBROUTINE_TYPE, 0x15, 0,
    0x49, 0x13, 0, 0,
    // The same for a procedure that returns nothing.
    X64_DWARF_ABBREVIATION_SUBROUTINE_TYPE_VOID, 0x15, 0,
    0, 0,
    // The code of a copy of a deferred statement, with a label as its child: low_pc, high_pc as a
    // size.
    X64_DWARF_ABBREVIATION_LEXICAL_BLOCK, 0x0B, 1,
    0x11, 0x01, 0x12, 0x07, 0, 0,
    // A place in the code with a name: name, low_pc.
    X64_DWARF_ABBREVIATION_LABEL, 0x0A, 0,
    0x03, 0x08, 0x11, 0x01, 0, 0,
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
        .data_size_bytes = 0,
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
        .loop_start = 0,
        .loop_exit_jumps = X64_JUMP_NONE,
        .text_rva = 0,
        .data_rva = 0,
        .module = root,
        .definition = AST_NODE_INDEX_NONE,
        .status = X64_BUILD_STATUS_OK,
        .is_temporary_used = false,
        .constants = {{0}},
    };

    bool is_ok = x64_structs_check(&builder);
    if (is_ok) {
        is_ok = x64_unions_check(&builder);
    }
    if (is_ok) {
        is_ok = x64_signatures_check(&builder);
    }
    if (is_ok) {
        is_ok = x64_constants_collect(&builder);
    }
    if (is_ok) {
        is_ok = x64_main_check(&builder);
    }
    if (is_ok) {
        is_ok = x64_data_layout(&builder);
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

        Ast_Node_Index procedure = definition_node->first_child;
        if (builder->constant_count == X64_CONSTANT_COUNT_MAX) {
            return x64_fail(builder, X64_BUILD_STATUS_CONSTANT_COUNT_EXCEEDED, name);
        }
        // A global variable is not a constant: its place is in `.data`, see `x64_data_layout`.
        if (definition_node->node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION) {
            definition = definition_node->next_sibling;
            continue;
        }
        if (definition_node->node_type != AST_NODE_TYPE_CONSTANT_DEFINITION) {
            return x64_fail(builder, X64_BUILD_STATUS_UNSUPPORTED, name);
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
        Ast_Node_Type value_type = builder->nodes[procedure].node_type;
        if (value_type == AST_NODE_TYPE_STRUCT_TYPE || value_type == AST_NODE_TYPE_ENUM_TYPE) {
            // A struct and an enum are types, which have no code, and the analysis does not count
            // them among the constants that a binding is the position of. The numbers of the
            // members of an enum are an `Int`, which is the only type of them.
            if (value_type == AST_NODE_TYPE_ENUM_TYPE &&
                not str_equals(builder->nodes[procedure].payload.enum_type.value_type, STR("Int"))) {
                return x64_fail(builder, X64_BUILD_STATUS_TYPE_UNSUPPORTED,
                                builder->nodes[procedure].payload.enum_type.value_type);
            }
            definition = definition_node->next_sibling;
            continue;
        }
        if (value_type == AST_NODE_TYPE_FIELD_ACCESS) {
            // The member of an enum, such as `DEFAULT :: Color.RED`, whose uses are its number.
            Ast_Node_Index enum_node = x64_enum_of_access(builder, procedure);
            int64_t number = 0;
            if (enum_node == AST_NODE_INDEX_NONE ||
                not x64_enum_member_find(builder, enum_node,
                                     builder->nodes[procedure].payload.field_access.name,
                                     &number)) {
                return x64_fail(builder, X64_BUILD_STATUS_ENUM_MEMBER_UNKNOWN,
                                builder->nodes[procedure].payload.field_access.name);
            }
            builder->constants[builder->constant_count] = (X64_Constant){
                .definition = definition,
                .offset = 0,
                .import_slot = 0,
                .kind = X64_CONSTANT_KIND_MEMBER,
            };
            builder->constant_count += 1;
            definition = definition_node->next_sibling;
            continue;
        }
        assert(value_type == AST_NODE_TYPE_PROCEDURE);

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
            X64_Type parameter_type = x64_type_of_node(builder, type);
            if (parameter_type.kind == X64_TYPE_KIND_ARRAY) {
                return x64_fail(builder, X64_BUILD_STATUS_ARRAY_TYPE_MISPLACED,
                                builder->nodes[parameter].payload.parameter.name);
            }
            if (parameter_type.kind == X64_TYPE_KIND_NONE ||
                (is_foreign && not x64_type_is_scalar(builder, parameter_type))) {
                Str subject = builder->nodes[parameter].payload.parameter.name;
                if (builder->nodes[type].node_type == AST_NODE_TYPE_TYPE_NAME) {
                    subject = builder->nodes[type].payload.type_name.name;
                }
                return x64_fail(builder, X64_BUILD_STATUS_TYPE_UNSUPPORTED, subject);
            }
            parameter = builder->nodes[parameter].next_sibling;
        }

        // A foreign procedure returns one word, which is what a function of C returns in `rax`.
        Str return_type = ast_procedure_return_type(&builder->nodes[procedure]);
        if (return_type.bytes != NULL) {
            X64_Type type = x64_type_of_name(builder, return_type);
            if (type.kind == X64_TYPE_KIND_NONE ||
                (is_foreign && not x64_type_is_scalar(builder, type))) {
                return x64_fail(builder, X64_BUILD_STATUS_TYPE_UNSUPPORTED, return_type);
            }
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
        if (not str_equals(name, STR("main"))) {
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
    layout.type_names_offset = layout.strings_offset + strings_bytes;
    layout.rdata_size_bytes = layout.type_names_offset +
                              x64_type_names_offset(builder, (Ast_Node_Index)builder->node_count);
    return layout;
}

/**
 * Returns where the names of the `type_info_of` before the node `before` end, counted from the
 * start of the names in `.rdata`: each one that is about an enum has the name of the enum and then
 * the names of its members, each with a zero after it. With the number of nodes as `before` it is
 * the size of all the names.
 */
static size_t x64_type_names_offset(X64_Builder const *builder, Ast_Node_Index before) {
    assert(before <= builder->node_count);

    size_t offset = 0;
    for (Ast_Node_Index node = 0; node < before; node += 1) {
        if (builder->nodes[node].node_type != AST_NODE_TYPE_TYPE_INFO_OF) {
            continue;
        }
        Ast_Node_Index enum_node = x64_type_info_enum(builder, node);
        if (enum_node == AST_NODE_INDEX_NONE) {
            continue;
        }
        Ast_Node_Index type = builder->nodes[node].first_child;
        offset += builder->nodes[type].payload.type_name.name.length + 1;
        for (Ast_Node_Index member = builder->nodes[enum_node].first_child;
             member != AST_NODE_INDEX_NONE; member = builder->nodes[member].next_sibling) {
            offset += builder->nodes[member].payload.enum_member.name.length + 1;
        }
    }
    return offset;
}

/**
 * Lays out `.data`, which has the temporary storage first, then a table for each `type_info_of` of
 * an enum, and then the global variables, in the order of the source, and checks the global
 * variables. A table has an `Enum_Member_Type_Info` for each member of the enum, which is its name
 * and its number, 3 words, and it is filled when the program starts, like the global variables get
 * their initial values, see `x64_data_start_write`. A global variable takes the words of its type,
 * which is written or the type of its initial value, and the initial value is a constant or a
 * field of the information about an enum. `builder->layouts` keeps the place of each in `.data`.
 * Returns false after it recorded an error.
 */
static bool x64_data_layout(X64_Builder *builder) {
    size_t size_bytes = X64_TEMPORARY_USED_SIZE_BYTES + X64_TEMPORARY_STORAGE_SIZE_BYTES;
    for (Ast_Node_Index node = 0; node < builder->node_count; node += 1) {
        if (builder->nodes[node].node_type != AST_NODE_TYPE_TYPE_INFO_OF) {
            continue;
        }
        Ast_Node_Index enum_node = x64_type_info_enum(builder, node);
        if (enum_node == AST_NODE_INDEX_NONE) {
            continue;
        }
        size_t member_count = 0;
        for (Ast_Node_Index member = builder->nodes[enum_node].first_child;
             member != AST_NODE_INDEX_NONE; member = builder->nodes[member].next_sibling) {
            member_count += 1;
        }
        builder->layouts[node].frame_offset = (uint32_t)size_bytes;
        size_bytes += member_count * X64_TYPE_INFO_MEMBER_SIZE_BYTES;
    }

    Ast_Node_Index definition = builder->nodes[builder->module].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && definition != AST_NODE_INDEX_NONE; i += 1) {
        Ast_Node const *node = &builder->nodes[definition];
        if (node->node_type != AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION) {
            definition = node->next_sibling;
            continue;
        }
        builder->definition = definition;
        Str name = node->payload.global_variable_definition.name;
        Ast_Node_Index value = node->last_child;
        Ast_Node_Index type_node = AST_NODE_INDEX_NONE;
        for (Ast_Node_Index child = node->first_child; child != value;
             child = builder->nodes[child].next_sibling) {
            if (builder->nodes[child].node_type != AST_NODE_TYPE_ATTRIBUTE) {
                type_node = child;
            }
        }

        X64_Type type = x64_type_make(X64_TYPE_KIND_NONE);
        if (x64_type_info_access_is(builder, value)) {
            type = x64_type_info_type(builder, value);
            if (type.kind == X64_TYPE_KIND_NONE) {
                return x64_fail(builder, X64_BUILD_STATUS_TYPE_INFO_UNSUPPORTED, name);
            }
        } else if (x64_is_constant(builder, value)) {
            type = x64_leaf_type(builder, value);
        }
        if (type_node != AST_NODE_INDEX_NONE) {
            X64_Type written = x64_type_of_node(builder, type_node);
            if (not x64_type_accepts(builder, written, value)) {
                if (written.kind == X64_TYPE_KIND_NONE) {
                    return x64_fail(builder, X64_BUILD_STATUS_TYPE_UNSUPPORTED,
                                    x64_type_subject(builder, type_node));
                }
                return x64_fail(builder, X64_BUILD_STATUS_VARIABLE_TYPE_MISMATCH, name);
            }
            type = written;
        }
        // A global variable is a number, a truth value, a string or the members of an enum, which
        // are the values that a constant expression or the information about a type has.
        bool is_valid = type.kind == X64_TYPE_KIND_BOOL || type.kind == X64_TYPE_KIND_BYTE ||
                        type.kind == X64_TYPE_KIND_INT || type.kind == X64_TYPE_KIND_STR ||
                        (type.kind == X64_TYPE_KIND_ARRAY && x64_type_info_access_is(builder,
                                                                                   value));
        if (not is_valid) {
            return x64_fail(builder, X64_BUILD_STATUS_TYPE_UNSUPPORTED, name);
        }
        builder->layouts[definition] = (X64_Node_Layout){
            .type = type,
            .frame_offset = (uint32_t)size_bytes,
        };
        size_bytes += x64_type_word_count(builder, type) * X64_WORD_SIZE_BYTES;
        definition = node->next_sibling;
    }

    builder->definition = AST_NODE_INDEX_NONE;
    builder->data_size_bytes = size_bytes;
    return true;
}

/**
 * Writes what the program does before `main`: the tables of the information about enums are filled
 * with the names and the numbers of their members, and every global variable is given its initial
 * value, which is a constant or a field of such information, so that every global variable has its
 * value before any procedure runs, see `x64_data_layout`.
 */
static void x64_data_start_write(X64_Builder *builder) {
    for (Ast_Node_Index node = 0; node < builder->node_count; node += 1) {
        if (builder->nodes[node].node_type != AST_NODE_TYPE_TYPE_INFO_OF) {
            continue;
        }
        Ast_Node_Index enum_node = x64_type_info_enum(builder, node);
        if (enum_node == AST_NODE_INDEX_NONE) {
            continue;
        }
        uint64_t table = builder->data_rva + builder->layouts[node].frame_offset;
        uint64_t name = X64_RDATA_RVA + builder->layout.type_names_offset +
                        x64_type_names_offset(builder, node) +
                        builder->nodes[builder->nodes[node].first_child]
                            .payload.type_name.name.length + 1;
        int64_t number = 0;
        for (Ast_Node_Index member = builder->nodes[enum_node].first_child;
             member != AST_NODE_INDEX_NONE; member = builder->nodes[member].next_sibling) {
            Str member_name = builder->nodes[member].payload.enum_member.name;
            x64_write_rip_memory(builder, X64_OPCODE_LEA, X64_REGISTER_RAX, name);
            x64_write_rip_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, table);
            x64_write_load_integer(builder, X64_REGISTER_RAX, (int64_t)member_name.length);
            x64_write_rip_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX,
                                 table + X64_WORD_SIZE_BYTES);
            x64_write_load_integer(builder, X64_REGISTER_RAX, number);
            x64_write_rip_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX,
                                 table + 2 * X64_WORD_SIZE_BYTES);
            table += X64_TYPE_INFO_MEMBER_SIZE_BYTES;
            name += member_name.length + 1;
            number += 1;
        }
    }

    Ast_Node_Index definition = builder->nodes[builder->module].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && definition != AST_NODE_INDEX_NONE; i += 1) {
        Ast_Node const *node = &builder->nodes[definition];
        if (node->node_type != AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION) {
            definition = node->next_sibling;
            continue;
        }
        uint64_t place = builder->data_rva + builder->layouts[definition].frame_offset;
        Ast_Node_Index value = node->last_child;
        Constant_Value constant = builder->constant_values[value];
        if (x64_type_info_access_is(builder, value)) {
            Ast_Node_Index info = builder->nodes[value].first_child;
            if (builder->layouts[definition].type.kind == X64_TYPE_KIND_ARRAY) {
                x64_write_rip_memory(builder, X64_OPCODE_LEA, X64_REGISTER_RAX,
                                     builder->data_rva + builder->layouts[info].frame_offset);
                x64_write_rip_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, place);
            } else {
                x64_type_name_address_write(builder, info, X64_REGISTER_RAX);
                x64_write_rip_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, place);
                x64_write_load_integer(
                    builder, X64_REGISTER_RAX,
                    (int64_t)builder->nodes[builder->nodes[info].first_child]
                        .payload.type_name.name.length);
                x64_write_rip_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX,
                                     place + X64_WORD_SIZE_BYTES);
            }
        } else if (constant.string != AST_NODE_INDEX_NONE) {
            x64_write_rip_memory(builder, X64_OPCODE_LEA, X64_REGISTER_RAX,
                                 x64_string_rva(builder, constant.string));
            x64_write_rip_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, place);
            x64_write_load_integer(
                builder, X64_REGISTER_RAX,
                (int64_t)builder->nodes[constant.string].payload.string_literal.text.length);
            x64_write_rip_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX,
                                 place + X64_WORD_SIZE_BYTES);
        } else {
            x64_write_load_integer(builder, X64_REGISTER_RAX, constant.integer);
            x64_write_rip_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, place);
        }
        definition = node->next_sibling;
    }
}

/**
 * Writes `lea register, [name]` of the name of the enum of the information `info`, which is the
 * first of its names in `.rdata`.
 */
static void x64_type_name_address_write(X64_Builder *builder, Ast_Node_Index info,
                                        byte register_number) {
    x64_write_rip_memory(builder, X64_OPCODE_LEA, register_number,
                         X64_RDATA_RVA + builder->layout.type_names_offset +
                             x64_type_names_offset(builder, info));
}

/**
 * Returns the address in memory, relative to the image, of the bytes of the string literal
 * `literal` in `.rdata`.
 */
static uint64_t x64_string_rva(X64_Builder const *builder, Ast_Node_Index literal) {
    return X64_RDATA_RVA + builder->layout.strings_offset + x64_string_offset(builder, literal);
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
                                   x64_align_up(builder->data_size_bytes,
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

    // The names of the enums and their members, each with a zero after it, in the order of the
    // `type_info_of` that gives them, see `x64_type_names_offset`.
    for (Ast_Node_Index node = 0; node < builder->node_count; node += 1) {
        if (builder->nodes[node].node_type != AST_NODE_TYPE_TYPE_INFO_OF) {
            continue;
        }
        Ast_Node_Index enum_node = x64_type_info_enum(builder, node);
        if (enum_node == AST_NODE_INDEX_NONE) {
            continue;
        }
        x64_write_string(writer, builder->nodes[builder->nodes[node].first_child]
                                     .payload.type_name.name);
        for (Ast_Node_Index member = builder->nodes[enum_node].first_child;
             member != AST_NODE_INDEX_NONE; member = builder->nodes[member].next_sibling) {
            x64_write_string(writer, builder->nodes[member].payload.enum_member.name);
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
    x64_data_start_write(builder);

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
        if (not x64_procedure_write(builder, i)) {
            return false;
        }
    }

    return true;
}

/**
 * Works out the frame of `procedure` before its code is written: the type of each of its variables
 * and the place of its value, in the order of the source, which `builder->layouts` keeps at the
 * node of the definition. A match statement has a place too, for its subject, and a for statement
 * for its iterator, which is a variable, and for what its loop keeps after it, and an array
 * literal for its elements. A variable takes
 * the words of its type, see `x64_type_word_count`, and has a place of its own also when it is in a
 * block, so that no two variables share memory and a debugger finds each at one place. A variable
 * without a type takes the type of its value, which only uses variables above it, whose types are
 * worked out already. The places start below the fixed part of the frame, see
 * `X64_FRAME_FIXED_SIZE_BYTES`, and their size is `builder->locals_size_bytes`, below which the
 * stack of values starts.
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
        } else if (node_type == AST_NODE_TYPE_FOR_STATEMENT) {
            // The iterator of a range is an `Int`, and the end of the range, which is read once,
            // is kept in the word after it. The iterator of the elements of an array is an
            // element, and the address of the next element and the end are kept after it.
            Ast_Node_Index range = builder->nodes[node].first_child;
            type = x64_type_make(X64_TYPE_KIND_INT);
            size_bytes += X64_WORD_SIZE_BYTES;
            if (builder->nodes[range].node_type != AST_NODE_TYPE_RANGE) {
                X64_Type sequence = x64_expression_type(builder, range);
                type = x64_type_make(X64_TYPE_KIND_NONE);
                if (sequence.kind == X64_TYPE_KIND_ARRAY || sequence.kind == X64_TYPE_KIND_SLICE) {
                    type = x64_type_element(sequence);
                }
                size_bytes += X64_WORD_SIZE_BYTES;
            }
        } else if (node_type == AST_NODE_TYPE_ARRAY_LITERAL) {
            // The elements, after which a place is aligned again.
            type = x64_leaf_type(builder, node);
            if (type.kind != X64_TYPE_KIND_NONE) {
                size_t elements_bytes =
                    (size_t)type.length * x64_type_stride_bytes(builder, x64_type_element(type));
                size_bytes += x64_align_up(elements_bytes, X64_WORD_SIZE_BYTES);
                if (size_bytes > X64_FRAME_SIZE_BYTES_MAX) {
                    size_bytes = X64_FRAME_SIZE_BYTES_MAX;
                }
            }
            assert(size_bytes <= UINT32_MAX);
            builder->layouts[node] = (X64_Node_Layout){
                .type = type,
                .frame_offset = (uint32_t)size_bytes,
            };
            continue;
        } else {
            continue;
        }
        // A variable of a type that the target does not have is an error that the code finds
        // where it is defined. It takes a word until then, so that the layout is complete.
        size_t word_count = 1;
        if (type.kind != X64_TYPE_KIND_NONE) {
            word_count = x64_type_word_count(builder, type);
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

    if (not x64_statements_write(builder, procedure)) {
        return false;
    }

    // `leave` and `ret` give the stack back and return. Falling off the end of a procedure that
    // has a result is a mistake of the program, so it stops with a breakpoint instead of returning
    // whatever is in `rax`.
    bool has_result = x64_return_type(builder, procedure).kind != X64_TYPE_KIND_NONE;
    Ast_Node_Index last = builder->nodes[procedure].last_child;
    bool is_last_return = builder->nodes[last].node_type == AST_NODE_TYPE_RETURN_STATEMENT;
    if (not is_last_return) {
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
 * list of statements that is being written, which is the body, a block or the one statement of a
 * `defer`, for each statement that has blocks, an if statement, a match statement, a for statement,
 * an infinite loop or a scope, and for a `return`, a `break` or a `continue` while the deferred
 * statements of the blocks that it leaves are written. A statement that has no blocks is written at
 * once. A frame is taken off when it has nothing more to write, and then the statement that a block
 * belongs to writes what comes after the block.
 *
 * A jump goes forward to a place that is not known when it is written, so its distance of 32 bits,
 * which is as long whatever the distance is, is filled in when the place is reached. The jumps that
 * go to one place are a chain, see `x64_jump_write`. A deferred statement is written again at each
 * place where its block is left, see `x64_defer_next`. Returns false after it recorded an error.
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
    frames[0] = x64_statement_frame_make(procedure, first);

    // A node is entered once and left once, a deferred statement is written once for each of the
    // jumps and the ends of blocks that come after it, and each of those finds every `defer` that
    // it runs with one step. So the steps are bounded by the nodes for each node.
    size_t step_max = (builder->node_count + 2) * (builder->node_count + 2);
    for (size_t step = 0; step < step_max && frame_count > 0; step += 1) {
        X64_Statement_Frame *frame = &frames[frame_count - 1];
        Ast_Node_Type node_type = builder->nodes[frame->node].node_type;
        bool is_list = node_type == AST_NODE_TYPE_BLOCK || node_type == AST_NODE_TYPE_PROCEDURE ||
                       node_type == AST_NODE_TYPE_DEFER_STATEMENT;
        bool is_jump = node_type == AST_NODE_TYPE_BREAK_STATEMENT ||
                       node_type == AST_NODE_TYPE_CONTINUE_STATEMENT ||
                       node_type == AST_NODE_TYPE_RETURN_STATEMENT;

        Ast_Node_Index pushed = AST_NODE_INDEX_NONE;
        if (is_jump) {
            // The deferred statements of the blocks that the jump leaves come first, the innermost
            // block first, and then the jump itself.
            pushed = x64_defer_next(builder, frames, frame);
            if (pushed == AST_NODE_INDEX_NONE) {
                Ast_Node_Index jump = frame->node;
                frame_count -= 1;
                x64_jump_end_write(builder, frames, frame_count, jump);
                continue;
            }
        } else if (frame->next == AST_NODE_INDEX_NONE) {
            // A block that ends with a jump never gets to its end, and has written its deferred
            // statements already.
            if (is_list) {
                Ast_Node_Index last = builder->nodes[frame->node].last_child;
                if (last != AST_NODE_INDEX_NONE &&
                    not x64_node_type_is_jump(builder->nodes[last].node_type)) {
                    pushed = x64_defer_next(builder, frames, frame);
                }
            }
            if (pushed == AST_NODE_INDEX_NONE) {
                // The list or the statement ends here, so the jumps to its end come here, and the
                // statement that a block is of goes on after it.
                x64_jumps_resolve(builder, frame->false_jumps);
                x64_jumps_resolve(builder, frame->end_jumps);
                Ast_Node_Index finished = frame->node;
                frame_count -= 1;
                if (frame_count > 0 && node_type == AST_NODE_TYPE_BLOCK) {
                    x64_block_end(builder, &frames[frame_count - 1], finished);
                }
                if (node_type == AST_NODE_TYPE_DEFER_STATEMENT) {
                    x64_debug_row_add(builder, X64_DEBUG_ROW_KIND_BLOCK_END, 0);
                }
                continue;
            }
        } else {
            Ast_Node_Index child = frame->next;
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
            } else if (is_list) {
                frame->next = builder->nodes[child].next_sibling;
                is_ok = x64_list_statement_write(builder, procedure, child, &pushed);
            } else {
                // A for statement, an infinite loop or a scope, whose block is next, after the
                // label of a scope.
                frame->next = AST_NODE_INDEX_NONE;
                pushed = builder->nodes[frame->node].last_child;
                assert(builder->nodes[pushed].node_type == AST_NODE_TYPE_BLOCK);
            }
            if (not is_ok) {
                return false;
            }
        }
        if (pushed == AST_NODE_INDEX_NONE) {
            continue;
        }

        if (frame_count == X64_STATEMENT_DEPTH_MAX) {
            return x64_fail_unsupported(builder);
        }
        frames[frame_count] = x64_statement_frame_make(pushed,
                                                       builder->nodes[pushed].first_child);
        X64_Statement_Frame *pushed_frame = &frames[frame_count];
        frame_count += 1;
        Ast_Node_Type pushed_type = builder->nodes[pushed].node_type;
        if (pushed_type == AST_NODE_TYPE_MATCH_STATEMENT) {
            // The cases follow the subject, which was written already.
            pushed_frame->next =
                builder->nodes[ast_match_subject(builder->nodes, pushed)].next_sibling;
        } else if (pushed_type == AST_NODE_TYPE_FOR_STATEMENT ||
                   pushed_type == AST_NODE_TYPE_LOOP_STATEMENT) {
            // The loop starts with its test, which `x64_list_statement_write` wrote, and a pass
            // goes back to it.
            pushed_frame->loop_start = builder->loop_start;
            pushed_frame->end_jumps = builder->loop_exit_jumps;
        } else if (x64_node_type_is_jump(pushed_type)) {
            // The jump writes the deferred statements of the list that it is in, and then of each
            // list around it.
            pushed_frame->exit_level = (uint32_t)(frame_count - 2);
            pushed_frame->defer_bound = pushed;
        } else if (pushed_type == AST_NODE_TYPE_DEFER_STATEMENT) {
            // Each copy of a deferred statement is a block of its own for a debugger, so that a
            // breakpoint on its line stops at every copy, see `x64_debug_blocks_write`.
            x64_debug_row_add(builder, X64_DEBUG_ROW_KIND_BLOCK_START, 0);
        }
    }

    if (frame_count != 0) {
        return x64_fail_unsupported(builder);
    }
    return true;
}

/**
 * Returns the frame of the statements of `node`, whose next child to write is `next`, with no jumps
 * that wait for it.
 */
static X64_Statement_Frame x64_statement_frame_make(Ast_Node_Index node, Ast_Node_Index next) {
    return (X64_Statement_Frame){
        .node = node,
        .next = next,
        .defer_bound = AST_NODE_INDEX_NONE,
        .end_jumps = X64_JUMP_NONE,
        .false_jumps = X64_JUMP_NONE,
        .continue_jumps = X64_JUMP_NONE,
        .loop_start = 0,
        .exit_level = 0,
    };
}

/**
 * Returns whether a statement of `node_type` leaves its block, so that the statements after it are
 * not reached.
 */
static bool x64_node_type_is_jump(Ast_Node_Type node_type) {
    return node_type == AST_NODE_TYPE_BREAK_STATEMENT ||
           node_type == AST_NODE_TYPE_CONTINUE_STATEMENT ||
           node_type == AST_NODE_TYPE_RETURN_STATEMENT;
}

/**
 * Writes the start of the statement `statement` of a list: the whole of a statement that has no
 * blocks, or the start of one that has, which the caller then makes a frame for, and which is set
 * in `pushed`. A `defer` writes nothing where it is, and a label gives its statement a name that
 * only the jumps need, so the statement after it is written instead. Returns false after it
 * recorded an error.
 */
static bool x64_list_statement_write(X64_Builder *builder, Ast_Node_Index procedure,
                                     Ast_Node_Index statement, Ast_Node_Index *pushed) {
    assert(*pushed == AST_NODE_INDEX_NONE);

    // A `defer` has no code where it is, so it has no line there: its line is at each place where
    // its statement is written, which is where a debugger stops for it.
    if (builder->nodes[statement].node_type == AST_NODE_TYPE_DEFER_STATEMENT) {
        return true;
    }
    x64_debug_row_add(builder, X64_DEBUG_ROW_KIND_LINE, builder->nodes[statement].source_offset);
    Ast_Node_Index current = statement;
    if (builder->nodes[current].node_type == AST_NODE_TYPE_LABELED_STATEMENT) {
        current = builder->nodes[current].first_child;
    }

    switch (builder->nodes[current].node_type) {
    case AST_NODE_TYPE_BREAK_STATEMENT:
    case AST_NODE_TYPE_CONTINUE_STATEMENT:
    case AST_NODE_TYPE_IF_STATEMENT:
    case AST_NODE_TYPE_SCOPE_STATEMENT:
        *pushed = current;
        return true;
    case AST_NODE_TYPE_FOR_STATEMENT:
        *pushed = current;
        return x64_for_start_write(builder, current);
    case AST_NODE_TYPE_LOOP_STATEMENT:
        *pushed = current;
        builder->loop_start = (uint32_t)(builder->writer.length - builder->text_start);
        builder->loop_exit_jumps = X64_JUMP_NONE;
        return true;
    case AST_NODE_TYPE_MATCH_STATEMENT:
        *pushed = current;
        return x64_match_start_write(builder, current);
    case AST_NODE_TYPE_RETURN_STATEMENT:
        *pushed = current;
        return x64_return_value_write(builder, procedure, current);
    default:
        break;
    }
    return x64_statement_write(builder, procedure, current);
}

/**
 * Writes the start of the for statement `statement` over a range: the first number goes to the
 * iterator and the end to the place after it, see `x64_procedure_layout`, so that the end is read
 * once, and then the test that the loop starts with, which leaves the loop when the iterator is not
 * below the end, or above it for an inclusive range. The place of the test and the jump that leaves
 * are kept in `builder` for the frame of the loop. Returns false after it recorded an error.
 */
static bool x64_for_start_write(X64_Builder *builder, Ast_Node_Index statement) {
    assert(builder->nodes[statement].node_type == AST_NODE_TYPE_FOR_STATEMENT);

    Ast_Node_Index range = builder->nodes[statement].first_child;
    if (builder->nodes[range].node_type != AST_NODE_TYPE_RANGE) {
        return x64_for_each_start_write(builder, statement);
    }
    int32_t iterator = -(int32_t)builder->layouts[statement].frame_offset;
    int32_t end = iterator + X64_WORD_SIZE_BYTES;
    X64_Type int_type = x64_type_make(X64_TYPE_KIND_INT);
    Ast_Node_Index bounds[2] = {
        builder->nodes[range].first_child,
        builder->nodes[range].last_child,
    };
    int32_t places[2] = {iterator, end};
    for (size_t i = 0; i < 2; i += 1) {
        if (not x64_type_accepts(builder, int_type, bounds[i])) {
            return x64_fail(builder, X64_BUILD_STATUS_RANGE_TYPE_INVALID,
                            x64_expression_subject(builder, bounds[i]));
        }
        if (not x64_expression_write(builder, bounds[i], int_type, X64_MODE_VALUE, true)) {
            return false;
        }
        x64_value_store(builder, int_type, places[i]);
    }

    builder->loop_start = (uint32_t)(builder->writer.length - builder->text_start);
    builder->loop_exit_jumps = X64_JUMP_NONE;
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP, iterator);
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RCX, X64_REGISTER_RBP, end);
    x64_write_registers(builder, X64_OPCODE_CMP, X64_REGISTER_RCX, X64_REGISTER_RAX);
    byte condition = X64_CONDITION_GREATER_EQUAL;
    if (builder->nodes[range].payload.range.is_inclusive) {
        condition = X64_CONDITION_GREATER;
    }
    x64_jump_write(builder, condition, &builder->loop_exit_jumps);
    return true;
}

/**
 * Writes the start of the for statement `statement` that goes through the elements of an array or
 * an array slice: the address of the first element and the address after the last one are kept
 * after the iterator, see `x64_procedure_layout`, which reads the address and the length once.
 * Then comes the start of the loop: a test that leaves it when the next address is not below the
 * end, the copy of the element there to the iterator, and the move of the address to the next
 * element, so that a `continue` needs only to go back to the test. Returns false after it recorded
 * an error.
 */
static bool x64_for_each_start_write(X64_Builder *builder, Ast_Node_Index statement) {
    Ast_Node_Index sequence = builder->nodes[statement].first_child;
    X64_Type type = x64_expression_type(builder, sequence);
    if (type.kind != X64_TYPE_KIND_ARRAY && type.kind != X64_TYPE_KIND_SLICE) {
        return x64_fail(builder, X64_BUILD_STATUS_ARRAY_EXPECTED,
                        x64_expression_subject(builder, sequence));
    }
    X64_Type element = x64_type_element(type);
    size_t stride = x64_type_stride_bytes(builder, element);
    int32_t iterator = -(int32_t)builder->layouts[statement].frame_offset;
    int32_t next =
        iterator + (int32_t)(x64_type_word_count(builder, element) * X64_WORD_SIZE_BYTES);
    int32_t end = next + X64_WORD_SIZE_BYTES;

    if (not x64_expression_write(builder, sequence, type, X64_MODE_VALUE, true)) {
        return false;
    }
    size_t sequence_words = x64_type_word_count(builder, type);
    int32_t place = x64_stack_displacement(builder, builder->stack_words - sequence_words,
                                           sequence_words);
    // The address, and the end: the address plus the length times the size of an element.
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP, place);
    x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, next);
    x64_sequence_length_load(builder, type, X64_REGISTER_RCX, place);
    x64_write_load_integer(builder, X64_REGISTER_RDX, (int64_t)stride);
    x64_write_multiply(builder, X64_REGISTER_RCX, X64_REGISTER_RDX);
    x64_write_registers(builder, X64_OPCODE_ADD, X64_REGISTER_RAX, X64_REGISTER_RCX);
    x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RCX, X64_REGISTER_RBP, end);
    x64_stack_pop(builder, sequence_words);

    builder->loop_start = (uint32_t)(builder->writer.length - builder->text_start);
    builder->loop_exit_jumps = X64_JUMP_NONE;
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP, next);
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RCX, X64_REGISTER_RBP, end);
    x64_write_registers(builder, X64_OPCODE_CMP, X64_REGISTER_RCX, X64_REGISTER_RAX);
    x64_jump_write(builder, X64_CONDITION_ABOVE_EQUAL, &builder->loop_exit_jumps);
    x64_write_registers(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_R10);
    x64_memory_load(builder, element, X64_REGISTER_R10);
    x64_value_store(builder, element, iterator);
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP, next);
    x64_write_load_integer(builder, X64_REGISTER_RCX, (int64_t)stride);
    x64_write_registers(builder, X64_OPCODE_ADD, X64_REGISTER_RCX, X64_REGISTER_RAX);
    x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, next);
    return true;
}

/**
 * Returns the next deferred statement that `frame` writes, after the ones that it wrote, or
 * `AST_NODE_INDEX_NONE` when there are no more, and moves the frame on. A list at its end writes
 * the deferred statements of its own `defer`s, and a jump those of every list that it leaves, from
 * the one that it is in out to the one of the statement that it goes to, or the body for a
 * `return`. In each list they run in the reverse order of the `defer`s, and only the ones that the
 * jump has passed, which are the ones before it: the nodes are in the order of the source, so they
 * are the `defer`s whose nodes are before the jump. The statement that a deferred statement is is
 * written by a frame of the `defer`, which is a list with that one statement.
 */
static Ast_Node_Index x64_defer_next(X64_Builder *builder, X64_Statement_Frame const *frames,
                                     X64_Statement_Frame *frame) {
    Ast_Node_Type node_type = builder->nodes[frame->node].node_type;
    bool is_jump = x64_node_type_is_jump(node_type);

    if (not is_jump) {
        Ast_Node_Index defer = x64_defer_last_before(builder, frame->node, frame->defer_bound);
        if (defer != AST_NODE_INDEX_NONE) {
            frame->defer_bound = defer;
        }
        return defer;
    }

    // The lowest list that the jump leaves is the body for a `return`, and the block of the
    // statement that it goes to for a `break` or a `continue`, which is the frame right above the
    // frame of that statement.
    size_t lowest = 0;
    if (node_type != AST_NODE_TYPE_RETURN_STATEMENT) {
        Ast_Node_Index target = builder->bindings[frame->node].target;
        for (size_t i = frame->exit_level + 1; i > 0; i -= 1) {
            if (frames[i - 1].node == target) {
                lowest = i;
                break;
            }
        }
    }
    // Each step goes to a list further out, so there are fewer steps than frames.
    for (size_t i = 0; i <= X64_STATEMENT_DEPTH_MAX; i += 1) {
        Ast_Node_Index list = frames[frame->exit_level].node;
        Ast_Node_Type list_type = builder->nodes[list].node_type;
        if (list_type == AST_NODE_TYPE_BLOCK || list_type == AST_NODE_TYPE_PROCEDURE) {
            Ast_Node_Index defer = x64_defer_last_before(builder, list, frame->defer_bound);
            if (defer != AST_NODE_INDEX_NONE) {
                frame->defer_bound = defer;
                return defer;
            }
        }
        if (frame->exit_level <= lowest) {
            return AST_NODE_INDEX_NONE;
        }
        // The jump is after every `defer` before it in the lists further out too.
        frame->exit_level -= 1;
        frame->defer_bound = frame->node;
    }
    assert(false);
    return AST_NODE_INDEX_NONE;
}

/**
 * Returns the last `defer` among the statements of `list`, a block or the body of a procedure,
 * whose node is before `bound`, or `AST_NODE_INDEX_NONE` when there is none. `bound` is
 * `AST_NODE_INDEX_NONE`, which is after every node, for all of them.
 */
static Ast_Node_Index x64_defer_last_before(X64_Builder const *builder, Ast_Node_Index list,
                                            Ast_Node_Index bound) {
    Ast_Node_Index found = AST_NODE_INDEX_NONE;
    Ast_Node_Index child = builder->nodes[list].first_child;
    // Every child is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && child != AST_NODE_INDEX_NONE && child < bound;
         i += 1) {
        if (builder->nodes[child].node_type == AST_NODE_TYPE_DEFER_STATEMENT) {
            found = child;
        }
        child = builder->nodes[child].next_sibling;
    }
    return found;
}

/**
 * Writes the jump `jump` itself, after the deferred statements of the blocks that it leaves: a
 * `return` gives the value that it left on the stack of values to the caller in `rax` and returns,
 * and a `break` or a `continue` jumps to the end of the statement that it leaves, or to where the
 * loop that it goes on with goes on, which join the chains of that statement. `frames` are the
 * `frame_count` frames below the jump.
 */
static void x64_jump_end_write(X64_Builder *builder, X64_Statement_Frame *frames,
                               size_t frame_count, Ast_Node_Index jump) {
    assert(frame_count > 0);

    Ast_Node_Type node_type = builder->nodes[jump].node_type;
    if (node_type == AST_NODE_TYPE_RETURN_STATEMENT) {
        Ast_Node_Index procedure = builder->nodes[builder->definition].first_child;
        X64_Type type = x64_return_type(builder, procedure);
        if (x64_result_is_hidden(builder, procedure)) {
            // The result goes to the place that the caller made for it, whose address is the
            // first word of the arguments, and the address is returned.
            x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_R10, X64_REGISTER_RBP,
                             X64_ARGUMENT_HOME_OFFSET_BYTES);
            x64_memory_store(builder, type, X64_REGISTER_R10);
            x64_write_registers(builder, X64_OPCODE_STORE, X64_REGISTER_R10, X64_REGISTER_RAX);
        } else {
            x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP,
                             x64_stack_displacement(builder, builder->stack_words - 1, 1));
            x64_stack_pop(builder, 1);
        }
        x64_epilogue_write(builder);
        return;
    }

    Ast_Node_Index target = builder->bindings[jump].target;
    for (size_t i = frame_count; i > 0; i -= 1) {
        X64_Statement_Frame *frame = &frames[i - 1];
        if (frame->node != target) {
            continue;
        }
        if (node_type == AST_NODE_TYPE_BREAK_STATEMENT) {
            x64_jump_write(builder, X64_CONDITION_ALWAYS, &frame->end_jumps);
        } else {
            x64_jump_write(builder, X64_CONDITION_ALWAYS, &frame->continue_jumps);
        }
        return;
    }
    // The analysis binds every jump to a statement around it.
    assert(false);
}

/**
 * Writes what comes after the block `block` of the statement of `frame`. When an if statement or a
 * match statement has more branches, that is a jump to its end, over the other branches, and then
 * the place that the jump of the condition of the branch goes to when it does not hold, which is
 * the next branch. A block that ends with a jump never gets to its end, so it needs no jump. A
 * loop goes on with its next pass: a for statement makes its iterator one more, which `continue`
 * comes to as well, and then the loop jumps back to its start. An inclusive range leaves before it
 * makes the iterator more when the iterator is the end, so that a range that ends at the largest
 * `Int` ends instead of wrapping around.
 */
static void x64_block_end(X64_Builder *builder, X64_Statement_Frame *frame,
                          Ast_Node_Index block) {
    assert(builder->nodes[block].node_type == AST_NODE_TYPE_BLOCK);

    Ast_Node_Index statement = frame->node;
    Ast_Node_Type node_type = builder->nodes[statement].node_type;
    if (node_type == AST_NODE_TYPE_FOR_STATEMENT || node_type == AST_NODE_TYPE_LOOP_STATEMENT) {
        x64_jumps_resolve(builder, frame->continue_jumps);
        frame->continue_jumps = X64_JUMP_NONE;
        Ast_Node_Index range = builder->nodes[statement].first_child;
        bool is_range = node_type == AST_NODE_TYPE_FOR_STATEMENT &&
                        builder->nodes[range].node_type == AST_NODE_TYPE_RANGE;
        if (is_range) {
            int32_t iterator = -(int32_t)builder->layouts[statement].frame_offset;
            x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP,
                             iterator);
            if (builder->nodes[range].payload.range.is_inclusive) {
                x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RCX, X64_REGISTER_RBP,
                                 iterator + X64_WORD_SIZE_BYTES);
                x64_write_registers(builder, X64_OPCODE_CMP, X64_REGISTER_RCX, X64_REGISTER_RAX);
                x64_jump_write(builder, X64_CONDITION_EQUAL, &frame->end_jumps);
            }
            x64_write_load_integer(builder, X64_REGISTER_RCX, 1);
            x64_write_registers(builder, X64_OPCODE_ADD, X64_REGISTER_RCX, X64_REGISTER_RAX);
            x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP,
                             iterator);
        }
        x64_jump_back_write(builder, frame->loop_start);
        return;
    }

    if (node_type != AST_NODE_TYPE_IF_STATEMENT && node_type != AST_NODE_TYPE_MATCH_STATEMENT) {
        return;
    }
    if (frame->next != AST_NODE_INDEX_NONE) {
        Ast_Node_Index last = builder->nodes[block].last_child;
        if (not x64_node_type_is_jump(builder->nodes[last].node_type)) {
            x64_jump_write(builder, X64_CONDITION_ALWAYS, &frame->end_jumps);
        }
    }
    x64_jumps_resolve(builder, frame->false_jumps);
    frame->false_jumps = X64_JUMP_NONE;
}

/**
 * Writes `jmp` back to `target`, a place in the code that is written already, which is how a loop
 * goes back to its start. The distance is negative, in two's complement.
 */
static void x64_jump_back_write(X64_Builder *builder, uint32_t target) {
    x64_write_byte(&builder->writer, 0xE9);
    size_t end = builder->writer.length + 4 - builder->text_start;
    assert(target < end);
    x64_write_u32(&builder->writer, (uint32_t)((int64_t)target - (int64_t)end));
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
    if (not x64_type_accepts(builder, bool_type, condition)) {
        return x64_fail(builder, X64_BUILD_STATUS_CONDITION_TYPE_INVALID,
                        x64_expression_subject(builder, condition));
    }
    x64_debug_row_add(builder, X64_DEBUG_ROW_KIND_LINE, builder->nodes[condition].source_offset);
    if (not x64_expression_write(builder, condition, bool_type, X64_MODE_VALUE, true)) {
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
 * and every case compares the same value. The subject is an `Int`, a `Byte`, a `Str` or an enum,
 * every pattern is one of that type, see `x64_match_pattern`, and no two patterns match a value in
 * common, so the order of the cases does not change what the statement does. A match of an enum
 * without a default case has a case for every member, unless it is `#(partial)`, which only a
 * match of an enum without a default case can be. Returns false after it recorded an error.
 */
static bool x64_match_start_write(X64_Builder *builder, Ast_Node_Index match) {
    assert(builder->nodes[match].node_type == AST_NODE_TYPE_MATCH_STATEMENT);

    Ast_Node_Index subject = ast_match_subject(builder->nodes, match);
    X64_Type subject_type = builder->layouts[match].type;
    if (subject_type.kind == X64_TYPE_KIND_ENUM_LITERAL) {
        return x64_fail(builder, X64_BUILD_STATUS_ENUM_TYPE_UNKNOWN,
                        builder->nodes[subject].payload.enum_literal.name);
    }
    bool is_valid = subject_type.kind == X64_TYPE_KIND_BYTE ||
                    subject_type.kind == X64_TYPE_KIND_ENUM ||
                    subject_type.kind == X64_TYPE_KIND_INT ||
                    subject_type.kind == X64_TYPE_KIND_STR;
    if (not is_valid) {
        if (subject_type.kind == X64_TYPE_KIND_NONE &&
            builder->nodes[subject].node_type != AST_NODE_TYPE_CALL) {
            return x64_fail_unsupported(builder);
        }
        return x64_fail(builder, X64_BUILD_STATUS_MATCH_SUBJECT_TYPE_INVALID,
                        x64_expression_subject(builder, subject));
    }

    Ast_Node_Index first_case = builder->nodes[subject].next_sibling;
    Ast_Node_Index last_case = builder->nodes[match].last_child;
    bool has_default = ast_match_case_pattern(builder->nodes, last_case) == AST_NODE_INDEX_NONE;
    // The analysis allows `partial` as the only attribute, once, so it is the first child if any.
    Ast_Node_Index attribute = builder->nodes[match].first_child;
    bool is_partial = builder->nodes[attribute].node_type == AST_NODE_TYPE_ATTRIBUTE;
    if (is_partial && (subject_type.kind != X64_TYPE_KIND_ENUM || has_default)) {
        return x64_fail(builder, X64_BUILD_STATUS_MATCH_PARTIAL_INVALID,
                        builder->nodes[attribute].payload.attribute.name);
    }

    size_t pattern_count = 0;
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
        if (checked.status == X64_BUILD_STATUS_ENUM_MEMBER_UNKNOWN) {
            // The member that is not there says more than the word of the case.
            return x64_fail(builder, checked.status,
                            x64_enum_member_name_of(builder, pattern, subject_type.definition));
        }
        if (checked.status != X64_BUILD_STATUS_OK) {
            return x64_fail(builder, checked.status, keyword);
        }
        Ast_Node_Index earlier = first_case;
        for (size_t j = 0; j < i; j += 1) {
            X64_Match_Pattern other = x64_match_pattern(
                builder, subject_type, ast_match_case_pattern(builder->nodes, earlier));
            assert(other.status == X64_BUILD_STATUS_OK);
            if (x64_match_patterns_overlap(builder, subject_type, other, checked)) {
                return x64_fail(builder, X64_BUILD_STATUS_MATCH_CASE_DUPLICATE, keyword);
            }
            earlier = builder->nodes[earlier].next_sibling;
        }
        pattern_count += 1;
        match_case = builder->nodes[match_case].next_sibling;
    }

    // Every pattern is a different member, so the match has a case for each one when it has as
    // many patterns as the enum has members.
    if (subject_type.kind == X64_TYPE_KIND_ENUM && not has_default && not is_partial) {
        size_t member_count = 0;
        for (Ast_Node_Index member = builder->nodes[subject_type.definition].first_child;
             member != AST_NODE_INDEX_NONE; member = builder->nodes[member].next_sibling) {
            member_count += 1;
        }
        if (pattern_count != member_count) {
            return x64_fail(builder, X64_BUILD_STATUS_MATCH_NOT_EXHAUSTIVE,
                            builder->nodes[match].payload.match.keyword);
        }
    }

    if (not x64_expression_write(builder, subject, subject_type, X64_MODE_VALUE, true)) {
        return false;
    }
    return x64_value_store(builder, subject_type,
                           -(int32_t)builder->layouts[match].frame_offset);
}

/**
 * Returns what `pattern`, the pattern of a case of a match statement whose subject is of the type
 * `subject`, matches, see `X64_Match_Pattern`. A pattern is known when the program is compiled: for
 * an enum it is a member of the enum of the subject, written `.RED` or `Color.RED`, and for an
 * `Int`, a `Byte` or a `Str` it is a constant of that type, or for an `Int` and a `Byte` a range of
 * constants, which must have a number in it. The status says what is wrong otherwise.
 */
static X64_Match_Pattern x64_match_pattern(X64_Builder const *builder, X64_Type subject,
                                           Ast_Node_Index pattern) {
    assert(pattern < builder->node_count);

    X64_Match_Pattern result = {
        .low = 0,
        .high = 0,
        .string = AST_NODE_INDEX_NONE,
        .status = X64_BUILD_STATUS_OK,
    };
    Ast_Node const *node = &builder->nodes[pattern];
    if (subject.kind == X64_TYPE_KIND_ENUM) {
        Str member = x64_enum_member_name_of(builder, pattern, subject.definition);
        if (member.bytes == NULL) {
            result.status = X64_BUILD_STATUS_MATCH_PATTERN_TYPE_MISMATCH;
        } else if (not x64_enum_member_find(builder, subject.definition, member, &result.low)) {
            result.status = X64_BUILD_STATUS_ENUM_MEMBER_UNKNOWN;
        }
        result.high = result.low;
        return result;
    }
    // A member of an enum is known when the program is compiled, but it is a pattern of the wrong
    // type for the other subjects.
    if (node->node_type == AST_NODE_TYPE_ENUM_LITERAL ||
        x64_enum_of_access(builder, pattern) != AST_NODE_INDEX_NONE) {
        result.status = X64_BUILD_STATUS_MATCH_PATTERN_TYPE_MISMATCH;
        return result;
    }

    Ast_Node_Index ends[2] = {pattern, pattern};
    if (node->node_type == AST_NODE_TYPE_RANGE) {
        if (subject.kind == X64_TYPE_KIND_STR) {
            result.status = X64_BUILD_STATUS_MATCH_PATTERN_TYPE_MISMATCH;
            return result;
        }
        ends[0] = node->first_child;
        ends[1] = node->last_child;
    }
    for (size_t i = 0; i < 2; i += 1) {
        if (not x64_is_constant(builder, ends[i])) {
            result.status = X64_BUILD_STATUS_MATCH_PATTERN_NOT_CONSTANT;
            return result;
        }
        if (not x64_type_accepts(builder, subject, ends[i])) {
            result.status = X64_BUILD_STATUS_MATCH_PATTERN_TYPE_MISMATCH;
            return result;
        }
    }
    if (subject.kind == X64_TYPE_KIND_STR) {
        result.string = builder->constant_values[pattern].string;
        assert(result.string != AST_NODE_INDEX_NONE);
        return result;
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
 * Returns whether the patterns `first` and `second`, which `x64_match_pattern` accepts for a
 * subject of `subject`, match a value in common: strings with the same bytes, or numbers whose
 * ranges overlap.
 */
static bool x64_match_patterns_overlap(X64_Builder const *builder, X64_Type subject,
                                       X64_Match_Pattern first, X64_Match_Pattern second) {
    if (subject.kind == X64_TYPE_KIND_STR) {
        return str_equals(builder->nodes[first.string].payload.string_literal.text,
                          builder->nodes[second.string].payload.string_literal.text);
    }
    return first.low <= second.high && second.low <= first.high;
}

/**
 * Writes the case `first` of the match statement of `frame`, and the cases after it that have no
 * block of their own and share the block of the last of them: the subject is compared with the
 * pattern of each, and a pattern that matches jumps to the block, which comes after the
 * comparisons, and when none does the jump over the block goes to the next case. A range compares
 * the subject with its two ends, with the signed order of an `Int`, and a `Str` is compared by its
 * length and its bytes, see `x64_match_string_write`. The default case has no
 * pattern, and its block always runs when it is reached. The next case of the frame is set to the
 * one after the block. Returns the block, which the caller writes next.
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
        } else if (subject_type.kind == X64_TYPE_KIND_STR) {
            X64_Match_Pattern checked = x64_match_pattern(builder, subject_type, pattern);
            assert(checked.status == X64_BUILD_STATUS_OK);
            x64_match_string_write(builder, subject, checked.string, &block_jumps);
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

    if (not has_default) {
        x64_jump_write(builder, X64_CONDITION_ALWAYS, &frame->false_jumps);
    }
    x64_jumps_resolve(builder, block_jumps);
    frame->next = builder->nodes[match_case].next_sibling;
    return block;
}

/**
 * Writes the comparison of the `Str` at `rbp + subject` with the string literal `literal`, which
 * jumps to the block of the case, joining `block_jumps`, when they have the same bytes, and goes on
 * after it when they do not. The lengths are compared first, and then each byte of the subject with
 * the byte of the literal, which is a number in the instruction, because the literal is known when
 * the program is compiled. The empty string is compared by its length only.
 */
static void x64_match_string_write(X64_Builder *builder, int32_t subject, Ast_Node_Index literal,
                                   uint32_t *block_jumps) {
    Str text = builder->nodes[literal].payload.string_literal.text;
    uint32_t different_jumps = X64_JUMP_NONE;
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP,
                     subject + X64_WORD_SIZE_BYTES);
    x64_write_load_integer(builder, X64_REGISTER_RCX, (int64_t)text.length);
    x64_write_registers(builder, X64_OPCODE_CMP, X64_REGISTER_RCX, X64_REGISTER_RAX);
    x64_jump_write(builder, X64_CONDITION_NOT_EQUAL, &different_jumps);
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_R10, X64_REGISTER_RBP, subject);
    for (size_t i = 0; i < text.length; i += 1) {
        x64_write_memory_two_byte(builder, X64_OPCODE_TWO_BYTE_MOVZX_BYTE, X64_REGISTER_RAX,
                                  X64_REGISTER_R10, (int32_t)i);
        x64_write_load_integer(builder, X64_REGISTER_RCX, (byte)text.bytes[i]);
        x64_write_registers(builder, X64_OPCODE_CMP, X64_REGISTER_RCX, X64_REGISTER_RAX);
        x64_jump_write(builder, X64_CONDITION_NOT_EQUAL, &different_jumps);
    }
    x64_jump_write(builder, X64_CONDITION_ALWAYS, block_jumps);
    x64_jumps_resolve(builder, different_jumps);
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
 * Writes the statement `statement` of `procedure`, which has no blocks and does not leave its
 * block. It leaves the stack of values as deep as it found it. Returns false after it recorded an
 * error.
 */
static bool x64_statement_write(X64_Builder *builder, Ast_Node_Index procedure,
                                Ast_Node_Index statement) {
    assert(statement < builder->node_count);
    (void)procedure;

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
                                    x64_type_make(X64_TYPE_KIND_NONE), X64_MODE_VALUE, false);
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
 * Writes the value of a `return` statement, which is worked out on the stack of values and stays
 * there while the deferred statements of the blocks that the return leaves are written, see
 * `x64_jump_end_write`, which then gives it to the caller. Returns false after it recorded an
 * error.
 */
static bool x64_return_value_write(X64_Builder *builder, Ast_Node_Index procedure,
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
        builder->nodes[value].node_type != AST_NODE_TYPE_CALL &&
        builder->nodes[value].node_type != AST_NODE_TYPE_STRUCT_LITERAL) {
        // An expression that the target does not build has no type.
        return x64_fail_unsupported(builder);
    }
    if (not x64_type_accepts(builder, return_type, value)) {
        // The call of a procedure that returns nothing is in here too.
        return x64_fail(builder, X64_BUILD_STATUS_RETURN_TYPE_MISMATCH, name);
    }
    // The temporary strings are given back by the return, so a pointer to one must not leave. A
    // string that is a `C_Str` is one, and a `C_Str` variable may hold one, so neither can be
    // returned either. A string that is a `Str` is a view of the bytes of the program, which stay.
    if (return_type.kind == X64_TYPE_KIND_C_STR) {
        if (x64_call_is_str_to_c_str(builder, value) ||
            x64_string_literal_of(builder, value) != AST_NODE_INDEX_NONE ||
            builder->bindings[value].kind == SEMANTIC_BINDING_KIND_VARIABLE) {
            return x64_fail_unsupported(builder);
        }
    }

    return x64_expression_write(builder, value, return_type, X64_MODE_VALUE, true);
}

/**
 * Writes the definition of a variable, `name := value`, `name : Type = value` or `name : Type`,
 * which stores the value in the place of the variable. Without a value the variable gets the
 * default value of its type, which is zero in each of its words: 0 for an `Int` and a `Byte`,
 * `false` for a `Bool`, the address 0 for a `C_Str` and `nil` for a pointer, and an array slice of
 * no elements. A variable of an array type holds the address of the elements of the array literal
 * that it is defined with, which are in a place of their own in the frame, and it has no default
 * value. Returns false after it recorded an error.
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
            return x64_fail(builder, X64_BUILD_STATUS_TYPE_UNSUPPORTED,
                            x64_type_subject(builder, type_node));
        }
        value = builder->nodes[type_node].next_sibling;
    }
    // The members of an enum are an array that is in `.data`, whose address is not copied either.
    if (type.kind == X64_TYPE_KIND_ARRAY &&
        (value == AST_NODE_INDEX_NONE ||
         (builder->nodes[value].node_type != AST_NODE_TYPE_ARRAY_LITERAL &&
          not x64_type_info_access_is(builder, value)))) {
        return x64_fail(builder, X64_BUILD_STATUS_ARRAY_INITIALIZER_INVALID, name);
    }

    if (value == AST_NODE_INDEX_NONE) {
        // The default value of every type of the target is zero in each of its words.
        x64_write_load_integer(builder, X64_REGISTER_RAX, 0);
        for (size_t i = 0; i < x64_type_word_count(builder, type); i += 1) {
            x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP,
                             displacement + (int32_t)(i * X64_WORD_SIZE_BYTES));
        }
        return true;
    }

    if (type.kind == X64_TYPE_KIND_NIL) {
        // `nil` does not say what it points to.
        return x64_fail(builder, X64_BUILD_STATUS_NIL_TYPE_UNKNOWN, name);
    }
    if (type.kind == X64_TYPE_KIND_ENUM_LITERAL) {
        // `.RED` does not say which enum it is of.
        return x64_fail(builder, X64_BUILD_STATUS_ENUM_TYPE_UNKNOWN, name);
    }
    if (type.kind == X64_TYPE_KIND_NONE) {
        // A variable without a type takes the type of its value, which the target does not build
        // when it has none. The call of a procedure that returns nothing has none either, which
        // the call reports as a call without a result.
        if (builder->nodes[value].node_type == AST_NODE_TYPE_STRUCT_LITERAL) {
            // A literal without a name, which has no type of its own.
            return x64_fail(builder, X64_BUILD_STATUS_STRUCT_EXPECTED, name);
        }
        if (builder->nodes[value].node_type != AST_NODE_TYPE_CALL) {
            return x64_fail_unsupported(builder);
        }
        return x64_expression_write(builder, value, type, X64_MODE_VALUE, true);
    }
    if (type_node != AST_NODE_INDEX_NONE && not x64_type_accepts(builder, type, value)) {
        return x64_fail(builder, X64_BUILD_STATUS_VARIABLE_TYPE_MISMATCH, name);
    }
    if (not x64_expression_write(builder, value, type, X64_MODE_VALUE, true)) {
        return false;
    }
    return x64_value_store(builder, type, displacement);
}

/**
 * Writes an assignment: `place = value`, which stores the value in the place, or one of `+=`, `-=`,
 * `*=` and `/=`, which works out the place and the value with the operator and stores the result.
 * The place is a variable of the procedure, a global variable, an element of an array or an array
 * slice, a field, or what a pointer points to, and its address is worked out before the value. A
 * slice as the place copies the elements of the value to the range, see
 * `x64_range_assignment_write`. Returns false after it recorded an error.
 */
static bool x64_assignment_write(X64_Builder *builder, Ast_Node_Index statement) {
    assert(ast_node_type_is_assignment(builder->nodes[statement].node_type));

    Ast_Node_Type node_type = builder->nodes[statement].node_type;
    Ast_Node_Index target = builder->nodes[statement].first_child;
    Ast_Node_Index value = builder->nodes[target].next_sibling;
    Ast_Node_Type target_type = builder->nodes[target].node_type;
    Str subject = x64_expression_subject(builder, target);

    X64_Type type = x64_type_make(X64_TYPE_KIND_NONE);
    X64_Build_Status mismatch = X64_BUILD_STATUS_VARIABLE_TYPE_MISMATCH;
    if (target_type == AST_NODE_TYPE_IDENTIFIER) {
        Semantic_Binding_Kind kind = builder->bindings[target].kind;
        if (kind != SEMANTIC_BINDING_KIND_VARIABLE && kind != SEMANTIC_BINDING_KIND_GLOBAL) {
            return x64_fail_unsupported(builder);
        }
        type = x64_expression_type(builder, target);
        if (type.kind == X64_TYPE_KIND_ARRAY) {
            // An array is not assigned to as a whole.
            return x64_fail(builder, X64_BUILD_STATUS_ARRAY_INITIALIZER_INVALID, subject);
        }
    } else if (target_type == AST_NODE_TYPE_SUBSCRIPT || target_type == AST_NODE_TYPE_DEREFERENCE ||
               target_type == AST_NODE_TYPE_FIELD_ACCESS) {
        type = x64_expression_type(builder, target);
        mismatch = X64_BUILD_STATUS_ARRAY_ELEMENT_TYPE_MISMATCH;
        if (target_type == AST_NODE_TYPE_DEREFERENCE) {
            mismatch = X64_BUILD_STATUS_POINTER_TARGET_TYPE_MISMATCH;
        }
        if (target_type == AST_NODE_TYPE_FIELD_ACCESS) {
            mismatch = X64_BUILD_STATUS_STRUCT_FIELD_TYPE_MISMATCH;
            subject = builder->nodes[target].payload.field_access.name;
            if (x64_field_target_is_read_only(builder, target)) {
                return x64_fail(builder, X64_BUILD_STATUS_FIELD_TARGET_READ_ONLY,
                                x64_expression_subject(builder, target));
            }
        }
    } else if (target_type == AST_NODE_TYPE_SLICE && node_type == AST_NODE_TYPE_ASSIGNMENT) {
        return x64_range_assignment_write(builder, statement);
    } else {
        return x64_fail_unsupported(builder);
    }

    // The address of the place, which the checks of the place report about, comes first.
    if (not x64_expression_write(builder, target, type, X64_MODE_TARGET, true)) {
        return false;
    }
    X64_Type value_type = type;
    if (node_type != AST_NODE_TYPE_ASSIGNMENT) {
        // The operators work on an `Int` only, the place and the value alike.
        value_type = x64_type_make(X64_TYPE_KIND_INT);
        if (type.kind != X64_TYPE_KIND_INT) {
            return x64_fail(builder, X64_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID, subject);
        }
        mismatch = X64_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID;
        subject = x64_expression_subject(builder, value);
    }
    if (not x64_type_accepts(builder, value_type, value)) {
        // An expression that the target does not build has no type.
        if (x64_expression_type_for(builder, value, value_type).kind == X64_TYPE_KIND_NONE &&
            builder->nodes[value].node_type != AST_NODE_TYPE_CALL) {
            return x64_fail_unsupported(builder);
        }
        if (type.kind == X64_TYPE_KIND_NIL) {
            return x64_fail(builder, X64_BUILD_STATUS_NIL_TYPE_UNKNOWN, subject);
        }
        return x64_fail(builder, mismatch, subject);
    }
    if (not x64_expression_write(builder, value, value_type, X64_MODE_VALUE, true)) {
        return false;
    }

    // The address is below the value on the stack.
    size_t value_words = x64_type_word_count(builder, value_type);
    int32_t address = x64_stack_displacement(builder, builder->stack_words - value_words - 1, 1);
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_R10, X64_REGISTER_RBP, address);
    if (node_type != AST_NODE_TYPE_ASSIGNMENT) {
        // `mov rax, [place]` and `mov rcx, value`, the operator, and the result is the value.
        int32_t operand = x64_stack_displacement(builder, builder->stack_words - 1, 1);
        x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_R10, 0);
        x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RCX, X64_REGISTER_RBP, operand);
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
        x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, operand);
    }
    x64_memory_store(builder, type, X64_REGISTER_R10);
    x64_stack_pop(builder, 1);
    return true;
}

/**
 * Returns whether the field `target` is assigned through a parameter or an iterator, which cannot
 * be assigned to: the fields lead to one without a pointer in between, which would make the field
 * a field of what the pointer points to, which can be assigned to.
 */
static bool x64_field_target_is_read_only(X64_Builder const *builder, Ast_Node_Index target) {
    Ast_Node_Index current = target;
    // Each step goes down one level, so there are fewer steps than nodes.
    for (size_t i = 0; i < builder->node_count; i += 1) {
        Ast_Node const *node = &builder->nodes[current];
        if (node->node_type == AST_NODE_TYPE_FIELD_ACCESS) {
            if (x64_expression_type(builder, node->first_child).kind == X64_TYPE_KIND_POINTER) {
                return false;
            }
            current = node->first_child;
            continue;
        }
        if (node->node_type != AST_NODE_TYPE_IDENTIFIER) {
            return false;
        }
        Semantic_Binding binding = builder->bindings[current];
        return binding.kind == SEMANTIC_BINDING_KIND_PARAMETER ||
               (binding.kind == SEMANTIC_BINDING_KIND_VARIABLE &&
                builder->nodes[binding.target].node_type == AST_NODE_TYPE_FOR_STATEMENT);
    }
    return false;
}

/**
 * Writes the assignment `a[low:high] = value` of `statement`, which copies the elements of the
 * value, an array or an array slice, to the range of elements of the slice on the left. It traps
 * unless the two have as many elements, and the copy is right also when the two ranges overlap,
 * see `x64_copy_write`. Returns false after it recorded an error.
 */
static bool x64_range_assignment_write(X64_Builder *builder, Ast_Node_Index statement) {
    Ast_Node_Index target = builder->nodes[statement].first_child;
    Ast_Node_Index value = builder->nodes[target].next_sibling;
    X64_Type type = x64_expression_type(builder, target);
    if (type.kind == X64_TYPE_KIND_NONE) {
        return x64_fail(builder, X64_BUILD_STATUS_ARRAY_EXPECTED,
                        x64_expression_subject(builder, target));
    }
    if (not x64_type_accepts(builder, type, value)) {
        return x64_fail(builder, X64_BUILD_STATUS_ARRAY_ELEMENT_TYPE_MISMATCH,
                        x64_expression_subject(builder, value));
    }
    if (not x64_expression_write(builder, target, type, X64_MODE_VALUE, true)) {
        return false;
    }
    if (not x64_expression_write(builder, value, type, X64_MODE_VALUE, true)) {
        return false;
    }

    int32_t source = x64_stack_displacement(builder, builder->stack_words - 2, 2);
    int32_t destination = x64_stack_displacement(builder, builder->stack_words - 4, 2);
    // `cmp` of the lengths and `je` over a breakpoint.
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RCX, X64_REGISTER_RBP,
                     destination + X64_WORD_SIZE_BYTES);
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP,
                     source + X64_WORD_SIZE_BYTES);
    x64_write_registers(builder, X64_OPCODE_CMP, X64_REGISTER_RCX, X64_REGISTER_RAX);
    x64_write_trap_unless(builder, X64_CONDITION_EQUAL);
    size_t stride = x64_type_stride_bytes(builder, x64_type_element(type));
    if (stride != 1) {
        x64_write_load_integer(builder, X64_REGISTER_RAX, (int64_t)stride);
        x64_write_multiply(builder, X64_REGISTER_RCX, X64_REGISTER_RAX);
    }
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_R10, X64_REGISTER_RBP, destination);
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_R11, X64_REGISTER_RBP, source);
    x64_copy_write(builder);
    x64_stack_pop(builder, 4);
    return true;
}

/**
 * Writes the copy of `rcx` bytes from the address in `r11` to the address in `r10`, one byte at a
 * time through `al`. When the target is above the source the bytes are copied from the last one
 * down, so that a byte is read before the copy of another one writes over it, and otherwise from
 * the first one up, which makes the copy right however the two overlap, like `memmove` of C.
 */
static void x64_copy_write(X64_Builder *builder) {
    uint32_t backward_jumps = X64_JUMP_NONE;
    uint32_t done_jumps = X64_JUMP_NONE;
    x64_write_registers(builder, X64_OPCODE_CMP, X64_REGISTER_R11, X64_REGISTER_R10);
    x64_jump_write(builder, X64_CONDITION_ABOVE, &backward_jumps);

    uint32_t forward = (uint32_t)(builder->writer.length - builder->text_start);
    x64_write_registers(builder, X64_OPCODE_TEST, X64_REGISTER_RCX, X64_REGISTER_RCX);
    x64_jump_write(builder, X64_CONDITION_EQUAL, &done_jumps);
    x64_write_memory_two_byte(builder, X64_OPCODE_TWO_BYTE_MOVZX_BYTE, X64_REGISTER_RAX,
                              X64_REGISTER_R11, 0);
    x64_write_memory(builder, X64_OPCODE_STORE_BYTE, X64_REGISTER_RAX, X64_REGISTER_R10, 0);
    x64_write_add_immediate(builder, X64_REGISTER_R10, 1);
    x64_write_add_immediate(builder, X64_REGISTER_R11, 1);
    x64_write_add_immediate(builder, X64_REGISTER_RCX, -1);
    x64_jump_back_write(builder, forward);

    x64_jumps_resolve(builder, backward_jumps);
    x64_write_registers(builder, X64_OPCODE_ADD, X64_REGISTER_RCX, X64_REGISTER_R10);
    x64_write_registers(builder, X64_OPCODE_ADD, X64_REGISTER_RCX, X64_REGISTER_R11);
    uint32_t backward = (uint32_t)(builder->writer.length - builder->text_start);
    x64_write_registers(builder, X64_OPCODE_TEST, X64_REGISTER_RCX, X64_REGISTER_RCX);
    x64_jump_write(builder, X64_CONDITION_EQUAL, &done_jumps);
    x64_write_add_immediate(builder, X64_REGISTER_R10, -1);
    x64_write_add_immediate(builder, X64_REGISTER_R11, -1);
    x64_write_memory_two_byte(builder, X64_OPCODE_TWO_BYTE_MOVZX_BYTE, X64_REGISTER_RAX,
                              X64_REGISTER_R11, 0);
    x64_write_memory(builder, X64_OPCODE_STORE_BYTE, X64_REGISTER_RAX, X64_REGISTER_R10, 0);
    x64_write_add_immediate(builder, X64_REGISTER_RCX, -1);
    x64_jump_back_write(builder, backward);
    x64_jumps_resolve(builder, done_jumps);
}

/**
 * Moves the value of `type` on the top of the stack of values to the memory at
 * `rbp + displacement`, one word at a time through `rax`, and takes it off the stack. Returns
 * true, so that a caller can return it.
 */
static bool x64_value_store(X64_Builder *builder, X64_Type type, int32_t displacement) {
    size_t word_count = x64_type_word_count(builder, type);
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
 * as a value of `wanted` when it is an untyped constant or an array where an array slice is
 * wanted, or nothing when `is_result_used` is not set and it is a call. With `X64_MODE_ADDRESS` it
 * leaves the address of the place that it is instead, which an assignment writes to. The tree is
 * walked without recursion, with the path from the root to the current node in a fixed array: a
 * node is entered, then its children are written one after the other, each of which leaves its
 * value on the stack, and then the node itself, which takes the values of its children off the
 * stack and leaves its own. A constant is its value, so its children are not written. Returns false
 * after it recorded an error.
 */
static bool x64_expression_write(X64_Builder *builder, Ast_Node_Index expression,
                                 X64_Type wanted, X64_Mode mode, bool is_result_used) {
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
        .mode = mode,
    };

    // False once the children of the last node have been written.
    bool is_descending = true;
    // Every node is entered once and left once, so twice the number of nodes steps are enough.
    for (size_t step = 0; step < builder->node_count * 2 + 2; step += 1) {
        X64_Expression_Frame *frame = &frames[frame_count - 1];

        if (is_descending) {
            if (not x64_node_enter(builder, frame)) {
                return false;
            }
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
                    .mode = x64_child_mode(builder, frame, first_child),
                };
                frame_count += 1;
                continue;
            }
        }

        // The children of the node are on the stack, so the node itself is written now. The value
        // of the root is dropped when it is not used, which only a call does without one.
        bool is_root = frame_count == 1;
        if (not x64_expression_node_write(builder, frame, not is_root || is_result_used)) {
            return false;
        }
        x64_conversion_write(builder, frame);
        if (is_root) {
            return true;
        }

        // What the parent does with the value of each child, and then the next sibling, or the
        // parent itself when there is none.
        X64_Expression_Frame const *parent = &frames[frame_count - 2];
        if (not x64_child_done(builder, parent, frame)) {
            return false;
        }
        Ast_Node_Index next = builder->nodes[frame->node].next_sibling;
        if (next != AST_NODE_INDEX_NONE) {
            uint32_t index = frame->index + 1;
            *frame = (X64_Expression_Frame){
                .wanted = x64_child_wanted(builder, parent, next, index),
                .node = next,
                .stack_start = (uint32_t)builder->stack_words,
                .index = index,
                .mode = x64_child_mode(builder, parent, next),
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
 * Returns how `parent` wants its child `child`: the operand of `%` as the address of its place,
 * and a struct whose field is accessed as its place too, for reading the field or, when the field
 * is assigned to, for writing it. A pointer whose field is accessed, an array that is subscripted
 * and a pointer that is followed are addresses already, so they are values, like every other child.
 */
static X64_Mode x64_child_mode(X64_Builder const *builder, X64_Expression_Frame const *parent,
                               Ast_Node_Index child) {
    Ast_Node_Type node_type = builder->nodes[parent->node].node_type;
    if (node_type == AST_NODE_TYPE_ADDRESS_OF) {
        return X64_MODE_ADDRESS;
    }
    if (node_type == AST_NODE_TYPE_FIELD_ACCESS &&
        x64_expression_type(builder, child).kind != X64_TYPE_KIND_POINTER) {
        if (parent->mode == X64_MODE_ADDRESS || parent->mode == X64_MODE_TARGET) {
            return parent->mode;
        }
        return X64_MODE_PLACE;
    }
    return X64_MODE_VALUE;
}

/**
 * Writes what the node of `frame` needs before its children: a struct literal makes the place of
 * its value on the stack of values, with each word zero, which is the value of a literal with
 * empty braces, so that the values of the fields can be stored in it as they are written, see
 * `x64_child_done`. The fields of a literal are checked here: each is a field of the struct, is
 * given once, and either every field is given or none is, where a field in a case of a union is
 * given only with the member of its case. A call of a procedure whose result is
 * more than a word makes the place for the result, see `x64_result_is_hidden`. Returns false
 * after it recorded an error.
 */
static bool x64_node_enter(X64_Builder *builder, X64_Expression_Frame const *frame) {
    Ast_Node_Index node = frame->node;
    Ast_Node_Type node_type = builder->nodes[node].node_type;

    if (node_type == AST_NODE_TYPE_CALL && not x64_call_is_built_in(builder, node)) {
        Ast_Node_Index procedure = x64_call_procedure(builder, node);
        if (procedure != AST_NODE_INDEX_NONE && x64_result_is_hidden(builder, procedure)) {
            x64_stack_push(builder,
                           x64_type_word_count(builder, x64_return_type(builder, procedure)));
        }
        return true;
    }
    if (node_type != AST_NODE_TYPE_STRUCT_LITERAL) {
        return true;
    }

    X64_Type type = x64_expression_type_for(builder, node, frame->wanted);
    Str subject = builder->nodes[node].payload.struct_literal.name;
    if (subject.bytes == NULL) {
        subject = ast_definition_name(&builder->nodes[builder->definition]);
    }
    if (type.kind != X64_TYPE_KIND_STRUCT) {
        return x64_fail(builder, X64_BUILD_STATUS_STRUCT_EXPECTED, subject);
    }

    size_t given_count = 0;
    for (Ast_Node_Index given = builder->nodes[node].first_child; given != AST_NODE_INDEX_NONE;
         given = builder->nodes[given].next_sibling) {
        Str name = builder->nodes[given].payload.field_initializer.name;
        size_t offset = 0;
        X64_Type field_type = x64_type_make(X64_TYPE_KIND_NONE);
        if (not x64_struct_field_find(builder, type.definition, name, &offset, &field_type)) {
            return x64_fail(builder, X64_BUILD_STATUS_STRUCT_FIELD_UNKNOWN, name);
        }
        for (Ast_Node_Index earlier = builder->nodes[node].first_child; earlier != given;
             earlier = builder->nodes[earlier].next_sibling) {
            if (str_equals(builder->nodes[earlier].payload.field_initializer.name, name)) {
                return x64_fail(builder, X64_BUILD_STATUS_STRUCT_FIELD_DUPLICATE, name);
            }
        }
        given_count += 1;
    }
    // A field in a case of a union match is given a value only when the literal gives the field
    // that the union is matched against the member of the case, or leaves it out, and so the first
    // member, which is the default value of an enum. The member is written out, because the case
    // is decided when the program is compiled. Once one field is given, every field is, except
    // the fields of the cases that the literal does not choose.
    for (Ast_Node_Index given = builder->nodes[node].first_child; given != AST_NODE_INDEX_NONE;
         given = builder->nodes[given].next_sibling) {
        Str name = builder->nodes[given].payload.field_initializer.name;
        Ast_Node_Index union_match = x64_field_union_match(
            builder, x64_struct_field_node_find(builder, type.definition, name));
        if (union_match == AST_NODE_INDEX_NONE) {
            continue;
        }
        Ast_Node_Index enum_node = x64_union_enum(builder, type.definition, union_match);
        Str given_member = x64_union_literal_member(builder, node, union_match, enum_node);
        if (given_member.bytes == NULL) {
            return x64_fail(builder, X64_BUILD_STATUS_UNION_KIND_NOT_CONSTANT, name);
        }
        Str case_member =
            builder->nodes[builder->nodes[union_match].first_child].payload.union_case.member;
        if (not str_equals(given_member, case_member)) {
            return x64_fail(builder, X64_BUILD_STATUS_UNION_FIELD_KIND_MISMATCH, name);
        }
    }
    for (Ast_Node_Index field = builder->nodes[type.definition].first_child;
         field != AST_NODE_INDEX_NONE && given_count != 0;
         field = builder->nodes[field].next_sibling) {
        Str field_name = builder->nodes[field].payload.field.name;
        bool is_given = false;
        for (Ast_Node_Index given = builder->nodes[node].first_child;
             given != AST_NODE_INDEX_NONE && not is_given;
             given = builder->nodes[given].next_sibling) {
            is_given = str_equals(builder->nodes[given].payload.field_initializer.name,
                                  field_name);
        }
        Ast_Node_Index union_match = x64_field_union_match(builder, field);
        bool is_required = true;
        if (union_match != AST_NODE_INDEX_NONE) {
            Ast_Node_Index enum_node = x64_union_enum(builder, type.definition, union_match);
            Str given_member = x64_union_literal_member(builder, node, union_match, enum_node);
            Str case_member =
                builder->nodes[builder->nodes[union_match].first_child].payload.union_case.member;
            is_required = given_member.bytes != NULL && str_equals(given_member, case_member);
        }
        if (not is_given && is_required) {
            return x64_fail(builder, X64_BUILD_STATUS_STRUCT_FIELD_MISSING, field_name);
        }
    }

    size_t word_count = x64_type_word_count(builder, type);
    int32_t target = x64_stack_push(builder, word_count);
    x64_write_load_integer(builder, X64_REGISTER_RAX, 0);
    for (size_t i = 0; i < word_count; i += 1) {
        x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP,
                         target + (int32_t)(i * X64_WORD_SIZE_BYTES));
    }
    return true;
}

/**
 * Writes what `parent` does with the value of its child of `child`, which is on the top of the
 * stack of values, before the next child is written: an element of an array literal is stored in
 * its place among the elements of the literal, see `x64_procedure_layout`, and the value of a field
 * of a struct literal in its words of the value of the literal, which is below it on the stack, see
 * `x64_node_enter`, and taken off the stack, so that the literal is the only value that it leaves.
 * Returns false after it recorded an error.
 */
static bool x64_child_done(X64_Builder *builder, X64_Expression_Frame const *parent,
                           X64_Expression_Frame const *child) {
    Ast_Node_Type node_type = builder->nodes[parent->node].node_type;
    if (node_type == AST_NODE_TYPE_ARRAY_LITERAL) {
        X64_Type element = x64_type_element(builder->layouts[parent->node].type);
        if (not x64_type_accepts(builder, element, child->node)) {
            return x64_fail(builder, X64_BUILD_STATUS_ARRAY_ELEMENT_TYPE_MISMATCH,
                            x64_expression_subject(builder, child->node));
        }
        // `lea r10, [element]` and the store of the value there.
        int32_t place = -(int32_t)builder->layouts[parent->node].frame_offset +
                        (int32_t)(child->index * x64_type_stride_bytes(builder, element));
        x64_write_memory(builder, X64_OPCODE_LEA, X64_REGISTER_R10, X64_REGISTER_RBP, place);
        x64_memory_store(builder, element, X64_REGISTER_R10);
        return true;
    }
    if (node_type != AST_NODE_TYPE_STRUCT_LITERAL) {
        return true;
    }

    X64_Type type = x64_expression_type_for(builder, parent->node, parent->wanted);
    Str name = builder->nodes[child->node].payload.field_initializer.name;
    Ast_Node_Index value = builder->nodes[child->node].first_child;
    size_t offset = 0;
    X64_Type field_type = x64_type_make(X64_TYPE_KIND_NONE);
    bool is_found = x64_struct_field_find(builder, type.definition, name, &offset, &field_type);
    assert(is_found);
    bool is_nested_literal = builder->nodes[value].node_type == AST_NODE_TYPE_STRUCT_LITERAL &&
                             builder->nodes[value].payload.struct_literal.name.bytes == NULL;
    if (not is_nested_literal && not x64_type_accepts(builder, field_type, value)) {
        return x64_fail(builder, X64_BUILD_STATUS_STRUCT_FIELD_TYPE_MISMATCH, name);
    }

    size_t word_count = x64_type_word_count(builder, field_type);
    int32_t source = x64_stack_displacement(builder, builder->stack_words - word_count,
                                            word_count);
    int32_t target = x64_stack_displacement(builder, parent->stack_start,
                                            x64_type_word_count(builder, type)) +
                     (int32_t)(offset * X64_WORD_SIZE_BYTES);
    for (size_t i = 0; i < word_count; i += 1) {
        int32_t word = (int32_t)(i * X64_WORD_SIZE_BYTES);
        x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP,
                         source + word);
        x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP,
                         target + word);
    }
    x64_stack_pop(builder, word_count);
    return true;
}

/**
 * Makes the value of `frame`, which is on the top of the stack of values, a value of the type
 * that its place wants, where that is not what it is: an array where an array slice is wanted
 * becomes a slice of all of its elements, the address and the length, which is part of the type
 * of the array. Nothing else is converted.
 */
static void x64_conversion_write(X64_Builder *builder, X64_Expression_Frame const *frame) {
    if (frame->wanted.kind != X64_TYPE_KIND_SLICE || frame->mode != X64_MODE_VALUE) {
        return;
    }
    X64_Type type = x64_expression_type(builder, frame->node);
    if (type.kind != X64_TYPE_KIND_ARRAY) {
        return;
    }
    assert(type.length >= 0);

    // The address is the first word of the slice, which starts a word lower on the stack.
    int32_t array = x64_stack_displacement(builder, builder->stack_words - 1, 1);
    x64_stack_pop(builder, 1);
    int32_t slice = x64_stack_push(builder, 2);
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP, array);
    x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, slice);
    x64_write_load_integer(builder, X64_REGISTER_RAX, type.length);
    x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP,
                     slice + X64_WORD_SIZE_BYTES);
}

/**
 * Returns the first child of `expression` that is written before it, or `AST_NODE_INDEX_NONE` when
 * it has none: a constant is its value, and so is the member of an enum, and the argument of
 * `str_to_c_str` is copied by the call itself when it is a string that is known when the program
 * is compiled. The elements of an array literal follow its type, which is not written.
 */
static Ast_Node_Index x64_child_first(X64_Builder const *builder, Ast_Node_Index expression) {
    assert(expression < builder->node_count);

    if (x64_is_constant(builder, expression)) {
        return AST_NODE_INDEX_NONE;
    }
    // The member of an enum is its number, and the information about an enum is in `.data`.
    if (x64_enum_of_access(builder, expression) != AST_NODE_INDEX_NONE ||
        x64_type_info_access_is(builder, expression)) {
        return AST_NODE_INDEX_NONE;
    }
    if (x64_call_is_str_to_c_str(builder, expression)) {
        // A string that is known is copied as it is, and a `Str` that is worked out is copied
        // when the program runs.
        Ast_Node_Index argument = builder->nodes[expression].first_child;
        if (argument != AST_NODE_INDEX_NONE && x64_string_literal_of(builder, argument) ==
                                                   AST_NODE_INDEX_NONE) {
            return argument;
        }
        return AST_NODE_INDEX_NONE;
    }
    switch (builder->nodes[expression].node_type) {
    case AST_NODE_TYPE_ADDITION:
    case AST_NODE_TYPE_ADDRESS_OF:
    case AST_NODE_TYPE_AND:
    case AST_NODE_TYPE_CALL:
    case AST_NODE_TYPE_CAST:
    case AST_NODE_TYPE_COMPARISON:
    case AST_NODE_TYPE_DEREFERENCE:
    case AST_NODE_TYPE_DIVISION:
    case AST_NODE_TYPE_MULTIPLICATION:
    case AST_NODE_TYPE_NOT:
    case AST_NODE_TYPE_OR:
    case AST_NODE_TYPE_FIELD_ACCESS:
    case AST_NODE_TYPE_FIELD_INITIALIZER:
    case AST_NODE_TYPE_SLICE:
    case AST_NODE_TYPE_STRUCT_LITERAL:
    case AST_NODE_TYPE_SUBSCRIPT:
    case AST_NODE_TYPE_SUBTRACTION:
        return builder->nodes[expression].first_child;
    case AST_NODE_TYPE_ARRAY_LITERAL:
        return builder->nodes[builder->nodes[expression].first_child].next_sibling;
    default:
        break;
    }
    return AST_NODE_INDEX_NONE;
}

/**
 * Returns the type that `parent` wants its child `child` at `index` to have, which an untyped
 * constant becomes: the type of the parameter for an argument, `Int` and `Bool` for the operands of
 * the operators that take them and for an index, the type of the elements for an element of an
 * array literal, and for an operand of a comparison that is an untyped constant or `nil` the type
 * of the other operand. The kind is `X64_TYPE_KIND_NONE` where the parent does not say.
 */
static X64_Type x64_child_wanted(X64_Builder const *builder, X64_Expression_Frame const *parent,
                                 Ast_Node_Index child, size_t index) {
    assert(child < builder->node_count);

    Ast_Node const *node = &builder->nodes[parent->node];
    switch (node->node_type) {
    case AST_NODE_TYPE_ADDITION:
    case AST_NODE_TYPE_DIVISION:
    case AST_NODE_TYPE_MULTIPLICATION:
    case AST_NODE_TYPE_SUBTRACTION:
        return x64_type_make(X64_TYPE_KIND_INT);
    case AST_NODE_TYPE_AND:
    case AST_NODE_TYPE_NOT:
    case AST_NODE_TYPE_OR:
        return x64_type_make(X64_TYPE_KIND_BOOL);
    case AST_NODE_TYPE_ARRAY_LITERAL:
        return x64_type_element(builder->layouts[parent->node].type);
    case AST_NODE_TYPE_FIELD_INITIALIZER:
        // The value of a field, which the field initializer passes on.
        return parent->wanted;
    case AST_NODE_TYPE_STRUCT_LITERAL: {
        X64_Type type = x64_expression_type_for(builder, parent->node, parent->wanted);
        size_t offset = 0;
        X64_Type field_type = x64_type_make(X64_TYPE_KIND_NONE);
        if (type.kind == X64_TYPE_KIND_STRUCT &&
            x64_struct_field_find(builder, type.definition,
                                  builder->nodes[child].payload.field_initializer.name, &offset,
                                  &field_type)) {
            return field_type;
        }
        break;
    }
    case AST_NODE_TYPE_SLICE:
    case AST_NODE_TYPE_SUBSCRIPT:
        if (index > 0) {
            return x64_type_make(X64_TYPE_KIND_INT);
        }
        break;
    case AST_NODE_TYPE_COMPARISON: {
        Ast_Node_Index other = node->first_child;
        if (index == 0) {
            other = node->last_child;
        }
        bool is_untyped = x64_is_constant(builder, child) ||
                          builder->nodes[child].node_type == AST_NODE_TYPE_NIL_LITERAL ||
                          builder->nodes[child].node_type == AST_NODE_TYPE_ENUM_LITERAL;
        if (is_untyped) {
            return x64_expression_type(builder, other);
        }
        break;
    }
    case AST_NODE_TYPE_CALL: {
        if (x64_call_is_built_in(builder, parent->node)) {
            if (str_equals(node->payload.call.name, STR("int_to_byte"))) {
                return x64_type_make(X64_TYPE_KIND_INT);
            }
            if (str_equals(node->payload.call.name, STR("str_to_c_str"))) {
                return x64_type_make(X64_TYPE_KIND_STR);
            }
            break;
        }
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
 * `frame->stack_start` on: it takes them off and leaves its own value there, or the address of its
 * place for `X64_MODE_ADDRESS`. `is_result_used` is not set for a call whose value is dropped,
 * which leaves nothing. Returns false after it recorded an error.
 */
static bool x64_expression_node_write(X64_Builder *builder, X64_Expression_Frame const *frame,
                                      bool is_result_used) {
    Ast_Node_Index node = frame->node;
    Ast_Node const *expression = &builder->nodes[node];

    if (frame->mode != X64_MODE_VALUE) {
        return x64_address_write(builder, frame);
    }
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
    case AST_NODE_TYPE_ADDRESS_OF:
        // The operand left the address of its place, which is the value of the pointer.
        return true;
    case AST_NODE_TYPE_ARRAY_LITERAL: {
        // The elements are in their places already, and the value is the address of the first.
        X64_Type type = builder->layouts[node].type;
        Ast_Node_Index type_node = expression->first_child;
        if (type.kind == X64_TYPE_KIND_NONE) {
            return x64_fail(builder, X64_BUILD_STATUS_TYPE_UNSUPPORTED,
                            x64_type_subject(builder, type_node));
        }
        X64_Type written = x64_type_of_node(builder, type_node);
        if (written.length >= 0 && written.length != type.length) {
            return x64_fail(builder, X64_BUILD_STATUS_ARRAY_LENGTH_MISMATCH,
                            x64_type_subject(builder, type_node));
        }
        int32_t target = x64_stack_push(builder, 1);
        x64_write_memory(builder, X64_OPCODE_LEA, X64_REGISTER_RAX, X64_REGISTER_RBP,
                         -(int32_t)builder->layouts[node].frame_offset);
        x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, target);
        return true;
    }
    case AST_NODE_TYPE_CALL:
        if (x64_call_is_str_to_c_str(builder, node)) {
            return x64_str_to_c_str_write(builder, node);
        }
        if (x64_call_is_built_in(builder, node)) {
            return x64_built_in_write(builder, frame);
        }
        return x64_call_write(builder, frame, is_result_used);
    case AST_NODE_TYPE_CAST:
        return x64_cast_write(builder, frame);
    case AST_NODE_TYPE_COMPARISON:
        return x64_comparison_write(builder, frame);
    case AST_NODE_TYPE_FIELD_INITIALIZER:
    case AST_NODE_TYPE_STRUCT_LITERAL:
        // The value of the field is on the stack for the literal, see `x64_child_done`, and the
        // value of the literal is in the place that it made, see `x64_node_enter`.
        return true;
    case AST_NODE_TYPE_ENUM_LITERAL: {
        Str member = expression->payload.enum_literal.name;
        if (frame->wanted.kind != X64_TYPE_KIND_ENUM) {
            return x64_fail(builder, X64_BUILD_STATUS_ENUM_TYPE_UNKNOWN, member);
        }
        return x64_member_write(builder, frame->wanted.definition, member);
    }
    case AST_NODE_TYPE_TYPE_INFO_OF:
        // The information is not a value, so a field of it is read right away.
        return x64_fail(builder, X64_BUILD_STATUS_TYPE_INFO_MISPLACED,
                        x64_type_subject(builder, expression->first_child));
    case AST_NODE_TYPE_FIELD_ACCESS:
        if (x64_enum_of_access(builder, node) != AST_NODE_INDEX_NONE) {
            return x64_member_write(builder, x64_enum_of_access(builder, node),
                                    expression->payload.field_access.name);
        }
        if (x64_type_info_access_is(builder, node)) {
            return x64_type_info_write(builder, node);
        }
        // A field of a struct.
        if (not x64_address_write(builder, frame)) {
            return false;
        }
        {
            X64_Type type = x64_expression_type(builder, node);
            int32_t address = x64_stack_displacement(builder, builder->stack_words - 1, 1);
            x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_R10, X64_REGISTER_RBP,
                             address);
            x64_stack_pop(builder, 1);
            x64_memory_load(builder, type, X64_REGISTER_R10);
        }
        return true;
    case AST_NODE_TYPE_DEREFERENCE:
    case AST_NODE_TYPE_SUBSCRIPT: {
        // The address of the place, and then what is there.
        if (not x64_address_write(builder, frame)) {
            return false;
        }
        X64_Type type = x64_expression_type(builder, node);
        int32_t address = x64_stack_displacement(builder, builder->stack_words - 1, 1);
        x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_R10, X64_REGISTER_RBP, address);
        x64_stack_pop(builder, 1);
        x64_memory_load(builder, type, X64_REGISTER_R10);
        return true;
    }
    case AST_NODE_TYPE_IDENTIFIER: {
        // The value of a parameter is in the words of the arguments, that of a variable in its
        // place in the frame, and that of a global variable in `.data`. A constant that is the
        // member of an enum is its number.
        Semantic_Binding binding = builder->bindings[node];
        if (binding.kind == SEMANTIC_BINDING_KIND_CONSTANT) {
            Ast_Node_Index value = builder->nodes[binding.target].first_child;
            Ast_Node_Index enum_node = x64_enum_of_access(builder, value);
            if (enum_node != AST_NODE_INDEX_NONE) {
                return x64_member_write(builder, enum_node,
                                        builder->nodes[value].payload.field_access.name);
            }
            if (builder->nodes[value].node_type == AST_NODE_TYPE_PROCEDURE) {
                return x64_procedure_address_write(builder, binding.position);
            }
            // A struct or an enum is a type, and has no value.
            return x64_fail(builder, X64_BUILD_STATUS_IDENTIFIER_NOT_VALUE,
                            expression->payload.identifier.name);
        }
        X64_Type type = x64_expression_type(builder, node);
        if (type.kind == X64_TYPE_KIND_NONE ||
            not x64_identifier_address_load(builder, node, X64_REGISTER_R11)) {
            return x64_fail_unsupported(builder);
        }
        x64_memory_load_words(builder, type, X64_REGISTER_R11);
        return true;
    }
    case AST_NODE_TYPE_NIL_LITERAL: {
        // `nil` is the address 0, which is not the address of anything that can be pointed to.
        int32_t target = x64_stack_push(builder, 1);
        x64_write_load_integer(builder, X64_REGISTER_RAX, 0);
        x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, target);
        return true;
    }
    case AST_NODE_TYPE_SLICE:
        return x64_slice_write(builder, frame);
    default:
        break;
    }
    return x64_fail_unsupported(builder);
}

/**
 * Sets `*place` to the distance from `rbp` of the value of the name `identifier`: a parameter is in
 * the words of the arguments, and a variable or the iterator of a for statement in its place in the
 * frame. Returns false for a name that is neither, which has no place in the frame.
 */
static bool x64_identifier_place(X64_Builder const *builder, Ast_Node_Index identifier,
                                 int32_t *place) {
    // The name of a call through a pointer is bound like a name, see `x64_call_write`.
    assert(builder->nodes[identifier].node_type == AST_NODE_TYPE_IDENTIFIER ||
           builder->nodes[identifier].node_type == AST_NODE_TYPE_CALL);

    Semantic_Binding binding = builder->bindings[identifier];
    if (binding.kind == SEMANTIC_BINDING_KIND_PARAMETER) {
        Ast_Node_Index procedure = builder->nodes[builder->definition].first_child;
        *place = (int32_t)(X64_ARGUMENT_HOME_OFFSET_BYTES +
                           x64_argument_word_offset(builder, procedure, binding.position) *
                               X64_WORD_SIZE_BYTES);
        return true;
    }
    if (binding.kind == SEMANTIC_BINDING_KIND_VARIABLE) {
        *place = -(int32_t)builder->layouts[binding.target].frame_offset;
        return true;
    }
    return false;
}

/**
 * Writes `lea register, [place]` of the place of the value of the name `identifier`: its place in
 * the frame, see `x64_identifier_place`, or for a global variable its place in `.data`, which is
 * relative to the instruction pointer. Returns false for a name that has no place.
 */
static bool x64_identifier_address_load(X64_Builder *builder, Ast_Node_Index identifier,
                                        byte register_number) {
    Semantic_Binding binding = builder->bindings[identifier];
    if (binding.kind == SEMANTIC_BINDING_KIND_GLOBAL) {
        x64_write_rip_memory(builder, X64_OPCODE_LEA, register_number,
                             builder->data_rva + builder->layouts[binding.target].frame_offset);
        return true;
    }
    int32_t place = 0;
    if (not x64_identifier_place(builder, identifier, &place)) {
        return false;
    }
    x64_write_memory(builder, X64_OPCODE_LEA, register_number, X64_REGISTER_RBP, place);
    return true;
}

/**
 * Puts the pointer to the procedure at `position` of the constants on the stack of values, which is
 * the address of its code, `lea` of it relative to the instruction pointer, or for a foreign
 * procedure the address that the loader wrote to its place in the table of imports. Returns true,
 * so that a caller can return it.
 */
static bool x64_procedure_address_write(X64_Builder *builder, size_t position) {
    assert(position < builder->constant_count);

    X64_Constant const *constant = &builder->constants[position];
    if (constant->kind == X64_CONSTANT_KIND_FOREIGN) {
        x64_write_rip_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX,
                             X64_RDATA_RVA + builder->layout.address_table_offset +
                                 (uint64_t)constant->import_slot * X64_IMPORT_ENTRY_SIZE_BYTES);
    } else {
        assert(constant->kind == X64_CONSTANT_KIND_PROCEDURE);
        x64_write_rip_memory(builder, X64_OPCODE_LEA, X64_REGISTER_RAX,
                             (uint64_t)builder->text_rva + constant->offset);
    }
    int32_t target = x64_stack_push(builder, 1);
    x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, target);
    return true;
}

/**
 * Puts the number of the member called `member` of the enum `enum_node` on the stack of values,
 * which is all that a member is. Returns false after it recorded an error, which is a member that
 * the enum does not have.
 */
static bool x64_member_write(X64_Builder *builder, Ast_Node_Index enum_node, Str member) {
    int64_t number = 0;
    if (not x64_enum_member_find(builder, enum_node, member, &number)) {
        return x64_fail(builder, X64_BUILD_STATUS_ENUM_MEMBER_UNKNOWN, member);
    }
    x64_write_load_integer(builder, X64_REGISTER_RAX, number);
    int32_t target = x64_stack_push(builder, 1);
    x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, target);
    return true;
}

/**
 * Writes the field `access` of the information about an enum: `members` is the address of the
 * table of its members in `.data`, see `x64_data_layout`, and `name` is the `Str` of its name in
 * `.rdata`. Returns false after it recorded an error.
 */
static bool x64_type_info_write(X64_Builder *builder, Ast_Node_Index access) {
    Ast_Node_Index info = builder->nodes[access].first_child;
    if (x64_type_info_enum(builder, info) == AST_NODE_INDEX_NONE) {
        return x64_fail(builder, X64_BUILD_STATUS_TYPE_INFO_UNSUPPORTED,
                        x64_type_subject(builder, builder->nodes[info].first_child));
    }
    X64_Type type = x64_type_info_type(builder, access);
    if (type.kind == X64_TYPE_KIND_NONE) {
        return x64_fail(builder, X64_BUILD_STATUS_STRUCT_FIELD_UNKNOWN,
                        builder->nodes[access].payload.field_access.name);
    }
    if (type.kind == X64_TYPE_KIND_ARRAY) {
        int32_t target = x64_stack_push(builder, 1);
        x64_write_rip_memory(builder, X64_OPCODE_LEA, X64_REGISTER_RAX,
                             builder->data_rva + builder->layouts[info].frame_offset);
        x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, target);
        return true;
    }
    int32_t target = x64_stack_push(builder, 2);
    x64_type_name_address_write(builder, info, X64_REGISTER_RAX);
    x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, target);
    x64_write_load_integer(
        builder, X64_REGISTER_RAX,
        (int64_t)builder->nodes[builder->nodes[info].first_child].payload.type_name.name.length);
    x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP,
                     target + X64_WORD_SIZE_BYTES);
    return true;
}

/**
 * Writes the address of the place that the node of `frame` is, whose children are on the stack of
 * values, and leaves it there: a variable is its place in the frame, an element is the address of
 * the first element plus its index times the size of an element, after the index was checked
 * against the length, which traps when it is not below it, and what a pointer points to is the
 * pointer, which traps when it is `nil`, and a field is the address of its struct plus the size of
 * the fields before it. A parameter and the iterator of a for statement cannot be assigned to, so
 * they have no address, except for `X64_MODE_PLACE`, which reads a field of them, and neither has a
 * value that is computed, or a variable of an array or array slice type, which cannot be pointed
 * to. Returns false after it recorded an error.
 */
static bool x64_address_write(X64_Builder *builder, X64_Expression_Frame const *frame) {
    Ast_Node_Index node = frame->node;
    Ast_Node const *expression = &builder->nodes[node];
    Str subject = x64_expression_subject(builder, node);

    if (expression->node_type == AST_NODE_TYPE_IDENTIFIER) {
        // A variable has an address, and is assigned to there. A global variable is assigned to
        // in `.data`, but has no address for `%`, because it is not in the frame where the other
        // places are. A place that is only read can also be a parameter or an iterator, which is
        // where a field of one is read from.
        Semantic_Binding binding = builder->bindings[node];
        X64_Type type = x64_expression_type(builder, node);
        bool is_variable =
            binding.kind == SEMANTIC_BINDING_KIND_VARIABLE &&
            builder->nodes[binding.target].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION;
        bool is_global = binding.kind == SEMANTIC_BINDING_KIND_GLOBAL;
        bool is_valid = is_variable;
        if (frame->mode == X64_MODE_TARGET) {
            is_valid = is_variable || is_global;
        } else if (frame->mode == X64_MODE_PLACE) {
            is_valid = binding.kind == SEMANTIC_BINDING_KIND_VARIABLE || is_global ||
                       binding.kind == SEMANTIC_BINDING_KIND_PARAMETER;
        }
        bool is_sequence = type.kind == X64_TYPE_KIND_ARRAY || type.kind == X64_TYPE_KIND_SLICE;
        if (not is_valid || (is_sequence && frame->mode == X64_MODE_ADDRESS)) {
            return x64_fail(builder, X64_BUILD_STATUS_ADDRESS_INVALID, subject);
        }
        int32_t target = x64_stack_push(builder, 1);
        x64_identifier_address_load(builder, node, X64_REGISTER_RAX);
        x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, target);
        return true;
    }

    if (expression->node_type == AST_NODE_TYPE_FIELD_ACCESS) {
        return x64_field_address_write(builder, frame);
    }

    if (expression->node_type == AST_NODE_TYPE_DEREFERENCE) {
        X64_Type pointer = x64_expression_type(builder, expression->first_child);
        if (pointer.kind != X64_TYPE_KIND_POINTER) {
            return x64_fail(builder, X64_BUILD_STATUS_POINTER_EXPECTED, subject);
        }
        // The pointer is the address, once it is known not to be `nil`: `test rax, rax` and `jne`
        // over a breakpoint.
        int32_t place = x64_stack_displacement(builder, frame->stack_start, 1);
        x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP, place);
        x64_write_registers(builder, X64_OPCODE_TEST, X64_REGISTER_RAX, X64_REGISTER_RAX);
        x64_write_trap_unless(builder, X64_CONDITION_NOT_EQUAL);
        return true;
    }

    if (expression->node_type == AST_NODE_TYPE_SUBSCRIPT) {
        Ast_Node_Index sequence = expression->first_child;
        Ast_Node_Index index = builder->nodes[sequence].next_sibling;
        X64_Type type = x64_expression_type(builder, sequence);
        if (type.kind != X64_TYPE_KIND_ARRAY && type.kind != X64_TYPE_KIND_SLICE) {
            return x64_fail(builder, X64_BUILD_STATUS_ARRAY_EXPECTED, subject);
        }
        if (not x64_type_accepts(builder, x64_type_make(X64_TYPE_KIND_INT), index)) {
            return x64_fail(builder, X64_BUILD_STATUS_INDEX_TYPE_INVALID,
                            x64_expression_subject(builder, index));
        }
        size_t sequence_words = x64_type_word_count(builder, type);
        int32_t first = x64_stack_displacement(builder, frame->stack_start, sequence_words);
        int32_t place = x64_stack_displacement(builder, frame->stack_start + sequence_words, 1);
        // `mov rax, index`, the length in `rcx`, and `cmp` and `jb` over a breakpoint, which is
        // unsigned, so that a negative index is above every length.
        x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP, place);
        x64_sequence_length_load(builder, type, X64_REGISTER_RCX, first);
        x64_write_registers(builder, X64_OPCODE_CMP, X64_REGISTER_RCX, X64_REGISTER_RAX);
        x64_write_trap_unless(builder, X64_CONDITION_BELOW);
        size_t stride = x64_type_stride_bytes(builder, x64_type_element(type));
        if (stride != 1) {
            x64_write_load_integer(builder, X64_REGISTER_RCX, (int64_t)stride);
            x64_write_multiply(builder, X64_REGISTER_RAX, X64_REGISTER_RCX);
        }
        x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RCX, X64_REGISTER_RBP, first);
        x64_write_registers(builder, X64_OPCODE_ADD, X64_REGISTER_RCX, X64_REGISTER_RAX);
        x64_stack_pop(builder, builder->stack_words - frame->stack_start);
        int32_t target = x64_stack_push(builder, 1);
        x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, target);
        return true;
    }

    return x64_fail(builder, X64_BUILD_STATUS_ADDRESS_INVALID, subject);
}

/**
 * Writes the address of the field of the field access of `frame`, whose struct is on the stack of
 * values: the address of a struct, or a pointer to one, which is followed, and traps when it is
 * `nil`. The field is that address plus 8 bytes for each word of the fields before it. A field is
 * read through a pointer that is one caret away from the struct, so `pp.age` of a `^^Person` is an
 * error, and through a place, so not through the value of a call. A field in a case of a union
 * match is only read and assigned where the program shows that its object is in the case, see
 * `x64_union_is_matched`. Returns false after it recorded
 * an error.
 */
static bool x64_field_address_write(X64_Builder *builder, X64_Expression_Frame const *frame) {
    Ast_Node_Index node = frame->node;
    Ast_Node_Index base = builder->nodes[node].first_child;
    Str name = builder->nodes[node].payload.field_access.name;
    X64_Type type = x64_expression_type(builder, base);
    bool is_pointer = type.kind == X64_TYPE_KIND_POINTER && type.depth == 1 &&
                      type.pointee_kind == X64_TYPE_KIND_STRUCT;
    if (type.kind != X64_TYPE_KIND_STRUCT && not is_pointer) {
        return x64_fail(builder, X64_BUILD_STATUS_FIELD_BASE_INVALID, name);
    }
    size_t offset = 0;
    X64_Type field_type = x64_type_make(X64_TYPE_KIND_NONE);
    if (not x64_struct_field_find(builder, type.definition, name, &offset, &field_type)) {
        return x64_fail(builder, X64_BUILD_STATUS_STRUCT_FIELD_UNKNOWN, name);
    }
    Ast_Node_Index union_match = x64_field_union_match(
        builder, x64_struct_field_node_find(builder, type.definition, name));
    if (union_match != AST_NODE_INDEX_NONE &&
        not x64_union_is_matched(builder, node, type.definition, union_match)) {
        return x64_fail(builder, X64_BUILD_STATUS_UNION_FIELD_UNMATCHED, name);
    }

    int32_t place = x64_stack_displacement(builder, frame->stack_start, 1);
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP, place);
    if (is_pointer) {
        x64_write_registers(builder, X64_OPCODE_TEST, X64_REGISTER_RAX, X64_REGISTER_RAX);
        x64_write_trap_unless(builder, X64_CONDITION_NOT_EQUAL);
    }
    if (offset != 0) {
        x64_write_load_integer(builder, X64_REGISTER_RCX,
                               (int64_t)(offset * X64_WORD_SIZE_BYTES));
        x64_write_registers(builder, X64_OPCODE_ADD, X64_REGISTER_RCX, X64_REGISTER_RAX);
    }
    x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, place);
    return true;
}

/**
 * Writes the slice `a[low:high]` of `frame`, whose array or array slice and bounds are on the
 * stack of values, and leaves the slice: the address of the element at `low` and the number of
 * elements up to `high`. It traps unless `low` is not above `high` and `high` is not above the
 * length, which are unsigned comparisons, so that a negative bound traps too. Returns false after
 * it recorded an error.
 */
static bool x64_slice_write(X64_Builder *builder, X64_Expression_Frame const *frame) {
    Ast_Node_Index node = frame->node;
    Ast_Node_Index sequence = builder->nodes[node].first_child;
    X64_Type type = x64_expression_type(builder, sequence);
    if (type.kind != X64_TYPE_KIND_ARRAY && type.kind != X64_TYPE_KIND_SLICE) {
        return x64_fail(builder, X64_BUILD_STATUS_ARRAY_EXPECTED,
                        x64_expression_subject(builder, node));
    }
    X64_Type int_type = x64_type_make(X64_TYPE_KIND_INT);
    for (Ast_Node_Index bound = builder->nodes[sequence].next_sibling;
         bound != AST_NODE_INDEX_NONE; bound = builder->nodes[bound].next_sibling) {
        if (not x64_type_accepts(builder, int_type, bound)) {
            return x64_fail(builder, X64_BUILD_STATUS_INDEX_TYPE_INVALID,
                            x64_expression_subject(builder, bound));
        }
    }

    size_t sequence_words = x64_type_word_count(builder, type);
    int32_t first = x64_stack_displacement(builder, frame->stack_start, sequence_words);
    int32_t low = x64_stack_displacement(builder, frame->stack_start + sequence_words, 1);
    int32_t high = x64_stack_displacement(builder, frame->stack_start + sequence_words + 1, 1);
    // `rax` is the low bound and `rcx` the high one, and `rdx` the length.
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP, low);
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RCX, X64_REGISTER_RBP, high);
    x64_write_registers(builder, X64_OPCODE_CMP, X64_REGISTER_RCX, X64_REGISTER_RAX);
    x64_write_trap_unless(builder, X64_CONDITION_BELOW_EQUAL);
    x64_sequence_length_load(builder, type, X64_REGISTER_RDX, first);
    x64_write_registers(builder, X64_OPCODE_CMP, X64_REGISTER_RDX, X64_REGISTER_RCX);
    x64_write_trap_unless(builder, X64_CONDITION_BELOW_EQUAL);
    // The length of the slice, and the address of its first element.
    x64_write_registers(builder, X64_OPCODE_SUB, X64_REGISTER_RAX, X64_REGISTER_RCX);
    size_t stride = x64_type_stride_bytes(builder, x64_type_element(type));
    if (stride != 1) {
        x64_write_load_integer(builder, X64_REGISTER_RDX, (int64_t)stride);
        x64_write_multiply(builder, X64_REGISTER_RAX, X64_REGISTER_RDX);
    }
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RDX, X64_REGISTER_RBP, first);
    x64_write_registers(builder, X64_OPCODE_ADD, X64_REGISTER_RDX, X64_REGISTER_RAX);

    x64_stack_pop(builder, builder->stack_words - frame->stack_start);
    int32_t target = x64_stack_push(builder, 2);
    x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, target);
    x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RCX, X64_REGISTER_RBP,
                     target + X64_WORD_SIZE_BYTES);
    return true;
}

/**
 * Writes the load of the number of elements of the array or array slice of `type`, whose value is
 * at `rbp + place`, to `register_number`: the length of an array is part of its type, and the
 * length of a slice is its second word.
 */
static void x64_sequence_length_load(X64_Builder *builder, X64_Type type, byte register_number,
                                     int32_t place) {
    assert(type.kind == X64_TYPE_KIND_ARRAY || type.kind == X64_TYPE_KIND_SLICE);

    if (type.kind == X64_TYPE_KIND_ARRAY) {
        assert(type.length >= 0);
        x64_write_load_integer(builder, register_number, type.length);
    } else {
        x64_write_memory(builder, X64_OPCODE_LOAD, register_number, X64_REGISTER_RBP,
                         place + X64_WORD_SIZE_BYTES);
    }
}

/**
 * Writes the cast of `frame`, whose operand is on the stack of values: a `Byte`, a `Bool` or an
 * `Int` is cast to an `Int`. A `Byte` and a `Bool` are numbers in a whole word, from 0 to 255 and
 * from 0 to 1, which are the same numbers as `Int` values, so the value stays where it is and no
 * instruction is needed. Returns false after it recorded an error.
 */
static bool x64_cast_write(X64_Builder *builder, X64_Expression_Frame const *frame) {
    Ast_Node const *cast = &builder->nodes[frame->node];
    X64_Type target = x64_type_of_name(builder, cast->payload.cast.type_name);
    X64_Type source = x64_expression_type(builder, cast->first_child);
    bool is_source_valid = source.kind == X64_TYPE_KIND_BYTE || source.kind == X64_TYPE_KIND_BOOL ||
                           source.kind == X64_TYPE_KIND_INT;
    if (target.kind != X64_TYPE_KIND_INT || not is_source_valid) {
        return x64_fail(builder, X64_BUILD_STATUS_CAST_INVALID, cast->payload.cast.type_name);
    }
    return true;
}

/**
 * Writes a call of the built-in procedure `length_of` or `int_to_byte` of `frame`, whose argument
 * is on the stack of values. `length_of` is the length of an array, which is part of its type, or
 * the second word of an array slice. `int_to_byte` traps unless the number is at most 255, as an
 * unsigned number, which a negative one is not. Returns false after it recorded an error.
 */
static bool x64_built_in_write(X64_Builder *builder, X64_Expression_Frame const *frame) {
    Ast_Node_Index call = frame->node;
    Str name = builder->nodes[call].payload.call.name;
    Ast_Node_Index argument = builder->nodes[call].first_child;
    if (argument == AST_NODE_INDEX_NONE ||
        builder->nodes[argument].next_sibling != AST_NODE_INDEX_NONE) {
        return x64_fail(builder, X64_BUILD_STATUS_CALL_ARGUMENT_COUNT_MISMATCH, name);
    }
    X64_Type type = x64_expression_type(builder, argument);
    int32_t place = x64_stack_displacement(builder, frame->stack_start,
                                           builder->stack_words - frame->stack_start);

    if (str_equals(name, STR("length_of"))) {
        if (type.kind != X64_TYPE_KIND_ARRAY && type.kind != X64_TYPE_KIND_SLICE) {
            return x64_fail(builder, X64_BUILD_STATUS_CALL_ARGUMENT_TYPE_MISMATCH, name);
        }
        x64_sequence_length_load(builder, type, X64_REGISTER_RAX, place);
    } else if (str_equals(name, STR("int_to_byte"))) {
        if (not x64_type_accepts(builder, x64_type_make(X64_TYPE_KIND_INT), argument)) {
            return x64_fail(builder, X64_BUILD_STATUS_CALL_ARGUMENT_TYPE_MISMATCH, name);
        }
        x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP, place);
        x64_write_load_integer(builder, X64_REGISTER_RCX, X64_BYTE_MAX);
        x64_write_registers(builder, X64_OPCODE_CMP, X64_REGISTER_RCX, X64_REGISTER_RAX);
        x64_write_trap_unless(builder, X64_CONDITION_BELOW_EQUAL);
    } else {
        return x64_fail_unsupported(builder);
    }

    x64_stack_pop(builder, builder->stack_words - frame->stack_start);
    int32_t target = x64_stack_push(builder, 1);
    x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, target);
    return true;
}

/**
 * Puts the value of `type` that is in memory at the address in `base` on the stack of values. A
 * `Byte` is one byte there, which `movzx` widens to its word, and every other value is its words.
 * `base` is `r10` or `r11`, which the copy does not use.
 */
static void x64_memory_load(X64_Builder *builder, X64_Type type, byte base) {
    assert(base == X64_REGISTER_R10 || base == X64_REGISTER_R11);

    size_t word_count = x64_type_word_count(builder, type);
    int32_t target = x64_stack_push(builder, word_count);
    if (type.kind == X64_TYPE_KIND_BYTE) {
        x64_write_memory_two_byte(builder, X64_OPCODE_TWO_BYTE_MOVZX_BYTE, X64_REGISTER_RAX, base,
                                  0);
        x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, target);
        return;
    }
    for (size_t i = 0; i < word_count; i += 1) {
        int32_t offset = (int32_t)(i * X64_WORD_SIZE_BYTES);
        x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, base, offset);
        x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP,
                         target + offset);
    }
}

/**
 * Puts the value of `type` that is in memory at the address in `base` on the stack of values, as
 * its words, also for a `Byte`, which is a word in the place of a variable, see `x64_memory_load`.
 * `base` is `r10` or `r11`, which the copy does not use.
 */
static void x64_memory_load_words(X64_Builder *builder, X64_Type type, byte base) {
    assert(base == X64_REGISTER_R10 || base == X64_REGISTER_R11);

    size_t word_count = x64_type_word_count(builder, type);
    int32_t target = x64_stack_push(builder, word_count);
    for (size_t i = 0; i < word_count; i += 1) {
        int32_t offset = (int32_t)(i * X64_WORD_SIZE_BYTES);
        x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, base, offset);
        x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP,
                         target + offset);
    }
}

/**
 * Moves the value of `type` on the top of the stack of values to the memory at the address in
 * `base`, and takes it off the stack: a `Byte` is stored as one byte, and every other value as its
 * words. `base` is `r10` or `r11`, which the copy does not use.
 */
static void x64_memory_store(X64_Builder *builder, X64_Type type, byte base) {
    assert(base == X64_REGISTER_R10 || base == X64_REGISTER_R11);

    size_t word_count = x64_type_word_count(builder, type);
    int32_t source = x64_stack_displacement(builder, builder->stack_words - word_count,
                                            word_count);
    for (size_t i = 0; i < word_count; i += 1) {
        int32_t offset = (int32_t)(i * X64_WORD_SIZE_BYTES);
        x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, X64_REGISTER_RBP,
                         source + offset);
        if (type.kind == X64_TYPE_KIND_BYTE) {
            x64_write_memory(builder, X64_OPCODE_STORE_BYTE, X64_REGISTER_RAX, base, offset);
        } else {
            x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, base, offset);
        }
    }
    x64_stack_pop(builder, word_count);
}

/**
 * Returns the text in the source that an error about the type node `type` points at: the name at
 * the end of its carets and brackets.
 */
static Str x64_type_subject(X64_Builder const *builder, Ast_Node_Index type) {
    Ast_Node_Index current = type;
    // Each step goes down one level, so there are fewer steps than nodes.
    for (size_t i = 0; i < builder->node_count &&
                       builder->nodes[current].node_type != AST_NODE_TYPE_TYPE_NAME &&
                       builder->nodes[current].first_child != AST_NODE_INDEX_NONE;
         i += 1) {
        current = builder->nodes[current].first_child;
    }
    if (builder->nodes[current].node_type == AST_NODE_TYPE_TYPE_NAME) {
        return builder->nodes[current].payload.type_name.name;
    }
    return ast_definition_name(&builder->nodes[builder->definition]);
}

/**
 * Writes the value of the constant of `frame`, which the analysis worked out, as a value of the
 * type that its place wants: a number or a truth value is one `mov` of its 64 bits, and a string is
 * a `Str`, the address of its bytes in `.rdata` and their number, or where a `C_Str` is wanted and
 * it is untyped, a copy in the temporary storage, which ends with a zero. Returns false after it
 * recorded an error.
 */
static bool x64_constant_write(X64_Builder *builder, X64_Expression_Frame const *frame) {
    Constant_Value constant = builder->constant_values[frame->node];
    assert(constant.status == CONSTANT_STATUS_OK);

    if (constant.string != AST_NODE_INDEX_NONE) {
        if (frame->wanted.kind == X64_TYPE_KIND_C_STR &&
            constant.type == CONSTANT_TYPE_UNTYPED_STRING) {
            if (not x64_c_str_write(builder, constant.string, X64_REGISTER_RAX)) {
                return false;
            }
            int32_t target = x64_stack_push(builder, 1);
            x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP,
                             target);
            return true;
        }
        int32_t target = x64_stack_push(builder, 2);
        x64_write_rip_memory(builder, X64_OPCODE_LEA, X64_REGISTER_RAX,
                             x64_string_rva(builder, constant.string));
        x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, target);
        x64_write_load_integer(
            builder, X64_REGISTER_RAX,
            (int64_t)builder->nodes[constant.string].payload.string_literal.text.length);
        x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP,
                         target + X64_WORD_SIZE_BYTES);
        return true;
    }

    // A number that is a `Byte` where one is wanted is the same word as the number.
    X64_Type type = x64_expression_type_for(builder, frame->node, frame->wanted);
    if (type.kind == X64_TYPE_KIND_NONE) {
        return x64_fail_unsupported(builder);
    }
    x64_write_load_integer(builder, X64_REGISTER_RAX, constant.integer);
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
    if (not x64_type_accepts(builder, operand_type, left)) {
        return x64_fail(builder, status, x64_expression_subject(builder, left));
    }
    if (right != AST_NODE_INDEX_NONE && not x64_type_accepts(builder, operand_type, right)) {
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
 * Writes a comparison, whose operands are on the stack of values: two `Int` values or two `Byte`
 * values with any of the six operators, two `Bool` values, two members of one enum, and two
 * pointers of one type, or a pointer and `nil`, with `==` and `not=`. The order of an `Int` is
 * signed, and a `Byte` is a number from 0 to 255 in a word, which compares the same way. `cmp`
 * compares them and `set` makes the `Bool` of the condition, which `movzx` widens to the 64 bits of
 * the value. Returns false after it recorded an error.
 */
static bool x64_comparison_write(X64_Builder *builder, X64_Expression_Frame const *frame) {
    Ast_Node_Index node = frame->node;
    Ast_Node_Index left = builder->nodes[node].first_child;
    Ast_Node_Index right = builder->nodes[left].next_sibling;
    Ast_Comparison_Operator operator = builder->nodes[node].payload.comparison.operator;

    // An untyped operand and `nil` take the type of the other one, so the type of the comparison
    // is the type of the operand that has one.
    X64_Type type = x64_expression_type(builder, left);
    bool is_left_untyped = x64_is_constant(builder, left) || type.kind == X64_TYPE_KIND_NIL ||
                           type.kind == X64_TYPE_KIND_ENUM_LITERAL;
    if (is_left_untyped && not x64_is_constant(builder, right)) {
        type = x64_expression_type(builder, right);
    }
    bool is_equality = operator == AST_COMPARISON_OPERATOR_EQUAL ||
                       operator == AST_COMPARISON_OPERATOR_NOT_EQUAL;
    bool is_ordered = type.kind == X64_TYPE_KIND_INT || type.kind == X64_TYPE_KIND_BYTE;
    bool is_equatable = type.kind == X64_TYPE_KIND_BOOL || type.kind == X64_TYPE_KIND_ENUM ||
                        type.kind == X64_TYPE_KIND_POINTER ||
                        type.kind == X64_TYPE_KIND_PROCEDURE;
    bool is_valid = is_ordered || (is_equatable && is_equality);
    if (not is_valid || not x64_type_accepts(builder, type, left) ||
        not x64_type_accepts(builder, type, right)) {
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
 * loaded. A foreign procedure is called through its place in the table of imports, and a pointer
 * to a procedure through a register, after a test that traps when it is `nil`. The arguments
 * are taken off the stack of values, and the result, which is in `rax`, is put on it when
 * `is_result_used` is set. Returns false after it recorded an error.
 */
static bool x64_call_write(X64_Builder *builder, X64_Expression_Frame const *frame,
                           bool is_result_used) {
    Ast_Node_Index call = frame->node;
    assert(builder->nodes[call].node_type == AST_NODE_TYPE_CALL);

    Str name = builder->nodes[call].payload.call.name;
    Semantic_Binding binding = builder->bindings[call];
    // A call of a variable, a parameter or a global variable is a call through the pointer to a
    // procedure that it holds, whose type says what the procedure takes and returns.
    Ast_Node_Index procedure = x64_call_procedure(builder, call);
    if (procedure == AST_NODE_INDEX_NONE) {
        return x64_fail(builder, X64_BUILD_STATUS_CALL_TARGET_NOT_PROCEDURE, name);
    }
    X64_Constant const *callee = NULL;
    if (binding.kind == SEMANTIC_BINDING_KIND_CONSTANT) {
        assert(binding.position < builder->constant_count);
        assert(builder->constants[binding.position].definition == binding.target);
        callee = &builder->constants[binding.position];
        // The analysis does not bind a call to a value, such as a string, which is not a
        // procedure.
        assert(callee->kind == X64_CONSTANT_KIND_FOREIGN ||
               callee->kind == X64_CONSTANT_KIND_PROCEDURE);
    }

    size_t parameter_count = x64_parameter_count(builder, procedure);
    Ast_Node_Index argument = builder->nodes[call].first_child;
    for (size_t i = 0; i < parameter_count; i += 1) {
        if (argument == AST_NODE_INDEX_NONE) {
            return x64_fail(builder, X64_BUILD_STATUS_CALL_ARGUMENT_COUNT_MISMATCH, name);
        }
        X64_Type parameter_type = x64_parameter_type(builder, procedure, i);
        if (not x64_type_accepts(builder, parameter_type, argument)) {
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
    // word is worked out from the place of its argument. A result of more than a word has its
    // place on the stack below the arguments, and its address is the first word.
    bool is_hidden = x64_result_is_hidden(builder, procedure);
    size_t result_words = 0;
    if (is_hidden) {
        result_words = x64_type_word_count(builder, return_type);
    }
    size_t word_count = x64_parameter_word_count(builder, procedure);
    size_t arguments_start = frame->stack_start + result_words;
    size_t hidden_words = 0;
    if (is_hidden) {
        hidden_words = 1;
    }
    assert(builder->stack_words - arguments_start + hidden_words == word_count);
    if (word_count > X64_ARGUMENT_REGISTER_COUNT &&
        word_count - X64_ARGUMENT_REGISTER_COUNT > builder->outgoing_words_max) {
        builder->outgoing_words_max = word_count - X64_ARGUMENT_REGISTER_COUNT;
    }
    size_t word = 0;
    if (is_hidden) {
        word = 1;
    }
    size_t argument_start = arguments_start;
    for (size_t i = 0; i < parameter_count; i += 1) {
        size_t argument_words =
            x64_type_word_count(builder, x64_parameter_type(builder, procedure, i));
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
    if (is_hidden) {
        x64_write_memory(builder, X64_OPCODE_LEA, X64_ARGUMENT_REGISTERS[0], X64_REGISTER_RBP,
                         x64_stack_displacement(builder, frame->stack_start, result_words));
        word = 1;
    }
    argument_start = arguments_start;
    for (size_t i = 0; i < parameter_count && word < X64_ARGUMENT_REGISTER_COUNT; i += 1) {
        size_t argument_words =
            x64_type_word_count(builder, x64_parameter_type(builder, procedure, i));
        int32_t source = x64_stack_displacement(builder, argument_start, argument_words);
        for (size_t j = 0; j < argument_words && word < X64_ARGUMENT_REGISTER_COUNT; j += 1) {
            x64_write_memory(builder, X64_OPCODE_LOAD, X64_ARGUMENT_REGISTERS[word],
                             X64_REGISTER_RBP, source + (int32_t)(j * X64_WORD_SIZE_BYTES));
            word += 1;
        }
        argument_start += argument_words;
    }

    if (callee == NULL) {
        // `mov r11, pointer`, `test` and `jne` over a breakpoint, because `nil` cannot be called,
        // and `call r11`, which no argument is in.
        x64_identifier_address_load(builder, call, X64_REGISTER_R11);
        x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_R11, X64_REGISTER_R11, 0);
        x64_write_registers(builder, X64_OPCODE_TEST, X64_REGISTER_R11, X64_REGISTER_R11);
        x64_write_trap_unless(builder, X64_CONDITION_NOT_EQUAL);
        x64_write_call_register(builder, X64_REGISTER_R11);
    } else if (callee->kind == X64_CONSTANT_KIND_FOREIGN) {
        x64_write_call_import(builder, callee->import_slot);
    } else {
        x64_write_call_local(builder, callee->offset);
    }

    x64_stack_pop(builder, builder->stack_words - arguments_start);
    if (is_hidden) {
        // The result is in its place already, which is a value that is not used for a statement.
        if (not is_result_used) {
            x64_stack_pop(builder, result_words);
        }
    } else if (is_result_used) {
        int32_t target = x64_stack_push(builder, 1);
        x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, target);
    }
    return true;
}

/**
 * Writes `str_to_c_str` of `call`, which makes a copy of the string with a zero after it in the
 * temporary storage, and puts its address on the stack of values. A string that is known when the
 * program is compiled is copied with stores of 8 bytes, see `x64_c_str_write`, and any other `Str`,
 * which is on the stack of values, is copied one byte at a time by a loop: the copy takes its
 * length and the zero, rounded up to 8, of the storage, and stops the program with a breakpoint
 * when that does not fit. Returns false after it recorded an error.
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
    if (literal != AST_NODE_INDEX_NONE) {
        if (not x64_c_str_write(builder, literal, X64_REGISTER_RAX)) {
            return false;
        }
        int32_t target = x64_stack_push(builder, 1);
        x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RAX, X64_REGISTER_RBP, target);
        return true;
    }
    if (not x64_type_accepts(builder, x64_type_make(X64_TYPE_KIND_STR), argument)) {
        return x64_fail(builder, X64_BUILD_STATUS_CALL_ARGUMENT_TYPE_MISMATCH, name);
    }

    // `r11` is the source and `rcx` the length, and `rax` how much of the storage is in use. The
    // size is the length and the zero rounded up to 8, which is `(length + 8) & -8`.
    int32_t text = x64_stack_displacement(builder, builder->stack_words - 2, 2);
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_R11, X64_REGISTER_RBP, text);
    x64_write_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RCX, X64_REGISTER_RBP,
                     text + X64_WORD_SIZE_BYTES);
    x64_stack_pop(builder, 2);
    x64_write_rip_memory(builder, X64_OPCODE_LOAD, X64_REGISTER_RAX, builder->data_rva);
    x64_write_memory(builder, X64_OPCODE_LEA, X64_REGISTER_RDX, X64_REGISTER_RCX,
                     X64_TEMPORARY_ALIGNMENT_BYTES);
    x64_write_and_immediate(builder, X64_REGISTER_RDX, -X64_TEMPORARY_ALIGNMENT_BYTES);
    x64_write_registers(builder, X64_OPCODE_ADD, X64_REGISTER_RAX, X64_REGISTER_RDX);
    x64_write_load_integer(builder, X64_REGISTER_R10, X64_TEMPORARY_STORAGE_SIZE_BYTES);
    x64_write_registers(builder, X64_OPCODE_CMP, X64_REGISTER_R10, X64_REGISTER_RDX);
    x64_write_trap_unless(builder, X64_CONDITION_BELOW_EQUAL);
    x64_write_rip_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RDX, builder->data_rva);
    // `r10` is the copy, whose zero is written first, and whose address is kept in `rdx`.
    x64_write_rip_memory(builder, X64_OPCODE_LEA, X64_REGISTER_R10,
                         builder->data_rva + X64_TEMPORARY_USED_SIZE_BYTES);
    x64_write_registers(builder, X64_OPCODE_ADD, X64_REGISTER_RAX, X64_REGISTER_R10);
    x64_write_registers(builder, X64_OPCODE_STORE, X64_REGISTER_R10, X64_REGISTER_RDX);
    x64_write_registers(builder, X64_OPCODE_STORE, X64_REGISTER_R10, X64_REGISTER_RAX);
    x64_write_registers(builder, X64_OPCODE_ADD, X64_REGISTER_RCX, X64_REGISTER_RAX);
    x64_write_load_integer(builder, X64_REGISTER_R8, 0);
    x64_write_memory(builder, X64_OPCODE_STORE_BYTE, X64_REGISTER_R8, X64_REGISTER_RAX, 0);
    x64_copy_write(builder);

    int32_t target = x64_stack_push(builder, 1);
    x64_write_memory(builder, X64_OPCODE_STORE, X64_REGISTER_RDX, X64_REGISTER_RBP, target);
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
 * Returns the signature that `call` calls with: the `AST_NODE_TYPE_PROCEDURE` of a procedure of the
 * module, or the signature of the type of the pointer to a procedure that the variable, the
 * parameter or the global variable of the call holds. Returns `AST_NODE_INDEX_NONE` for the call
 * of a built-in procedure, and of a name that does not hold a pointer to a procedure.
 */
static Ast_Node_Index x64_call_procedure(X64_Builder const *builder, Ast_Node_Index call) {
    assert(builder->nodes[call].node_type == AST_NODE_TYPE_CALL);

    Semantic_Binding binding = builder->bindings[call];
    if (binding.kind == SEMANTIC_BINDING_KIND_CONSTANT) {
        Ast_Node_Index procedure = builder->nodes[binding.target].first_child;
        if (builder->nodes[procedure].node_type != AST_NODE_TYPE_PROCEDURE) {
            return AST_NODE_INDEX_NONE;
        }
        return procedure;
    }
    X64_Type type = x64_type_make(X64_TYPE_KIND_NONE);
    if (binding.kind == SEMANTIC_BINDING_KIND_PARAMETER) {
        type = x64_type_of_node(builder, builder->nodes[binding.target].first_child);
    } else if (binding.kind == SEMANTIC_BINDING_KIND_VARIABLE ||
               binding.kind == SEMANTIC_BINDING_KIND_GLOBAL) {
        type = builder->layouts[binding.target].type;
    }
    if (type.kind != X64_TYPE_KIND_PROCEDURE) {
        return AST_NODE_INDEX_NONE;
    }
    return type.definition;
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
 * untyped constant becomes the type that is wanted when it fits in it: an untyped string is a
 * `C_Str`, the address of a copy of its bytes with a zero after them, and an untyped integer from 0
 * to 255 is a `Byte`. A struct literal without a name is of the struct that is wanted, and a member
 * of an enum written without its enum, such as `.RED`, is of the enum that is wanted, when it has
 * the member. `nil` is a pointer of any type, and an array is an array slice of all of its
 * elements, which `x64_conversion_write` makes of it. The caller compares the type with `wanted`,
 * so an expression that does not become it keeps the type that it has where nothing else says.
 */
static X64_Type x64_expression_type_for(X64_Builder const *builder, Ast_Node_Index expression,
                                        X64_Type wanted) {
    Constant_Value constant = builder->constant_values[expression];
    if (constant.status == CONSTANT_STATUS_OK && constant.type == CONSTANT_TYPE_UNTYPED_STRING &&
        wanted.kind == X64_TYPE_KIND_C_STR) {
        return wanted;
    }
    if (constant.status == CONSTANT_STATUS_OK && constant_is_byte(constant) &&
        wanted.kind == X64_TYPE_KIND_BYTE) {
        return wanted;
    }
    if (builder->nodes[expression].node_type == AST_NODE_TYPE_STRUCT_LITERAL &&
        builder->nodes[expression].payload.struct_literal.name.bytes == NULL &&
        wanted.kind == X64_TYPE_KIND_STRUCT) {
        return wanted;
    }
    X64_Type type = x64_expression_type(builder, expression);
    if (type.kind == X64_TYPE_KIND_NIL &&
        (wanted.kind == X64_TYPE_KIND_POINTER || wanted.kind == X64_TYPE_KIND_PROCEDURE)) {
        return wanted;
    }
    if (type.kind == X64_TYPE_KIND_ENUM_LITERAL && wanted.kind == X64_TYPE_KIND_ENUM) {
        int64_t number = 0;
        if (x64_enum_member_find(builder, wanted.definition,
                                 builder->nodes[expression].payload.enum_literal.name, &number)) {
            return wanted;
        }
    }
    if (type.kind == X64_TYPE_KIND_ARRAY && wanted.kind == X64_TYPE_KIND_SLICE) {
        X64_Type slice = type;
        slice.kind = X64_TYPE_KIND_SLICE;
        slice.length = -1;
        if (x64_type_equals(slice, wanted)) {
            return wanted;
        }
    }
    return type;
}

/**
 * Returns the type of `expression`, or a type of the kind `X64_TYPE_KIND_NONE` if it has none,
 * which is the case for a call of a procedure that returns nothing and for everything that the
 * target does not build. The type of a place that is reached through others, such as `a[1]^`, is
 * worked out without recursion: the path down to the expression that the places start from is
 * kept, and its type is changed by each step back up. A pointer has the type of a pointer to what
 * it points to, a subscript the type of the elements, a slice the type of a slice of them, and a
 * field the type of the field.
 */
static X64_Type x64_expression_type(X64_Builder const *builder, Ast_Node_Index expression) {
    assert(expression < builder->node_count);

    Ast_Node_Index path[X64_EXPRESSION_DEPTH_MAX];
    size_t path_count = 0;
    Ast_Node_Index current = expression;
    while (path_count < X64_EXPRESSION_DEPTH_MAX && not x64_is_constant(builder, current)) {
        Ast_Node_Type node_type = builder->nodes[current].node_type;
        if (node_type != AST_NODE_TYPE_ADDRESS_OF && node_type != AST_NODE_TYPE_DEREFERENCE &&
            node_type != AST_NODE_TYPE_FIELD_ACCESS && node_type != AST_NODE_TYPE_SUBSCRIPT &&
            node_type != AST_NODE_TYPE_SLICE) {
            break;
        }
        // The member of an enum and a field of the information about an enum are no places.
        if (x64_enum_of_access(builder, current) != AST_NODE_INDEX_NONE ||
            x64_type_info_access_is(builder, current)) {
            break;
        }
        path[path_count] = current;
        path_count += 1;
        current = builder->nodes[current].first_child;
    }

    X64_Type type = x64_leaf_type(builder, current);
    for (size_t i = path_count; i > 0 && type.kind != X64_TYPE_KIND_NONE; i -= 1) {
        switch (builder->nodes[path[i - 1]].node_type) {
        case AST_NODE_TYPE_ADDRESS_OF:
            type = x64_type_pointer_to(type);
            break;
        case AST_NODE_TYPE_DEREFERENCE:
            if (type.kind != X64_TYPE_KIND_POINTER) {
                return x64_type_make(X64_TYPE_KIND_NONE);
            }
            type = x64_type_pointee(type);
            break;
        case AST_NODE_TYPE_FIELD_ACCESS: {
            // A field of a struct, or of the struct that a pointer of one caret points to.
            bool is_struct = type.kind == X64_TYPE_KIND_STRUCT ||
                             (type.kind == X64_TYPE_KIND_POINTER && type.depth == 1 &&
                              type.pointee_kind == X64_TYPE_KIND_STRUCT);
            size_t offset = 0;
            X64_Type field_type = x64_type_make(X64_TYPE_KIND_NONE);
            if (not is_struct ||
                not x64_struct_field_find(builder, type.definition,
                                      builder->nodes[path[i - 1]].payload.field_access.name,
                                      &offset, &field_type)) {
                return x64_type_make(X64_TYPE_KIND_NONE);
            }
            type = field_type;
            break;
        }
        case AST_NODE_TYPE_SLICE:
        case AST_NODE_TYPE_SUBSCRIPT:
            if (type.kind != X64_TYPE_KIND_ARRAY && type.kind != X64_TYPE_KIND_SLICE) {
                return x64_type_make(X64_TYPE_KIND_NONE);
            }
            if (builder->nodes[path[i - 1]].node_type == AST_NODE_TYPE_SUBSCRIPT) {
                type = x64_type_element(type);
            } else {
                type = x64_type_sequence_of(x64_type_element(type), X64_TYPE_KIND_SLICE, -1);
            }
            break;
        default:
            assert(false);
            break;
        }
    }
    return type;
}

/**
 * Returns the type of `expression`, which is not a place that is reached through another, see
 * `x64_expression_type`. A constant has the type that it has where nothing else says, see
 * `constant_type_default`, which is `Int` for an untyped integer and `Bool` for an untyped truth
 * value and `Str` for an untyped string. A variable has the type that the layout of its procedure
 * worked out, see `x64_procedure_layout`, and a global variable the type that the layout of `.data`
 * worked out, see `x64_data_layout`.
 */
static X64_Type x64_leaf_type(X64_Builder const *builder, Ast_Node_Index expression) {
    assert(expression < builder->node_count);

    Ast_Node const *node = &builder->nodes[expression];
    Constant_Value constant = builder->constant_values[expression];
    if (constant.status == CONSTANT_STATUS_OK) {
        switch (constant_type_default(constant.type)) {
        case CONSTANT_TYPE_BOOL:
            return x64_type_make(X64_TYPE_KIND_BOOL);
        case CONSTANT_TYPE_BYTE:
            return x64_type_make(X64_TYPE_KIND_BYTE);
        case CONSTANT_TYPE_INT:
            return x64_type_make(X64_TYPE_KIND_INT);
        case CONSTANT_TYPE_STR:
            return x64_type_make(X64_TYPE_KIND_STR);
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
    case AST_NODE_TYPE_CAST:
        // The type that is cast to. A type that is not allowed is an error when the cast is
        // written, see `x64_cast_write`.
        return x64_type_of_name(builder, node->payload.cast.type_name);
    case AST_NODE_TYPE_ARRAY_LITERAL: {
        X64_Type type = x64_type_of_node(builder, node->first_child);
        if (type.kind != X64_TYPE_KIND_ARRAY) {
            return x64_type_make(X64_TYPE_KIND_NONE);
        }
        // The elements are the children after the type, and the type of a literal has the length
        // of its elements, which is checked against the length that is written.
        int64_t count = 0;
        for (Ast_Node_Index child = builder->nodes[node->first_child].next_sibling;
             child != AST_NODE_INDEX_NONE; child = builder->nodes[child].next_sibling) {
            count += 1;
        }
        type.length = count;
        return type;
    }
    case AST_NODE_TYPE_CALL: {
        if (x64_call_is_built_in(builder, expression)) {
            Str name = node->payload.call.name;
            if (str_equals(name, STR("str_to_c_str"))) {
                return x64_type_make(X64_TYPE_KIND_C_STR);
            }
            if (str_equals(name, STR("int_to_byte"))) {
                return x64_type_make(X64_TYPE_KIND_BYTE);
            }
            return x64_type_make(X64_TYPE_KIND_INT);
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
        if (binding.kind == SEMANTIC_BINDING_KIND_VARIABLE ||
            binding.kind == SEMANTIC_BINDING_KIND_GLOBAL) {
            return builder->layouts[binding.target].type;
        }
        if (binding.kind == SEMANTIC_BINDING_KIND_CONSTANT) {
            Ast_Node_Index value = builder->nodes[binding.target].first_child;
            Ast_Node_Index enum_node = x64_enum_of_access(builder, value);
            if (enum_node != AST_NODE_INDEX_NONE) {
                X64_Type member = x64_type_make(X64_TYPE_KIND_ENUM);
                member.definition = enum_node;
                return member;
            }
            // The name of a procedure that is not called is a pointer to it.
            if (builder->nodes[value].node_type == AST_NODE_TYPE_PROCEDURE) {
                X64_Type procedure = x64_type_make(X64_TYPE_KIND_PROCEDURE);
                procedure.definition = x64_signature_canonical(builder, value);
                return procedure;
            }
        }
        break;
    }
    case AST_NODE_TYPE_ENUM_LITERAL:
        return x64_type_make(X64_TYPE_KIND_ENUM_LITERAL);
    case AST_NODE_TYPE_FIELD_ACCESS: {
        // The member of an enum written with its enum, and a field of the information about an
        // enum, which are not places.
        if (x64_type_info_access_is(builder, expression)) {
            return x64_type_info_type(builder, expression);
        }
        Ast_Node_Index enum_node = x64_enum_of_access(builder, expression);
        if (enum_node != AST_NODE_INDEX_NONE) {
            X64_Type member = x64_type_make(X64_TYPE_KIND_ENUM);
            member.definition = enum_node;
            return member;
        }
        break;
    }
    case AST_NODE_TYPE_NIL_LITERAL:
        return x64_type_make(X64_TYPE_KIND_NIL);
    case AST_NODE_TYPE_STRUCT_LITERAL:
        // A literal of a field that is a struct has no name, and gets its type from the field, see
        // `x64_expression_type_for`.
        if (node->payload.struct_literal.name.bytes != NULL) {
            return x64_type_of_name(builder, node->payload.struct_literal.name);
        }
        break;
    default:
        break;
    }

    return x64_type_make(X64_TYPE_KIND_NONE);
}

/**
 * Returns the type that a procedure returns, or a type of the kind `X64_TYPE_KIND_NONE` when it
 * returns nothing. `procedure` is an `AST_NODE_TYPE_PROCEDURE` or an
 * `AST_NODE_TYPE_PROCEDURE_TYPE`.
 */
static X64_Type x64_return_type(X64_Builder const *builder, Ast_Node_Index procedure) {
    assert(builder->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE ||
           builder->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE_TYPE);

    Str name = ast_procedure_return_type(&builder->nodes[procedure]);
    if (name.bytes == NULL) {
        return x64_type_make(X64_TYPE_KIND_NONE);
    }
    return x64_type_of_name(builder, name);
}

/**
 * Returns the enum called `name`, which is the `AST_NODE_TYPE_ENUM_TYPE` that is the value of the
 * constant of that name, or `AST_NODE_INDEX_NONE` when the module has no enum of that name.
 */
static Ast_Node_Index x64_enum_find(X64_Builder const *builder, Str name) {
    Ast_Node_Index definition = builder->nodes[builder->module].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && definition != AST_NODE_INDEX_NONE; i += 1) {
        Ast_Node const *node = &builder->nodes[definition];
        if (node->node_type == AST_NODE_TYPE_CONSTANT_DEFINITION &&
            builder->nodes[node->first_child].node_type == AST_NODE_TYPE_ENUM_TYPE &&
            str_equals(node->payload.constant_definition.name, name)) {
            return node->first_child;
        }
        definition = node->next_sibling;
    }
    return AST_NODE_INDEX_NONE;
}

/**
 * Sets `*number` to the number of the member called `member` of the enum `enum_node`, which is its
 * place among the members, counted from 0. Returns false when the enum has no such member.
 */
static bool x64_enum_member_find(X64_Builder const *builder, Ast_Node_Index enum_node, Str member,
                                 int64_t *number) {
    assert(builder->nodes[enum_node].node_type == AST_NODE_TYPE_ENUM_TYPE);

    int64_t count = 0;
    Ast_Node_Index child = builder->nodes[enum_node].first_child;
    // Every member is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && child != AST_NODE_INDEX_NONE; i += 1) {
        if (str_equals(builder->nodes[child].payload.enum_member.name, member)) {
            *number = count;
            return true;
        }
        count += 1;
        child = builder->nodes[child].next_sibling;
    }
    return false;
}

/**
 * Returns the enum of `access` when it is the member of an enum written with the name of the enum,
 * such as `Color.RED`, which is a field access of the name of the enum, or `AST_NODE_INDEX_NONE`
 * for any other node.
 */
static Ast_Node_Index x64_enum_of_access(X64_Builder const *builder, Ast_Node_Index access) {
    if (builder->nodes[access].node_type != AST_NODE_TYPE_FIELD_ACCESS) {
        return AST_NODE_INDEX_NONE;
    }
    Ast_Node_Index base = builder->nodes[access].first_child;
    Semantic_Binding binding = builder->bindings[base];
    if (builder->nodes[base].node_type != AST_NODE_TYPE_IDENTIFIER ||
        binding.kind != SEMANTIC_BINDING_KIND_CONSTANT) {
        return AST_NODE_INDEX_NONE;
    }
    Ast_Node_Index value = builder->nodes[binding.target].first_child;
    if (builder->nodes[value].node_type != AST_NODE_TYPE_ENUM_TYPE) {
        return AST_NODE_INDEX_NONE;
    }
    return value;
}

/**
 * Returns the name of the member of the enum `enum_node` that `expression` is, when that is known
 * from the text of the program: `.RED`, or `Color.RED` where `Color` is that enum. Otherwise
 * returns a `Str` whose `bytes` is NULL.
 */
static Str x64_enum_member_name_of(X64_Builder const *builder, Ast_Node_Index expression,
                                   Ast_Node_Index enum_node) {
    Ast_Node const *node = &builder->nodes[expression];
    if (node->node_type == AST_NODE_TYPE_ENUM_LITERAL) {
        return node->payload.enum_literal.name;
    }
    if (x64_enum_of_access(builder, expression) == enum_node &&
        enum_node != AST_NODE_INDEX_NONE) {
        return node->payload.field_access.name;
    }
    return STR_EMPTY;
}

/**
 * Returns whether `access` is a field of the information about a type, such as
 * `type_info_of(Color).members`, which is a field access of an `AST_NODE_TYPE_TYPE_INFO_OF`.
 */
static bool x64_type_info_access_is(X64_Builder const *builder, Ast_Node_Index access) {
    return builder->nodes[access].node_type == AST_NODE_TYPE_FIELD_ACCESS &&
           builder->nodes[builder->nodes[access].first_child].node_type ==
               AST_NODE_TYPE_TYPE_INFO_OF;
}

/**
 * Returns the enum that the information `info`, an `AST_NODE_TYPE_TYPE_INFO_OF`, is about, or
 * `AST_NODE_INDEX_NONE` when its type is not an enum, which are the only types that the target
 * gives information about.
 */
static Ast_Node_Index x64_type_info_enum(X64_Builder const *builder, Ast_Node_Index info) {
    assert(builder->nodes[info].node_type == AST_NODE_TYPE_TYPE_INFO_OF);

    Ast_Node_Index type = builder->nodes[info].first_child;
    if (type == AST_NODE_INDEX_NONE || builder->nodes[type].node_type != AST_NODE_TYPE_TYPE_NAME) {
        return AST_NODE_INDEX_NONE;
    }
    return x64_enum_find(builder, builder->nodes[type].payload.type_name.name);
}

/**
 * Returns the type of the field access `access` of the information about a type: `members` is an
 * array of `Enum_Member_Type_Info`, which the parser adds to the program, and `name` is a `Str`.
 * The kind is `X64_TYPE_KIND_NONE` for a type that is not an enum and for any other field.
 */
static X64_Type x64_type_info_type(X64_Builder const *builder, Ast_Node_Index access) {
    assert(x64_type_info_access_is(builder, access));

    Ast_Node_Index enum_node = x64_type_info_enum(builder, builder->nodes[access].first_child);
    if (enum_node == AST_NODE_INDEX_NONE) {
        return x64_type_make(X64_TYPE_KIND_NONE);
    }
    Str field = builder->nodes[access].payload.field_access.name;
    if (str_equals(field, STR("name"))) {
        return x64_type_make(X64_TYPE_KIND_STR);
    }
    Ast_Node_Index member_struct = x64_struct_find(builder, STR("Enum_Member_Type_Info"));
    if (not str_equals(field, STR("members")) || member_struct == AST_NODE_INDEX_NONE) {
        return x64_type_make(X64_TYPE_KIND_NONE);
    }
    X64_Type element = x64_type_make(X64_TYPE_KIND_STRUCT);
    element.definition = member_struct;
    int64_t count = 0;
    for (Ast_Node_Index member = builder->nodes[enum_node].first_child;
         member != AST_NODE_INDEX_NONE; member = builder->nodes[member].next_sibling) {
        count += 1;
    }
    return x64_type_sequence_of(element, X64_TYPE_KIND_ARRAY, count);
}

/**
 * Returns the struct called `name`, which is the `AST_NODE_TYPE_STRUCT_TYPE` that is the value of
 * the constant of that name, or `AST_NODE_INDEX_NONE` when the module has no struct of that name.
 */
static Ast_Node_Index x64_struct_find(X64_Builder const *builder, Str name) {
    Ast_Node_Index definition = builder->nodes[builder->module].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && definition != AST_NODE_INDEX_NONE; i += 1) {
        Ast_Node const *node = &builder->nodes[definition];
        if (node->node_type == AST_NODE_TYPE_CONSTANT_DEFINITION &&
            builder->nodes[node->first_child].node_type == AST_NODE_TYPE_STRUCT_TYPE &&
            str_equals(node->payload.constant_definition.name, name)) {
            return node->first_child;
        }
        definition = node->next_sibling;
    }
    return AST_NODE_INDEX_NONE;
}

/**
 * Returns the number of words of the struct `struct_node`, an `AST_NODE_TYPE_STRUCT_TYPE`, which
 * are the words of its fields one after the other, where a field that is a struct takes the words
 * of its own fields in its place, and nothing else: there is no padding, because every word is 8
 * bytes, so every word is aligned. A `Byte` takes a word too, so that every field is a number of
 * words, and a `Str` takes two. A pointer is one word, and the struct that it points to is not
 * walked, so a struct can have a pointer to itself. The struct is walked without recursion, with
 * the field that is next in each struct that is open, as deep as `X64_STRUCT_DEPTH_MAX`. Returns
 * `X64_WORD_COUNT_INVALID` when the structs are inside each other deeper than that, which is the
 * case for a struct that contains itself, and a field of a type that the target does not have
 * counts as a word, because `x64_structs_check` rejects it.
 */
static size_t x64_struct_word_count(X64_Builder const *builder, Ast_Node_Index struct_node) {
    assert(builder->nodes[struct_node].node_type == AST_NODE_TYPE_STRUCT_TYPE);

    Ast_Node_Index next_fields[X64_STRUCT_DEPTH_MAX];
    size_t depth = 1;
    next_fields[0] = builder->nodes[struct_node].first_child;
    size_t count = 0;
    for (size_t step = 0; depth > 0; step += 1) {
        if (step == X64_STRUCT_STEP_MAX) {
            return X64_WORD_COUNT_INVALID;
        }
        Ast_Node_Index field = next_fields[depth - 1];
        if (field == AST_NODE_INDEX_NONE) {
            depth -= 1;
            continue;
        }
        next_fields[depth - 1] = builder->nodes[field].next_sibling;

        Ast_Node_Index type_node = builder->nodes[field].first_child;
        Ast_Node_Index nested = AST_NODE_INDEX_NONE;
        size_t leaf_words = 1;
        if (builder->nodes[type_node].node_type == AST_NODE_TYPE_STRUCT_TYPE) {
            nested = type_node;
        } else if (builder->nodes[type_node].node_type == AST_NODE_TYPE_TYPE_NAME) {
            Str name = builder->nodes[type_node].payload.type_name.name;
            nested = x64_struct_find(builder, name);
            if (str_equals(name, STR("Str"))) {
                leaf_words = 2;
            }
        }
        if (nested == AST_NODE_INDEX_NONE) {
            count += leaf_words;
            continue;
        }
        if (depth == X64_STRUCT_DEPTH_MAX) {
            return X64_WORD_COUNT_INVALID;
        }
        next_fields[depth] = builder->nodes[nested].first_child;
        depth += 1;
    }
    return count;
}

/**
 * Finds the field called `name` of the struct `struct_node`, an `AST_NODE_TYPE_STRUCT_TYPE`, and
 * sets `*word_offset` to the number of words before it in the struct and `*type` to its type.
 * Returns false when the struct has no such field.
 */
static bool x64_struct_field_find(X64_Builder const *builder, Ast_Node_Index struct_node, Str name,
                                  size_t *word_offset, X64_Type *type) {
    assert(builder->nodes[struct_node].node_type == AST_NODE_TYPE_STRUCT_TYPE);

    size_t offset = 0;
    Ast_Node_Index field = builder->nodes[struct_node].first_child;
    // Every field is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && field != AST_NODE_INDEX_NONE; i += 1) {
        X64_Type field_type = x64_type_of_element_node(builder, builder->nodes[field].first_child);
        if (str_equals(builder->nodes[field].payload.field.name, name)) {
            *word_offset = offset;
            *type = field_type;
            return true;
        }
        offset += x64_type_word_count(builder, field_type);
        field = builder->nodes[field].next_sibling;
    }
    return false;
}

/**
 * Returns the `AST_NODE_TYPE_UNION_MATCH` after the type of `field`, which says that the field is
 * in a case of a union match, or `AST_NODE_INDEX_NONE` for a field that is not.
 */
static Ast_Node_Index x64_field_union_match(X64_Builder const *builder, Ast_Node_Index field) {
    assert(builder->nodes[field].node_type == AST_NODE_TYPE_FIELD);

    Ast_Node_Index match = builder->nodes[builder->nodes[field].first_child].next_sibling;
    assert(match == AST_NODE_INDEX_NONE ||
           builder->nodes[match].node_type == AST_NODE_TYPE_UNION_MATCH);
    return match;
}

/**
 * Returns the `AST_NODE_TYPE_FIELD` of the struct `struct_node` that is called `name`, or
 * `AST_NODE_INDEX_NONE` when it has none.
 */
static Ast_Node_Index x64_struct_field_node_find(X64_Builder const *builder,
                                                 Ast_Node_Index struct_node, Str name) {
    assert(builder->nodes[struct_node].node_type == AST_NODE_TYPE_STRUCT_TYPE);

    Ast_Node_Index field = builder->nodes[struct_node].first_child;
    // Every field is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && field != AST_NODE_INDEX_NONE; i += 1) {
        if (str_equals(builder->nodes[field].payload.field.name, name)) {
            return field;
        }
        field = builder->nodes[field].next_sibling;
    }
    return AST_NODE_INDEX_NONE;
}

/**
 * Returns the enum of the field that the union match `union_match`, a child of a field of
 * `struct_node`, is matched against, or `AST_NODE_INDEX_NONE` when that is not a field of the
 * struct that has an enum for its type, or is in a case itself: what the cases are members of must
 * exist whatever the case is.
 */
static Ast_Node_Index x64_union_enum(X64_Builder const *builder, Ast_Node_Index struct_node,
                                     Ast_Node_Index union_match) {
    assert(builder->nodes[union_match].node_type == AST_NODE_TYPE_UNION_MATCH);

    Ast_Node_Index discriminant = x64_struct_field_node_find(
        builder, struct_node, builder->nodes[union_match].payload.union_match.discriminant);
    if (discriminant == AST_NODE_INDEX_NONE ||
        x64_field_union_match(builder, discriminant) != AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }
    Ast_Node_Index type = builder->nodes[discriminant].first_child;
    if (builder->nodes[type].node_type != AST_NODE_TYPE_TYPE_NAME) {
        return AST_NODE_INDEX_NONE;
    }
    return x64_enum_find(builder, builder->nodes[type].payload.type_name.name);
}

/**
 * Returns the node whose child `child` is, or `AST_NODE_INDEX_NONE` for the root. The tree has no
 * links to the parents, so they are looked for, which only the checks of unions need.
 */
static Ast_Node_Index x64_parent_find(X64_Builder const *builder, Ast_Node_Index child) {
    for (Ast_Node_Index node = 0; node < builder->node_count; node += 1) {
        Ast_Node_Index sibling = builder->nodes[node].first_child;
        // Every child is a distinct node, so there cannot be more than there are nodes.
        for (size_t i = 0; i < builder->node_count && sibling != AST_NODE_INDEX_NONE; i += 1) {
            if (sibling == child) {
                return node;
            }
            sibling = builder->nodes[sibling].next_sibling;
        }
    }
    return AST_NODE_INDEX_NONE;
}

/**
 * Checks the union matches of the structs: the field that is matched must be a field of an enum
 * type that is not in a case, and every case must be a member of that enum. Returns false after it
 * recorded an error.
 */
static bool x64_unions_check(X64_Builder *builder) {
    for (Ast_Node_Index node = 0; node < builder->node_count; node += 1) {
        if (builder->nodes[node].node_type != AST_NODE_TYPE_FIELD) {
            continue;
        }
        Ast_Node_Index match = x64_field_union_match(builder, node);
        if (match == AST_NODE_INDEX_NONE) {
            continue;
        }
        Ast_Node_Index struct_node = x64_parent_find(builder, node);
        assert(struct_node != AST_NODE_INDEX_NONE);
        Ast_Node_Index enum_node = x64_union_enum(builder, struct_node, match);
        if (enum_node == AST_NODE_INDEX_NONE) {
            return x64_fail(builder, X64_BUILD_STATUS_UNION_DISCRIMINANT_INVALID,
                            builder->nodes[match].payload.union_match.discriminant);
        }
        Str member = builder->nodes[builder->nodes[match].first_child].payload.union_case.member;
        int64_t number = 0;
        if (not x64_enum_member_find(builder, enum_node, member, &number)) {
            return x64_fail(builder, X64_BUILD_STATUS_UNION_CASE_UNKNOWN, member);
        }
    }
    return true;
}

/**
 * Returns whether `first` and `second` are the same place: the same variable or parameter, or the
 * same field or what the same pointer points to, of places that are the same. Such a place has the
 * same value at two points of the program as long as nothing assigns to it in between. An element
 * of an array is never taken as the same place as another, because its index may differ.
 */
static bool x64_path_equals(X64_Builder const *builder, Ast_Node_Index first,
                            Ast_Node_Index second) {
    Ast_Node_Index a = first;
    Ast_Node_Index b = second;
    // Each step goes down one node of both, so there are fewer steps than nodes.
    for (size_t i = 0; i < builder->node_count; i += 1) {
        Ast_Node const *node_a = &builder->nodes[a];
        Ast_Node const *node_b = &builder->nodes[b];
        if (node_a->node_type != node_b->node_type) {
            return false;
        }
        if (node_a->node_type == AST_NODE_TYPE_IDENTIFIER) {
            Semantic_Binding binding_a = builder->bindings[a];
            Semantic_Binding binding_b = builder->bindings[b];
            return binding_a.kind == binding_b.kind && binding_a.target == binding_b.target &&
                   binding_a.position == binding_b.position;
        }
        if (node_a->node_type == AST_NODE_TYPE_FIELD_ACCESS) {
            if (not str_equals(node_a->payload.field_access.name,
                           node_b->payload.field_access.name)) {
                return false;
            }
        } else if (node_a->node_type != AST_NODE_TYPE_DEREFERENCE) {
            return false;
        }
        a = node_a->first_child;
        b = node_b->first_child;
    }
    return false;
}

/**
 * Returns whether the statements in `block` can change which case the object `container` is in,
 * which they do when they assign to the field called `discriminant` of it, to the object itself, or
 * to a place it is in, or take the address of one of them, because the address can be used to
 * assign later. `container` is the expression before the dot, such as `t` in `t.identifier`.
 */
static bool x64_union_block_changes(X64_Builder const *builder, Ast_Node_Index block,
                                    Ast_Node_Index container, Str discriminant) {
    assert(builder->nodes[block].node_type == AST_NODE_TYPE_BLOCK);

    // The nodes of the block are the ones from its own index up to its end.
    for (Ast_Node_Index node = block + 1; node < builder->nodes[block].payload.block.end;
         node += 1) {
        Ast_Node_Type node_type = builder->nodes[node].node_type;
        if (not ast_node_type_is_assignment(node_type) &&
            node_type != AST_NODE_TYPE_ADDRESS_OF) {
            continue;
        }
        // The place that is assigned to, or whose address is taken, is the first child.
        Ast_Node_Index place = builder->nodes[node].first_child;
        if (builder->nodes[place].node_type == AST_NODE_TYPE_FIELD_ACCESS &&
            str_equals(builder->nodes[place].payload.field_access.name, discriminant) &&
            x64_path_equals(builder, builder->nodes[place].first_child, container)) {
            return true;
        }
        // The object itself or a place that it is in: `t`, and for `a.b.t` also `a.b` and `a`. A
        // pointer that the object is reached through counts as such a place.
        Ast_Node_Index outer = container;
        for (size_t i = 0; i < builder->node_count; i += 1) {
            if (x64_path_equals(builder, place, outer)) {
                return true;
            }
            Ast_Node_Type outer_type = builder->nodes[outer].node_type;
            if (outer_type != AST_NODE_TYPE_FIELD_ACCESS &&
                outer_type != AST_NODE_TYPE_DEREFERENCE) {
                break;
            }
            outer = builder->nodes[outer].first_child;
        }
    }
    return false;
}

/**
 * Returns whether `condition` holds only when the field of the union match `union_match` of the
 * object `container` is the member of its case, which is when it is a comparison with `==` of the
 * two, or an `and` that has such a comparison among its operands, however deep. The comparison can
 * have either side first. `enum_node` is the enum of the field.
 */
static bool x64_union_condition_matches(X64_Builder const *builder, Ast_Node_Index condition,
                                        Ast_Node_Index container, Ast_Node_Index union_match,
                                        Ast_Node_Index enum_node) {
    assert(builder->nodes[union_match].node_type == AST_NODE_TYPE_UNION_MATCH);

    Str discriminant = builder->nodes[union_match].payload.union_match.discriminant;
    Str member = builder->nodes[builder->nodes[union_match].first_child].payload.union_case.member;

    // The operands of the `and`s that are still to be looked at, kept in an array instead of
    // calling this function again. A condition with more `and`s than fit is not matched.
    Ast_Node_Index pending[X64_STRUCT_DEPTH_MAX];
    size_t pending_count = 1;
    pending[0] = condition;
    // Each step takes a node from the condition, so there are fewer steps than nodes.
    for (size_t step = 0; step < builder->node_count && pending_count > 0; step += 1) {
        pending_count -= 1;
        Ast_Node const *node = &builder->nodes[pending[pending_count]];
        if (node->node_type == AST_NODE_TYPE_AND) {
            if (pending_count + 2 > X64_STRUCT_DEPTH_MAX) {
                return false;
            }
            pending[pending_count] = node->first_child;
            pending[pending_count + 1] = node->last_child;
            pending_count += 2;
            continue;
        }
        if (node->node_type != AST_NODE_TYPE_COMPARISON ||
            node->payload.comparison.operator != AST_COMPARISON_OPERATOR_EQUAL) {
            continue;
        }
        Ast_Node_Index sides[2] = {node->first_child, node->last_child};
        for (size_t side = 0; side < 2; side += 1) {
            Ast_Node const *matched = &builder->nodes[sides[side]];
            if (matched->node_type != AST_NODE_TYPE_FIELD_ACCESS ||
                not str_equals(matched->payload.field_access.name, discriminant) ||
                not x64_path_equals(builder, matched->first_child, container)) {
                continue;
            }
            Str other_member = x64_enum_member_name_of(builder, sides[1 - side], enum_node);
            if (other_member.bytes != NULL && str_equals(other_member, member)) {
                return true;
            }
        }
    }
    return false;
}

/**
 * Returns whether the compiler can tell that the field in a case of the union match `union_match`,
 * which the field access `access` reads or assigns, exists. The field is of the object that is the
 * expression before the dot, and that must be known to be in the case. It is where the access is in
 * the block of an if statement, or in an `and` after a comparison, whose condition compares the
 * field that is matched with the member of the case, and where the statements of the block do not
 * change the object, see `x64_union_block_changes`. The search goes outwards from the access, one
 * node at a time, through all of the if statements around it. `struct_node` is the struct that
 * the field is in.
 */
static bool x64_union_is_matched(X64_Builder const *builder, Ast_Node_Index access,
                                 Ast_Node_Index struct_node, Ast_Node_Index union_match) {
    assert(builder->nodes[access].node_type == AST_NODE_TYPE_FIELD_ACCESS);

    Ast_Node_Index enum_node = x64_union_enum(builder, struct_node, union_match);
    assert(enum_node != AST_NODE_INDEX_NONE);
    Ast_Node_Index container = builder->nodes[access].first_child;
    Str discriminant = builder->nodes[union_match].payload.union_match.discriminant;

    Ast_Node_Index node = access;
    // Each step goes up one node, so there are fewer steps than nodes.
    for (size_t step = 0; step < builder->node_count; step += 1) {
        Ast_Node_Index parent = x64_parent_find(builder, node);
        if (parent == AST_NODE_INDEX_NONE ||
            builder->nodes[parent].node_type == AST_NODE_TYPE_PROCEDURE ||
            builder->nodes[parent].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION) {
            return false;
        }
        // The right operand of an `and` is only evaluated when the left one holds.
        if (builder->nodes[parent].node_type == AST_NODE_TYPE_AND &&
            builder->nodes[parent].last_child == node &&
            x64_union_condition_matches(builder, builder->nodes[parent].first_child, container,
                                        union_match, enum_node)) {
            return true;
        }
        if (builder->nodes[parent].node_type == AST_NODE_TYPE_BLOCK) {
            Ast_Node_Index statement = x64_parent_find(builder, parent);
            // The children of an if statement are a condition and its block, again and again, and
            // the block of the `else` comes last, with a block before it instead of a condition.
            Ast_Node_Index previous = AST_NODE_INDEX_NONE;
            if (statement != AST_NODE_INDEX_NONE &&
                builder->nodes[statement].node_type == AST_NODE_TYPE_IF_STATEMENT) {
                Ast_Node_Index child = builder->nodes[statement].first_child;
                // The block is one of the children, so there are fewer steps than nodes.
                for (size_t j = 0; j < builder->node_count && child != parent; j += 1) {
                    previous = child;
                    child = builder->nodes[child].next_sibling;
                }
            }
            if (previous != AST_NODE_INDEX_NONE &&
                builder->nodes[previous].node_type != AST_NODE_TYPE_BLOCK &&
                x64_union_condition_matches(builder, previous, container, union_match,
                                            enum_node) &&
                not x64_union_block_changes(builder, parent, container, discriminant)) {
                return true;
            }
        }
        node = parent;
    }
    return false;
}

/**
 * Returns the name of the member that the struct literal `literal` gives the field that the union
 * match `union_match` is matched against, which is the member written in the literal, or the
 * first member of `enum_node`, the default value of an enum, when the literal does not give the
 * field. A value that is not a member written out has no name, whose `bytes` is NULL.
 */
static Str x64_union_literal_member(X64_Builder const *builder, Ast_Node_Index literal,
                                    Ast_Node_Index union_match, Ast_Node_Index enum_node) {
    Str matched_name = builder->nodes[union_match].payload.union_match.discriminant;
    for (Ast_Node_Index matched = builder->nodes[literal].first_child;
         matched != AST_NODE_INDEX_NONE; matched = builder->nodes[matched].next_sibling) {
        if (str_equals(builder->nodes[matched].payload.field_initializer.name, matched_name)) {
            return x64_enum_member_name_of(builder, builder->nodes[matched].first_child,
                                           enum_node);
        }
    }
    return builder->nodes[builder->nodes[enum_node].first_child].payload.enum_member.name;
}

/**
 * Checks every struct of the module, also the structs of fields, which have no name: a struct does
 * not contain itself, and every field is of a type that a field can have, which is an `Int`, a
 * `Bool`, a `Byte`, a `Str`, an enum, another struct or a pointer. Returns false after it recorded
 * an error.
 */
static bool x64_structs_check(X64_Builder *builder) {
    for (Ast_Node_Index node = 0; node < builder->node_count; node += 1) {
        if (builder->nodes[node].node_type != AST_NODE_TYPE_STRUCT_TYPE) {
            continue;
        }
        Ast_Node_Index field = builder->nodes[node].first_child;
        if (field == AST_NODE_INDEX_NONE) {
            continue;
        }
        if (x64_struct_word_count(builder, node) == X64_WORD_COUNT_INVALID) {
            return x64_fail(builder, X64_BUILD_STATUS_STRUCT_RECURSIVE,
                            builder->nodes[field].payload.field.name);
        }
        // Every field is a distinct node, so there cannot be more than there are nodes.
        for (size_t i = 0; i < builder->node_count && field != AST_NODE_INDEX_NONE; i += 1) {
            Ast_Node_Index type_node = builder->nodes[field].first_child;
            X64_Type type = x64_type_of_element_node(builder, type_node);
            bool is_valid = type.kind == X64_TYPE_KIND_BOOL || type.kind == X64_TYPE_KIND_BYTE ||
                            type.kind == X64_TYPE_KIND_ENUM || type.kind == X64_TYPE_KIND_INT ||
                            type.kind == X64_TYPE_KIND_POINTER ||
                            type.kind == X64_TYPE_KIND_PROCEDURE ||
                            type.kind == X64_TYPE_KIND_STR || type.kind == X64_TYPE_KIND_STRUCT;
            if (not is_valid) {
                return x64_fail(builder, X64_BUILD_STATUS_TYPE_UNSUPPORTED,
                                x64_type_subject(builder, type_node));
            }
            field = builder->nodes[field].next_sibling;
        }
    }
    return true;
}

/**
 * Returns a type of `kind` that has nothing more to it.
 */
static X64_Type x64_type_make(X64_Type_Kind kind) {
    return (X64_Type){
        .length = -1,
        .kind = kind,
        .element_kind = X64_TYPE_KIND_NONE,
        .pointee_kind = X64_TYPE_KIND_NONE,
        .element_pointee_kind = X64_TYPE_KIND_NONE,
        .definition = AST_NODE_INDEX_NONE,
        .element_definition = AST_NODE_INDEX_NONE,
        .depth = 0,
        .element_depth = 0,
    };
}

/**
 * Returns the type of a pointer to a value of `target`, which is a pointer with one more caret when
 * `target` is a pointer already. There is no pointer to an array or to an array slice, which are
 * addresses already, so those, and a type that the target does not have, give a type of the kind
 * `X64_TYPE_KIND_NONE`.
 */
static X64_Type x64_type_pointer_to(X64_Type target) {
    switch (target.kind) {
    case X64_TYPE_KIND_BOOL:
    case X64_TYPE_KIND_BYTE:
    case X64_TYPE_KIND_C_STR:
    case X64_TYPE_KIND_ENUM:
    case X64_TYPE_KIND_INT:
    case X64_TYPE_KIND_PROCEDURE:
    case X64_TYPE_KIND_STR:
    case X64_TYPE_KIND_STRUCT: {
        X64_Type pointer = x64_type_make(X64_TYPE_KIND_POINTER);
        pointer.pointee_kind = target.kind;
        pointer.definition = target.definition;
        pointer.depth = 1;
        return pointer;
    }
    case X64_TYPE_KIND_POINTER: {
        assert(target.depth >= 1);
        assert(target.depth < UINT32_MAX);
        X64_Type pointer = target;
        pointer.depth += 1;
        return pointer;
    }
    case X64_TYPE_KIND_ARRAY:
    case X64_TYPE_KIND_ENUM_LITERAL:
    case X64_TYPE_KIND_NIL:
    case X64_TYPE_KIND_NONE:
    case X64_TYPE_KIND_SLICE:
        break;
    }
    return x64_type_make(X64_TYPE_KIND_NONE);
}

/**
 * Returns the type that the pointer type `pointer` points to: the pointer type with one caret
 * less, or the type at the end of the carets when it has one.
 */
static X64_Type x64_type_pointee(X64_Type pointer) {
    assert(pointer.kind == X64_TYPE_KIND_POINTER);
    assert(pointer.depth >= 1);

    if (pointer.depth > 1) {
        X64_Type inner = pointer;
        inner.depth -= 1;
        return inner;
    }
    X64_Type pointee = x64_type_make(pointer.pointee_kind);
    pointee.definition = pointer.definition;
    return pointee;
}

/**
 * Returns the type of the elements of the array or array slice type `sequence`.
 */
static X64_Type x64_type_element(X64_Type sequence) {
    assert(sequence.kind == X64_TYPE_KIND_ARRAY || sequence.kind == X64_TYPE_KIND_SLICE);

    X64_Type element = x64_type_make(sequence.element_kind);
    element.definition = sequence.element_definition;
    element.pointee_kind = sequence.element_pointee_kind;
    element.depth = sequence.element_depth;
    return element;
}

/**
 * Returns the type of an array of `length` elements of `element`, or of an array slice of them for
 * `X64_TYPE_KIND_SLICE` as `kind`. The elements are an `Int`, a `Bool`, a `Byte`, a `Str`, an enum,
 * a struct or a pointer, and any other type gives a type of the kind `X64_TYPE_KIND_NONE`.
 */
static X64_Type x64_type_sequence_of(X64_Type element, X64_Type_Kind kind, int64_t length) {
    assert(kind == X64_TYPE_KIND_ARRAY || kind == X64_TYPE_KIND_SLICE);

    if (element.kind != X64_TYPE_KIND_BOOL && element.kind != X64_TYPE_KIND_BYTE &&
        element.kind != X64_TYPE_KIND_ENUM && element.kind != X64_TYPE_KIND_INT &&
        element.kind != X64_TYPE_KIND_POINTER && element.kind != X64_TYPE_KIND_PROCEDURE &&
        element.kind != X64_TYPE_KIND_STR && element.kind != X64_TYPE_KIND_STRUCT) {
        return x64_type_make(X64_TYPE_KIND_NONE);
    }
    X64_Type sequence = x64_type_make(kind);
    sequence.length = length;
    if (kind == X64_TYPE_KIND_SLICE) {
        sequence.length = -1;
    }
    sequence.element_kind = element.kind;
    sequence.element_definition = element.definition;
    sequence.element_pointee_kind = element.pointee_kind;
    sequence.element_depth = element.depth;
    return sequence;
}

/**
 * Returns whether two types are the same. An array whose length is not known, which is the type of
 * `[const]Int`, has the length of any array.
 */
static bool x64_type_equals(X64_Type first, X64_Type second) {
    bool is_length_equal = first.length == second.length || first.length < 0 ||
                           second.length < 0;
    return first.kind == second.kind && first.element_kind == second.element_kind &&
           first.pointee_kind == second.pointee_kind &&
           first.element_pointee_kind == second.element_pointee_kind &&
           first.definition == second.definition &&
           first.element_definition == second.element_definition &&
           first.depth == second.depth && first.element_depth == second.element_depth &&
           is_length_equal;
}

/**
 * Returns whether a value of `type` is one word, which the instructions handle in a register.
 */
static bool x64_type_is_scalar(X64_Builder const *builder, X64_Type type) {
    return x64_type_word_count(builder, type) == 1;
}

/**
 * Returns the number of words of 8 bytes that a value of `type` takes: one for an `Int`, which is
 * a 64-bit integer, one for a `Bool`, which is 0 or 1 in a word, so that it is passed to and from
 * the C library like an integer, one for a `Byte`, from 0 to 255 in a word, and one for a `C_Str`
 * and a pointer, which are addresses. An enum is the number of its member, counted from 0 in the
 * order of the definition. A `Str` is the address of its bytes and their number. An array is the
 * address of its first element, because its length is part of its type, and an array slice is the
 * address of its first element and its number of elements. A struct is the words of its fields,
 * see `x64_struct_word_count`.
 */
static size_t x64_type_word_count(X64_Builder const *builder, X64_Type type) {
    switch (type.kind) {
    case X64_TYPE_KIND_ARRAY:
    case X64_TYPE_KIND_BOOL:
    case X64_TYPE_KIND_BYTE:
    case X64_TYPE_KIND_C_STR:
    case X64_TYPE_KIND_ENUM:
    case X64_TYPE_KIND_ENUM_LITERAL:
    case X64_TYPE_KIND_INT:
    case X64_TYPE_KIND_NIL:
    case X64_TYPE_KIND_POINTER:
    case X64_TYPE_KIND_PROCEDURE:
        return 1;
    case X64_TYPE_KIND_SLICE:
    case X64_TYPE_KIND_STR:
        return 2;
    case X64_TYPE_KIND_STRUCT: {
        size_t word_count = x64_struct_word_count(builder, type.definition);
        assert(word_count != X64_WORD_COUNT_INVALID);
        return word_count;
    }
    case X64_TYPE_KIND_NONE:
        break;
    }

    // Unreachable: a type that the target does not have takes no place anywhere.
    assert(false);
    return 1;
}

/**
 * Returns the number of bytes that a value of `type` takes in memory as an element of an array or
 * as what a pointer points to: 8 for each of its words, so that every word is aligned, except a
 * `Byte`, which takes one byte, so that an array of bytes is as small as the bytes in it and is
 * what the C library takes as a buffer.
 */
static size_t x64_type_stride_bytes(X64_Builder const *builder, X64_Type type) {
    if (type.kind == X64_TYPE_KIND_BYTE) {
        return 1;
    }
    return x64_type_word_count(builder, type) * X64_WORD_SIZE_BYTES;
}

/**
 * Returns the type that the name `name` stands for, a primitive type or a struct, which can have
 * the carets of a pointer type before it, such as `^Int`, or a type of the kind
 * `X64_TYPE_KIND_NONE` if the target does not have a type of that name.
 */
static X64_Type x64_type_of_name(X64_Builder const *builder, Str name) {
    size_t start = 0;
    uint32_t depth = 0;
    // Every step reads a byte, so there are at most as many steps as bytes.
    for (size_t i = 0; i < name.length; i += 1) {
        char character = name.bytes[start];
        if (character == '^') {
            depth += 1;
        } else if (character != ' ' && character != '\t') {
            break;
        }
        start += 1;
    }
    Str base_name = str_make(name.bytes + start, name.length - start);

    X64_Type base = x64_type_make(X64_TYPE_KIND_NONE);
    if (str_equals(base_name, STR("Bool"))) {
        base = x64_type_make(X64_TYPE_KIND_BOOL);
    } else if (str_equals(base_name, STR("Byte"))) {
        base = x64_type_make(X64_TYPE_KIND_BYTE);
    } else if (str_equals(base_name, STR("C_Str"))) {
        base = x64_type_make(X64_TYPE_KIND_C_STR);
    } else if (str_equals(base_name, STR("Int"))) {
        base = x64_type_make(X64_TYPE_KIND_INT);
    } else if (str_equals(base_name, STR("Str"))) {
        base = x64_type_make(X64_TYPE_KIND_STR);
    } else if (x64_enum_find(builder, base_name) != AST_NODE_INDEX_NONE) {
        base = x64_type_make(X64_TYPE_KIND_ENUM);
        base.definition = x64_enum_find(builder, base_name);
    } else {
        Ast_Node_Index definition = x64_struct_find(builder, base_name);
        if (definition != AST_NODE_INDEX_NONE) {
            base = x64_type_make(X64_TYPE_KIND_STRUCT);
            base.definition = definition;
        }
    }
    if (depth == 0 || base.kind == X64_TYPE_KIND_NONE) {
        return base;
    }
    // A pointer to a pointer is one with more carets, so the pointer is made from the base once
    // and has the carets that were counted.
    X64_Type pointer = x64_type_pointer_to(base);
    pointer.depth = depth;
    return pointer;
}

/**
 * Returns the type that the type node `type` stands for, or a type of the kind
 * `X64_TYPE_KIND_NONE` if the target does not have it. An array type is an array slice when it has
 * no length, and has the length that is written, or -1 for `[const]`, which takes the number of
 * elements of its literal.
 */
static X64_Type x64_type_of_node(X64_Builder const *builder, Ast_Node_Index type) {
    assert(type < builder->node_count);

    Ast_Node const *node = &builder->nodes[type];
    if (node->node_type != AST_NODE_TYPE_ARRAY_TYPE) {
        return x64_type_of_element_node(builder, type);
    }
    X64_Type element = x64_type_of_element_node(builder, node->first_child);
    if (node->payload.array_type.length_kind == AST_ARRAY_LENGTH_OMITTED) {
        return x64_type_sequence_of(element, X64_TYPE_KIND_SLICE, -1);
    }
    int64_t length = -1;
    if (node->payload.array_type.length_kind == AST_ARRAY_LENGTH_FIXED) {
        length = node->payload.array_type.length;
    }
    return x64_type_sequence_of(element, X64_TYPE_KIND_ARRAY, length);
}

/**
 * Returns the type that the type node `type` stands for, which is a name or a pointer type, as
 * the elements of an array type are, or a type of the kind `X64_TYPE_KIND_NONE` if the target does
 * not have it. Each caret of a pointer type is a node with the rest of the type as its child.
 */
static X64_Type x64_type_of_element_node(X64_Builder const *builder, Ast_Node_Index type) {
    assert(type < builder->node_count);

    uint32_t depth = 0;
    Ast_Node_Index current = type;
    // Each step goes down one level, so there are fewer steps than nodes.
    for (size_t i = 0; i < builder->node_count &&
                       builder->nodes[current].node_type == AST_NODE_TYPE_POINTER_TYPE;
         i += 1) {
        depth += 1;
        current = builder->nodes[current].first_child;
    }
    if (depth == 0 && builder->nodes[current].node_type == AST_NODE_TYPE_STRUCT_TYPE) {
        // The type of a field that is a struct of its own, which has no name.
        X64_Type nested = x64_type_make(X64_TYPE_KIND_STRUCT);
        nested.definition = current;
        return nested;
    }
    if (builder->nodes[current].node_type == AST_NODE_TYPE_PROCEDURE_TYPE) {
        // The first caret is the pointer to the procedure, and each one more is a pointer to that.
        X64_Type procedure = x64_type_make(X64_TYPE_KIND_PROCEDURE);
        procedure.definition = x64_signature_canonical(builder, current);
        if (depth == 1) {
            return procedure;
        }
        X64_Type pointer = x64_type_pointer_to(procedure);
        pointer.depth = depth - 1;
        return pointer;
    }
    if (builder->nodes[current].node_type != AST_NODE_TYPE_TYPE_NAME) {
        // An array of arrays is not built.
        return x64_type_make(X64_TYPE_KIND_NONE);
    }
    X64_Type base = x64_type_of_name(builder, builder->nodes[current].payload.type_name.name);
    if (depth == 0 || base.kind == X64_TYPE_KIND_NONE) {
        return base;
    }
    X64_Type pointer = x64_type_pointer_to(base);
    pointer.depth += depth - 1;
    return pointer;
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
 * Returns the number of parameters of `signature`, an `AST_NODE_TYPE_PROCEDURE`, whose parameters
 * are its first children, or an `AST_NODE_TYPE_PROCEDURE_TYPE`, whose children are all types of
 * parameters.
 */
static size_t x64_parameter_count(X64_Builder const *builder, Ast_Node_Index signature) {
    Ast_Node_Type node_type = builder->nodes[signature].node_type;
    assert(node_type == AST_NODE_TYPE_PROCEDURE || node_type == AST_NODE_TYPE_PROCEDURE_TYPE);

    size_t count = 0;
    Ast_Node_Index child = builder->nodes[signature].first_child;
    // Every child is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && child != AST_NODE_INDEX_NONE; i += 1) {
        if (node_type == AST_NODE_TYPE_PROCEDURE &&
            builder->nodes[child].node_type != AST_NODE_TYPE_PARAMETER) {
            break;
        }
        count += 1;
        child = builder->nodes[child].next_sibling;
    }

    return count;
}

/**
 * Returns the type of the parameter at `position` of `signature`, see `x64_parameter_count`.
 */
static X64_Type x64_parameter_type(X64_Builder const *builder, Ast_Node_Index signature,
                                   size_t position) {
    assert(position < x64_parameter_count(builder, signature));

    return x64_type_of_node(builder, x64_parameter_type_node(builder, signature, position));
}

/**
 * Returns the type node of the parameter at `position` of `signature`, see `x64_parameter_count`.
 */
static Ast_Node_Index x64_parameter_type_node(X64_Builder const *builder, Ast_Node_Index signature,
                                              size_t position) {
    assert(position < x64_parameter_count(builder, signature));

    Ast_Node_Index parameter = builder->nodes[signature].first_child;
    for (size_t i = 0; i < position; i += 1) {
        parameter = builder->nodes[parameter].next_sibling;
    }
    if (builder->nodes[signature].node_type == AST_NODE_TYPE_PROCEDURE_TYPE) {
        return parameter;
    }
    return builder->nodes[parameter].first_child;
}

/**
 * Returns whether the type nodes `first` and `second` are written as the same type: the same
 * carets and brackets, with the same lengths, and the same name at their end. A name stands for
 * one thing in the whole program, so the same name is the same type, and this compares types
 * without working them out.
 */
static bool x64_type_nodes_equal(X64_Builder const *builder, Ast_Node_Index first,
                                 Ast_Node_Index second) {
    Ast_Node_Index a = first;
    Ast_Node_Index b = second;
    // Each step goes down one level of both, so there are fewer steps than nodes.
    for (size_t i = 0; i < builder->node_count; i += 1) {
        Ast_Node const *node_a = &builder->nodes[a];
        Ast_Node const *node_b = &builder->nodes[b];
        if (node_a->node_type != node_b->node_type) {
            return false;
        }
        if (node_a->node_type == AST_NODE_TYPE_TYPE_NAME) {
            return str_equals(node_a->payload.type_name.name, node_b->payload.type_name.name);
        }
        if (node_a->node_type == AST_NODE_TYPE_ARRAY_TYPE &&
            (node_a->payload.array_type.length_kind != node_b->payload.array_type.length_kind ||
             node_a->payload.array_type.length != node_b->payload.array_type.length)) {
            return false;
        }
        if (node_a->node_type != AST_NODE_TYPE_ARRAY_TYPE &&
            node_a->node_type != AST_NODE_TYPE_POINTER_TYPE) {
            return false;
        }
        a = node_a->first_child;
        b = node_b->first_child;
    }
    return false;
}

/**
 * Returns the first procedure or procedure type of the program that has the signature of
 * `signature`: the same types of parameters, in the same order, and the same return type. It is
 * what makes two pointers to procedures the same type, whatever names the procedures and their
 * parameters have, see `X64_Type`.
 */
static Ast_Node_Index x64_signature_canonical(X64_Builder const *builder,
                                              Ast_Node_Index signature) {
    size_t parameter_count = x64_parameter_count(builder, signature);
    X64_Type return_type = x64_return_type(builder, signature);
    for (Ast_Node_Index node = 0; node < signature; node += 1) {
        Ast_Node_Type node_type = builder->nodes[node].node_type;
        if ((node_type != AST_NODE_TYPE_PROCEDURE && node_type != AST_NODE_TYPE_PROCEDURE_TYPE) ||
            x64_parameter_count(builder, node) != parameter_count ||
            not x64_type_equals(x64_return_type(builder, node), return_type)) {
            continue;
        }
        bool is_equal = true;
        for (size_t i = 0; i < parameter_count && is_equal; i += 1) {
            is_equal = x64_type_nodes_equal(builder, x64_parameter_type_node(builder, node, i),
                                            x64_parameter_type_node(builder, signature, i));
        }
        if (is_equal) {
            return node;
        }
    }
    return signature;
}

/**
 * Checks the types of the pointers to procedures of the program: every type of a parameter and the
 * return type are types that the target has. Returns false after it recorded an error.
 */
static bool x64_signatures_check(X64_Builder *builder) {
    for (Ast_Node_Index node = 0; node < builder->node_count; node += 1) {
        if (builder->nodes[node].node_type != AST_NODE_TYPE_PROCEDURE_TYPE) {
            continue;
        }
        size_t parameter_count = x64_parameter_count(builder, node);
        for (size_t i = 0; i < parameter_count; i += 1) {
            Ast_Node_Index type_node = x64_parameter_type_node(builder, node, i);
            if (x64_type_of_node(builder, type_node).kind == X64_TYPE_KIND_NONE) {
                return x64_fail(builder, X64_BUILD_STATUS_TYPE_UNSUPPORTED,
                                x64_type_subject(builder, type_node));
            }
        }
        Str return_type = ast_procedure_return_type(&builder->nodes[node]);
        if (return_type.bytes != NULL &&
            x64_type_of_name(builder, return_type).kind == X64_TYPE_KIND_NONE) {
            return x64_fail(builder, X64_BUILD_STATUS_TYPE_UNSUPPORTED, return_type);
        }
    }
    return true;
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
 * the number of parameters, for the number of words of all of them. A procedure whose result is
 * more than a word has the address of the place for its result before them, see
 * `x64_result_is_hidden`.
 */
static size_t x64_argument_word_offset(X64_Builder const *builder, Ast_Node_Index procedure,
                                       size_t position) {
    assert(position <= x64_parameter_count(builder, procedure));

    size_t offset = 0;
    if (x64_result_is_hidden(builder, procedure)) {
        offset = 1;
    }
    for (size_t i = 0; i < position; i += 1) {
        X64_Type type = x64_parameter_type(builder, procedure, i);
        assert(type.kind != X64_TYPE_KIND_NONE);
        offset += x64_type_word_count(builder, type);
    }
    return offset;
}

/**
 * Returns whether the result of `procedure` is more than a word, which does not fit in `rax`. Then
 * the caller makes the place for the result and passes its address as the first word of the
 * arguments, before the others, and the procedure writes the result there and returns the address
 * in `rax`, which is what the calling convention of Windows on x64 does with a struct.
 */
static bool x64_result_is_hidden(X64_Builder const *builder, Ast_Node_Index procedure) {
    X64_Type type = x64_return_type(builder, procedure);
    return type.kind != X64_TYPE_KIND_NONE && not x64_type_is_scalar(builder, type);
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
 * `X64_OPCODE_LOAD`. The prefix says that the operands are 64-bit, and has the extensions of the
 * register and of the base when they are one of `r8` to `r15`, which also makes the lowest byte of
 * `rax` `al` for `X64_OPCODE_STORE_BYTE`.
 */
static void x64_write_memory(X64_Builder *builder, byte opcode, byte register_number, byte base,
                             int32_t displacement) {
    assert(register_number < 16);
    assert(base < 16);

    x64_write_byte(&builder->writer,
                   (byte)(X64_REX_WIDE | ((register_number >> 3) << 2) | (base >> 3)));
    x64_write_byte(&builder->writer, opcode);
    x64_write_memory_operand(builder, register_number, base, displacement);
}

/**
 * Writes an instruction of two bytes of opcode, `0x0F` and `opcode`, between the register
 * `register_number` and the memory at `base + displacement`, such as `movzx`.
 */
static void x64_write_memory_two_byte(X64_Builder *builder, byte opcode, byte register_number,
                                      byte base, int32_t displacement) {
    assert(register_number < 16);
    assert(base < 16);

    x64_write_byte(&builder->writer,
                   (byte)(X64_REX_WIDE | ((register_number >> 3) << 2) | (base >> 3)));
    x64_write_byte(&builder->writer, X64_OPCODE_TWO_BYTE);
    x64_write_byte(&builder->writer, opcode);
    x64_write_memory_operand(builder, register_number, base, displacement);
}

/**
 * Writes the bytes after the opcode that say that the operands are the register `register_number`
 * and the memory at `base + displacement`. The displacement takes no byte when it is zero, which
 * `rbp` cannot have, because that form of it means an address relative to the instruction
 * pointer, one byte when it fits in one and four otherwise. Both writes of the code make the same
 * choice, because the displacement depends only on the procedure. A base of `rsp` needs the byte
 * that adds an index, which says that there is none.
 */
static void x64_write_memory_operand(X64_Builder *builder, byte register_number, byte base,
                                     int32_t displacement) {
    byte modrm = (byte)(((register_number & 7) << 3) | (base & 7));
    bool is_short = displacement >= INT8_MIN && displacement <= INT8_MAX;
    bool is_none = displacement == 0 && (base & 7) != X64_REGISTER_RBP;
    if (is_none) {
        x64_write_byte(&builder->writer, modrm);
    } else if (is_short) {
        x64_write_byte(&builder->writer, (byte)(0x40 | modrm));
    } else {
        x64_write_byte(&builder->writer, (byte)(0x80 | modrm));
    }
    if ((base & 7) == X64_REGISTER_RSP) {
        x64_write_byte(&builder->writer, 0x24);
    }
    if (is_none) {
        return;
    }
    if (is_short) {
        x64_write_byte(&builder->writer, (byte)(int8_t)displacement);
    } else {
        x64_write_u32(&builder->writer, (uint32_t)displacement);
    }
}

/**
 * Writes an instruction of 64 bits with `opcode` between the register `register_number` and the
 * memory at the address `target_rva`, relative to the image, which the instruction has as a
 * distance from its end: `mov`, `lea` and the stores of `.data`.
 */
static void x64_write_rip_memory(X64_Builder *builder, byte opcode, byte register_number,
                                 uint64_t target_rva) {
    assert(register_number < 16);

    x64_write_byte(&builder->writer, (byte)(X64_REX_WIDE | ((register_number >> 3) << 2)));
    x64_write_byte(&builder->writer, opcode);
    x64_write_byte(&builder->writer, (byte)(((register_number & 7) << 3) | X64_REGISTER_RBP));
    x64_write_rip_displacement(builder, target_rva);
}

/**
 * Writes `and register, number`, with a number that fits in a byte, which the processor widens with
 * its sign, so that -8 clears the lowest three bits.
 */
static void x64_write_and_immediate(X64_Builder *builder, byte register_number, int8_t number) {
    assert(register_number < 16);

    x64_write_byte(&builder->writer, (byte)(X64_REX_WIDE | (register_number >> 3)));
    x64_write_byte(&builder->writer, 0x83);
    // The middle bits are 4, which say that the instruction is `and`.
    x64_write_byte(&builder->writer, (byte)(0xC0 | (4 << 3) | (register_number & 7)));
    x64_write_byte(&builder->writer, (byte)number);
}

/**
 * Writes `add register, number`, with a number that fits in a byte, which the processor widens with
 * its sign, so that a negative number subtracts.
 */
static void x64_write_add_immediate(X64_Builder *builder, byte register_number, int8_t number) {
    assert(register_number < 16);

    x64_write_byte(&builder->writer, (byte)(X64_REX_WIDE | (register_number >> 3)));
    x64_write_byte(&builder->writer, 0x83);
    // The middle bits are 0, which say that the instruction is `add`.
    x64_write_byte(&builder->writer, (byte)(0xC0 | (register_number & 7)));
    x64_write_byte(&builder->writer, (byte)number);
}

/**
 * Writes a trap that is taken unless the flags of the last comparison hold `condition`: a short
 * jump of that condition over a breakpoint. The breakpoint stops a program that is not debugged
 * with an exception of Windows, and a debugger stops at it, which is how an index outside of an
 * array, a `nil` that is followed and a number that is not a `Byte` stop the program.
 */
static void x64_write_trap_unless(X64_Builder *builder, byte condition) {
    assert(condition < 16);

    x64_write_byte(&builder->writer, (byte)(0x70 | condition));
    x64_write_byte(&builder->writer, 1);
    x64_write_byte(&builder->writer, 0xCC);
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
 * Writes `call register`, which calls the address in the register.
 */
static void x64_write_call_register(X64_Builder *builder, byte register_number) {
    assert(register_number < 16);

    if (register_number >= 8) {
        x64_write_byte(&builder->writer, 0x41);
    }
    x64_write_byte(&builder->writer, 0xFF);
    // The middle bits are 2, which say that the instruction is `call`.
    x64_write_byte(&builder->writer, (byte)(0xC0 | (2 << 3) | (register_number & 7)));
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
    x64_store_u32(optional + 12, (uint32_t)builder->data_size_bytes);
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
    // The temporary storage and the global variables: it has no bytes in the file, and the
    // system gives it zeroed.
    memcpy(section, ".data\0\0\0", 8);
    x64_store_u32(section + 8, (uint32_t)builder->data_size_bytes);
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
 * nothing for a file without debug information. A deferred statement is written again at each
 * place where its block is left, with its rows, so a program with many of them can need more rows
 * than `X64_DEBUG_ROWS_PER_NODE` for each node, and then the rows after the last that fits are left
 * out, which only makes the debug information less complete.
 */
static void x64_debug_row_add(X64_Builder *builder, X64_Debug_Row_Kind kind,
                              uint32_t source_offset) {
    if (builder->debug == NULL || builder->row_count == builder->row_capacity) {
        return;
    }
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
 * Writes `.debug_info`. An entry refers to another by its place, and a struct can refer to one that
 * comes after it, so the unit is written twice: the first time finds where the entries of the
 * structs and the enums are, which `builder->layouts` keeps at their definitions, and the second
 * time writes the unit with their places. The size of every entry is the same whatever the places
 * that it refers to are, which are 4 bytes each, so both times write the same entries at the same
 * places.
 */
static void x64_debug_info_write(X64_Builder *builder, size_t text_size_bytes) {
    size_t start = builder->writer.length;
    x64_debug_unit_write(builder, text_size_bytes);
    if (builder->writer.is_overflowed) {
        return;
    }
    builder->writer.length = start;
    x64_debug_unit_write(builder, text_size_bytes);
}

/**
 * Writes the one unit of `.debug_info`, which is the source file, with the types, the constants
 * that are strings, and the procedures with their parameters and variables. A location is where
 * the value is relative to the CFA of the procedure, see `X64_DEBUG_CFA_FRAME_OFFSET_BYTES`, which
 * gdb works out from `.debug_frame`, and the procedures say that with `DW_OP_call_frame_cfa`. The
 * language is C, because gdb has no Mialu, and the C of gdb shows the types of the target as they
 * are: a `Str` and an array slice are structs of an address and a length, a struct is a struct,
 * an enum is an enum, so its members are shown by their names, and an array is the address of its
 * elements, a pointer to an array of its length, see `x64_debug_type_write`.
 */
static void x64_debug_unit_write(X64_Builder *builder, size_t text_size_bytes) {
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
    types.unit_start = unit_start;

    // A `Str` is the address of its bytes and their number, which gdb shows as the text up to a
    // zero and the number.
    types.str_type = (uint32_t)(writer->length - unit_start);
    x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_STRUCTURE_TYPE);
    x64_write_string(writer, STR("Str"));
    x64_write_unsigned_leb128(writer, 2 * X64_WORD_SIZE_BYTES);
    x64_debug_member_write(writer, STR("data"), pointer_type, 0);
    x64_debug_member_write(writer, STR("length"), types.int_type, X64_WORD_SIZE_BYTES);
    x64_write_byte(writer, 0);                            // The end of the fields.

    x64_debug_enums_write(builder, types);
    x64_debug_structs_write(builder, types);

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

    // A global variable is at its place in `.data`, see `x64_data_layout`.
    for (Ast_Node_Index node = 0; node < builder->node_count; node += 1) {
        if (builder->nodes[node].node_type != AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION) {
            continue;
        }
        uint32_t reference = x64_debug_type_write(builder, types, builder->layouts[node].type);
        assert(reference != 0);
        cursor = x64_debug_cursor_move(debug->source, cursor, builder->nodes[node].source_offset);
        x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_VARIABLE);
        x64_write_string(writer, builder->nodes[node].payload.global_variable_definition.name);
        x64_write_byte(writer, 1);
        x64_write_u32(writer, cursor.line);
        x64_write_u32(writer, reference);
        x64_write_unsigned_leb128(writer, 1 + X64_DEBUG_ADDRESS_SIZE_BYTES);
        x64_write_byte(writer, X64_DWARF_OP_ADDR);
        x64_write_u64(writer, X64_IMAGE_BASE + builder->data_rva +
                                  builder->layouts[node].frame_offset);
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

        // The type of the result is an entry before the procedure, at the level of the unit.
        uint32_t return_reference = x64_debug_type_write(builder, types, return_type);
        if (return_reference == 0) {
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
        if (return_reference != 0) {
            x64_write_u32(writer, return_reference);
        }

        // The words of the arguments are kept in the space that the caller made, at the CFA and
        // above, see `x64_argument_word_offset`.
        size_t parameter_count = x64_parameter_count(builder, procedure);
        Ast_Node_Index parameter = builder->nodes[procedure].first_child;
        for (size_t j = 0; j < parameter_count; j += 1) {
            // The type is an entry of its own, before the parameter, when it is made of others.
            uint32_t parameter_reference =
                x64_debug_type_write(builder, types, x64_parameter_type(builder, procedure, j));
            assert(parameter_reference != 0);
            cursor = x64_debug_cursor_move(debug->source, cursor,
                                           builder->nodes[parameter].source_offset);
            x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_PARAMETER);
            x64_write_string(writer, builder->nodes[parameter].payload.parameter.name);
            x64_write_byte(writer, 1);
            x64_write_u32(writer, cursor.line);
            x64_write_u32(writer, parameter_reference);
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
            uint32_t reference = x64_debug_type_write(builder, types, type);
            assert(reference != 0);
            cursor = x64_debug_cursor_move(debug->source, cursor,
                                           builder->nodes[node].source_offset);
            x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_VARIABLE);
            x64_write_string(writer, builder->nodes[node].payload.variable_definition.name);
            x64_write_byte(writer, 1);
            x64_write_u32(writer, cursor.line);
            x64_write_u32(writer, reference);
            x64_debug_location_write(writer, -(int64_t)builder->layouts[node].frame_offset -
                                                 X64_DEBUG_CFA_FRAME_OFFSET_BYTES);
        }

        // The iterator of a for statement is a variable at the start of the place of the
        // statement, see `x64_procedure_layout`.
        for (Ast_Node_Index node = procedure; node < procedure_end; node += 1) {
            if (builder->nodes[node].node_type != AST_NODE_TYPE_FOR_STATEMENT) {
                continue;
            }
            X64_Type type = builder->layouts[node].type;
            uint32_t reference = x64_debug_type_write(builder, types, type);
            assert(reference != 0);
            cursor = x64_debug_cursor_move(debug->source, cursor,
                                           builder->nodes[node].source_offset);
            x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_VARIABLE);
            x64_write_string(writer, builder->nodes[node].payload.for_statement.name);
            x64_write_byte(writer, 1);
            x64_write_u32(writer, cursor.line);
            x64_write_u32(writer, reference);
            x64_debug_location_write(writer, -(int64_t)builder->layouts[node].frame_offset -
                                                 X64_DEBUG_CFA_FRAME_OFFSET_BYTES);
        }
        x64_debug_blocks_write(builder, start, end);

        x64_write_byte(writer, 0);                        // The end of the children.
    }

    x64_write_byte(writer, 0);                            // The end of the children of the unit.
    x64_patch_u32(writer, unit_start, (uint32_t)(writer->length - unit_start - 4));
}

/**
 * Writes a lexical block for each copy of a deferred statement in the code of the procedure from
 * `start` to `end`, which the rows of `X64_DEBUG_ROW_KIND_BLOCK_START` and
 * `X64_DEBUG_ROW_KIND_BLOCK_END` mark. gdb sets a breakpoint on a line at the first place of the
 * line in each block of a procedure, so a deferred statement, whose line has a place in each of
 * the copies, needs a block for each, to stop at all of them. gdb leaves out a block that has
 * nothing in it, so each has a label, `defer`, at its start, which is not a variable and is not
 * shown among them.
 */
static void x64_debug_blocks_write(X64_Builder *builder, size_t start, size_t end) {
    X64_Writer *writer = &builder->writer;
    size_t block_start = 0;
    bool is_open = false;
    for (size_t i = 0; i < builder->row_count; i += 1) {
        X64_Debug_Row row = builder->rows[i];
        if (row.code_offset < start || row.code_offset >= end) {
            continue;
        }
        if (row.kind == X64_DEBUG_ROW_KIND_BLOCK_START) {
            block_start = row.code_offset;
            is_open = true;
        } else if (row.kind == X64_DEBUG_ROW_KIND_BLOCK_END && is_open) {
            is_open = false;
            if (row.code_offset == block_start) {
                continue;
            }
            uint64_t address = X64_IMAGE_BASE + x64_debug_address_rva(builder, block_start);
            x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_LEXICAL_BLOCK);
            x64_write_u64(writer, address);
            x64_write_u64(writer, row.code_offset - block_start);
            x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_LABEL);
            x64_write_string(writer, STR("defer"));
            x64_write_u64(writer, address);
            x64_write_byte(writer, 0);                    // The end of the children.
        }
    }
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
 * Returns where the entry of the type of `kind` with `definition` is in `.debug_info`, for a type
 * that has a name and an entry of its own: the primitive types, a struct and an enum, whose places
 * the first writing of the unit found, see `x64_debug_info_write`. Returns 0, which no entry is
 * at, for the other types, which are made of others, see `x64_debug_type_write`.
 */
static uint32_t x64_debug_type_reference(X64_Builder const *builder, X64_Debug_Types types,
                                         X64_Type_Kind kind, Ast_Node_Index definition) {
    switch (kind) {
    case X64_TYPE_KIND_BOOL:
        return types.bool_type;
    case X64_TYPE_KIND_BYTE:
        return types.byte_type;
    case X64_TYPE_KIND_C_STR:
        return types.c_str_type;
    case X64_TYPE_KIND_ENUM:
    case X64_TYPE_KIND_STRUCT:
        return builder->layouts[definition].frame_offset;
    case X64_TYPE_KIND_INT:
        return types.int_type;
    case X64_TYPE_KIND_STR:
        return types.str_type;
    case X64_TYPE_KIND_ARRAY:
    case X64_TYPE_KIND_ENUM_LITERAL:
    case X64_TYPE_KIND_NIL:
    case X64_TYPE_KIND_NONE:
    case X64_TYPE_KIND_POINTER:
    case X64_TYPE_KIND_PROCEDURE:
    case X64_TYPE_KIND_SLICE:
        break;
    }
    return 0;
}

/**
 * Writes the entry of `type` when it is made of other types, and returns where the entry of `type`
 * is in `.debug_info`, or 0 for a type that has none, which is no type. An array is the address of
 * its first element, so it is a pointer to an array of its length, which gdb shows whole with `*`,
 * and an array slice is a struct without a name of a pointer to its first element and its length.
 * The types of the elements and what a pointer points to are written by
 * `x64_debug_scalar_type_write`, which writes no arrays.
 */
static uint32_t x64_debug_type_write(X64_Builder *builder, X64_Debug_Types types, X64_Type type) {
    X64_Writer *writer = &builder->writer;
    if (type.kind != X64_TYPE_KIND_ARRAY && type.kind != X64_TYPE_KIND_SLICE) {
        return x64_debug_scalar_type_write(builder, types, type);
    }

    uint32_t element = x64_debug_scalar_type_write(builder, types, x64_type_element(type));
    if (type.kind == X64_TYPE_KIND_ARRAY) {
        uint32_t array = (uint32_t)(writer->length - types.unit_start);
        x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_ARRAY_TYPE);
        x64_write_u32(writer, element);
        // The range is the last index, which is -1 for an array of no elements.
        x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_SUBRANGE_TYPE);
        x64_write_u32(writer, (uint32_t)(type.length - 1));
        x64_write_byte(writer, 0);                        // The end of the range.
        return x64_debug_pointers_write(builder, types, array, 1);
    }

    uint32_t data = x64_debug_pointers_write(builder, types, element, 1);
    uint32_t slice = (uint32_t)(writer->length - types.unit_start);
    x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_STRUCTURE_TYPE_ANONYMOUS);
    x64_write_unsigned_leb128(writer, 2 * X64_WORD_SIZE_BYTES);
    x64_debug_member_write(writer, STR("data"), data, 0);
    x64_debug_member_write(writer, STR("length"), types.int_type, X64_WORD_SIZE_BYTES);
    x64_write_byte(writer, 0);                            // The end of the fields.
    return slice;
}

/**
 * Writes the entry of `type` when it is a pointer, which is an entry for each caret, or a pointer
 * to a procedure, which is a pointer to the type of the procedure, and returns where the entry of
 * `type` is. Every other type that is no array has an entry of its own, see
 * `x64_debug_type_reference`, or none.
 */
static uint32_t x64_debug_scalar_type_write(X64_Builder *builder, X64_Debug_Types types,
                                            X64_Type type) {
    if (type.kind == X64_TYPE_KIND_PROCEDURE) {
        uint32_t procedure = x64_debug_subroutine_write(builder, types, type.definition);
        return x64_debug_pointers_write(builder, types, procedure, 1);
    }
    if (type.kind != X64_TYPE_KIND_POINTER) {
        return x64_debug_type_reference(builder, types, type.kind, type.definition);
    }
    // A pointer to a pointer to a procedure has one caret more than the pointer to the procedure.
    if (type.pointee_kind == X64_TYPE_KIND_PROCEDURE) {
        uint32_t procedure = x64_debug_subroutine_write(builder, types, type.definition);
        return x64_debug_pointers_write(builder, types, procedure, type.depth + 1);
    }
    uint32_t pointee =
        x64_debug_type_reference(builder, types, type.pointee_kind, type.definition);
    return x64_debug_pointers_write(builder, types, pointee, type.depth);
}

/**
 * Writes `depth` entries of pointers, the first to the entry at `target` and each other to the one
 * before it, and returns where the last one is.
 */
static uint32_t x64_debug_pointers_write(X64_Builder *builder, X64_Debug_Types types,
                                         uint32_t target, uint32_t depth) {
    assert(depth >= 1);

    X64_Writer *writer = &builder->writer;
    uint32_t pointer = target;
    for (uint32_t i = 0; i < depth; i += 1) {
        uint32_t place = (uint32_t)(writer->length - types.unit_start);
        x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_POINTER_TYPE);
        x64_write_byte(writer, X64_DEBUG_ADDRESS_SIZE_BYTES);
        x64_write_u32(writer, pointer);
        pointer = place;
    }
    return pointer;
}

/**
 * Writes the entry of the type of the procedures of `signature`, which is what a pointer to one of
 * them points to, with the type of its result, and returns where it is. The result is a name with
 * carets before it, so its type is a pointer at most, which is written before the entry.
 */
static uint32_t x64_debug_subroutine_write(X64_Builder *builder, X64_Debug_Types types,
                                           Ast_Node_Index signature) {
    X64_Writer *writer = &builder->writer;
    X64_Type result = x64_return_type(builder, signature);
    uint32_t result_reference = 0;
    if (result.kind == X64_TYPE_KIND_POINTER) {
        result_reference = x64_debug_pointers_write(
            builder, types,
            x64_debug_type_reference(builder, types, result.pointee_kind, result.definition),
            result.depth);
    } else if (result.kind != X64_TYPE_KIND_NONE) {
        result_reference = x64_debug_type_reference(builder, types, result.kind,
                                                    result.definition);
    }
    uint32_t place = (uint32_t)(writer->length - types.unit_start);
    if (result_reference == 0) {
        x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_SUBROUTINE_TYPE_VOID);
    } else {
        x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_SUBROUTINE_TYPE);
        x64_write_u32(writer, result_reference);
    }
    return place;
}

/**
 * Writes the entry of a field `name` of the type at `type`, at `offset` bytes from the start of its
 * struct.
 */
static void x64_debug_member_write(X64_Writer *writer, Str name, uint32_t type, size_t offset) {
    x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_MEMBER);
    x64_write_string(writer, name);
    x64_write_u32(writer, type);
    x64_write_unsigned_leb128(writer, offset);
}

/**
 * Writes the entry of every enum of the module, with its members and their numbers, so that gdb
 * shows a member by its name. `builder->layouts` keeps where each is at its definition.
 */
static void x64_debug_enums_write(X64_Builder *builder, X64_Debug_Types types) {
    X64_Writer *writer = &builder->writer;
    Ast_Node_Index definition = builder->nodes[builder->module].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && definition != AST_NODE_INDEX_NONE; i += 1) {
        Ast_Node const *node = &builder->nodes[definition];
        definition = node->next_sibling;
        if (node->node_type != AST_NODE_TYPE_CONSTANT_DEFINITION ||
            builder->nodes[node->first_child].node_type != AST_NODE_TYPE_ENUM_TYPE) {
            continue;
        }
        Ast_Node_Index enum_node = node->first_child;
        builder->layouts[enum_node].frame_offset = (uint32_t)(writer->length - types.unit_start);
        x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_ENUMERATION_TYPE);
        x64_write_string(writer, node->payload.constant_definition.name);
        x64_write_byte(writer, X64_WORD_SIZE_BYTES);
        x64_write_u32(writer, types.int_type);
        int64_t number = 0;
        for (Ast_Node_Index member = builder->nodes[enum_node].first_child;
             member != AST_NODE_INDEX_NONE; member = builder->nodes[member].next_sibling) {
            x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_ENUMERATOR);
            x64_write_string(writer, builder->nodes[member].payload.enum_member.name);
            x64_write_signed_leb128(writer, number);
            number += 1;
        }
        x64_write_byte(writer, 0);                        // The end of the members.
    }
}

/**
 * Writes the entry of every struct of the module, also of the structs of fields, which have no
 * name, with its fields, each at 8 bytes for each word of the fields before it, see
 * `x64_struct_word_count`. The type of a field that is made of others is an entry before its
 * struct. `builder->layouts` keeps where each struct is at its node.
 */
static void x64_debug_structs_write(X64_Builder *builder, X64_Debug_Types types) {
    X64_Writer *writer = &builder->writer;
    for (Ast_Node_Index node = 0; node < builder->node_count; node += 1) {
        if (builder->nodes[node].node_type != AST_NODE_TYPE_STRUCT_TYPE) {
            continue;
        }
        // The types of the fields, whose places the layouts of the fields keep until the struct
        // refers to them.
        for (Ast_Node_Index field = builder->nodes[node].first_child; field != AST_NODE_INDEX_NONE;
             field = builder->nodes[field].next_sibling) {
            X64_Type type = x64_type_of_element_node(builder, builder->nodes[field].first_child);
            builder->layouts[field].frame_offset = x64_debug_scalar_type_write(builder, types,
                                                                               type);
        }

        builder->layouts[node].frame_offset = (uint32_t)(writer->length - types.unit_start);
        Ast_Node_Index definition = x64_parent_find(builder, node);
        size_t size_bytes = x64_struct_word_count(builder, node) * X64_WORD_SIZE_BYTES;
        if (definition != AST_NODE_INDEX_NONE &&
            builder->nodes[definition].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION) {
            x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_STRUCTURE_TYPE);
            x64_write_string(writer, builder->nodes[definition].payload.constant_definition.name);
        } else {
            x64_write_unsigned_leb128(writer, X64_DWARF_ABBREVIATION_STRUCTURE_TYPE_ANONYMOUS);
        }
        x64_write_unsigned_leb128(writer, size_bytes);
        size_t offset_bytes = 0;
        for (Ast_Node_Index field = builder->nodes[node].first_child; field != AST_NODE_INDEX_NONE;
             field = builder->nodes[field].next_sibling) {
            X64_Type type = x64_type_of_element_node(builder, builder->nodes[field].first_child);
            x64_debug_member_write(writer, builder->nodes[field].payload.field.name,
                                   builder->layouts[field].frame_offset, offset_bytes);
            offset_bytes += x64_type_word_count(builder, type) * X64_WORD_SIZE_BYTES;
        }
        x64_write_byte(writer, 0);                        // The end of the fields.
    }
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
