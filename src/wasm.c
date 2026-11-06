#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <mialu/common_types.h>
#include <mialu/semantic.h>
#include <mialu/wasm.h>

// The constants of the binary format are in wasm.h, because wasm-text.c reads what this writes.

enum {
    // The deepest expression that can be written. The writer keeps the path from the root of the
    // expression in a fixed array instead of recursing. Left associative operators nest to the
    // left, so `a + b + c + ...` is as deep as it has operators. A longer chain is rejected.
    WASM_EXPRESSION_DEPTH_MAX = 1024,
    // The most statement lists that can be open at once while the statements of a procedure are
    // written: the body, and for each nested if statement the statement itself and its current
    // block. The parser does not accept deeper nesting, see `AST_BLOCK_DEPTH_MAX`.
    WASM_STATEMENT_DEPTH_MAX = 2 * AST_BLOCK_DEPTH_MAX + 2,
    // The size of the stack in memory that the arrays of the running procedures are on. Arrays are
    // not in locals, because an array slice refers to elements by address. A recursion that
    // needs more traps, see `wasm_frame_enter`.
    WASM_STACK_SIZE_BYTES = 1024 * 1024,
    // An element is an `Int`, which is 8 bytes. The shift multiplies an index by that size, and is
    // also the alignment of a load or a store of an element, as a power of two.
    WASM_ELEMENT_SIZE_BYTES = 8,
    WASM_ELEMENT_SIZE_SHIFT = 3,
    // The most structs that are open at once while the fields of a struct are walked, which is how
    // far structs can be inside each other, as a field of a struct that is a struct. A struct that
    // has itself as a field is infinitely large, and is found by going deeper than this.
    WASM_STRUCT_DEPTH_MAX = 64,
    // The most fields that a walk over a struct visits. Structs whose fields are structs with
    // several fields of a struct each have more slots than any module can use, and the walk must
    // end.
    WASM_STRUCT_STEP_MAX = 1 << 20,
};

// What the walk over the slots of a struct answers when it has to stop, because the struct is
// inside itself or is too large.
static size_t const WASM_SLOT_COUNT_INVALID = SIZE_MAX;

byte const WASM_MODULE_HEADER[8] = {0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00};

/**
 * Where the module is written. A write that does not fit sets `is_overflowed` and the rest of the
 * writes are ignored, so the code that writes does not check after every byte. The flag is
 * checked once, when the module is finished.
 */
typedef struct {
    byte *bytes;
    size_t capacity;
    size_t length;
    bool is_overflowed;
} Wasm_Writer;

// Alphabetical, because nothing depends on the order. `WASM_VALUE_TYPE_NONE` is the result of a
// name that is not a type that the target has. An array is one 32-bit local that holds the address
// of its first element, and its length is known from the program, see `wasm_array_length`. An
// array slice is two 32-bit values, the address of its first element and its length, like `Str`. A
// struct is the values of its fields one after the other, see `wasm_struct_slots`. A pointer is one
// 32-bit value, the address of what it points to in the memory of the module. A pointer to a
// procedure, which is the kind `WASM_VALUE_TYPE_PROCEDURE`, is one 32-bit value as well, but it is
// not an address in memory: it is the index of the procedure in the table of the module, see
// `wasm_procedure_table_index`. `nil` has the kind `WASM_VALUE_TYPE_NIL`, which is a pointer to
// nothing in particular: it is accepted where any pointer is wanted, see `wasm_type_equals`, and no
// variable has it as its type. An enum is one 32-bit value, the number of the member, counted from
// zero in the order of the definition, and its `definition` is the `AST_NODE_TYPE_ENUM_TYPE`. The
// member of an enum that is written without the enum, such as `.RED`, has the kind
// `WASM_VALUE_TYPE_ENUM_LITERAL`, which like `nil` is accepted where any enum is wanted and is
// given a number when the enum is known, see `wasm_enum_literal_expected`.
typedef enum {
    WASM_VALUE_TYPE_ARRAY,
    WASM_VALUE_TYPE_BOOL,
    WASM_VALUE_TYPE_ENUM,
    WASM_VALUE_TYPE_ENUM_LITERAL,
    WASM_VALUE_TYPE_INT,
    WASM_VALUE_TYPE_NIL,
    WASM_VALUE_TYPE_NONE,
    WASM_VALUE_TYPE_POINTER,
    WASM_VALUE_TYPE_PROCEDURE,
    WASM_VALUE_TYPE_SLICE,
    WASM_VALUE_TYPE_STR,
    WASM_VALUE_TYPE_STRUCT,
} Wasm_Value_Type;

/**
 * A type, with what the kind alone does not say. `definition` is the `AST_NODE_TYPE_STRUCT_TYPE`
 * node of a struct, which is what makes two structs the same type: the type of a struct is its
 * definition, and not the shape of it. For an array and for an array slice, `element_kind` and
 * `element_definition` say the same about their elements, which are an `Int`, a `Bool`, a `Str`,
 * a struct or a pointer. What does not apply is `WASM_VALUE_TYPE_NONE` and `AST_NODE_INDEX_NONE`.
 *
 * A pointer type is written `^^Int`, which is a pointer to a pointer to an `Int`. It has `depth`,
 * the number of carets, which is at least 1, and the type at the end of them, which is never a
 * pointer, an array or an array slice: `pointee_kind` is its kind, and `definition` is the struct
 * when that is one. So a pointer is described without a type inside a type. The elements of an
 * array that are pointers are described the same way by `element_depth`, `element_pointee_kind`
 * and `element_definition`. `depth` and `element_depth` are zero when there is no pointer.
 *
 * A pointer to a procedure, written `^proc(Int) Int`, has the kind `WASM_VALUE_TYPE_PROCEDURE`,
 * and its `definition` is the signature, an `AST_NODE_TYPE_PROCEDURE` or an
 * `AST_NODE_TYPE_PROCEDURE_TYPE`. Signatures with the same parameter types and return type are one
 * type, so `definition` is the first node that has the signature, see `wasm_signature_canonical`,
 * and two types are compared by comparing nodes. A pointer to a pointer to a procedure is a pointer
 * as above whose `pointee_kind` is `WASM_VALUE_TYPE_PROCEDURE`, and it has the signature as its
 * `definition`.
 */
typedef struct {
    Wasm_Value_Type kind;
    Wasm_Value_Type element_kind;
    Wasm_Value_Type pointee_kind;
    Wasm_Value_Type element_pointee_kind;
    Ast_Node_Index definition;
    Ast_Node_Index element_definition;
    uint32_t depth;
    uint32_t element_depth;
} Wasm_Type;


// The locals that a procedure with arrays has after its parameters and variables, for the code that
// is written for subscripts, slices and the like to hold values in. The order is the order of the
// locals: the first three are 64-bit, and the others 32-bit. A scratch local is only used between
// two instructions that follow each other, never while an expression is evaluated in between, so
// that one is enough for any nesting of expressions. The exception is the address of the element
// that is assigned to, which is kept while the value is evaluated, and which nothing that
// evaluates a value uses.
typedef enum {
    WASM_SCRATCH_LOW,
    WASM_SCRATCH_HIGH,
    WASM_SCRATCH_TEMPORARY_64,
    WASM_SCRATCH_FIRST,
    WASM_SCRATCH_SECOND,
    // The address of the element that a statement assigns to.
    WASM_SCRATCH_ADDRESS,
    WASM_SCRATCH_TEMPORARY_32,
    // The address of the array literals of the procedure, see `wasm_frame_enter`.
    WASM_SCRATCH_FRAME,
} Wasm_Scratch;

/**
 * Where a field is, found from the expression `a.b.c`: `root` is the node of the name or the
 * element that the fields are accessed through, `slot_offset` is the number of slots from the
 * start of the value of the root to the field, and `type` is the type of the field. When `status`
 * is not `WASM_BUILD_STATUS_OK` the access is wrong, `subject` is the name that the error is about
 * and `type` has the kind `WASM_VALUE_TYPE_NONE`.
 */
typedef struct {
    Str subject;
    Wasm_Type type;
    size_t slot_offset;
    Ast_Node_Index root;
    Wasm_Build_Status status;
    bool is_through_pointer;
} Wasm_Field_Path;

/**
 * The state shared by the building functions. A building function returns false after it recorded
 * an error here, and its caller then returns false too without recording anything more. So only
 * the first error is kept. `bindings` says what each name in the tree stands for, so the builder
 * never searches for a name itself.
 *
 * The bytes of all string literals are written to one data segment at address zero, in the order
 * of the literal nodes. So the address of a literal is the length of the literals before it, which
 * is found when its code is written, whatever the order of the code is: the fields of a struct
 * literal are written in the order of the struct, not the order of the source.
 */
typedef struct {
    Wasm_Writer writer;
    Ast_Node const *nodes;
    Semantic_Binding const *bindings;
    Str subject;
    size_t node_count;
    Ast_Node_Index module;
    // The number of foreign procedures. They are imported functions, which come before the
    // functions of the module in the function index space, see `wasm_function_index`.
    size_t foreign_count;
    // The number of functions that the module defines, which are not foreign, see
    // `wasm_function_count`.
    size_t definition_count;
    size_t data_size_bytes;
    // Where the stack of the arrays ends at the low side, which is after the data, and where it
    // starts. The stack grows down from the top, which is the end of the memory.
    size_t stack_limit_bytes;
    size_t stack_top_bytes;
    Wasm_Build_Status status;
    // Whether the module has a memory, which it has when there is a string literal to put in it, or
    // an array type, because arrays and array slices are in memory.
    bool has_memory;
    // Whether there is a data section, which there is when there is a string literal.
    bool has_data;
    // Whether the module has a stack in its memory, and a global that points to it, which it has
    // when there is an array literal.
    bool has_stack;
    // Whether the module has a table with its functions, which it has when a pointer to a
    // procedure can be made or called, see `wasm_data_measure`.
    bool has_table;
} Wasm_Builder;

/**
 * A node whose children are being written, and the child that is next. It is the procedure for the
 * body, a block for the statements of the block, and an if statement for its conditions and blocks.
 * `if_count` is how many `if` instructions of an if statement are open, which is one for the `if`
 * and one for each `else if`, and which the end of the statement closes. A block that is the body
 * of a for statement, a loop statement or a scope statement has the statement in `statement`.
 */
typedef struct {
    Ast_Node_Index node;
    Ast_Node_Index next;
    size_t if_count;
    // The for statement, loop statement or scope statement whose block this is, or
    // `AST_NODE_INDEX_NONE`. The end of the block of a loop writes what the loop does after each
    // pass, and closes the loop, and the end of the block of a scope closes its `block`.
    Ast_Node_Index statement;
} Wasm_Statement_Frame;

static size_t          wasm_block_begin                       (Wasm_Writer *writer);
static void            wasm_block_end                         (Wasm_Writer *writer,
                                                               size_t block_start);
static bool            wasm_build_code_section                (Wasm_Builder *builder,
                                                               Ast_Node_Index module,
                                                               size_t definition_count);
static bool            wasm_build_code_section_body           (Wasm_Builder *builder,
                                                               Ast_Node_Index definition);
static void            wasm_frame_push                        (Wasm_Builder const *builder,
                                                               Wasm_Statement_Frame *frames,
                                                               size_t *frame_count,
                                                               Ast_Node_Index node,
                                                               Ast_Node_Index statement);
static void            wasm_structured_begin                  (Wasm_Builder *builder, byte opcode);
static bool            wasm_build_code_section_body_call      (Wasm_Builder *builder,
                                                               Ast_Node_Index call,
                                                               bool is_result_unused);
static void            wasm_build_code_section_body_jump       (Wasm_Builder *builder,
                                                               Wasm_Statement_Frame const *frames,
                                                               size_t frame_count,
                                                               Ast_Node_Index jump);
static bool            wasm_build_code_section_body_for        (Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index statement);
static void            wasm_build_code_section_body_for_end    (Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index statement);
static size_t          wasm_variable_slot_count                (Wasm_Builder const *builder,
                                                               Ast_Node_Index variable);
static bool            wasm_build_code_section_body_call_statement(Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index statement);
static bool            wasm_build_code_section_body_assignment(Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index assignment);
static bool            wasm_build_code_section_body_operands  (Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index operation);
static bool            wasm_build_code_section_body_expression(Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index expression,
                                                               bool is_result_unused);
static void            wasm_build_code_section_body_identifier(Wasm_Builder *builder,
                                                               Ast_Node_Index procedure,
                                                               Semantic_Binding binding);
static bool            wasm_build_code_section_body_procedure (Wasm_Builder *builder,
                                                               Ast_Node_Index definition);
static bool            wasm_build_code_section_body_return    (Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index statement);
static bool            wasm_build_code_section_body_variable  (Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index variable);
static void            wasm_build_code_section_body_string    (Wasm_Builder *builder,
                                                               Ast_Node_Index literal);
static void            wasm_build_data_section                (Wasm_Builder *builder);
static bool            wasm_build_export_section              (Wasm_Builder *builder,
                                                               Ast_Node_Index module,
                                                               size_t definition_count);
static void            wasm_build_function_section            (Wasm_Builder *builder,
                                                               size_t definition_count);
static void            wasm_build_memory_section              (Wasm_Builder *builder);
static void            wasm_build_type_section                (Wasm_Builder *builder,
                                                               Ast_Node_Index module,
                                                               size_t definition_count);
static byte            wasm_comparison_opcode                 (Ast_Comparison_Operator operator);
static bool            wasm_check_signatures                  (Wasm_Builder *builder,
                                                               Ast_Node_Index module,
                                                               size_t definition_count);
static size_t          wasm_children_count                    (Wasm_Builder const *builder,
                                                               Ast_Node_Index parent);
static bool            wasm_check_globals                     (Wasm_Builder *builder);
static Ast_Node_Index  wasm_constant_find                     (Wasm_Builder const *builder,
                                                               Ast_Node_Index from);
static Ast_Node_Index  wasm_function_find                     (Wasm_Builder const *builder,
                                                               Ast_Node_Index from,
                                                               bool is_foreign);
static size_t          wasm_function_count                    (Wasm_Builder const *builder,
                                                               Ast_Node_Index module,
                                                               bool is_foreign);
static size_t          wasm_function_index                    (Wasm_Builder const *builder,
                                                               Ast_Node_Index definition);
static bool            wasm_constant_is_foreign               (Wasm_Builder const *builder,
                                                               Ast_Node_Index definition);
static void            wasm_build_import_section              (Wasm_Builder *builder,
                                                               Ast_Node_Index module);
static void            wasm_build_function_type               (Wasm_Builder *builder,
                                                               Ast_Node_Index definition);
static Ast_Node_Index  wasm_global_value                      (Wasm_Builder const *builder,
                                                               Ast_Node_Index global);
static size_t          wasm_global_slot_index                 (Wasm_Builder const *builder,
                                                               Ast_Node_Index global);
static size_t          wasm_global_slot_count                 (Wasm_Builder const *builder);
static void            wasm_build_global_section              (Wasm_Builder *builder);
static bool            wasm_data_measure                      (Wasm_Builder *builder);
static Ast_Node_Index  wasm_enum_literal_expected             (Wasm_Builder const *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index literal);
static bool            wasm_enum_member_find                  (Wasm_Builder const *builder,
                                                               Ast_Node_Index enum_node, Str name,
                                                               size_t *number);
static bool            wasm_enum_member_write                 (Wasm_Builder *builder,
                                                               Ast_Node_Index enum_node,
                                                               Str member_name);
static Ast_Node_Index  wasm_enum_of_access                    (Wasm_Builder const *builder,
                                                               Ast_Node_Index access);
static void            wasm_build_table_section               (Wasm_Builder *builder);
static void            wasm_build_element_section             (Wasm_Builder *builder);
static void            wasm_write_signature                   (Wasm_Builder *builder,
                                                               Ast_Node_Index signature);
static Ast_Node_Index  wasm_signature_canonical               (Wasm_Builder const *builder,
                                                               Ast_Node_Index signature);
static size_t          wasm_signature_parameter_count         (Wasm_Builder const *builder,
                                                               Ast_Node_Index signature);
static Ast_Node_Index  wasm_signature_parameter_type          (Wasm_Builder const *builder,
                                                               Ast_Node_Index signature,
                                                               size_t position);
static size_t          wasm_signature_type_index              (Wasm_Builder const *builder,
                                                               Ast_Node_Index signature);
static Ast_Node_Index  wasm_call_signature                    (Wasm_Builder const *builder,
                                                               Ast_Node_Index call);
static size_t          wasm_children_count                    (Wasm_Builder const *builder,
                                                               Ast_Node_Index parent);
static Str             wasm_expression_subject                (Wasm_Builder const *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index expression);
static bool            wasm_fail                              (Wasm_Builder *builder,
                                                               Wasm_Build_Status status,
                                                               Str subject);
static size_t          wasm_leb128_signed_encode              (int64_t number, byte *encoded);
static size_t          wasm_leb128_unsigned_encode            (uint64_t number, byte *encoded);
static size_t          wasm_parameter_count                   (Wasm_Builder const *builder,
                                                               Ast_Node_Index procedure);
static size_t          wasm_parameter_local_index             (Wasm_Builder const *builder,
                                                               Ast_Node_Index procedure,
                                                               size_t position);
static size_t          wasm_parameter_slot_count              (Wasm_Builder const *builder,
                                                               Ast_Node_Index procedure);
static Ast_Node_Index  wasm_procedure_of                      (Wasm_Builder const *builder,
                                                               Ast_Node_Index definition);
static size_t          wasm_section_begin                     (Wasm_Writer *writer,
                                                               byte section_id);
static size_t          wasm_variable_local_index              (Wasm_Builder const *builder,
                                                               Ast_Node_Index procedure,
                                                               Ast_Node_Index variable);
static void            wasm_variables_write_locals            (Wasm_Builder *builder,
                                                               Ast_Node_Index procedure);
static void            wasm_write_byte                        (Wasm_Writer *writer, byte content);
static void            wasm_write_bytes                       (Wasm_Writer *writer,
                                                               byte const *bytes, size_t count);
static void            wasm_write_signed                      (Wasm_Writer *writer, int64_t number);
static void            wasm_write_text                        (Wasm_Writer *writer, Str text);
static void            wasm_write_unsigned                    (Wasm_Writer *writer,
                                                               uint64_t number);
static size_t          wasm_array_length                      (Wasm_Builder const *builder,
                                                               Ast_Node_Index expression);
static void            wasm_array_decay                       (Wasm_Builder *builder,
                                                               Ast_Node_Index expression);
static bool            wasm_build_code_section_body_address_of(Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index address_of);
static bool            wasm_build_code_section_body_array_literal(Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index literal);
static bool            wasm_build_code_section_body_dereference(Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index dereference);
static bool            wasm_build_code_section_body_dereference_assignment(
                                                               Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index assignment);
static bool            wasm_build_code_section_body_element_address(Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index subscript);
static bool            wasm_build_code_section_body_pointer   (Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index pointer,
                                                               bool is_checked);
static void            wasm_pointer_check_write               (Wasm_Builder *builder,
                                                               Ast_Node_Index procedure);
static void            wasm_variable_address_write            (Wasm_Builder *builder,
                                                               Ast_Node_Index procedure,
                                                               Ast_Node_Index variable,
                                                               size_t offset_bytes);
static bool            wasm_variable_is_in_memory             (Wasm_Builder const *builder,
                                                               Ast_Node_Index variable);
static size_t          wasm_variable_frame_offset             (Wasm_Builder const *builder,
                                                               Ast_Node_Index procedure,
                                                               Ast_Node_Index variable);
static Wasm_Type       wasm_type_pointee                      (Wasm_Type pointer);
static Wasm_Type       wasm_type_pointer_to                   (Wasm_Type target);
static bool            wasm_type_elements_equal               (Wasm_Type first, Wasm_Type second);
static bool            wasm_build_code_section_body_child     (Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index parent,
                                                               Ast_Node_Index child);
static bool            wasm_build_code_section_body_element_assignment(Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index assignment);
static bool            wasm_build_code_section_body_for_each  (Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index statement);
static bool            wasm_build_code_section_body_slice     (Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index slice);
static bool            wasm_build_code_section_body_subscript (Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index subscript);
static byte            wasm_assignment_opcode                 (Ast_Node_Type node_type);
static Ast_Node_Index  wasm_expression_child_first            (Wasm_Builder const *builder,
                                                               Ast_Node_Index expression);
static void            wasm_frame_enter                       (Wasm_Builder *builder,
                                                               Ast_Node_Index procedure);
static bool            wasm_frame_is_needed                   (Wasm_Builder const *builder,
                                                               Ast_Node_Index procedure);
static void            wasm_frame_leave                       (Wasm_Builder *builder,
                                                               Ast_Node_Index procedure);
static size_t          wasm_frame_literal_offset              (Wasm_Builder const *builder,
                                                               Ast_Node_Index procedure,
                                                               Ast_Node_Index literal);
static size_t          wasm_frame_size_bytes                  (Wasm_Builder const *builder,
                                                               Ast_Node_Index procedure);
static bool            wasm_for_is_each                       (Wasm_Builder const *builder,
                                                               Ast_Node_Index statement);
static Ast_Node_Index  wasm_global_type_node                  (Wasm_Builder const *builder,
                                                               Ast_Node_Index global);
static bool            wasm_node_is_type                      (Wasm_Builder const *builder,
                                                               Ast_Node_Index node);
static bool            wasm_procedure_uses_arrays             (Wasm_Builder const *builder,
                                                               Ast_Node_Index procedure);
static size_t          wasm_scratch_local_index               (Wasm_Builder const *builder,
                                                               Ast_Node_Index procedure,
                                                               Wasm_Scratch scratch);
static size_t          wasm_stack_global_index                (Wasm_Builder const *builder);
static void            wasm_structured_trap                   (Wasm_Builder *builder);
static Str             wasm_type_subject                      (Wasm_Builder const *builder,
                                                               Ast_Node_Index type);
static size_t          wasm_variable_locals_end               (Wasm_Builder const *builder,
                                                               Ast_Node_Index procedure);
static void            wasm_write_i32_const                   (Wasm_Writer *writer, int64_t number);
static void            wasm_write_local_instruction           (Wasm_Writer *writer, byte opcode,
                                                               size_t local_index);
static void            wasm_write_memory_instruction          (Wasm_Writer *writer, byte opcode,
                                                               uint64_t offset_bytes);
static Wasm_Type       wasm_binding_value_type                (Wasm_Builder const *builder,
                                                               Semantic_Binding binding);
static Wasm_Type       wasm_global_type                       (Wasm_Builder const *builder,
                                                               Ast_Node_Index global);
static Wasm_Type       wasm_expression_type                   (Wasm_Builder const *builder,
                                                               Ast_Node_Index expression);
static Wasm_Type       wasm_variable_type                     (Wasm_Builder const *builder,
                                                               Ast_Node_Index variable);
static Wasm_Type       wasm_type_make                         (Wasm_Value_Type kind);
static Wasm_Type       wasm_type_of_name                      (Wasm_Builder const *builder,
                                                               Str type_name);
static Wasm_Type       wasm_type_of_plain_name                (Wasm_Builder const *builder,
                                                               Str type_name);
static Wasm_Type       wasm_type_of_node                      (Wasm_Builder const *builder,
                                                               Ast_Node_Index type);
static Wasm_Type       wasm_type_element                      (Wasm_Type sequence);
static size_t          wasm_type_slot_count                   (Wasm_Builder const *builder,
                                                               Wasm_Type type);
static byte            wasm_type_slot_kind                    (Wasm_Builder const *builder,
                                                               Wasm_Type type, size_t slot);
static size_t          wasm_type_stride_bytes                 (Wasm_Builder const *builder,
                                                               Wasm_Type element);
static bool            wasm_type_accepts                      (Wasm_Type target, Wasm_Type value);
static bool            wasm_type_equals                       (Wasm_Type first, Wasm_Type second);
static bool            wasm_type_is_sequence                  (Wasm_Type type);
static void            wasm_write_type                        (Wasm_Builder *builder,
                                                               Wasm_Type type);
static size_t          wasm_type_group_count                  (Wasm_Builder const *builder,
                                                               Wasm_Type type);
static void            wasm_write_type_groups                 (Wasm_Builder *builder,
                                                               Wasm_Type type);
static Ast_Node_Index  wasm_struct_find                       (Wasm_Builder const *builder,
                                                               Str name);
static size_t          wasm_struct_slots                      (Wasm_Builder const *builder,
                                                               Ast_Node_Index struct_node,
                                                               size_t wanted, byte *kind);
static bool            wasm_struct_field_find                 (Wasm_Builder const *builder,
                                                               Ast_Node_Index struct_node,
                                                               Str name, size_t *slot_offset,
                                                               Wasm_Type *type);
static bool            wasm_check_structs                     (Wasm_Builder *builder);
static void            wasm_default_write                     (Wasm_Builder *builder,
                                                               Wasm_Type type);
static void            wasm_slots_load                        (Wasm_Builder *builder,
                                                               Wasm_Type type, size_t address,
                                                               size_t offset_bytes);
static void            wasm_slots_load_from_stack             (Wasm_Builder *builder,
                                                               Ast_Node_Index procedure,
                                                               Wasm_Type type,
                                                               size_t offset_bytes);
static void            wasm_slots_store                       (Wasm_Builder *builder,
                                                               Ast_Node_Index procedure,
                                                               Wasm_Type type, size_t address,
                                                               size_t offset_bytes);
static void            wasm_slots_set                         (Wasm_Builder *builder,
                                                               Wasm_Type type, byte opcode,
                                                               size_t first);
static void            wasm_scale_write                       (Wasm_Builder *builder,
                                                               size_t stride_bytes);
static void            wasm_element_address_write             (Wasm_Builder *builder,
                                                               Ast_Node_Index procedure,
                                                               size_t stride_bytes);
static void            wasm_slice_write                       (Wasm_Builder *builder,
                                                               Ast_Node_Index procedure,
                                                               size_t stride_bytes);
static bool            wasm_variables_check                   (Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index procedure);
static bool            wasm_expression_fail                   (Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index expression);
static bool            wasm_build_code_section_body_field_access(Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index access);
static bool            wasm_build_code_section_body_field_assignment(Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index assignment);
static bool            wasm_build_code_section_body_place_store(Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index assignment,
                                                               Wasm_Type type,
                                                               size_t offset_bytes);
static bool            wasm_build_code_section_body_struct_literal(Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index literal,
                                                               Ast_Node_Index expected);
static bool            wasm_build_code_section_body_variable_assignment(Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index assignment);
static size_t          wasm_string_address                    (Wasm_Builder const *builder,
                                                               Ast_Node_Index literal);
static Wasm_Field_Path wasm_field_path                        (Wasm_Builder const *builder,
                                                               Ast_Node_Index access);
static byte            wasm_load_opcode                       (byte kind);
static byte            wasm_store_opcode                      (byte kind);

Wasm_Build_Result wasm_build(Array_Slice nodes, Array_Slice bindings, Ast_Node_Index root,
                             Array_Slice output_buffer) {
    assert(nodes.elements != NULL);
    assert(bindings.elements != NULL);
    assert(output_buffer.elements != NULL);
    assert(root < nodes.length);
    // The bindings are of this tree, one for each node.
    assert(bindings.length == nodes.length);

    Ast_Node const *node_elements = nodes.elements;
    Semantic_Binding const *binding_elements = bindings.elements;
    assert(node_elements[root].node_type == AST_NODE_TYPE_MODULE);

    Wasm_Builder builder = {
        .writer =
            {
                .bytes = output_buffer.elements,
                .capacity = output_buffer.length,
                .length = 0,
                .is_overflowed = false,
            },
        .nodes = node_elements,
        .bindings = binding_elements,
        .subject = STR_EMPTY,
        .node_count = nodes.length,
        .module = root,
        .foreign_count = 0,
        .definition_count = 0,
        .data_size_bytes = 0,
        .stack_limit_bytes = 0,
        .stack_top_bytes = 0,
        .status = WASM_BUILD_STATUS_OK,
        .has_memory = false,
        .has_data = false,
        .has_stack = false,
        .has_table = false,
    };

    // Only the constant definitions are functions. A global variable is a global of the module.
    // The foreign ones are imported, and the others are defined in the module and exported.
    builder.foreign_count = wasm_function_count(&builder, root, true);
    size_t definition_count = wasm_function_count(&builder, root, false);
    builder.definition_count = definition_count;
    wasm_write_bytes(&builder.writer, WASM_MODULE_HEADER, sizeof(WASM_MODULE_HEADER));

    // The types are checked first, so that the sections below can rely on every type being known.
    // The size of the data is needed before the code is written, because the memory has to be
    // big enough for it and comes before the code.
    bool is_ok = wasm_data_measure(&builder);
    if (is_ok) {
        is_ok = wasm_check_structs(&builder);
    }
    if (is_ok) {
        is_ok = wasm_check_signatures(&builder, root, definition_count);
    }
    if (is_ok) {
        is_ok = wasm_check_globals(&builder);
    }

    // The sections must come in this order, and each function refers to its type and to its body
    // by the index of its definition, so one count describes the type, function and code sections.
    if (is_ok) {
        wasm_build_type_section(&builder, root, definition_count);
        if (builder.foreign_count > 0) {
            wasm_build_import_section(&builder, root);
        }
        wasm_build_function_section(&builder, definition_count);
        if (builder.has_table) {
            wasm_build_table_section(&builder);
        }
        if (builder.has_memory) {
            wasm_build_memory_section(&builder);
        }
        if (wasm_global_slot_count(&builder) > 0 || builder.has_stack) {
            wasm_build_global_section(&builder);
        }
        is_ok = wasm_build_export_section(&builder, root, definition_count);
    }
    if (is_ok && builder.has_table) {
        wasm_build_element_section(&builder);
    }
    if (is_ok) {
        is_ok = wasm_build_code_section(&builder, root, definition_count);
    }
    if (is_ok && builder.has_data) {
        wasm_build_data_section(&builder);
    }

    // A real error is more useful than the overflow that may follow from it, so the overflow is
    // only reported when nothing else went wrong.
    if (is_ok) {
        if (builder.writer.is_overflowed) {
            is_ok = wasm_fail(&builder, WASM_BUILD_STATUS_OUTPUT_TOO_LARGE, STR_EMPTY);
        }
    }

    // Nothing was built when there was an error, so no bytes are handed out.
    size_t length_bytes = 0;
    if (is_ok) {
        length_bytes = builder.writer.length;
    }

    assert(is_ok == (builder.status == WASM_BUILD_STATUS_OK));
    assert(builder.writer.length <= output_buffer.length);
    return (Wasm_Build_Result){
        .bytes = array_slice_make(output_buffer.elements, length_bytes),
        .subject = builder.subject,
        .status = builder.status,
    };
}

/**
 * Measures the bytes of all the string literals, and decides from them and from the arrays whether
 * the module has a memory, and how big it is. The data is at the start of the memory, and the stack
 * of the arrays is after it, where it grows down from the end of the memory. Returns false after it
 * recorded an error when they do not fit in the memory.
 */
static bool wasm_data_measure(Wasm_Builder *builder) {
    assert(builder->data_size_bytes == 0);
    assert(builder->has_memory == false);

    size_t size_bytes = 0;
    bool has_literal = false;
    bool has_array_type = false;
    bool has_array_literal = false;
    bool has_address_of = false;
    bool has_pointer = false;
    bool has_procedure_pointer = false;
    for (size_t i = 0; i < builder->node_count; i += 1) {
        if (builder->nodes[i].node_type == AST_NODE_TYPE_PROCEDURE_TYPE) {
            has_procedure_pointer = true;
        } else if (builder->nodes[i].node_type == AST_NODE_TYPE_IDENTIFIER &&
                   builder->bindings[i].kind == SEMANTIC_BINDING_KIND_CONSTANT) {
            // The name of a procedure that is not called is the pointer to it.
            Ast_Node_Index constant_value = builder->nodes[builder->bindings[i].target].first_child;
            if (builder->nodes[constant_value].node_type == AST_NODE_TYPE_PROCEDURE) {
                has_procedure_pointer = true;
            }
        }
        if (builder->nodes[i].node_type == AST_NODE_TYPE_STRING_LITERAL) {
            has_literal = true;
            // Each literal is shorter than 4 GiB and there are fewer than 4 Gi of them, so the
            // sum fits in 64 bits.
            size_bytes += builder->nodes[i].payload.string_literal.text.length;
        } else if (builder->nodes[i].node_type == AST_NODE_TYPE_ARRAY_TYPE) {
            has_array_type = true;
        } else if (builder->nodes[i].node_type == AST_NODE_TYPE_ARRAY_LITERAL) {
            has_array_literal = true;
        } else if (builder->nodes[i].node_type == AST_NODE_TYPE_ADDRESS_OF) {
            has_address_of = true;
        } else if (builder->nodes[i].node_type == AST_NODE_TYPE_DEREFERENCE ||
                   builder->nodes[i].node_type == AST_NODE_TYPE_POINTER_TYPE) {
            has_pointer = true;
        }
    }

    // Addresses and lengths are written as `i32.const`, whose immediate is signed, so they stay
    // below 2 GiB. A module that big would not fit in the output anyway, so the same status says
    // it. The stack and the rounding up to a whole page are counted too.
    size_t stack_limit_bytes = (size_bytes + WASM_ELEMENT_SIZE_BYTES - 1) /
                               WASM_ELEMENT_SIZE_BYTES * WASM_ELEMENT_SIZE_BYTES;
    size_t stack_top_bytes = stack_limit_bytes + WASM_STACK_SIZE_BYTES;
    stack_top_bytes = (stack_top_bytes + WASM_MEMORY_PAGE_SIZE_BYTES - 1) /
                      WASM_MEMORY_PAGE_SIZE_BYTES * WASM_MEMORY_PAGE_SIZE_BYTES;
    if (size_bytes > INT32_MAX || stack_top_bytes > INT32_MAX) {
        return wasm_fail(builder, WASM_BUILD_STATUS_OUTPUT_TOO_LARGE, STR_EMPTY);
    }

    builder->data_size_bytes = size_bytes;
    builder->has_data = has_literal;
    // The elements of an array slice are read from memory, so a module that has a type for one
    // needs a memory, also when it has no array of its own. A pointer points into the memory, so a
    // module that has a pointer type or follows a pointer needs one as well, also when it takes no
    // address itself, because a caller from JavaScript can pass the address of something it wrote.
    builder->has_memory = has_literal || has_array_type || has_address_of || has_pointer;
    // A variable whose address is taken is on the stack, in the frame of its procedure, like the
    // elements of an array literal.
    builder->has_stack = has_array_literal || has_address_of;
    // A pointer to a procedure is an index in the table, so the module needs one as soon as such a
    // pointer can be made, which is when a procedure is used as a value, or called, which needs a
    // type of a pointer to a procedure to be written.
    builder->has_table = has_procedure_pointer;
    builder->stack_limit_bytes = stack_limit_bytes;
    builder->stack_top_bytes = stack_top_bytes;
    return true;
}

/**
 * Checks the types of the signature of every procedure: its parameters, and its return type when
 * it has one, and the same for every procedure type. Returns false after it recorded an error.
 * What comes after can ask for the type of a parameter or of a result and assume that it is known.
 */
static bool wasm_check_signatures(Wasm_Builder *builder, Ast_Node_Index module,
                                  size_t definition_count) {
    assert(module < builder->node_count);
    assert(builder->nodes[module].node_type == AST_NODE_TYPE_MODULE);

    // The types in the types of pointers to procedures too, wherever they are written.
    for (Ast_Node_Index node = 0; node < builder->node_count; node += 1) {
        if (builder->nodes[node].node_type != AST_NODE_TYPE_PROCEDURE_TYPE) {
            continue;
        }
        size_t parameter_count = wasm_signature_parameter_count(builder, node);
        for (size_t i = 0; i < parameter_count; i += 1) {
            Ast_Node_Index parameter = wasm_signature_parameter_type(builder, node, i);
            if (wasm_type_of_node(builder, parameter).kind == WASM_VALUE_TYPE_NONE) {
                return wasm_fail(builder, WASM_BUILD_STATUS_TYPE_UNSUPPORTED,
                                 wasm_type_subject(builder, parameter));
            }
        }
        Str return_type = ast_procedure_return_type(&builder->nodes[node]);
        if (return_type.bytes != NULL &&
            wasm_type_of_name(builder, return_type).kind == WASM_VALUE_TYPE_NONE) {
            return wasm_fail(builder, WASM_BUILD_STATUS_TYPE_UNSUPPORTED, return_type);
        }
    }

    Ast_Node_Index definition = wasm_constant_find(builder, builder->nodes[module].first_child);
    for (size_t i = 0; i < definition_count; i += 1) {
        assert(definition != AST_NODE_INDEX_NONE);

        // A string has no signature to check: it has no parameters, and its type is known. The
        // member of an enum has a type that is known as well, but it must be a member of an enum.
        Ast_Node_Index value = builder->nodes[definition].first_child;
        if (builder->nodes[value].node_type == AST_NODE_TYPE_FIELD_ACCESS) {
            Ast_Node_Index enum_node = wasm_enum_of_access(builder, value);
            Str member_name = builder->nodes[value].payload.field_access.name;
            if (enum_node == AST_NODE_INDEX_NONE) {
                return wasm_fail(builder, WASM_BUILD_STATUS_TYPE_UNSUPPORTED,
                                 builder->nodes[builder->nodes[value].first_child]
                                     .payload.identifier.name);
            }
            size_t number = 0;
            if (wasm_enum_member_find(builder, enum_node, member_name, &number) == false) {
                return wasm_fail(builder, WASM_BUILD_STATUS_ENUM_MEMBER_UNKNOWN, member_name);
            }
        }
        if (builder->nodes[value].node_type == AST_NODE_TYPE_PROCEDURE) {
            size_t parameter_count = wasm_parameter_count(builder, value);
            Ast_Node_Index parameter = builder->nodes[value].first_child;
            for (size_t j = 0; j < parameter_count; j += 1) {
                assert(parameter != AST_NODE_INDEX_NONE);
                Ast_Node const *parameter_node = &builder->nodes[parameter];
                assert(parameter_node->node_type == AST_NODE_TYPE_PARAMETER);

                Ast_Node_Index type = parameter_node->first_child;
                Wasm_Type parameter_type = wasm_type_of_node(builder, type);
                if (parameter_type.kind == WASM_VALUE_TYPE_NONE) {
                    return wasm_fail(builder, WASM_BUILD_STATUS_TYPE_UNSUPPORTED,
                                     wasm_type_subject(builder, type));
                }
                // The elements of an array are in the frame of the procedure that has it, and a
                // parameter would have to be a copy of them, so an array slice is passed instead.
                if (parameter_type.kind == WASM_VALUE_TYPE_ARRAY) {
                    return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_TYPE_MISPLACED,
                                     wasm_type_subject(builder, type));
                }

                parameter = parameter_node->next_sibling;
            }

            Str return_type = builder->nodes[value].payload.procedure.return_type;
            if (return_type.bytes != NULL) {
                if (wasm_type_of_name(builder, return_type).kind == WASM_VALUE_TYPE_NONE) {
                    return wasm_fail(builder, WASM_BUILD_STATUS_TYPE_UNSUPPORTED, return_type);
                }
            }
        }

        definition = wasm_constant_find(builder, builder->nodes[definition].next_sibling);
    }

    return true;
}

/**
 * Returns the first constant definition among `from` and the siblings after it, or
 * `AST_NODE_INDEX_NONE` when there is none. `from` can be `AST_NODE_INDEX_NONE`. The children of
 * the module are constant definitions, which become functions, and global variables, which do not.
 */
static Ast_Node_Index wasm_constant_find(Wasm_Builder const *builder, Ast_Node_Index from) {
    Ast_Node_Index current = from;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && current != AST_NODE_INDEX_NONE; i += 1) {
        // A struct and an enum are constants too, but they are types and not functions.
        if (builder->nodes[current].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION &&
            builder->nodes[builder->nodes[current].first_child].node_type !=
                AST_NODE_TYPE_STRUCT_TYPE &&
            builder->nodes[builder->nodes[current].first_child].node_type !=
                AST_NODE_TYPE_ENUM_TYPE) {
            return current;
        }
        current = builder->nodes[current].next_sibling;
    }

    return AST_NODE_INDEX_NONE;
}

/**
 * Returns whether the constant `definition` is a procedure that is declared in a `foreign js ->`
 * block, which is imported and has no body in the module.
 */
static bool wasm_constant_is_foreign(Wasm_Builder const *builder, Ast_Node_Index definition) {
    assert(definition < builder->node_count);
    assert(builder->nodes[definition].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION);

    Ast_Node_Index value = builder->nodes[definition].first_child;
    return builder->nodes[value].node_type == AST_NODE_TYPE_PROCEDURE &&
           ast_procedure_is_foreign(builder->nodes, value);
}

/**
 * Returns the first function among the constant definitions `from` and the siblings after it that
 * is foreign when `is_foreign` is set and defined in the module otherwise, or
 * `AST_NODE_INDEX_NONE` when there is none. `from` can be `AST_NODE_INDEX_NONE`.
 */
static Ast_Node_Index wasm_function_find(Wasm_Builder const *builder, Ast_Node_Index from,
                                         bool is_foreign) {
    Ast_Node_Index current = wasm_constant_find(builder, from);
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && current != AST_NODE_INDEX_NONE; i += 1) {
        if (wasm_constant_is_foreign(builder, current) == is_foreign) {
            return current;
        }
        current = wasm_constant_find(builder, builder->nodes[current].next_sibling);
    }

    return AST_NODE_INDEX_NONE;
}

/**
 * Returns the number of functions of `module` that are foreign when `is_foreign` is set, and of
 * those that are defined in it otherwise.
 */
static size_t wasm_function_count(Wasm_Builder const *builder, Ast_Node_Index module,
                                  bool is_foreign) {
    assert(module < builder->node_count);

    size_t count = 0;
    Ast_Node_Index definition = wasm_function_find(builder, builder->nodes[module].first_child,
                                                   is_foreign);
    for (size_t i = 0; i < builder->node_count && definition != AST_NODE_INDEX_NONE; i += 1) {
        count += 1;
        definition = wasm_function_find(builder, builder->nodes[definition].next_sibling,
                                        is_foreign);
    }

    return count;
}

/**
 * Returns the index of the function that the constant `definition` is, in the function index space
 * of WebAssembly, where the imported functions come first. So a foreign procedure has the number of
 * foreign procedures above it, and any other has the number of foreign procedures there are in all
 * and the number of functions above it that are not foreign. That is not the position of the
 * definition among the constants, which does not tell the two kinds apart.
 */
static size_t wasm_function_index(Wasm_Builder const *builder, Ast_Node_Index definition) {
    assert(definition < builder->node_count);
    assert(builder->nodes[definition].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION);

    bool is_foreign = wasm_constant_is_foreign(builder, definition);
    size_t index = 0;
    Ast_Node_Index current = wasm_function_find(builder, builder->nodes[builder->module].first_child,
                                                is_foreign);
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && current != definition; i += 1) {
        assert(current != AST_NODE_INDEX_NONE);
        index += 1;
        current = wasm_function_find(builder, builder->nodes[current].next_sibling, is_foreign);
    }

    assert(current == definition);
    if (is_foreign) {
        return index;
    }
    return builder->foreign_count + index;
}

/**
 * Returns the pointer to the procedure that the constant `definition` is, which is its slot in the
 * table of the module: the function index plus one, because slot 0 is for `nil`, see
 * `wasm_build_table_section`.
 */
static size_t wasm_procedure_table_index(Wasm_Builder const *builder, Ast_Node_Index definition) {
    assert(builder->has_table);

    return wasm_function_index(builder, definition) + 1;
}

/**
 * Returns the initial value of the global variable `global`, which is its last child.
 */
static Ast_Node_Index wasm_global_value(Wasm_Builder const *builder, Ast_Node_Index global) {
    assert(global < builder->node_count);
    assert(builder->nodes[global].node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION);

    Ast_Node_Index value = builder->nodes[global].last_child;
    assert(value != AST_NODE_INDEX_NONE);
    return value;
}

/**
 * Returns the type of the global variable `global`: the one its definition gives, which is the
 * child after the attributes, or else the type of its value. Returns a type of the kind
 * `WASM_VALUE_TYPE_NONE` when the type that the definition gives is not one that the target has,
 * which `wasm_check_globals` rejects.
 */
static Wasm_Type wasm_global_type(Wasm_Builder const *builder, Ast_Node_Index global) {
    assert(global < builder->node_count);
    assert(builder->nodes[global].node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION);

    Ast_Node_Index type = wasm_global_type_node(builder, global);
    if (type != AST_NODE_INDEX_NONE) {
        return wasm_type_of_node(builder, type);
    }
    // Without a type the only child left is the value, which is a literal.
    return wasm_expression_type(builder, wasm_global_value(builder, global));
}

/**
 * Returns the type that the definition of the global variable `global` gives, which is the child
 * after the attributes, or `AST_NODE_INDEX_NONE` when it gives none.
 */
static Ast_Node_Index wasm_global_type_node(Wasm_Builder const *builder, Ast_Node_Index global) {
    assert(global < builder->node_count);
    assert(builder->nodes[global].node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION);

    Ast_Node_Index child = builder->nodes[global].first_child;
    // Every child is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count; i += 1) {
        assert(child != AST_NODE_INDEX_NONE);
        if (builder->nodes[child].node_type != AST_NODE_TYPE_ATTRIBUTE) {
            break;
        }
        child = builder->nodes[child].next_sibling;
    }

    if (wasm_node_is_type(builder, child)) {
        return child;
    }
    return AST_NODE_INDEX_NONE;
}

/**
 * Checks the type of every global variable: the type it gives must be one that the target has, and
 * the value must be of that type. Returns false after it recorded an error. What comes after can
 * ask for the type of a global variable and assume that it is known.
 */
static bool wasm_check_globals(Wasm_Builder *builder) {
    Ast_Node_Index global = builder->nodes[builder->module].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && global != AST_NODE_INDEX_NONE; i += 1) {
        if (builder->nodes[global].node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION) {
            Wasm_Type global_type = wasm_global_type(builder, global);
            Ast_Node_Index value = wasm_global_value(builder, global);
            if (global_type.kind == WASM_VALUE_TYPE_NONE) {
                // The only way to have no type is a type name that the target does not have, and
                // it is the child before the value.
                Ast_Node_Index type = wasm_global_type_node(builder, global);
                assert(type != AST_NODE_INDEX_NONE);
                return wasm_fail(builder, WASM_BUILD_STATUS_TYPE_UNSUPPORTED,
                                 wasm_type_subject(builder, type));
            }
            // A global is set once, from a literal, and no literal makes an array or a slice.
            if (global_type.kind == WASM_VALUE_TYPE_ARRAY ||
                global_type.kind == WASM_VALUE_TYPE_SLICE) {
                Ast_Node_Index type = wasm_global_type_node(builder, global);
                assert(type != AST_NODE_INDEX_NONE);
                return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_TYPE_MISPLACED,
                                 wasm_type_subject(builder, type));
            }
            if (wasm_type_equals(wasm_expression_type(builder, value), global_type) == false) {
                return wasm_fail(builder, WASM_BUILD_STATUS_VARIABLE_TYPE_MISMATCH,
                                 builder->nodes[global].payload.global_variable_definition.name);
            }
        }
        global = builder->nodes[global].next_sibling;
    }

    return true;
}

/**
 * Returns the index of the first WebAssembly global that holds the global variable `global`: the
 * number of globals that the global variables before it take, which is not their number, because a
 * `Str` takes two. The types of the global variables must be known.
 */
static size_t wasm_global_slot_index(Wasm_Builder const *builder, Ast_Node_Index global) {
    assert(global < builder->node_count);
    assert(builder->nodes[global].node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION);

    size_t slot_index = 0;
    Ast_Node_Index current = builder->nodes[builder->module].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && current != global; i += 1) {
        assert(current != AST_NODE_INDEX_NONE);
        if (builder->nodes[current].node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION) {
            slot_index += wasm_type_slot_count(builder, wasm_global_type(builder, current));
        }
        current = builder->nodes[current].next_sibling;
    }

    assert(current == global);
    return slot_index;
}

/**
 * Returns the number of WebAssembly globals that all the global variables take together.
 */
static size_t wasm_global_slot_count(Wasm_Builder const *builder) {
    size_t slot_count = 0;
    Ast_Node_Index current = builder->nodes[builder->module].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && current != AST_NODE_INDEX_NONE; i += 1) {
        if (builder->nodes[current].node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION) {
            slot_count += wasm_type_slot_count(builder, wasm_global_type(builder, current));
        }
        current = builder->nodes[current].next_sibling;
    }

    return slot_count;
}

/**
 * Writes the global section, which has the WebAssembly globals of all the global variables, in the
 * order of the source: one for an `Int` or a `Bool`, and two for a `Str`, its address and its
 * length. They are mutable, so a procedure can assign to them, and the initial value is a constant
 * expression, which is evaluated once when the module is instantiated. That is why the value of a
 * global variable is a literal. The address of a string is where its bytes are in the data section,
 * which is the length of the strings whose nodes come before it. The types must have been checked
 * by `wasm_check_globals`. When the program has array literals, the stack pointer comes after the
 * globals of the program.
 */
static void wasm_build_global_section(Wasm_Builder *builder) {
    size_t section = wasm_section_begin(&builder->writer, WASM_SECTION_ID_GLOBAL);
    size_t stack_global_count = 0;
    if (builder->has_stack) {
        stack_global_count = 1;
    }
    wasm_write_unsigned(&builder->writer, wasm_global_slot_count(builder) + stack_global_count);

    // The length of the strings in the nodes before `scanned_end`.
    size_t address = 0;
    size_t scanned_end = 0;
    Ast_Node_Index global = builder->nodes[builder->module].first_child;
    // Every definition is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && global != AST_NODE_INDEX_NONE; i += 1) {
        if (builder->nodes[global].node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION) {
            Wasm_Type value_type = wasm_global_type(builder, global);
            Ast_Node_Index value = wasm_global_value(builder, global);
            Ast_Node const *value_node = &builder->nodes[value];

            // Both are written as `i32.const` or `i64.const` followed by the end of the expression.
            if (value_type.kind == WASM_VALUE_TYPE_STR) {
                for (; scanned_end < value; scanned_end += 1) {
                    if (builder->nodes[scanned_end].node_type == AST_NODE_TYPE_STRING_LITERAL) {
                        address += builder->nodes[scanned_end].payload.string_literal.text.length;
                    }
                }
                size_t length_bytes = value_node->payload.string_literal.text.length;

                for (size_t slot = 0; slot < 2; slot += 1) {
                    wasm_write_byte(&builder->writer, WASM_TYPE_I32);
                    wasm_write_byte(&builder->writer, WASM_GLOBAL_MUTABLE);
                    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_CONST);
                    // Both are below 2 GiB, because `wasm_data_measure` checked the total.
                    wasm_write_signed(&builder->writer,
                                      (int64_t)(slot == 0 ? address : length_bytes));
                    wasm_write_byte(&builder->writer, WASM_OPCODE_END);
                }
            } else if (value_type.kind == WASM_VALUE_TYPE_BOOL) {
                wasm_write_byte(&builder->writer, WASM_TYPE_I32);
                wasm_write_byte(&builder->writer, WASM_GLOBAL_MUTABLE);
                wasm_write_byte(&builder->writer, WASM_OPCODE_I32_CONST);
                wasm_write_signed(&builder->writer, value_node->payload.bool_literal.value ? 1 : 0);
                wasm_write_byte(&builder->writer, WASM_OPCODE_END);
            } else {
                wasm_write_byte(&builder->writer, WASM_TYPE_I64);
                wasm_write_byte(&builder->writer, WASM_GLOBAL_MUTABLE);
                wasm_write_byte(&builder->writer, WASM_OPCODE_I64_CONST);
                wasm_write_signed(&builder->writer, value_node->payload.integer_literal.value);
                wasm_write_byte(&builder->writer, WASM_OPCODE_END);
            }
        }
        global = builder->nodes[global].next_sibling;
    }

    // The last global is the stack pointer, which starts at the top of the stack, because the stack
    // grows down. It is after the globals of the program, see `wasm_stack_global_index`.
    if (builder->has_stack) {
        wasm_write_byte(&builder->writer, WASM_TYPE_I32);
        wasm_write_byte(&builder->writer, WASM_GLOBAL_MUTABLE);
        wasm_write_byte(&builder->writer, WASM_OPCODE_I32_CONST);
        wasm_write_signed(&builder->writer, (int64_t)builder->stack_top_bytes);
        wasm_write_byte(&builder->writer, WASM_OPCODE_END);
    }

    wasm_block_end(&builder->writer, section);
}

/**
 * Returns the index of the global that is the stack pointer, which is the one after the globals of
 * the program. The module has it when `has_stack` is set.
 */
static size_t wasm_stack_global_index(Wasm_Builder const *builder) {
    assert(builder->has_stack);

    return wasm_global_slot_count(builder);
}

/**
 * Writes the type section, which has one function type per function: first the foreign procedures,
 * which are the imported functions, and then the definitions of the module, so that the function at
 * index `i` has the type at index `i`. The type is the parameters and the result of its procedure.
 * Types are not shared, because that would save only a few bytes. After them come the types of the
 * pointers to procedures whose signature no procedure has first, see `wasm_signature_canonical`.
 * The types in the signatures must have been checked by `wasm_check_signatures`.
 */
static void wasm_build_type_section(Wasm_Builder *builder, Ast_Node_Index module,
                                    size_t definition_count) {
    assert(module < builder->node_count);
    assert(builder->nodes[module].node_type == AST_NODE_TYPE_MODULE);

    // The types of the procedures are followed by the signature of each type of a pointer to a
    // procedure that no procedure has the signature of first, see `wasm_signature_canonical`.
    size_t signature_count = 0;
    for (Ast_Node_Index node = 0; node < builder->node_count; node += 1) {
        if (builder->nodes[node].node_type == AST_NODE_TYPE_PROCEDURE_TYPE &&
            wasm_signature_canonical(builder, node) == node) {
            signature_count += 1;
        }
    }

    size_t section = wasm_section_begin(&builder->writer, WASM_SECTION_ID_TYPE);
    wasm_write_unsigned(&builder->writer,
                        builder->foreign_count + definition_count + signature_count);

    // Every function is in one of the two passes, and the foreign ones are first.
    for (int pass = 0; pass < 2; pass += 1) {
        bool is_foreign = pass == 0;
        size_t count = definition_count;
        if (is_foreign) {
            count = builder->foreign_count;
        }

        Ast_Node_Index definition =
            wasm_function_find(builder, builder->nodes[module].first_child, is_foreign);
        for (size_t i = 0; i < count; i += 1) {
            assert(definition != AST_NODE_INDEX_NONE);

            wasm_build_function_type(builder, definition);

            definition = wasm_function_find(builder, builder->nodes[definition].next_sibling,
                                            is_foreign);
        }
    }

    for (Ast_Node_Index node = 0; node < builder->node_count; node += 1) {
        if (builder->nodes[node].node_type == AST_NODE_TYPE_PROCEDURE_TYPE &&
            wasm_signature_canonical(builder, node) == node) {
            wasm_write_signature(builder, node);
        }
    }

    wasm_block_end(&builder->writer, section);
}

/**
 * Writes the function type of the constant `definition`: the marker, the parameters and the result.
 */
static void wasm_build_function_type(Wasm_Builder *builder, Ast_Node_Index definition) {
    assert(definition < builder->node_count);

    Ast_Node_Index value = builder->nodes[definition].first_child;
    if (builder->nodes[value].node_type == AST_NODE_TYPE_FIELD_ACCESS) {
        // A constant that is the member of an enum is a function without parameters that returns
        // its number.
        wasm_write_byte(&builder->writer, WASM_TYPE_FUNCTION);
        wasm_write_unsigned(&builder->writer, 0);
        wasm_write_unsigned(&builder->writer, 1);
        wasm_write_byte(&builder->writer, WASM_TYPE_I32);
        return;
    }
    if (builder->nodes[value].node_type == AST_NODE_TYPE_STRING_LITERAL) {
        // A constant string is a function without parameters that returns the string.
        wasm_write_byte(&builder->writer, WASM_TYPE_FUNCTION);
        wasm_write_unsigned(&builder->writer, 0);
        Wasm_Type string = wasm_type_make(WASM_VALUE_TYPE_STR);
        wasm_write_unsigned(&builder->writer, wasm_type_slot_count(builder, string));
        wasm_write_type(builder, string);
        return;
    }

    wasm_write_signature(builder, wasm_procedure_of(builder, definition));
}

/**
 * Writes a function type: the marker, the parameters and the result of `signature`, which is an
 * `AST_NODE_TYPE_PROCEDURE` or an `AST_NODE_TYPE_PROCEDURE_TYPE`. The types in it must have been
 * checked.
 */
static void wasm_write_signature(Wasm_Builder *builder, Ast_Node_Index signature) {
    assert(signature < builder->node_count);

    wasm_write_byte(&builder->writer, WASM_TYPE_FUNCTION);

    // A `Str` is two WebAssembly values, so the count is of values and not of parameters.
    size_t parameter_count = wasm_signature_parameter_count(builder, signature);
    size_t slot_count = 0;
    for (size_t i = 0; i < parameter_count; i += 1) {
        Ast_Node_Index type = wasm_signature_parameter_type(builder, signature, i);
        slot_count += wasm_type_slot_count(builder, wasm_type_of_node(builder, type));
    }
    wasm_write_unsigned(&builder->writer, slot_count);
    for (size_t i = 0; i < parameter_count; i += 1) {
        Ast_Node_Index type = wasm_signature_parameter_type(builder, signature, i);
        wasm_write_type(builder, wasm_type_of_node(builder, type));
    }

    Str return_type_name = ast_procedure_return_type(&builder->nodes[signature]);
    if (return_type_name.bytes == NULL) {
        wasm_write_unsigned(&builder->writer, 0);
    } else {
        Wasm_Type return_type = wasm_type_of_name(builder, return_type_name);
        wasm_write_unsigned(&builder->writer, wasm_type_slot_count(builder, return_type));
        wasm_write_type(builder, return_type);
    }
}

/**
 * Writes the import section, which brings in the foreign procedures as functions. The module name
 * of an import is the name that `import foreign js` gives, which is the property of the object of
 * imports that JavaScript passes when it instantiates the module, and the name of the import is the
 * name of the procedure, which is the property of that object. The type of the import at index `i`
 * is the type at index `i`, see `wasm_build_type_section`.
 */
static void wasm_build_import_section(Wasm_Builder *builder, Ast_Node_Index module) {
    assert(module < builder->node_count);
    assert(builder->foreign_count > 0);

    // The analysis checked that the program has one, above the first foreign procedure.
    Str module_name = STR_EMPTY;
    Ast_Node_Index child = builder->nodes[module].first_child;
    // Every child is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && child != AST_NODE_INDEX_NONE; i += 1) {
        if (builder->nodes[child].node_type == AST_NODE_TYPE_FOREIGN_IMPORT) {
            module_name = builder->nodes[child].payload.foreign_import.module_name;
            break;
        }
        child = builder->nodes[child].next_sibling;
    }
    assert(module_name.bytes != NULL);

    size_t section = wasm_section_begin(&builder->writer, WASM_SECTION_ID_IMPORT);
    wasm_write_unsigned(&builder->writer, builder->foreign_count);

    Ast_Node_Index definition =
        wasm_function_find(builder, builder->nodes[module].first_child, true);
    for (size_t i = 0; i < builder->foreign_count; i += 1) {
        assert(definition != AST_NODE_INDEX_NONE);

        wasm_write_text(&builder->writer, module_name);
        wasm_write_text(&builder->writer, builder->nodes[definition].payload.constant_definition.name);
        wasm_write_byte(&builder->writer, WASM_IMPORT_KIND_FUNCTION);
        // One type for each function, and the imports are the first of them.
        wasm_write_unsigned(&builder->writer, i);

        definition = wasm_function_find(builder, builder->nodes[definition].next_sibling, true);
    }

    wasm_block_end(&builder->writer, section);
}

/**
 * Writes the memory section, which declares the one memory of the module. It is just big enough
 * for the data, and the stack of the arrays when there is one, and at least one page, so that the
 * memory exists with nothing in it as well.
 */
static void wasm_build_memory_section(Wasm_Builder *builder) {
    assert(builder->has_memory);
    assert(builder->data_size_bytes <= INT32_MAX);

    size_t page_count = (builder->data_size_bytes + WASM_MEMORY_PAGE_SIZE_BYTES - 1) /
                        WASM_MEMORY_PAGE_SIZE_BYTES;
    // With a stack the memory ends where the stack starts, which is a whole number of pages.
    if (builder->has_stack) {
        assert(builder->stack_top_bytes % WASM_MEMORY_PAGE_SIZE_BYTES == 0);
        page_count = builder->stack_top_bytes / WASM_MEMORY_PAGE_SIZE_BYTES;
    }
    if (page_count == 0) {
        page_count = 1;
    }

    size_t section = wasm_section_begin(&builder->writer, WASM_SECTION_ID_MEMORY);
    wasm_write_unsigned(&builder->writer, 1);
    wasm_write_byte(&builder->writer, WASM_MEMORY_LIMITS_MIN_ONLY);
    wasm_write_unsigned(&builder->writer, page_count);
    wasm_block_end(&builder->writer, section);
}

/**
 * Writes the data section, which has one segment: the bytes of all string literals, in the order
 * of their nodes, copied to the start of the memory when the module is instantiated.
 */
static void wasm_build_data_section(Wasm_Builder *builder) {
    assert(builder->has_memory);
    assert(builder->data_size_bytes <= INT32_MAX);

    size_t section = wasm_section_begin(&builder->writer, WASM_SECTION_ID_DATA);
    wasm_write_unsigned(&builder->writer, 1);
    wasm_write_byte(&builder->writer, WASM_DATA_SEGMENT_ACTIVE);
    // The address of the segment is a constant expression: a constant, and the end of it.
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_CONST);
    wasm_write_signed(&builder->writer, 0);
    wasm_write_byte(&builder->writer, WASM_OPCODE_END);
    wasm_write_unsigned(&builder->writer, builder->data_size_bytes);

    size_t written_bytes = 0;
    for (size_t i = 0; i < builder->node_count; i += 1) {
        if (builder->nodes[i].node_type == AST_NODE_TYPE_STRING_LITERAL) {
            Str text = builder->nodes[i].payload.string_literal.text;
            wasm_write_bytes(&builder->writer, (byte const *)text.bytes, text.length);
            written_bytes += text.length;
        }
    }

    assert(written_bytes == builder->data_size_bytes);
    wasm_block_end(&builder->writer, section);
}

/**
 * Writes the function section, which says which type each function of the module has. The
 * function at index `i` has the type at index `i`, because the type section has one type per
 * function. The imported functions are before the ones of the module, so the type of the first
 * function of the module is the one after theirs.
 */
static void wasm_build_function_section(Wasm_Builder *builder, size_t definition_count) {
    assert(definition_count <= builder->node_count);

    size_t section = wasm_section_begin(&builder->writer, WASM_SECTION_ID_FUNCTION);
    wasm_write_unsigned(&builder->writer, definition_count);
    for (size_t i = 0; i < definition_count; i += 1) {
        // The types of the imported functions come first.
        wasm_write_unsigned(&builder->writer, builder->foreign_count + i);
    }
    wasm_block_end(&builder->writer, section);
}

/**
 * Writes the table section, which declares the one table of the module. It has a slot for each
 * function of the module, the imported ones too, and one more at index 0, which is left empty: a
 * pointer to a procedure is the index of the procedure in the table plus one, so that the null
 * pointer `nil`, which is 0, is a slot that has no function, and calling it traps.
 */
static void wasm_build_table_section(Wasm_Builder *builder) {
    assert(builder->has_table);

    size_t section = wasm_section_begin(&builder->writer, WASM_SECTION_ID_TABLE);
    wasm_write_unsigned(&builder->writer, 1);
    wasm_write_byte(&builder->writer, WASM_TYPE_FUNCREF);
    wasm_write_byte(&builder->writer, WASM_TABLE_LIMITS_MIN_ONLY);
    wasm_write_unsigned(&builder->writer, 1 + builder->foreign_count + builder->definition_count);
    wasm_block_end(&builder->writer, section);
}

/**
 * Writes the element section, which has one segment: all functions of the module, in the order of
 * their indexes, put in the table from slot 1 on when the module is instantiated, see
 * `wasm_build_table_section`. So the slot of a function is its function index plus one.
 */
static void wasm_build_element_section(Wasm_Builder *builder) {
    assert(builder->has_table);

    size_t function_count = builder->foreign_count + builder->definition_count;
    size_t section = wasm_section_begin(&builder->writer, WASM_SECTION_ID_ELEMENT);
    wasm_write_unsigned(&builder->writer, 1);
    wasm_write_byte(&builder->writer, WASM_ELEMENT_SEGMENT_ACTIVE);
    // The index of the first slot is a constant expression: a constant, and the end of it.
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_CONST);
    wasm_write_signed(&builder->writer, 1);
    wasm_write_byte(&builder->writer, WASM_OPCODE_END);
    wasm_write_unsigned(&builder->writer, function_count);
    for (size_t i = 0; i < function_count; i += 1) {
        wasm_write_unsigned(&builder->writer, i);
    }
    wasm_block_end(&builder->writer, section);
}

/**
 * Writes the export section, which makes every function of the module callable from outside it
 * under the name of its definition, which the imported functions are not, and the memory, if there is one, readable under the name `memory`.
 */
static bool wasm_build_export_section(Wasm_Builder *builder, Ast_Node_Index module,
                                      size_t definition_count) {
    assert(module < builder->node_count);
    assert(definition_count <= builder->node_count);

    // Strings are in the memory, and a caller that gets the address of one cannot read it
    // without it.
    size_t memory_export_count = 0;
    if (builder->has_memory) {
        memory_export_count = 1;
    }

    size_t section = wasm_section_begin(&builder->writer, WASM_SECTION_ID_EXPORT);
    wasm_write_unsigned(&builder->writer, definition_count + memory_export_count);

    Ast_Node_Index definition =
        wasm_function_find(builder, builder->nodes[module].first_child, false);
    for (size_t i = 0; i < definition_count; i += 1) {
        assert(definition != AST_NODE_INDEX_NONE);
        Str name = builder->nodes[definition].payload.constant_definition.name;

        // The name is taken whether the module has a memory or not, so that adding a string to
        // a program does not make it stop compiling.
        if (str_equals(name, STR("memory"))) {
            return wasm_fail(builder, WASM_BUILD_STATUS_DEFINITION_NAME_RESERVED, name);
        }

        // Export names must be unique, which they are, because the names of constants are, as
        // `semantic_analyze` checked.

        wasm_write_text(&builder->writer, name);
        wasm_write_byte(&builder->writer, WASM_EXPORT_KIND_FUNCTION);
        wasm_write_unsigned(&builder->writer, builder->foreign_count + i);

        definition = wasm_function_find(builder, builder->nodes[definition].next_sibling, false);
    }

    if (builder->has_memory) {
        wasm_write_text(&builder->writer, STR("memory"));
        wasm_write_byte(&builder->writer, WASM_EXPORT_KIND_MEMORY);
        wasm_write_unsigned(&builder->writer, 0);
    }

    wasm_block_end(&builder->writer, section);
    return true;
}

/**
 * Writes the code section, which has the body of each function, in the order of the definitions.
 */
static bool wasm_build_code_section(Wasm_Builder *builder, Ast_Node_Index module,
                                    size_t definition_count) {
    assert(module < builder->node_count);
    assert(definition_count <= builder->node_count);

    size_t section = wasm_section_begin(&builder->writer, WASM_SECTION_ID_CODE);
    wasm_write_unsigned(&builder->writer, definition_count);

    Ast_Node_Index definition =
        wasm_function_find(builder, builder->nodes[module].first_child, false);
    for (size_t i = 0; i < definition_count; i += 1) {
        assert(definition != AST_NODE_INDEX_NONE);

        if (wasm_build_code_section_body(builder, definition) == false) {
            return false;
        }
        definition = wasm_function_find(builder, builder->nodes[definition].next_sibling, false);
    }

    wasm_block_end(&builder->writer, section);
    return true;
}

/**
 * Writes one entry of the code section: the size of the body, then the body.
 */
static bool wasm_build_code_section_body(Wasm_Builder *builder, Ast_Node_Index definition) {
    assert(definition < builder->node_count);
    assert(builder->nodes[definition].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION);

    size_t body = wasm_block_begin(&builder->writer);

    Ast_Node_Index value = builder->nodes[definition].first_child;
    if (builder->nodes[value].node_type == AST_NODE_TYPE_STRING_LITERAL) {
        // A constant string has no locals. The string is the result, and is left on the stack for
        // the `end` to return.
        wasm_write_unsigned(&builder->writer, 0);
        wasm_build_code_section_body_string(builder, value);
    } else if (builder->nodes[value].node_type == AST_NODE_TYPE_FIELD_ACCESS) {
        // The same for the number of the member of an enum, which was checked.
        wasm_write_unsigned(&builder->writer, 0);
        Ast_Node_Index enum_node = wasm_enum_of_access(builder, value);
        assert(enum_node != AST_NODE_INDEX_NONE);
        if (wasm_enum_member_write(builder, enum_node,
                                   builder->nodes[value].payload.field_access.name) == false) {
            return false;
        }
    } else {
        // The parameters are the first locals of the function already, and the variables come
        // after them.
        if (wasm_variables_check(builder, definition, value) == false) {
            return false;
        }
        wasm_variables_write_locals(builder, value);
        wasm_frame_enter(builder, value);
        if (wasm_build_code_section_body_procedure(builder, definition) == false) {
            return false;
        }
    }

    // After a `return` the code is unreachable, and WebAssembly accepts anything there, so the
    // body ends with the `end` instruction even though no value is left on the stack. When the
    // last statement is not a `return`, for example an if statement that returns in every branch,
    // WebAssembly does not know that the end cannot be reached, so it is marked as unreachable.
    if (builder->nodes[value].node_type == AST_NODE_TYPE_PROCEDURE) {
        Ast_Node const *procedure_node = &builder->nodes[value];
        bool has_result = procedure_node->payload.procedure.return_type.bytes != NULL;
        Ast_Node_Type last_type = builder->nodes[procedure_node->last_child].node_type;
        // A `return` gives the frame back before it returns, so only the end of the body, which
        // is reached when the last statement is something else, has to do it.
        if (last_type != AST_NODE_TYPE_RETURN_STATEMENT) {
            wasm_frame_leave(builder, value);
        }
        if (has_result && last_type != AST_NODE_TYPE_RETURN_STATEMENT) {
            wasm_write_byte(&builder->writer, WASM_OPCODE_UNREACHABLE);
        }
    }
    wasm_write_byte(&builder->writer, WASM_OPCODE_END);
    wasm_block_end(&builder->writer, body);
    return true;
}

/**
 * Writes the instructions that push the string of `literal`: its address and then its length.
 * The address is where the string is in the data section, see `wasm_string_address`.
 */
static void wasm_build_code_section_body_string(Wasm_Builder *builder, Ast_Node_Index literal) {
    assert(literal < builder->node_count);
    assert(builder->nodes[literal].node_type == AST_NODE_TYPE_STRING_LITERAL);
    assert(builder->has_memory);

    size_t address = wasm_string_address(builder, literal);
    size_t length_bytes = builder->nodes[literal].payload.string_literal.text.length;
    assert(address + length_bytes <= builder->data_size_bytes);

    // Both are below 2 GiB, because `wasm_data_measure` checked the total.
    wasm_write_i32_const(&builder->writer, (int64_t)address);
    wasm_write_i32_const(&builder->writer, (int64_t)length_bytes);
}

/**
 * Writes the structured instruction `opcode`, which is a `block`, a `loop` or an `if`, with the
 * block type that takes and leaves no values.
 */
static void wasm_structured_begin(Wasm_Builder *builder, byte opcode) {
    wasm_write_byte(&builder->writer, opcode);
    wasm_write_byte(&builder->writer, WASM_BLOCK_TYPE_EMPTY);
}

/**
 * Opens the list of the children of `node` as the innermost one in `frames`, which has
 * `*frame_count` open lists. `statement` is the statement whose block `node` is, or
 * `AST_NODE_INDEX_NONE`, see `Wasm_Statement_Frame`.
 */
static void wasm_frame_push(Wasm_Builder const *builder, Wasm_Statement_Frame *frames,
                            size_t *frame_count, Ast_Node_Index node, Ast_Node_Index statement) {
    assert(node < builder->node_count);
    assert(*frame_count < WASM_STATEMENT_DEPTH_MAX);

    frames[*frame_count] = (Wasm_Statement_Frame){
        .node = node,
        .next = builder->nodes[node].first_child,
        .if_count = 0,
        .statement = statement,
    };
    *frame_count += 1;
}

/**
 * Writes the statements of a procedure. A variable definition becomes its value stored in the
 * locals of the variable, an assignment becomes the new value of the variable stored in the same
 * way, and a `return` becomes the value followed by the `return` instruction.
 *
 * A for statement becomes a `block` that holds a `loop` that holds a `block`, see
 * `wasm_build_code_section_body_for`, and so does a loop statement, without the test. `break` and
 * `continue` are a `br`, see `wasm_build_code_section_body_jump`.
 *
 * An if statement becomes the structured instructions of WebAssembly: the condition, `if`, the
 * statements of its block, and `else` followed by what the next branch is, up to `end`. An
 * `else if` is an `else` that holds another `if`, so the statement ends with as many `end`
 * instructions as it has `if` instructions. The tree is walked with an array of the lists of
 * statements that are open, the way expressions are, instead of calling itself.
 */
static bool wasm_build_code_section_body_procedure(Wasm_Builder *builder,
                                                   Ast_Node_Index definition) {
    assert(definition < builder->node_count);

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);

    Wasm_Statement_Frame frames[WASM_STATEMENT_DEPTH_MAX];
    size_t frame_count = 0;
    wasm_frame_push(builder, frames, &frame_count, procedure, AST_NODE_INDEX_NONE);

    // Every step takes a child of a list or closes a list, and each node is a child once and each
    // list is closed once, so twice the number of nodes steps are enough.
    for (size_t step = 0; step < builder->node_count * 2 + 2 && frame_count > 0; step += 1) {
        Wasm_Statement_Frame *frame = &frames[frame_count - 1];
        Ast_Node const *frame_node = &builder->nodes[frame->node];

        if (frame->next == AST_NODE_INDEX_NONE) {
            for (size_t i = 0; i < frame->if_count; i += 1) {
                wasm_write_byte(&builder->writer, WASM_OPCODE_END);
            }
            if (frame->statement != AST_NODE_INDEX_NONE &&
                (builder->nodes[frame->statement].node_type == AST_NODE_TYPE_SCOPE_STATEMENT ||
                 builder->nodes[frame->statement].node_type == AST_NODE_TYPE_IF_STATEMENT)) {
                // The `block` of a scope, or the one around a labeled if statement.
                wasm_write_byte(&builder->writer, WASM_OPCODE_END);
            } else if (frame->statement != AST_NODE_INDEX_NONE) {
                wasm_build_code_section_body_for_end(builder, definition, frame->statement);
            }
            frame_count -= 1;
            continue;
        }

        Ast_Node_Index child = frame->next;
        Ast_Node const *child_node = &builder->nodes[child];
        frame->next = child_node->next_sibling;

        // A label on an if, for or loop statement is a node around it that writes nothing.
        bool is_labeled = child_node->node_type == AST_NODE_TYPE_LABELED_STATEMENT;
        if (is_labeled) {
            child = child_node->first_child;
            child_node = &builder->nodes[child];
        }

        if (frame_node->node_type == AST_NODE_TYPE_IF_STATEMENT) {
            bool is_first = child == frame_node->first_child;
            if (child_node->node_type != AST_NODE_TYPE_BLOCK) {
                // A condition after the first is an `else if`, which is inside the `else` of the
                // branch before it.
                if (is_first == false) {
                    wasm_write_byte(&builder->writer, WASM_OPCODE_ELSE);
                }
                if (wasm_build_code_section_body_expression(builder, definition, child,
                                                            false) == false) {
                    return false;
                }
                // There is no conversion to a `Bool`, so an `Int` or a `Str` is not a condition.
                if (wasm_expression_type(builder, child).kind != WASM_VALUE_TYPE_BOOL) {
                    Str subject = wasm_expression_subject(builder, definition, child);
                    return wasm_fail(builder, WASM_BUILD_STATUS_CONDITION_TYPE_INVALID, subject);
                }
                wasm_structured_begin(builder, WASM_OPCODE_IF);
                frame->if_count += 1;
            } else {
                assert(child_node->node_type == AST_NODE_TYPE_BLOCK);
                // A block that is last, and is not the block of a condition, is the `else`.
                if (child == frame_node->last_child && frame_node->payload.if_statement.has_else) {
                    wasm_write_byte(&builder->writer, WASM_OPCODE_ELSE);
                }
                wasm_frame_push(builder, frames, &frame_count, child, AST_NODE_INDEX_NONE);
            }
        } else if (child_node->node_type == AST_NODE_TYPE_RETURN_STATEMENT) {
            if (wasm_build_code_section_body_return(builder, definition, child) == false) {
                return false;
            }
        } else if (child_node->node_type == AST_NODE_TYPE_CALL_STATEMENT) {
            if (wasm_build_code_section_body_call_statement(builder, definition, child) == false) {
                return false;
            }
        } else if (child_node->node_type == AST_NODE_TYPE_VARIABLE_DEFINITION) {
            if (wasm_build_code_section_body_variable(builder, definition, child) == false) {
                return false;
            }
        } else if (ast_node_type_is_assignment(child_node->node_type)) {
            if (wasm_build_code_section_body_assignment(builder, definition, child) == false) {
                return false;
            }
        } else if (child_node->node_type == AST_NODE_TYPE_IF_STATEMENT) {
            // A labeled if statement is inside a `block`, which a `break` with the label leaves.
            if (is_labeled) {
                wasm_structured_begin(builder, WASM_OPCODE_BLOCK);
            }
            Ast_Node_Index label_owner = is_labeled ? child : AST_NODE_INDEX_NONE;
            wasm_frame_push(builder, frames, &frame_count, child, label_owner);
        } else if (child_node->node_type == AST_NODE_TYPE_BREAK_STATEMENT ||
                   child_node->node_type == AST_NODE_TYPE_CONTINUE_STATEMENT) {
            wasm_build_code_section_body_jump(builder, frames, frame_count, child);
        } else if (child_node->node_type == AST_NODE_TYPE_SCOPE_STATEMENT) {
            // A scope is a `block` that `break` leaves with a `br`.
            wasm_structured_begin(builder, WASM_OPCODE_BLOCK);
            Ast_Node_Index block = child_node->last_child;
            wasm_frame_push(builder, frames, &frame_count, block, child);
        } else if (child_node->node_type == AST_NODE_TYPE_LOOP_STATEMENT) {
            // The same three structured instructions as a for statement has, without a test.
            wasm_structured_begin(builder, WASM_OPCODE_BLOCK);
            wasm_structured_begin(builder, WASM_OPCODE_LOOP);
            wasm_structured_begin(builder, WASM_OPCODE_BLOCK);
            Ast_Node_Index block = child_node->last_child;
            wasm_frame_push(builder, frames, &frame_count, block, child);
        } else if (child_node->node_type == AST_NODE_TYPE_FOR_STATEMENT) {
            if (wasm_build_code_section_body_for(builder, definition, child) == false) {
                return false;
            }
            Ast_Node_Index block = child_node->last_child;
            wasm_frame_push(builder, frames, &frame_count, block, child);
        } else {
            // Parameters come first and are not code.
            assert(child_node->node_type == AST_NODE_TYPE_PARAMETER);
        }
    }

    assert(frame_count == 0);
    return true;
}

/**
 * Writes the start of the for statement `statement`: the first number of the range is stored in the
 * local of the iterator and the end in the local after it, so that the end is read once and the
 * block cannot change it. Then come a `block` that a finished loop leaves, a `loop` that is
 * repeated, and the test that leaves the block when the iterator is past the end, which is when it
 * is not below the end of an exclusive range or above the end of an inclusive one. The statements
 * of the block follow, in a `block` of their own, and then `wasm_build_code_section_body_for_end`.
 * Returns false after it recorded an error.
 */
static bool wasm_build_code_section_body_for(Wasm_Builder *builder, Ast_Node_Index definition,
                                             Ast_Node_Index statement) {
    assert(statement < builder->node_count);
    assert(builder->nodes[statement].node_type == AST_NODE_TYPE_FOR_STATEMENT);

    // The other kind of for statement goes through the elements of an array.
    if (wasm_for_is_each(builder, statement)) {
        return wasm_build_code_section_body_for_each(builder, definition, statement);
    }

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index range = builder->nodes[statement].first_child;
    assert(builder->nodes[range].node_type == AST_NODE_TYPE_RANGE);
    Ast_Node_Index first = builder->nodes[range].first_child;
    Ast_Node_Index end = builder->nodes[range].last_child;

    size_t local_index = wasm_variable_local_index(builder, procedure, statement);

    // Both ends are `Int`, there is no conversion to one.
    Ast_Node_Index ends[2] = {first, end};
    for (size_t i = 0; i < 2; i += 1) {
        if (wasm_build_code_section_body_expression(builder, definition, ends[i], false) == false) {
            return false;
        }
        if (wasm_expression_type(builder, ends[i]).kind != WASM_VALUE_TYPE_INT) {
            Str subject = wasm_expression_subject(builder, definition, ends[i]);
            return wasm_fail(builder, WASM_BUILD_STATUS_RANGE_TYPE_INVALID, subject);
        }
        wasm_write_byte(&builder->writer, WASM_OPCODE_LOCAL_SET);
        wasm_write_unsigned(&builder->writer, local_index + i);
    }

    wasm_structured_begin(builder, WASM_OPCODE_BLOCK);
    wasm_structured_begin(builder, WASM_OPCODE_LOOP);

    wasm_write_byte(&builder->writer, WASM_OPCODE_LOCAL_GET);
    wasm_write_unsigned(&builder->writer, local_index);
    wasm_write_byte(&builder->writer, WASM_OPCODE_LOCAL_GET);
    wasm_write_unsigned(&builder->writer, local_index + 1);
    if (builder->nodes[range].payload.range.is_inclusive) {
        wasm_write_byte(&builder->writer, WASM_OPCODE_I64_GT_S);
    } else {
        wasm_write_byte(&builder->writer, WASM_OPCODE_I64_GE_S);
    }
    // Label 0 is the loop, and label 1 is the block around it, which is left.
    wasm_write_byte(&builder->writer, WASM_OPCODE_BR_IF);
    wasm_write_unsigned(&builder->writer, 1);

    // The statements of the block are in a `block` of their own, which `continue` leaves.
    wasm_structured_begin(builder, WASM_OPCODE_BLOCK);

    return true;
}

/**
 * Writes the end of the for statement or loop statement `statement`, after the statements of its
 * block. A loop statement starts over. For a for statement the iterator goes to the next number and
 * the loop starts over. An inclusive range leaves the loop before the step when the iterator is at
 * the end, so that the step cannot overflow when the end is the largest `Int`.
 */
static void wasm_build_code_section_body_for_end(Wasm_Builder *builder, Ast_Node_Index definition,
                                                 Ast_Node_Index statement) {
    assert(statement < builder->node_count);
    assert(builder->nodes[statement].node_type == AST_NODE_TYPE_FOR_STATEMENT ||
           builder->nodes[statement].node_type == AST_NODE_TYPE_LOOP_STATEMENT);

    // The `block` that `continue` leaves.
    wasm_write_byte(&builder->writer, WASM_OPCODE_END);

    // A for statement over the elements of an array has stepped before its block, see
    // `wasm_build_code_section_body_for_each`, so like a loop statement it has nothing to step.
    if (builder->nodes[statement].node_type == AST_NODE_TYPE_LOOP_STATEMENT ||
        wasm_for_is_each(builder, statement)) {
        // Nothing to step: the block starts over, and only a `break` or a `return` ends it.
        wasm_write_byte(&builder->writer, WASM_OPCODE_BR);
        wasm_write_unsigned(&builder->writer, 0);
        wasm_write_byte(&builder->writer, WASM_OPCODE_END);
        wasm_write_byte(&builder->writer, WASM_OPCODE_END);
        return;
    }

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index range = builder->nodes[statement].first_child;
    size_t local_index = wasm_variable_local_index(builder, procedure, statement);

    if (builder->nodes[range].payload.range.is_inclusive) {
        wasm_write_byte(&builder->writer, WASM_OPCODE_LOCAL_GET);
        wasm_write_unsigned(&builder->writer, local_index);
        wasm_write_byte(&builder->writer, WASM_OPCODE_LOCAL_GET);
        wasm_write_unsigned(&builder->writer, local_index + 1);
        wasm_write_byte(&builder->writer, WASM_OPCODE_I64_EQ);
        wasm_write_byte(&builder->writer, WASM_OPCODE_BR_IF);
        wasm_write_unsigned(&builder->writer, 1);
    }

    wasm_write_byte(&builder->writer, WASM_OPCODE_LOCAL_GET);
    wasm_write_unsigned(&builder->writer, local_index);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I64_CONST);
    wasm_write_signed(&builder->writer, 1);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I64_ADD);
    wasm_write_byte(&builder->writer, WASM_OPCODE_LOCAL_SET);
    wasm_write_unsigned(&builder->writer, local_index);
    wasm_write_byte(&builder->writer, WASM_OPCODE_BR);
    wasm_write_unsigned(&builder->writer, 0);

    // The `loop` and then the `block`.
    wasm_write_byte(&builder->writer, WASM_OPCODE_END);
    wasm_write_byte(&builder->writer, WASM_OPCODE_END);
}

/**
 * Returns the number of locals that `variable` takes: the ones of its type for a variable
 * definition, none when it is in the frame, and for the iterator of a for statement two, which also
 * keeps the end of its range, or the ones of an element and two more when it goes through the
 * elements of an array, which keeps where it is and where it ends.
 */
static size_t wasm_variable_slot_count(Wasm_Builder const *builder, Ast_Node_Index variable) {
    assert(variable < builder->node_count);

    // A variable in the frame is in memory, and takes no local.
    if (wasm_variable_is_in_memory(builder, variable)) {
        return 0;
    }
    if (builder->nodes[variable].node_type == AST_NODE_TYPE_FOR_STATEMENT) {
        if (wasm_for_is_each(builder, variable)) {
            return wasm_type_slot_count(builder, wasm_variable_type(builder, variable)) + 2;
        }
        return 2;
    }
    return wasm_type_slot_count(builder, wasm_variable_type(builder, variable));
}

/**
 * Writes the `br` of the `break` or `continue` statement `jump`. A loop is written as a `block`
 * that holds a `loop` that holds a second `block`, around the statements of its block, and a scope
 * as a `block`. `continue` leaves the inner `block` of a loop, which is followed by what the loop
 * does after every pass, and `break` leaves the outer one, or the `block` of a scope. The label is
 * the number of structured instructions between the jump and the target, which the analysis bound
 * the jump to: going down the frames from the jump, each `if` that is open counts one, a scope
 * that is passed counts one, and a loop that is passed counts three, until the frame of the target.
 * Then the inner `block` and the `loop` are counted for a `break` out of a loop.
 */
static void wasm_build_code_section_body_jump(Wasm_Builder *builder,
                                              Wasm_Statement_Frame const *frames,
                                              size_t frame_count, Ast_Node_Index jump) {
    assert(frame_count > 0);
    assert(builder->nodes[jump].node_type == AST_NODE_TYPE_BREAK_STATEMENT ||
           builder->nodes[jump].node_type == AST_NODE_TYPE_CONTINUE_STATEMENT);
    assert(builder->bindings[jump].kind == SEMANTIC_BINDING_KIND_JUMP_TARGET);

    Ast_Node_Index target = builder->bindings[jump].target;

    size_t label = 0;
    size_t i = frame_count;
    // The analysis checked that the target is around the jump, so it is found before the bottom.
    while (i > 0 && frames[i - 1].statement != target) {
        label += frames[i - 1].if_count;
        if (frames[i - 1].statement != AST_NODE_INDEX_NONE) {
            // A scope and a labeled if statement are one `block`, and a loop is three.
            Ast_Node_Type passed = builder->nodes[frames[i - 1].statement].node_type;
            bool is_loop_passed = passed == AST_NODE_TYPE_FOR_STATEMENT ||
                                  passed == AST_NODE_TYPE_LOOP_STATEMENT;
            label += is_loop_passed ? 3 : 1;
        }
        i -= 1;
    }
    assert(i > 0);

    // The frame of a labeled if statement counts its open `if` instructions, which are inside the
    // `block` that is left.
    if (builder->nodes[target].node_type == AST_NODE_TYPE_IF_STATEMENT) {
        label += frames[i - 1].if_count;
    }

    bool is_loop = builder->nodes[target].node_type == AST_NODE_TYPE_FOR_STATEMENT ||
                   builder->nodes[target].node_type == AST_NODE_TYPE_LOOP_STATEMENT;
    if (is_loop && builder->nodes[jump].node_type == AST_NODE_TYPE_BREAK_STATEMENT) {
        label += 2;
    }
    wasm_write_byte(&builder->writer, WASM_OPCODE_BR);
    wasm_write_unsigned(&builder->writer, label);
}

/**
 * Writes the instructions of a call that is a statement: the call, and then a drop for each value
 * that it leaves, because nobody uses it. Returns false after it recorded an error.
 */
static bool wasm_build_code_section_body_call_statement(Wasm_Builder *builder,
                                                        Ast_Node_Index definition,
                                                        Ast_Node_Index statement) {
    assert(statement < builder->node_count);
    assert(builder->nodes[statement].node_type == AST_NODE_TYPE_CALL_STATEMENT);

    Ast_Node_Index call = builder->nodes[statement].first_child;
    if (wasm_build_code_section_body_expression(builder, definition, call, true) == false) {
        return false;
    }

    Wasm_Type value_type = wasm_expression_type(builder, call);
    if (value_type.kind != WASM_VALUE_TYPE_NONE) {
        size_t slot_count = wasm_type_slot_count(builder, value_type);
        for (size_t slot = 0; slot < slot_count; slot += 1) {
            wasm_write_byte(&builder->writer, WASM_OPCODE_DROP);
        }
    }
    return true;
}

/**
 * Checks the call `call`, whose arguments were written already: there must be as many arguments as
 * the procedure has parameters, each of the type of its parameter, and the procedure must return a
 * value unless `is_result_unused`. Returns false after it recorded an error.
 */
static bool wasm_build_code_section_body_call(Wasm_Builder *builder, Ast_Node_Index call,
                                              bool is_result_unused) {
    assert(call < builder->node_count);
    assert(builder->nodes[call].node_type == AST_NODE_TYPE_CALL);

    Str name = builder->nodes[call].payload.call.name;

    // The procedure is known by its name, or by the type of the pointer to it that the name stands
    // for. A name that is not a pointer to a procedure has none.
    Ast_Node_Index callee = wasm_call_signature(builder, call);
    if (callee == AST_NODE_INDEX_NONE) {
        return wasm_fail(builder, WASM_BUILD_STATUS_CALL_TARGET_NOT_PROCEDURE, name);
    }

    size_t argument_count = wasm_children_count(builder, call);
    if (argument_count != wasm_signature_parameter_count(builder, callee)) {
        return wasm_fail(builder, WASM_BUILD_STATUS_CALL_ARGUMENT_COUNT_MISMATCH, name);
    }

    Ast_Node_Index argument = builder->nodes[call].first_child;
    for (size_t i = 0; i < argument_count; i += 1) {
        assert(argument != AST_NODE_INDEX_NONE);

        Ast_Node_Index type = wasm_signature_parameter_type(builder, callee, i);
        Wasm_Type parameter_type = wasm_type_of_node(builder, type);
        // An array is passed as an array slice, see `wasm_array_decay`.
        if (wasm_type_accepts(parameter_type, wasm_expression_type(builder, argument)) == false) {
            return wasm_fail(builder, WASM_BUILD_STATUS_CALL_ARGUMENT_TYPE_MISMATCH, name);
        }
        argument = builder->nodes[argument].next_sibling;
    }

    bool has_result = ast_procedure_return_type(&builder->nodes[callee]).bytes != NULL;
    if (has_result == false && is_result_unused == false) {
        return wasm_fail(builder, WASM_BUILD_STATUS_CALL_RESULT_MISSING, name);
    }
    return true;
}

/**
 * Writes the instructions of a `return`: the value, and the `return` instruction. The value must
 * be of the return type of the procedure that `definition` is, which must have one. Returns false
 * after it recorded an error.
 */
static bool wasm_build_code_section_body_return(Wasm_Builder *builder, Ast_Node_Index definition,
                                                Ast_Node_Index statement) {
    assert(statement < builder->node_count);
    assert(builder->nodes[statement].node_type == AST_NODE_TYPE_RETURN_STATEMENT);

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    if (builder->nodes[procedure].payload.procedure.return_type.bytes == NULL) {
        Str name = builder->nodes[definition].payload.constant_definition.name;
        return wasm_fail(builder, WASM_BUILD_STATUS_RETURN_TYPE_MISSING, name);
    }

    Ast_Node_Index value = builder->nodes[statement].first_child;
    if (wasm_build_code_section_body_expression(builder, definition, value, false) == false) {
        return false;
    }

    // The value on the stack must be what the procedure says that it returns. The expression was
    // written already, so every name in it is known.
    Str return_type_name = builder->nodes[procedure].payload.procedure.return_type;
    Wasm_Type return_type = wasm_type_of_name(builder, return_type_name);
    if (wasm_type_equals(wasm_expression_type(builder, value), return_type) == false) {
        Str name = builder->nodes[definition].payload.constant_definition.name;
        return wasm_fail(builder, WASM_BUILD_STATUS_RETURN_TYPE_MISMATCH, name);
    }

    // The value is on the stack already, and giving the frame back does not touch it.
    wasm_frame_leave(builder, procedure);
    wasm_write_byte(&builder->writer, WASM_OPCODE_RETURN);
    return true;
}

/**
 * Writes the instructions of `assignment`. What is assigned to decides how: a variable is stored in
 * its locals, a field of a variable too, in the locals that it takes, and an element of an array, a
 * field of an element, a range of elements, what a pointer points to and a variable whose address
 * is taken are in memory. Returns false after it recorded an error.
 */
static bool wasm_build_code_section_body_assignment(Wasm_Builder *builder,
                                                    Ast_Node_Index definition,
                                                    Ast_Node_Index assignment) {
    assert(assignment < builder->node_count);
    assert(ast_node_type_is_assignment(builder->nodes[assignment].node_type));

    Ast_Node_Index target = builder->nodes[assignment].first_child;
    Ast_Node_Type target_type = builder->nodes[target].node_type;
    if (target_type == AST_NODE_TYPE_IDENTIFIER) {
        return wasm_build_code_section_body_variable_assignment(builder, definition, assignment);
    }
    if (target_type == AST_NODE_TYPE_FIELD_ACCESS) {
        return wasm_build_code_section_body_field_assignment(builder, definition, assignment);
    }
    if (target_type == AST_NODE_TYPE_DEREFERENCE) {
        return wasm_build_code_section_body_dereference_assignment(builder, definition,
                                                                   assignment);
    }
    return wasm_build_code_section_body_element_assignment(builder, definition, assignment);
}

/**
 * Writes the instructions of an assignment to a variable. The plain `=` stores the value in the
 * locals of the variable, or in the globals when it is a global variable, with the last one first
 * because the last value is on top of the stack. For `x += 2` and the others the value of the
 * variable is pushed, then the value of the assignment, then the operator is applied to them, and
 * the result is stored. Only an `Int` can be assigned that way, so a variable of any other type,
 * or a value of any other type, is rejected. Returns false after it recorded an error.
 */
static bool wasm_build_code_section_body_variable_assignment(Wasm_Builder *builder,
                                                             Ast_Node_Index definition,
                                                             Ast_Node_Index assignment) {
    assert(assignment < builder->node_count);

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index target = builder->nodes[assignment].first_child;
    Ast_Node_Index value = builder->nodes[assignment].last_child;

    // The analysis checked that the name stands for a variable or a global variable. A global is
    // read and stored with the global instructions, and a variable with the local ones.
    Semantic_Binding binding = builder->bindings[target];
    assert(binding.kind == SEMANTIC_BINDING_KIND_VARIABLE ||
           binding.kind == SEMANTIC_BINDING_KIND_GLOBAL);
    bool is_global = binding.kind == SEMANTIC_BINDING_KIND_GLOBAL;
    Str target_name = builder->nodes[target].payload.identifier.name;
    Wasm_Type variable_type = wasm_binding_value_type(builder, binding);

    // A variable whose address is taken is in the frame. Its address is the frame plus its offset,
    // and from there it is a place in memory like an element is.
    if (is_global == false && wasm_variable_is_in_memory(builder, binding.target)) {
        wasm_write_local_instruction(
            &builder->writer, WASM_OPCODE_LOCAL_GET,
            wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_FRAME));
        wasm_write_local_instruction(
            &builder->writer, WASM_OPCODE_LOCAL_SET,
            wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_ADDRESS));
        return wasm_build_code_section_body_place_store(
            builder, definition, assignment, variable_type,
            wasm_variable_frame_offset(builder, procedure, binding.target));
    }

    size_t storage_index = 0;
    if (is_global) {
        storage_index = wasm_global_slot_index(builder, binding.target);
    } else {
        storage_index = wasm_variable_local_index(builder, procedure, binding.target);
    }

    if (builder->nodes[assignment].node_type == AST_NODE_TYPE_ASSIGNMENT) {
        // The address of an array is where its elements are, and an array is never given another.
        if (variable_type.kind == WASM_VALUE_TYPE_ARRAY) {
            return wasm_fail(builder, WASM_BUILD_STATUS_VARIABLE_TYPE_MISMATCH, target_name);
        }
        if (wasm_build_code_section_body_expression(builder, definition, value, false) == false) {
            return false;
        }
        if (wasm_type_accepts(variable_type, wasm_expression_type(builder, value)) == false) {
            return wasm_fail(builder, WASM_BUILD_STATUS_VARIABLE_TYPE_MISMATCH, target_name);
        }
        if (variable_type.kind == WASM_VALUE_TYPE_SLICE) {
            wasm_array_decay(builder, value);
        }

        byte opcode = WASM_OPCODE_LOCAL_SET;
        if (is_global) {
            opcode = WASM_OPCODE_GLOBAL_SET;
        }
        wasm_slots_set(builder, variable_type, opcode, storage_index);
        return true;
    }

    if (variable_type.kind != WASM_VALUE_TYPE_INT) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID, target_name);
    }

    if (is_global) {
        wasm_write_byte(&builder->writer, WASM_OPCODE_GLOBAL_GET);
    } else {
        wasm_write_byte(&builder->writer, WASM_OPCODE_LOCAL_GET);
    }
    wasm_write_unsigned(&builder->writer, storage_index);

    if (wasm_build_code_section_body_expression(builder, definition, value, false) == false) {
        return false;
    }

    if (wasm_expression_type(builder, value).kind != WASM_VALUE_TYPE_INT) {
        Str subject = wasm_expression_subject(builder, definition, value);
        return wasm_fail(builder, WASM_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID, subject);
    }

    // The operator of the assignment is applied to the variable and the value.
    wasm_write_byte(&builder->writer, wasm_assignment_opcode(builder->nodes[assignment].node_type));
    if (is_global) {
        wasm_write_byte(&builder->writer, WASM_OPCODE_GLOBAL_SET);
    } else {
        wasm_write_byte(&builder->writer, WASM_OPCODE_LOCAL_SET);
    }
    wasm_write_unsigned(&builder->writer, storage_index);

    return true;
}

/**
 * Writes the instructions of an assignment to a field, such as `p.age = 31` or `p.age += 1`. The
 * field of a variable is in the locals of the variable, so it is stored there, unless the address
 * of the variable is taken, which puts it in the frame. The field of an element of an array, such
 * as `persons[0].age = 31`, and of what a pointer points to, such as `q^.age = 31`, is in memory,
 * at the address of the element or the pointer plus the offset of the field. Its address is
 * computed first and kept in a local while the value is evaluated, see
 * `wasm_build_code_section_body_place_store`. Returns false after it recorded an error.
 */
static bool wasm_build_code_section_body_field_assignment(Wasm_Builder *builder,
                                                          Ast_Node_Index definition,
                                                          Ast_Node_Index assignment) {
    assert(assignment < builder->node_count);

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index target = builder->nodes[assignment].first_child;
    Ast_Node_Index value = builder->nodes[assignment].last_child;
    bool is_plain = builder->nodes[assignment].node_type == AST_NODE_TYPE_ASSIGNMENT;

    Wasm_Field_Path path = wasm_field_path(builder, target);
    if (path.status != WASM_BUILD_STATUS_OK) {
        return wasm_fail(builder, path.status, path.subject);
    }
    Str field_name = builder->nodes[target].payload.field_access.name;

    // The field is of what a pointer points to, as in `p.age = 31` and `p^.age = 31`, which is in
    // memory at the address that the pointer is, plus the offset of the field. The pointer is not
    // changed, so it can be a parameter.
    if (path.is_through_pointer) {
        if (wasm_build_code_section_body_pointer(builder, definition, path.root, true) == false) {
            return false;
        }
        wasm_write_local_instruction(
            &builder->writer, WASM_OPCODE_LOCAL_SET,
            wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_ADDRESS));
        return wasm_build_code_section_body_place_store(
            builder, definition, assignment, path.type,
            path.slot_offset * WASM_ELEMENT_SIZE_BYTES);
    }

    if (builder->nodes[path.root].node_type == AST_NODE_TYPE_IDENTIFIER) {
        // A field is assigned through a variable only: a parameter and the iterator of a for
        // statement cannot be assigned to, and so neither can their fields. The analysis cannot
        // tell, because a field of a pointer is not a field of the pointer, so it is told here.
        Semantic_Binding binding = builder->bindings[path.root];
        if (binding.kind != SEMANTIC_BINDING_KIND_VARIABLE ||
            builder->nodes[binding.target].node_type != AST_NODE_TYPE_VARIABLE_DEFINITION) {
            return wasm_fail(builder, WASM_BUILD_STATUS_FIELD_TARGET_READ_ONLY,
                             builder->nodes[path.root].payload.identifier.name);
        }

        // A variable whose address is taken is in the frame, and its field is in memory.
        if (wasm_variable_is_in_memory(builder, binding.target)) {
            wasm_write_local_instruction(
                &builder->writer, WASM_OPCODE_LOCAL_GET,
                wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_FRAME));
            wasm_write_local_instruction(
                &builder->writer, WASM_OPCODE_LOCAL_SET,
                wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_ADDRESS));
            return wasm_build_code_section_body_place_store(
                builder, definition, assignment, path.type,
                wasm_variable_frame_offset(builder, procedure, binding.target) +
                    path.slot_offset * WASM_ELEMENT_SIZE_BYTES);
        }
        size_t first = wasm_variable_local_index(builder, procedure, binding.target) +
                       path.slot_offset;

        if (is_plain) {
            if (wasm_build_code_section_body_expression(builder, definition, value, false) ==
                false) {
                return false;
            }
            if (wasm_type_equals(path.type, wasm_expression_type(builder, value)) == false) {
                return wasm_fail(builder, WASM_BUILD_STATUS_STRUCT_FIELD_TYPE_MISMATCH,
                                 field_name);
            }
            wasm_slots_set(builder, path.type, WASM_OPCODE_LOCAL_SET, first);
            return true;
        }

        if (path.type.kind != WASM_VALUE_TYPE_INT) {
            return wasm_fail(builder, WASM_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID,
                             field_name);
        }
        wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, first);
        if (wasm_build_code_section_body_expression(builder, definition, value, false) == false) {
            return false;
        }
        if (wasm_expression_type(builder, value).kind != WASM_VALUE_TYPE_INT) {
            return wasm_fail(builder, WASM_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID,
                             wasm_expression_subject(builder, definition, value));
        }
        wasm_write_byte(&builder->writer,
                        wasm_assignment_opcode(builder->nodes[assignment].node_type));
        wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_SET, first);
        return true;
    }

    // The field is of an element, which is in memory.
    assert(builder->nodes[path.root].node_type == AST_NODE_TYPE_SUBSCRIPT);
    Ast_Node_Index base = builder->nodes[path.root].first_child;
    Ast_Node_Index index = builder->nodes[path.root].last_child;
    if (wasm_build_code_section_body_expression(builder, definition, base, false) == false) {
        return false;
    }
    Wasm_Type base_type = wasm_expression_type(builder, base);
    if (wasm_type_is_sequence(base_type) == false) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_EXPECTED,
                         wasm_expression_subject(builder, definition, base));
    }
    wasm_array_decay(builder, base);

    if (wasm_build_code_section_body_expression(builder, definition, index, false) == false) {
        return false;
    }
    if (wasm_expression_type(builder, index).kind != WASM_VALUE_TYPE_INT) {
        return wasm_fail(builder, WASM_BUILD_STATUS_INDEX_TYPE_INVALID,
                         wasm_expression_subject(builder, definition, index));
    }
    wasm_element_address_write(builder, procedure,
                               wasm_type_stride_bytes(builder, wasm_type_element(base_type)));
    wasm_write_local_instruction(
        &builder->writer, WASM_OPCODE_LOCAL_SET,
        wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_ADDRESS));

    return wasm_build_code_section_body_place_store(builder, definition, assignment, path.type,
                                                    path.slot_offset * WASM_ELEMENT_SIZE_BYTES);
}

/**
 * Writes the rest of an assignment to a place in memory, whose address is in the scratch local
 * `WASM_SCRATCH_ADDRESS` already, plus `offset_bytes`, and which holds a value of `type`. The
 * plain `=` evaluates the value and stores its slots. For `+=` and the others the address is used
 * by the load and by the store, so it is pushed for each, and the type must be an `Int`. The
 * scratch local is not used by the evaluation of a value, which is why the address is kept there.
 * Returns false after it recorded an error.
 */
static bool wasm_build_code_section_body_place_store(Wasm_Builder *builder,
                                                     Ast_Node_Index definition,
                                                     Ast_Node_Index assignment, Wasm_Type type,
                                                     size_t offset_bytes) {
    assert(assignment < builder->node_count);

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index target = builder->nodes[assignment].first_child;
    Ast_Node_Index value = builder->nodes[assignment].last_child;
    bool is_plain = builder->nodes[assignment].node_type == AST_NODE_TYPE_ASSIGNMENT;
    size_t address = wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_ADDRESS);

    if (is_plain) {
        if (wasm_build_code_section_body_expression(builder, definition, value, false) == false) {
            return false;
        }
        if (wasm_type_equals(type, wasm_expression_type(builder, value)) == false) {
            if (builder->nodes[target].node_type == AST_NODE_TYPE_FIELD_ACCESS) {
                return wasm_fail(builder, WASM_BUILD_STATUS_STRUCT_FIELD_TYPE_MISMATCH,
                                 builder->nodes[target].payload.field_access.name);
            }
            if (builder->nodes[target].node_type == AST_NODE_TYPE_IDENTIFIER) {
                return wasm_fail(builder, WASM_BUILD_STATUS_VARIABLE_TYPE_MISMATCH,
                                 builder->nodes[target].payload.identifier.name);
            }
            if (builder->nodes[target].node_type == AST_NODE_TYPE_DEREFERENCE) {
                return wasm_fail(builder, WASM_BUILD_STATUS_POINTER_TARGET_TYPE_MISMATCH,
                                 wasm_expression_subject(builder, definition, target));
            }
            return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_ELEMENT_TYPE_MISMATCH,
                             wasm_expression_subject(builder, definition, value));
        }
        wasm_slots_store(builder, procedure, type, address, offset_bytes);
        return true;
    }

    if (type.kind != WASM_VALUE_TYPE_INT) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID,
                         wasm_expression_subject(builder, definition, target));
    }
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, address);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, address);
    wasm_write_memory_instruction(&builder->writer, WASM_OPCODE_I64_LOAD, offset_bytes);

    if (wasm_build_code_section_body_expression(builder, definition, value, false) == false) {
        return false;
    }
    if (wasm_expression_type(builder, value).kind != WASM_VALUE_TYPE_INT) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID,
                         wasm_expression_subject(builder, definition, value));
    }
    wasm_write_byte(&builder->writer,
                    wasm_assignment_opcode(builder->nodes[assignment].node_type));
    wasm_write_memory_instruction(&builder->writer, WASM_OPCODE_I64_STORE, offset_bytes);
    return true;
}

/**
 * Writes the instructions of the definition of `variable`: the initial value is left on the stack
 * and then stored in the locals of the variable. The last local is stored first, because the
 * value of a `Str` has its length on top of the stack. A definition that has a type and no value
 * gets the default value of the type.
 */
static bool wasm_build_code_section_body_variable(Wasm_Builder *builder,
                                                  Ast_Node_Index definition,
                                                  Ast_Node_Index variable) {
    assert(variable < builder->node_count);
    assert(builder->nodes[variable].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION);

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index declared = builder->nodes[variable].first_child;
    Ast_Node_Index value = builder->nodes[variable].last_child;
    Wasm_Type variable_type = wasm_variable_type(builder, variable);

    if (declared == value && wasm_node_is_type(builder, declared)) {
        // The check of the variables rejected an array without a value.
        wasm_default_write(builder, variable_type);
    } else {
        if (wasm_build_code_section_body_expression(builder, definition, value, false) == false) {
            return false;
        }

        // The value was written, so every name in it is known. It must be of the type of the
        // variable, which is the type that was written when the definition has one, and deduced
        // otherwise.
        if (wasm_type_accepts(variable_type, wasm_expression_type(builder, value)) == false) {
            Str name = builder->nodes[variable].payload.variable_definition.name;
            return wasm_fail(builder, WASM_BUILD_STATUS_VARIABLE_TYPE_MISMATCH, name);
        }

        // An array whose type gives the length must have exactly that many elements.
        if (variable_type.kind == WASM_VALUE_TYPE_ARRAY &&
            builder->nodes[declared].node_type == AST_NODE_TYPE_ARRAY_TYPE &&
            builder->nodes[declared].payload.array_type.length_kind == AST_ARRAY_LENGTH_FIXED &&
            (size_t)builder->nodes[declared].payload.array_type.length !=
                wasm_array_length(builder, value)) {
            return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_LENGTH_MISMATCH,
                             wasm_type_subject(builder, declared));
        }

        // A slice is given an array by viewing all of its elements.
        if (variable_type.kind == WASM_VALUE_TYPE_SLICE) {
            wasm_array_decay(builder, value);
        }
    }

    // A variable whose address is taken is stored in the frame, where the address points to.
    if (wasm_variable_is_in_memory(builder, variable)) {
        wasm_slots_store(builder, procedure, variable_type,
                         wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_FRAME),
                         wasm_variable_frame_offset(builder, procedure, variable));
        return true;
    }

    wasm_slots_set(builder, variable_type, WASM_OPCODE_LOCAL_SET,
                   wasm_variable_local_index(builder, procedure, variable));
    return true;
}

/**
 * Checks the type of every variable of `procedure`, also the ones in its blocks, so that what
 * comes after can ask for the type of a variable and assume that it is known. A variable has the
 * type that its definition gives, or else the type of its value. A variable of an array type
 * must be given an array literal, and a for statement that goes through elements must have an
 * array or an array slice to go through. Returns false after it recorded an error.
 */
static bool wasm_variables_check(Wasm_Builder *builder, Ast_Node_Index definition,
                                 Ast_Node_Index procedure) {
    assert(procedure < builder->node_count);
    assert(builder->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);

    Ast_Node_Index end = ast_procedure_end(builder->nodes, builder->node_count, procedure);
    for (Ast_Node_Index statement = procedure + 1; statement < end; statement += 1) {
        Ast_Node const *statement_node = &builder->nodes[statement];

        if (statement_node->node_type == AST_NODE_TYPE_FOR_STATEMENT &&
            wasm_for_is_each(builder, statement)) {
            Ast_Node_Index elements = statement_node->first_child;
            Wasm_Type sequence = wasm_expression_type(builder, elements);
            if (sequence.kind == WASM_VALUE_TYPE_NONE) {
                return wasm_expression_fail(builder, definition, elements);
            }
            if (wasm_type_is_sequence(sequence) == false) {
                return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_EXPECTED,
                                 wasm_expression_subject(builder, definition, elements));
            }
        }

        if (statement_node->node_type == AST_NODE_TYPE_VARIABLE_DEFINITION) {
            Ast_Node_Index first = statement_node->first_child;
            Ast_Node_Index value = statement_node->last_child;
            // A definition with a type can leave out the value, and then the type is the only
            // child.
            bool has_value = wasm_node_is_type(builder, first) == false || first != value;

            if (wasm_node_is_type(builder, first)) {
                if (wasm_type_of_node(builder, first).kind == WASM_VALUE_TYPE_NONE) {
                    return wasm_fail(builder, WASM_BUILD_STATUS_TYPE_UNSUPPORTED,
                                     wasm_type_subject(builder, first));
                }
            } else if (wasm_variable_type(builder, statement).kind == WASM_VALUE_TYPE_NONE) {
                return wasm_expression_fail(builder, definition, value);
            }

            // The elements of an array are put in the frame of the procedure by the array literal
            // that makes them, so an array variable is made by one, and is not a copy of another.
            if (wasm_variable_type(builder, statement).kind == WASM_VALUE_TYPE_ARRAY &&
                (has_value == false ||
                 builder->nodes[value].node_type != AST_NODE_TYPE_ARRAY_LITERAL)) {
                return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_INITIALIZER_INVALID,
                                 statement_node->payload.variable_definition.name);
            }

            // `nil` is a pointer of any type, so it does not say the type of a variable. The same
            // is true of the member of an enum that is written without the enum.
            if (wasm_variable_type(builder, statement).kind == WASM_VALUE_TYPE_NIL) {
                return wasm_fail(builder, WASM_BUILD_STATUS_NIL_TYPE_UNKNOWN,
                                 statement_node->payload.variable_definition.name);
            }
            if (wasm_variable_type(builder, statement).kind == WASM_VALUE_TYPE_ENUM_LITERAL) {
                return wasm_fail(builder, WASM_BUILD_STATUS_ENUM_TYPE_UNKNOWN,
                                 builder->nodes[value].payload.enum_literal.name);
            }

            // There is no pointer to an array or to an array slice, and a variable in the frame is
            // one that has a pointer to it. An array already is the address of its elements.
            Wasm_Type variable_type = wasm_variable_type(builder, statement);
            if (wasm_variable_is_in_memory(builder, statement) &&
                wasm_type_pointer_to(variable_type).kind == WASM_VALUE_TYPE_NONE) {
                return wasm_fail(builder, WASM_BUILD_STATUS_ADDRESS_INVALID,
                                 statement_node->payload.variable_definition.name);
            }
        }
    }

    // Every array literal has its place in the frame, which must fit in the stack. Each element is
    // a node, so this takes a source that is far larger than the buffers of the compiler allow.
    if (wasm_frame_size_bytes(builder, procedure) > WASM_STACK_SIZE_BYTES) {
        return wasm_fail(builder, WASM_BUILD_STATUS_OUTPUT_TOO_LARGE, STR_EMPTY);
    }

    return true;
}

/**
 * Records the error for the expression `expression`, which has no type that the target has, and
 * returns false. What is wrong depends on what the expression is: a call of a procedure that
 * returns nothing, a name that is a procedure or a struct, a field that is not there, a struct that
 * is not defined, or something that is indexed and is not an array.
 */
static bool wasm_expression_fail(Wasm_Builder *builder, Ast_Node_Index definition,
                                 Ast_Node_Index expression) {
    assert(expression < builder->node_count);

    Ast_Node const *node = &builder->nodes[expression];
    if (node->node_type == AST_NODE_TYPE_CALL) {
        if (wasm_call_signature(builder, expression) == AST_NODE_INDEX_NONE) {
            return wasm_fail(builder, WASM_BUILD_STATUS_CALL_TARGET_NOT_PROCEDURE,
                             node->payload.call.name);
        }
        return wasm_fail(builder, WASM_BUILD_STATUS_CALL_RESULT_MISSING, node->payload.call.name);
    }
    if (node->node_type == AST_NODE_TYPE_IDENTIFIER) {
        return wasm_fail(builder, WASM_BUILD_STATUS_IDENTIFIER_NOT_VALUE,
                         node->payload.identifier.name);
    }
    if (node->node_type == AST_NODE_TYPE_FIELD_ACCESS) {
        Wasm_Field_Path path = wasm_field_path(builder, expression);
        if (path.status != WASM_BUILD_STATUS_OK) {
            return wasm_fail(builder, path.status, path.subject);
        }
    }
    if (node->node_type == AST_NODE_TYPE_STRUCT_LITERAL &&
        node->payload.struct_literal.name.bytes != NULL) {
        return wasm_fail(builder, WASM_BUILD_STATUS_TYPE_UNSUPPORTED,
                         node->payload.struct_literal.name);
    }
    if (node->node_type == AST_NODE_TYPE_SUBSCRIPT || node->node_type == AST_NODE_TYPE_SLICE) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_EXPECTED,
                         wasm_expression_subject(builder, definition, expression));
    }
    if (node->node_type == AST_NODE_TYPE_DEREFERENCE) {
        return wasm_fail(builder, WASM_BUILD_STATUS_POINTER_EXPECTED,
                         wasm_expression_subject(builder, definition, expression));
    }
    if (node->node_type == AST_NODE_TYPE_ADDRESS_OF) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ADDRESS_INVALID,
                         wasm_expression_subject(builder, definition, expression));
    }

    return wasm_fail(builder, WASM_BUILD_STATUS_TYPE_UNSUPPORTED,
                     wasm_expression_subject(builder, definition, expression));
}

/**
 * Writes the locals of the variables of `procedure`, which come after the parameters: a count of
 * groups, and for each variable the number of locals it takes and their type, where neighbors of
 * one type are one group. The variables of the blocks are included, in the order of the source: a
 * variable has its own local, also when another variable in another block has the same name. The
 * types must have been checked by `wasm_variables_check`. A procedure that uses arrays has the
 * locals of `Wasm_Scratch` after the variables.
 */
static void wasm_variables_write_locals(Wasm_Builder *builder, Ast_Node_Index procedure) {
    assert(procedure < builder->node_count);

    Ast_Node_Index end = ast_procedure_end(builder->nodes, builder->node_count, procedure);

    bool uses_arrays = wasm_procedure_uses_arrays(builder, procedure);
    size_t group_count = 0;
    for (Ast_Node_Index statement = procedure + 1; statement < end; statement += 1) {
        if (wasm_variable_is_in_memory(builder, statement)) {
            // A variable in the frame is in memory, and has no locals.
            continue;
        }
        if (builder->nodes[statement].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION) {
            group_count += wasm_type_group_count(builder, wasm_variable_type(builder, statement));
        } else if (builder->nodes[statement].node_type == AST_NODE_TYPE_FOR_STATEMENT) {
            if (wasm_for_is_each(builder, statement)) {
                // The iterator, and then the address of the next element and the address after
                // the last one.
                Wasm_Type element = wasm_variable_type(builder, statement);
                group_count += wasm_type_group_count(builder, element) + 1;
            } else {
                group_count += 1;
            }
        }
    }
    if (uses_arrays) {
        group_count += 2;
    }
    wasm_write_unsigned(&builder->writer, group_count);

    for (Ast_Node_Index statement = procedure + 1; statement < end; statement += 1) {
        if (wasm_variable_is_in_memory(builder, statement)) {
            continue;
        }
        if (builder->nodes[statement].node_type == AST_NODE_TYPE_FOR_STATEMENT) {
            if (wasm_for_is_each(builder, statement)) {
                wasm_write_type_groups(builder, wasm_variable_type(builder, statement));
                wasm_write_unsigned(&builder->writer, 2);
                wasm_write_byte(&builder->writer, WASM_TYPE_I32);
            } else {
                // The iterator and the end of the range, which are both `Int`.
                wasm_write_unsigned(&builder->writer, 2);
                wasm_write_byte(&builder->writer, WASM_TYPE_I64);
            }
        } else if (builder->nodes[statement].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION) {
            wasm_write_type_groups(builder, wasm_variable_type(builder, statement));
        }
    }

    // The order of `Wasm_Scratch`: three 64-bit locals, and then five 32-bit ones.
    if (uses_arrays) {
        wasm_write_unsigned(&builder->writer, 3);
        wasm_write_byte(&builder->writer, WASM_TYPE_I64);
        wasm_write_unsigned(&builder->writer, 5);
        wasm_write_byte(&builder->writer, WASM_TYPE_I32);
    }
}

/**
 * Returns the type of `variable`: the one its definition gives, or else the type of its value. A
 * name as the value has the type of what it stands for, so the search follows a chain of variables
 * that were defined from each other, which ends because a variable can only use the ones above
 * it. The iterator of a for statement is an `Int`, or an element of what the statement goes
 * through. Returns a type of the kind `WASM_VALUE_TYPE_NONE` when the type is not one that the
 * target has, or the value is a procedure.
 */
static Wasm_Type wasm_variable_type(Wasm_Builder const *builder, Ast_Node_Index variable) {
    assert(variable < builder->node_count);
    assert(builder->nodes[variable].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION ||
           builder->nodes[variable].node_type == AST_NODE_TYPE_FOR_STATEMENT);

    Ast_Node_Index current = variable;
    // Each step goes to a variable above, so there are fewer steps than nodes.
    for (size_t i = 0; i < builder->node_count; i += 1) {
        Ast_Node const *current_node = &builder->nodes[current];
        if (current_node->node_type == AST_NODE_TYPE_FOR_STATEMENT) {
            Ast_Node_Index first = current_node->first_child;
            if (builder->nodes[first].node_type == AST_NODE_TYPE_RANGE) {
                // The iterator counts numbers.
                return wasm_type_make(WASM_VALUE_TYPE_INT);
            }
            Wasm_Type sequence = wasm_expression_type(builder, first);
            if (wasm_type_is_sequence(sequence) == false) {
                return wasm_type_make(WASM_VALUE_TYPE_NONE);
            }
            return wasm_type_element(sequence);
        }
        if (wasm_node_is_type(builder, current_node->first_child)) {
            return wasm_type_of_node(builder, current_node->first_child);
        }

        Ast_Node_Index value = current_node->last_child;
        Ast_Node const *value_node = &builder->nodes[value];
        if (value_node->node_type != AST_NODE_TYPE_IDENTIFIER) {
            return wasm_expression_type(builder, value);
        }

        Semantic_Binding binding = builder->bindings[value];
        if (binding.kind != SEMANTIC_BINDING_KIND_VARIABLE) {
            return wasm_binding_value_type(builder, binding);
        }
        current = binding.target;
    }

    // Unreachable: a chain of variables is shorter than the number of nodes.
    assert(false);
    return wasm_type_make(WASM_VALUE_TYPE_NONE);
}

/**
 * Returns the index of the first local that holds `variable`. The locals of a function are its
 * parameters, then its variables, in the order of the source, so that is the number of locals of
 * the parameters and of the variables above it, also the ones in blocks. The types of the
 * parameters and variables must be known.
 */
static size_t wasm_variable_local_index(Wasm_Builder const *builder, Ast_Node_Index procedure,
                                        Ast_Node_Index variable) {
    assert(procedure < builder->node_count);
    assert(variable < builder->node_count);
    assert(variable > procedure);

    size_t local_index = wasm_parameter_slot_count(builder, procedure);
    for (Ast_Node_Index statement = procedure + 1; statement < variable; statement += 1) {
        if (builder->nodes[statement].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION ||
            builder->nodes[statement].node_type == AST_NODE_TYPE_FOR_STATEMENT) {
            local_index += wasm_variable_slot_count(builder, statement);
        }
    }

    assert(builder->nodes[variable].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION ||
           builder->nodes[variable].node_type == AST_NODE_TYPE_FOR_STATEMENT);
    // A variable in the frame has no local.
    assert(wasm_variable_is_in_memory(builder, variable) == false);
    return local_index;
}

/**
 * Returns the type of the value of `expression`. The expression must have been written already,
 * so that every name in it is known to have a value. Only the root matters: an arithmetic
 * operation is an `Int` and a comparison or a logical operation is a `Bool`, because their operands
 * were checked, as they were written. The type has the kind `WASM_VALUE_TYPE_NONE` when the
 * expression has no value, such as a call of a procedure that returns nothing, a name that is a
 * procedure, a field that the struct does not have or a struct literal of a struct that is not
 * defined. A struct literal that has no name gets its type from the field that it is the value of,
 * so it has none here.
 */
static Wasm_Type wasm_expression_type(Wasm_Builder const *builder, Ast_Node_Index expression) {
    assert(expression < builder->node_count);

    Ast_Node const *node = &builder->nodes[expression];
    switch (node->node_type) {
    case AST_NODE_TYPE_ADDITION:
    case AST_NODE_TYPE_DIVISION:
    case AST_NODE_TYPE_INTEGER_LITERAL:
    case AST_NODE_TYPE_MULTIPLICATION:
    case AST_NODE_TYPE_SUBTRACTION:
        return wasm_type_make(WASM_VALUE_TYPE_INT);
    case AST_NODE_TYPE_IDENTIFIER:
        return wasm_binding_value_type(builder, builder->bindings[expression]);
    case AST_NODE_TYPE_AND:
    case AST_NODE_TYPE_BOOL_LITERAL:
    case AST_NODE_TYPE_COMPARISON:
    case AST_NODE_TYPE_NOT:
    case AST_NODE_TYPE_OR:
        return wasm_type_make(WASM_VALUE_TYPE_BOOL);
    case AST_NODE_TYPE_CALL: {
        // The type of what the procedure returns, which is none for a procedure without a result,
        // and for a name that is not a pointer to a procedure, which is an error found later.
        Ast_Node_Index callee = wasm_call_signature(builder, expression);
        if (callee == AST_NODE_INDEX_NONE) {
            return wasm_type_make(WASM_VALUE_TYPE_NONE);
        }
        Str return_type = ast_procedure_return_type(&builder->nodes[callee]);
        if (return_type.bytes == NULL) {
            return wasm_type_make(WASM_VALUE_TYPE_NONE);
        }
        return wasm_type_of_name(builder, return_type);
    }
    case AST_NODE_TYPE_STRING_LITERAL:
        return wasm_type_make(WASM_VALUE_TYPE_STR);
    case AST_NODE_TYPE_NIL_LITERAL:
        return wasm_type_make(WASM_VALUE_TYPE_NIL);
    case AST_NODE_TYPE_ADDRESS_OF:
        // A pointer to the operand, if the operand is something that can be pointed to.
        return wasm_type_pointer_to(wasm_expression_type(builder, node->first_child));
    case AST_NODE_TYPE_ARRAY_LITERAL:
        return wasm_type_of_node(builder, node->first_child);
    case AST_NODE_TYPE_DEREFERENCE: {
        Wasm_Type pointer = wasm_expression_type(builder, node->first_child);
        if (pointer.kind != WASM_VALUE_TYPE_POINTER) {
            return wasm_type_make(WASM_VALUE_TYPE_NONE);
        }
        return wasm_type_pointee(pointer);
    }
    case AST_NODE_TYPE_FIELD_ACCESS: {
        // `Color.RED` is a member of an enum, and any other access is a field.
        Ast_Node_Index enum_node = wasm_enum_of_access(builder, expression);
        if (enum_node != AST_NODE_INDEX_NONE) {
            Wasm_Type member = wasm_type_make(WASM_VALUE_TYPE_ENUM);
            member.definition = enum_node;
            return member;
        }
        return wasm_field_path(builder, expression).type;
    }
    case AST_NODE_TYPE_ENUM_LITERAL:
        return wasm_type_make(WASM_VALUE_TYPE_ENUM_LITERAL);
    case AST_NODE_TYPE_SLICE: {
        Wasm_Type slice = wasm_expression_type(builder, node->first_child);
        if (wasm_type_is_sequence(slice) == false) {
            return wasm_type_make(WASM_VALUE_TYPE_NONE);
        }
        slice.kind = WASM_VALUE_TYPE_SLICE;
        return slice;
    }
    case AST_NODE_TYPE_STRUCT_LITERAL: {
        if (node->payload.struct_literal.name.bytes == NULL) {
            return wasm_type_make(WASM_VALUE_TYPE_NONE);
        }
        return wasm_type_of_name(builder, node->payload.struct_literal.name);
    }
    case AST_NODE_TYPE_SUBSCRIPT: {
        Wasm_Type sequence = wasm_expression_type(builder, node->first_child);
        if (wasm_type_is_sequence(sequence) == false) {
            return wasm_type_make(WASM_VALUE_TYPE_NONE);
        }
        return wasm_type_element(sequence);
    }
    case AST_NODE_TYPE_ADDITION_ASSIGNMENT:
    case AST_NODE_TYPE_ARRAY_TYPE:
    case AST_NODE_TYPE_ASSIGNMENT:
    case AST_NODE_TYPE_ATTRIBUTE:
    case AST_NODE_TYPE_BLOCK:
    case AST_NODE_TYPE_CALL_STATEMENT:
    case AST_NODE_TYPE_CONSTANT_DEFINITION:
    case AST_NODE_TYPE_DIVISION_ASSIGNMENT:
    case AST_NODE_TYPE_ENUM_MEMBER:
    case AST_NODE_TYPE_ENUM_TYPE:
    case AST_NODE_TYPE_BREAK_STATEMENT:
    case AST_NODE_TYPE_CONTINUE_STATEMENT:
    case AST_NODE_TYPE_FIELD:
    case AST_NODE_TYPE_FIELD_INITIALIZER:
    case AST_NODE_TYPE_FOR_STATEMENT:
    case AST_NODE_TYPE_FOREIGN_BODY:
    case AST_NODE_TYPE_FOREIGN_IMPORT:
    case AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION:
    case AST_NODE_TYPE_IF_STATEMENT:
    case AST_NODE_TYPE_LABEL:
    case AST_NODE_TYPE_LABELED_STATEMENT:
    case AST_NODE_TYPE_LOOP_STATEMENT:
    case AST_NODE_TYPE_MODULE:
    case AST_NODE_TYPE_MULTIPLICATION_ASSIGNMENT:
    case AST_NODE_TYPE_PARAMETER:
    case AST_NODE_TYPE_POINTER_TYPE:
    case AST_NODE_TYPE_PROCEDURE:
    case AST_NODE_TYPE_PROCEDURE_TYPE:
    case AST_NODE_TYPE_RANGE:
    case AST_NODE_TYPE_RETURN_STATEMENT:
    case AST_NODE_TYPE_SCOPE_STATEMENT:
    case AST_NODE_TYPE_STRUCT_TYPE:
    case AST_NODE_TYPE_SUBTRACTION_ASSIGNMENT:
    case AST_NODE_TYPE_TYPE_NAME:
    case AST_NODE_TYPE_VARIABLE_DEFINITION:
        // None of these is part of an expression.
        assert(false);
        break;
    }

    return wasm_type_make(WASM_VALUE_TYPE_NONE);
}

/**
 * Returns the text in the source that an error about `expression` points at: the first name or
 * string in it, which is the leftmost leaf of its tree, or the name of a call. An expression that
 * starts with a number or with `true` or `false` has no such text, so the name of the definition is
 * the closest place that the tree knows.
 */
static Str wasm_expression_subject(Wasm_Builder const *builder, Ast_Node_Index definition,
                                   Ast_Node_Index expression) {
    assert(expression < builder->node_count);
    assert(definition < builder->node_count);

    Ast_Node_Index current = expression;
    // Each step goes down one level, so there are fewer steps than nodes.
    for (size_t i = 0; i < builder->node_count; i += 1) {
        // The operand of `%` is written by the code of the operator, so it is not what
        // `wasm_expression_child_first` answers, but the text of an error is still the operand.
        if (builder->nodes[current].node_type == AST_NODE_TYPE_ADDRESS_OF) {
            current = builder->nodes[current].first_child;
            continue;
        }

        Ast_Node_Index child = wasm_expression_child_first(builder, current);
        if (child == AST_NODE_INDEX_NONE ||
            builder->nodes[current].node_type == AST_NODE_TYPE_CALL) {
            break;
        }
        current = child;
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
    if (leaf->node_type == AST_NODE_TYPE_FIELD_ACCESS) {
        return leaf->payload.field_access.name;
    }
    if (leaf->node_type == AST_NODE_TYPE_STRUCT_LITERAL &&
        leaf->payload.struct_literal.name.bytes != NULL) {
        return leaf->payload.struct_literal.name;
    }
    return builder->nodes[definition].payload.constant_definition.name;
}

/**
 * Checks the operands of the arithmetic operation, comparison or logical operation `operation`,
 * which were written already. `and`, `or` and `not` take `Bool` values only. Arithmetic is defined
 * for `Int` only. A comparison takes two `Int` values with any
 * operator, or two `Bool` values with `==` and `not=`, because a `Bool` has no order. Returns false
 * after it recorded an error.
 */
static bool wasm_build_code_section_body_operands(Wasm_Builder *builder, Ast_Node_Index definition,
                                                  Ast_Node_Index operation) {
    assert(operation < builder->node_count);

    Ast_Node const *node = &builder->nodes[operation];
    Ast_Node_Index left = node->first_child;
    Ast_Node_Index right = node->last_child;
    assert(left != AST_NODE_INDEX_NONE);

    if (node->node_type == AST_NODE_TYPE_NOT) {
        assert(right == left);
        if (wasm_expression_type(builder, left).kind != WASM_VALUE_TYPE_BOOL) {
            return wasm_fail(builder, WASM_BUILD_STATUS_LOGICAL_OPERAND_TYPE_INVALID,
                             wasm_expression_subject(builder, definition, left));
        }
        return true;
    }
    assert(right != left);

    Wasm_Type left_type = wasm_expression_type(builder, left);
    Wasm_Type right_type = wasm_expression_type(builder, right);

    if (node->node_type == AST_NODE_TYPE_AND || node->node_type == AST_NODE_TYPE_OR) {
        if (left_type.kind != WASM_VALUE_TYPE_BOOL) {
            return wasm_fail(builder, WASM_BUILD_STATUS_LOGICAL_OPERAND_TYPE_INVALID,
                             wasm_expression_subject(builder, definition, left));
        }
        if (right_type.kind != WASM_VALUE_TYPE_BOOL) {
            return wasm_fail(builder, WASM_BUILD_STATUS_LOGICAL_OPERAND_TYPE_INVALID,
                             wasm_expression_subject(builder, definition, right));
        }
        return true;
    }

    if (node->node_type != AST_NODE_TYPE_COMPARISON) {
        if (left_type.kind != WASM_VALUE_TYPE_INT) {
            return wasm_fail(builder, WASM_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID,
                             wasm_expression_subject(builder, definition, left));
        }
        if (right_type.kind != WASM_VALUE_TYPE_INT) {
            return wasm_fail(builder, WASM_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID,
                             wasm_expression_subject(builder, definition, right));
        }
        return true;
    }

    Ast_Comparison_Operator operator = node->payload.comparison.operator;
    bool is_equality = operator == AST_COMPARISON_OPERATOR_EQUAL ||
                       operator == AST_COMPARISON_OPERATOR_NOT_EQUAL;
    // Pointers have no order either, and are the same when they hold the same address. `nil` is
    // compared with a pointer of any type.
    // The members of an enum are compared like addresses: they are the same when the numbers are.
    bool is_address = left_type.kind == WASM_VALUE_TYPE_POINTER ||
                      left_type.kind == WASM_VALUE_TYPE_PROCEDURE ||
                      left_type.kind == WASM_VALUE_TYPE_ENUM ||
                      left_type.kind == WASM_VALUE_TYPE_ENUM_LITERAL ||
                      left_type.kind == WASM_VALUE_TYPE_NIL;
    bool is_left_comparable = left_type.kind == WASM_VALUE_TYPE_INT ||
                              ((left_type.kind == WASM_VALUE_TYPE_BOOL || is_address) &&
                               is_equality);
    if (is_left_comparable == false) {
        return wasm_fail(builder, WASM_BUILD_STATUS_COMPARISON_OPERAND_TYPE_INVALID,
                         wasm_expression_subject(builder, definition, left));
    }
    if (is_address && wasm_type_equals(left_type, right_type) == false) {
        return wasm_fail(builder, WASM_BUILD_STATUS_COMPARISON_OPERAND_TYPE_INVALID,
                         wasm_expression_subject(builder, definition, right));
    }
    if (is_address == false && right_type.kind != left_type.kind) {
        return wasm_fail(builder, WASM_BUILD_STATUS_COMPARISON_OPERAND_TYPE_INVALID,
                         wasm_expression_subject(builder, definition, right));
    }
    return true;
}

/**
 * Returns the instruction that compares two `Int` values with `operator`, and leaves a 32-bit
 * integer on the stack that is 1 when the comparison holds and 0 when it does not. The comparison
 * is signed, like the division.
 */
static byte wasm_comparison_opcode(Ast_Comparison_Operator operator) {
    switch (operator) {
    case AST_COMPARISON_OPERATOR_EQUAL:
        return WASM_OPCODE_I64_EQ;
    case AST_COMPARISON_OPERATOR_GREATER:
        return WASM_OPCODE_I64_GT_S;
    case AST_COMPARISON_OPERATOR_GREATER_EQUAL:
        return WASM_OPCODE_I64_GE_S;
    case AST_COMPARISON_OPERATOR_LESS:
        return WASM_OPCODE_I64_LT_S;
    case AST_COMPARISON_OPERATOR_LESS_EQUAL:
        return WASM_OPCODE_I64_LE_S;
    case AST_COMPARISON_OPERATOR_NOT_EQUAL:
        return WASM_OPCODE_I64_NE;
    }

    // Unreachable: every operator is handled above.
    assert(false);
    return WASM_OPCODE_I64_EQ;
}

/**
 * Writes the instructions of the expression below `expression`, which leave its value on the stack.
 * A call of a procedure that returns nothing leaves no value, so it is accepted only as the whole
 * expression, and only when `is_result_unused` says that the caller does not need a value.
 * WebAssembly is a stack machine, so an operator comes after its operands: the nodes are written
 * depth first, each node after all of its children. The walk keeps the path from the root of the
 * expression in an array instead of calling itself.
 */
static bool wasm_build_code_section_body_expression(Wasm_Builder *builder,
                                                    Ast_Node_Index definition,
                                                    Ast_Node_Index expression,
                                                    bool is_result_unused) {
    assert(expression < builder->node_count);
    assert(definition < builder->node_count);

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);

    // The nodes on the path from the root to the current node, excluding the current node.
    Ast_Node_Index path[WASM_EXPRESSION_DEPTH_MAX] = {0};
    size_t path_count = 0;

    Ast_Node_Index current = expression;
    // False once the children of `current` have been written.
    bool is_descending = true;
    // Every node is entered once and left once, so twice the number of nodes steps are enough.
    for (size_t step = 0; step < builder->node_count * 2; step += 1) {
        Ast_Node const *node = &builder->nodes[current];

        if (is_descending) {
            // An element of an array literal is stored where the literal is in the frame, so the
            // address goes on the stack before the value of the element does, and the store comes
            // after it, see `wasm_build_code_section_body_child`.
            if (path_count > 0 &&
                builder->nodes[path[path_count - 1]].node_type == AST_NODE_TYPE_ARRAY_LITERAL &&
                wasm_type_of_node(builder, builder->nodes[path[path_count - 1]].first_child)
                        .element_kind == WASM_VALUE_TYPE_INT) {
                wasm_write_local_instruction(
                    &builder->writer, WASM_OPCODE_LOCAL_GET,
                    wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_FRAME));
            }

            Ast_Node_Index first_child = wasm_expression_child_first(builder, current);
            if (first_child != AST_NODE_INDEX_NONE) {
                if (path_count == WASM_EXPRESSION_DEPTH_MAX) {
                    // The tree has no source positions, so the definition is the closest place.
                    Str name = builder->nodes[definition].payload.constant_definition.name;
                    return wasm_fail(builder, WASM_BUILD_STATUS_EXPRESSION_TOO_DEEP, name);
                }
                path[path_count] = current;
                path_count += 1;
                current = first_child;
                continue;
            }
        }

        // The operands of an operator have been written when the operator is reached, so their
        // types are known and are checked here.
        switch (node->node_type) {
        case AST_NODE_TYPE_ADDITION:
        case AST_NODE_TYPE_DIVISION:
        case AST_NODE_TYPE_MULTIPLICATION:
        case AST_NODE_TYPE_SUBTRACTION:
        case AST_NODE_TYPE_COMPARISON:
        case AST_NODE_TYPE_AND:
        case AST_NODE_TYPE_NOT:
        case AST_NODE_TYPE_OR:
            if (wasm_build_code_section_body_operands(builder, definition, current) == false) {
                return false;
            }
            break;
        case AST_NODE_TYPE_CALL:
            // The value of a call that is an operand is always needed. The call that is the whole
            // expression is the only one that can go without.
            if (wasm_build_code_section_body_call(
                    builder, current, is_result_unused && current == expression) == false) {
                return false;
            }
            break;
        default:
            break;
        }

        switch (node->node_type) {
        case AST_NODE_TYPE_ADDITION:
            wasm_write_byte(&builder->writer, WASM_OPCODE_I64_ADD);
            break;
        case AST_NODE_TYPE_AND:
            // Both operands are on the stack, as 0 or 1, so the bitwise instruction is the logical
            // one. There is no short circuit: both operands are always evaluated.
            wasm_write_byte(&builder->writer, WASM_OPCODE_I32_AND);
            break;
        case AST_NODE_TYPE_NOT:
            // 1 for a 0 and 0 for a 1.
            wasm_write_byte(&builder->writer, WASM_OPCODE_I32_EQZ);
            break;
        case AST_NODE_TYPE_OR:
            wasm_write_byte(&builder->writer, WASM_OPCODE_I32_OR);
            break;
        case AST_NODE_TYPE_CALL: {
            Semantic_Binding callee_binding = builder->bindings[current];
            if (callee_binding.kind == SEMANTIC_BINDING_KIND_CONSTANT) {
                // The function index of a procedure is not the position of its definition,
                // because the foreign procedures are imported, and so come first.
                wasm_write_byte(&builder->writer, WASM_OPCODE_CALL);
                wasm_write_unsigned(&builder->writer,
                                    wasm_function_index(builder, callee_binding.target));
                break;
            }
            // The procedure that is called is what the name holds, which is pushed after the
            // arguments, because `call_indirect` takes it from the top of the stack. It checks
            // that the type of the function it finds is the type that is written here, which
            // is the type that the pointer was made for, and traps for the null pointer.
            wasm_build_code_section_body_identifier(builder, procedure, callee_binding);
            wasm_write_byte(&builder->writer, WASM_OPCODE_CALL_INDIRECT);
            wasm_write_unsigned(
                &builder->writer,
                wasm_signature_type_index(builder, wasm_call_signature(builder, current)));
            wasm_write_unsigned(&builder->writer, 0);
            break;
        }
        case AST_NODE_TYPE_BOOL_LITERAL:
            wasm_write_byte(&builder->writer, WASM_OPCODE_I32_CONST);
            wasm_write_signed(&builder->writer, node->payload.bool_literal.value ? 1 : 0);
            break;
        case AST_NODE_TYPE_COMPARISON:
            // `Bool` values and addresses are compared as the 32-bit integers that they are.
            if (wasm_expression_type(builder, node->first_child).kind == WASM_VALUE_TYPE_BOOL ||
                wasm_expression_type(builder, node->first_child).kind == WASM_VALUE_TYPE_ENUM ||
                wasm_expression_type(builder, node->first_child).kind ==
                    WASM_VALUE_TYPE_ENUM_LITERAL ||
                wasm_expression_type(builder, node->first_child).kind == WASM_VALUE_TYPE_NIL ||
                wasm_expression_type(builder, node->first_child).kind ==
                    WASM_VALUE_TYPE_PROCEDURE ||
                wasm_expression_type(builder, node->first_child).kind ==
                    WASM_VALUE_TYPE_POINTER) {
                if (node->payload.comparison.operator == AST_COMPARISON_OPERATOR_EQUAL) {
                    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_EQ);
                } else {
                    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_NE);
                }
            } else {
                wasm_write_byte(&builder->writer,
                                wasm_comparison_opcode(node->payload.comparison.operator));
            }
            break;
        case AST_NODE_TYPE_DIVISION:
            wasm_write_byte(&builder->writer, WASM_OPCODE_I64_DIV_S);
            break;
        case AST_NODE_TYPE_MULTIPLICATION:
            wasm_write_byte(&builder->writer, WASM_OPCODE_I64_MUL);
            break;
        case AST_NODE_TYPE_SUBTRACTION:
            wasm_write_byte(&builder->writer, WASM_OPCODE_I64_SUB);
            break;
        case AST_NODE_TYPE_CONSTANT_DEFINITION:
            // The parser only makes operators, literals and names in an expression.
            assert(false);
            break;
        case AST_NODE_TYPE_IDENTIFIER: {
            Str name = node->payload.identifier.name;
            Semantic_Binding binding = builder->bindings[current];
            Wasm_Type value_type = wasm_binding_value_type(builder, binding);
            if (value_type.kind == WASM_VALUE_TYPE_NONE) {
                return wasm_fail(builder, WASM_BUILD_STATUS_IDENTIFIER_NOT_VALUE, name);
            }

            wasm_build_code_section_body_identifier(builder, procedure, binding);
            break;
        }
        case AST_NODE_TYPE_NIL_LITERAL:
            // The null pointer is the address 0, which is not the address of anything.
            wasm_write_i32_const(&builder->writer, 0);
            break;
        case AST_NODE_TYPE_ENUM_LITERAL:
            // The number of the member depends on the enum, which the place of the literal says.
            if (wasm_enum_member_write(
                    builder, wasm_enum_literal_expected(builder, definition, current),
                    node->payload.enum_literal.name) == false) {
                return false;
            }
            break;
        case AST_NODE_TYPE_INTEGER_LITERAL:
            wasm_write_byte(&builder->writer, WASM_OPCODE_I64_CONST);
            wasm_write_signed(&builder->writer, node->payload.integer_literal.value);
            break;
        case AST_NODE_TYPE_STRING_LITERAL:
            wasm_build_code_section_body_string(builder, current);
            break;
        case AST_NODE_TYPE_ARRAY_LITERAL:
            if (wasm_build_code_section_body_array_literal(builder, definition, current) ==
                false) {
                return false;
            }
            break;
        case AST_NODE_TYPE_SLICE:
            if (wasm_build_code_section_body_slice(builder, definition, current) == false) {
                return false;
            }
            break;
        case AST_NODE_TYPE_FIELD_ACCESS: {
            Ast_Node_Index access_enum = wasm_enum_of_access(builder, current);
            if (access_enum != AST_NODE_INDEX_NONE) {
                if (wasm_enum_member_write(builder, access_enum,
                                           node->payload.field_access.name) == false) {
                    return false;
                }
                break;
            }
            if (wasm_build_code_section_body_field_access(builder, definition, current) ==
                false) {
                return false;
            }
            break;
        }
        case AST_NODE_TYPE_STRUCT_LITERAL:
            if (wasm_build_code_section_body_struct_literal(builder, definition, current,
                                                            AST_NODE_INDEX_NONE) == false) {
                return false;
            }
            break;
        case AST_NODE_TYPE_SUBSCRIPT:
            if (wasm_build_code_section_body_subscript(builder, definition, current) == false) {
                return false;
            }
            break;
        case AST_NODE_TYPE_ADDRESS_OF:
            if (wasm_build_code_section_body_address_of(builder, definition, current) == false) {
                return false;
            }
            break;
        case AST_NODE_TYPE_DEREFERENCE:
            if (wasm_build_code_section_body_dereference(builder, definition, current) == false) {
                return false;
            }
            break;
        case AST_NODE_TYPE_ADDITION_ASSIGNMENT:
        case AST_NODE_TYPE_ARRAY_TYPE:
        case AST_NODE_TYPE_ASSIGNMENT:
        case AST_NODE_TYPE_ATTRIBUTE:
        case AST_NODE_TYPE_BLOCK:
        case AST_NODE_TYPE_CALL_STATEMENT:
        case AST_NODE_TYPE_ENUM_MEMBER:
        case AST_NODE_TYPE_ENUM_TYPE:
        case AST_NODE_TYPE_FIELD:
        case AST_NODE_TYPE_FIELD_INITIALIZER:
        case AST_NODE_TYPE_STRUCT_TYPE:
        case AST_NODE_TYPE_DIVISION_ASSIGNMENT:
        case AST_NODE_TYPE_BREAK_STATEMENT:
        case AST_NODE_TYPE_CONTINUE_STATEMENT:
        case AST_NODE_TYPE_FOR_STATEMENT:
        case AST_NODE_TYPE_FOREIGN_BODY:
        case AST_NODE_TYPE_FOREIGN_IMPORT:
        case AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION:
        case AST_NODE_TYPE_IF_STATEMENT:
        case AST_NODE_TYPE_LABEL:
        case AST_NODE_TYPE_LABELED_STATEMENT:
        case AST_NODE_TYPE_LOOP_STATEMENT:
        case AST_NODE_TYPE_MODULE:
        case AST_NODE_TYPE_MULTIPLICATION_ASSIGNMENT:
        case AST_NODE_TYPE_PARAMETER:
        case AST_NODE_TYPE_POINTER_TYPE:
        case AST_NODE_TYPE_PROCEDURE:
        case AST_NODE_TYPE_PROCEDURE_TYPE:
        case AST_NODE_TYPE_RANGE:
        case AST_NODE_TYPE_RETURN_STATEMENT:
        case AST_NODE_TYPE_SCOPE_STATEMENT:
        case AST_NODE_TYPE_SUBTRACTION_ASSIGNMENT:
        case AST_NODE_TYPE_TYPE_NAME:
        case AST_NODE_TYPE_VARIABLE_DEFINITION:
            // Same as above: none of these is part of an expression.
            assert(false);
            break;
        }

        // What the parent needs of its children, which is not part of the child itself.
        if (path_count > 0 &&
            wasm_build_code_section_body_child(builder, definition, path[path_count - 1],
                                               current) == false) {
            return false;
        }

        if (path_count == 0) {
            assert(current == expression);
            return true;
        }

        // Go on with the next sibling, or finish the parent when there is none.
        if (node->next_sibling != AST_NODE_INDEX_NONE) {
            current = node->next_sibling;
            is_descending = true;
        } else {
            path_count -= 1;
            current = path[path_count];
            is_descending = false;
        }
    }

    // Unreachable: the step budget covers every node twice.
    assert(false);
    return false;
}

/**
 * Writes the instructions that push the value that a name stands for. A parameter is already in
 * locals of the function, a variable is in locals or, when its address is taken, in the frame, and
 * a constant string is the result of a function of its own, which is called. The constant is the
 * definition `binding.target`, and its function is found by `wasm_function_index`.
 */
static void wasm_build_code_section_body_identifier(Wasm_Builder *builder,
                                                    Ast_Node_Index procedure,
                                                    Semantic_Binding binding) {
    assert(procedure < builder->node_count);

    Wasm_Type value_type = wasm_binding_value_type(builder, binding);
    assert(value_type.kind != WASM_VALUE_TYPE_NONE);

    switch (binding.kind) {
    case SEMANTIC_BINDING_KIND_CONSTANT:
        // The name of a procedure is the pointer to it, and the name of a string is a call.
        if (value_type.kind == WASM_VALUE_TYPE_PROCEDURE) {
            wasm_write_i32_const(&builder->writer,
                                 (int64_t)wasm_procedure_table_index(builder, binding.target));
            break;
        }
        wasm_write_byte(&builder->writer, WASM_OPCODE_CALL);
        wasm_write_unsigned(&builder->writer, wasm_function_index(builder, binding.target));
        break;
    case SEMANTIC_BINDING_KIND_GLOBAL: {
        // Every global of the value is pushed, which is two for a `Str`.
        size_t global_index = wasm_global_slot_index(builder, binding.target);
        size_t slot_count = wasm_type_slot_count(builder, value_type);
        for (size_t slot = 0; slot < slot_count; slot += 1) {
            wasm_write_byte(&builder->writer, WASM_OPCODE_GLOBAL_GET);
            wasm_write_unsigned(&builder->writer, global_index + slot);
        }
        break;
    }
    case SEMANTIC_BINDING_KIND_JUMP_TARGET:
    case SEMANTIC_BINDING_KIND_NONE:
        // A name that is used was bound by the analysis, and is not a jump.
        assert(false);
        break;
    case SEMANTIC_BINDING_KIND_PARAMETER: {
        // Every local of the value is pushed, which is two for a `Str`.
        size_t local_index = wasm_parameter_local_index(builder, procedure, binding.position);
        size_t slot_count = wasm_type_slot_count(builder, value_type);
        for (size_t slot = 0; slot < slot_count; slot += 1) {
            wasm_write_byte(&builder->writer, WASM_OPCODE_LOCAL_GET);
            wasm_write_unsigned(&builder->writer, local_index + slot);
        }
        break;
    }
    case SEMANTIC_BINDING_KIND_VARIABLE: {
        // A variable whose address is taken is in the frame, and its slots are loaded from there.
        if (wasm_variable_is_in_memory(builder, binding.target)) {
            wasm_slots_load(builder, value_type,
                            wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_FRAME),
                            wasm_variable_frame_offset(builder, procedure, binding.target));
            break;
        }
        size_t local_index = wasm_variable_local_index(builder, procedure, binding.target);
        size_t slot_count = wasm_type_slot_count(builder, value_type);
        for (size_t slot = 0; slot < slot_count; slot += 1) {
            wasm_write_byte(&builder->writer, WASM_OPCODE_LOCAL_GET);
            wasm_write_unsigned(&builder->writer, local_index + slot);
        }
        break;
    }
    }
}

/**
 * Returns the type of the value that `binding` stands for, or a type of the kind
 * `WASM_VALUE_TYPE_NONE` when the target cannot use it as a value, which is the case for a struct.
 * The types of the parameters must be known.
 */
static Wasm_Type wasm_binding_value_type(Wasm_Builder const *builder, Semantic_Binding binding) {
    assert(binding.target < builder->node_count);

    switch (binding.kind) {
    case SEMANTIC_BINDING_KIND_CONSTANT: {
        Ast_Node_Index value = builder->nodes[binding.target].first_child;
        if (builder->nodes[value].node_type == AST_NODE_TYPE_STRING_LITERAL) {
            return wasm_type_make(WASM_VALUE_TYPE_STR);
        }
        // The member of an enum is the enum.
        if (builder->nodes[value].node_type == AST_NODE_TYPE_FIELD_ACCESS) {
            Wasm_Type member = wasm_type_make(WASM_VALUE_TYPE_ENUM);
            member.definition = wasm_enum_of_access(builder, value);
            return member;
        }
        // A procedure is the pointer to it, and a struct and an enum are types.
        if (builder->nodes[value].node_type == AST_NODE_TYPE_PROCEDURE) {
            Wasm_Type pointer = wasm_type_make(WASM_VALUE_TYPE_PROCEDURE);
            pointer.definition = wasm_signature_canonical(builder, value);
            return pointer;
        }
        return wasm_type_make(WASM_VALUE_TYPE_NONE);
    }
    case SEMANTIC_BINDING_KIND_GLOBAL:
        return wasm_global_type(builder, binding.target);
    case SEMANTIC_BINDING_KIND_JUMP_TARGET:
    case SEMANTIC_BINDING_KIND_NONE:
        assert(false);
        break;
    case SEMANTIC_BINDING_KIND_PARAMETER: {
        Ast_Node_Index type = builder->nodes[binding.target].first_child;
        return wasm_type_of_node(builder, type);
    }
    case SEMANTIC_BINDING_KIND_VARIABLE:
        return wasm_variable_type(builder, binding.target);
    }

    return wasm_type_make(WASM_VALUE_TYPE_NONE);
}

/**
 * Returns the procedure that `definition` binds to its name. The definition must not be of a
 * string, which has no procedure.
 */
static Ast_Node_Index wasm_procedure_of(Wasm_Builder const *builder, Ast_Node_Index definition) {
    assert(definition < builder->node_count);
    assert(builder->nodes[definition].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION);

    Ast_Node_Index procedure = builder->nodes[definition].first_child;
    assert(procedure < builder->node_count);
    assert(builder->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);
    return procedure;
}

/**
 * Returns the number of children of `parent`, which for a procedure includes its statements.
 */
static size_t wasm_children_count(Wasm_Builder const *builder, Ast_Node_Index parent) {
    assert(parent < builder->node_count);

    size_t count = 0;
    Ast_Node_Index child = builder->nodes[parent].first_child;
    // Every child is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && child != AST_NODE_INDEX_NONE; i += 1) {
        count += 1;
        child = builder->nodes[child].next_sibling;
    }

    assert(child == AST_NODE_INDEX_NONE);
    return count;
}

/**
 * Returns the number of parameters of `procedure`. They are its first children, before the
 * statements of the body.
 */
static size_t wasm_parameter_count(Wasm_Builder const *builder, Ast_Node_Index procedure) {
    assert(procedure < builder->node_count);
    assert(builder->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);

    size_t count = 0;
    Ast_Node_Index child = builder->nodes[procedure].first_child;
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
 * Returns the number of WebAssembly values that the parameters of `procedure` take together,
 * which is more than the number of parameters when one is a `Str`. The types of the parameters
 * must be known.
 */
static size_t wasm_parameter_slot_count(Wasm_Builder const *builder, Ast_Node_Index procedure) {
    assert(procedure < builder->node_count);
    assert(builder->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);

    size_t parameter_count = wasm_parameter_count(builder, procedure);
    size_t slot_count = 0;
    Ast_Node_Index parameter = builder->nodes[procedure].first_child;
    for (size_t i = 0; i < parameter_count; i += 1) {
        assert(parameter != AST_NODE_INDEX_NONE);
        Ast_Node const *parameter_node = &builder->nodes[parameter];

        slot_count += wasm_type_slot_count(builder, 
            wasm_type_of_node(builder, parameter_node->first_child));
        parameter = parameter_node->next_sibling;
    }

    return slot_count;
}

/**
 * Returns the index of the first local that holds the parameter at `position` of `procedure`. The
 * locals of a function are its parameters, in order, so that is the number of locals of the
 * parameters before it, which is not `position`, because a `Str` takes two locals. The types of
 * the parameters must be known.
 */
static size_t wasm_parameter_local_index(Wasm_Builder const *builder, Ast_Node_Index procedure,
                                         size_t position) {
    assert(procedure < builder->node_count);
    assert(position < wasm_parameter_count(builder, procedure));

    size_t local_index = 0;
    Ast_Node_Index parameter = builder->nodes[procedure].first_child;
    for (size_t i = 0; i < position; i += 1) {
        assert(parameter != AST_NODE_INDEX_NONE);
        Ast_Node const *parameter_node = &builder->nodes[parameter];

        local_index += wasm_type_slot_count(builder, 
            wasm_type_of_node(builder, parameter_node->first_child));
        parameter = parameter_node->next_sibling;
    }

    return local_index;
}

/**
 * Returns whether `node` is a type: the name of one, such as `Int`, a pointer type, such as `^Int`,
 * or an array type, such as `[3]Int`.
 */
static bool wasm_node_is_type(Wasm_Builder const *builder, Ast_Node_Index node) {
    assert(node < builder->node_count);

    return builder->nodes[node].node_type == AST_NODE_TYPE_TYPE_NAME ||
           builder->nodes[node].node_type == AST_NODE_TYPE_ARRAY_TYPE ||
           builder->nodes[node].node_type == AST_NODE_TYPE_POINTER_TYPE ||
           builder->nodes[node].node_type == AST_NODE_TYPE_PROCEDURE_TYPE ||
           builder->nodes[node].node_type == AST_NODE_TYPE_STRUCT_TYPE;
}

/**
 * Returns the text in the source that an error about the type `type` points at: the name of the
 * type, or the name of the type of the elements for an array type, or the name that the carets of
 * a pointer type are before. For a pointer to a procedure that is the name of the type of the first
 * parameter that the target does not have, the return type, or the keyword `proc`.
 */
static Str wasm_type_subject(Wasm_Builder const *builder, Ast_Node_Index type) {
    assert(wasm_node_is_type(builder, type));

    // An array type has the type of its elements as its child, which can be a pointer type, and a
    // pointer type has the rest of the type, so the name is at the bottom. Each step goes down one
    // level, so there are fewer steps than nodes.
    Ast_Node_Index current = type;
    for (size_t i = 0; i < builder->node_count &&
                       (builder->nodes[current].node_type == AST_NODE_TYPE_ARRAY_TYPE ||
                        builder->nodes[current].node_type == AST_NODE_TYPE_POINTER_TYPE);
         i += 1) {
        current = builder->nodes[current].first_child;
    }
    // A struct that is the type of a field has no name, so its first field stands for it.
    if (builder->nodes[current].node_type == AST_NODE_TYPE_STRUCT_TYPE) {
        return builder->nodes[builder->nodes[current].first_child].payload.field.name;
    }
    // A procedure type has no name either: the type of the first parameter or of the result that
    // the target does not have stands for it, and else the keyword or the result.
    if (builder->nodes[current].node_type == AST_NODE_TYPE_PROCEDURE_TYPE) {
        size_t parameter_count = wasm_signature_parameter_count(builder, current);
        for (size_t i = 0; i < parameter_count; i += 1) {
            Ast_Node_Index parameter = wasm_signature_parameter_type(builder, current, i);
            // The type of a parameter is a name or a pointer, which this function does not
            // recurse on any further.
            if (wasm_type_of_node(builder, parameter).kind == WASM_VALUE_TYPE_NONE) {
                return wasm_type_subject(builder, parameter);
            }
        }
        return builder->nodes[current].payload.procedure.return_type;
    }
    return builder->nodes[current].payload.type_name.name;
}

/**
 * Returns whether the for statement `statement` goes through the elements of an array or an array
 * slice, instead of through the numbers of a range.
 */
static bool wasm_for_is_each(Wasm_Builder const *builder, Ast_Node_Index statement) {
    assert(statement < builder->node_count);
    assert(builder->nodes[statement].node_type == AST_NODE_TYPE_FOR_STATEMENT);

    Ast_Node_Index first = builder->nodes[statement].first_child;
    return builder->nodes[first].node_type != AST_NODE_TYPE_RANGE;
}

/**
 * Returns the instruction that applies the operator of the assignment `node_type`, such as `-=`, to
 * two `Int` values.
 */
static byte wasm_assignment_opcode(Ast_Node_Type node_type) {
    assert(node_type != AST_NODE_TYPE_ASSIGNMENT);

    if (node_type == AST_NODE_TYPE_DIVISION_ASSIGNMENT) {
        return WASM_OPCODE_I64_DIV_S;
    }
    if (node_type == AST_NODE_TYPE_MULTIPLICATION_ASSIGNMENT) {
        return WASM_OPCODE_I64_MUL;
    }
    if (node_type == AST_NODE_TYPE_SUBTRACTION_ASSIGNMENT) {
        return WASM_OPCODE_I64_SUB;
    }
    assert(node_type == AST_NODE_TYPE_ADDITION_ASSIGNMENT);
    return WASM_OPCODE_I64_ADD;
}

/**
 * Writes an instruction with a local as its immediate, such as `local.get`.
 */
static void wasm_write_local_instruction(Wasm_Writer *writer, byte opcode, size_t local_index) {
    assert(opcode == WASM_OPCODE_LOCAL_GET || opcode == WASM_OPCODE_LOCAL_SET ||
           opcode == WASM_OPCODE_LOCAL_TEE);

    wasm_write_byte(writer, opcode);
    wasm_write_unsigned(writer, local_index);
}

/**
 * Writes `i32.const` and its number.
 */
static void wasm_write_i32_const(Wasm_Writer *writer, int64_t number) {
    assert(number >= INT32_MIN);
    assert(number <= INT32_MAX);

    wasm_write_byte(writer, WASM_OPCODE_I32_CONST);
    wasm_write_signed(writer, number);
}

/**
 * Writes the load or the store of an `Int` or of a 32-bit value at the address on the stack plus
 * `offset_bytes`. The alignment is the size of the value, which is as high as the instruction
 * allows, because the stack and the elements are aligned.
 */
static void wasm_write_memory_instruction(Wasm_Writer *writer, byte opcode,
                                          uint64_t offset_bytes) {
    assert(opcode == WASM_OPCODE_I64_LOAD || opcode == WASM_OPCODE_I64_STORE ||
           opcode == WASM_OPCODE_I32_LOAD || opcode == WASM_OPCODE_I32_STORE);

    wasm_write_byte(writer, opcode);
    if (opcode == WASM_OPCODE_I32_LOAD || opcode == WASM_OPCODE_I32_STORE) {
        wasm_write_unsigned(writer, 2);
    } else {
        wasm_write_unsigned(writer, WASM_ELEMENT_SIZE_SHIFT);
    }
    wasm_write_unsigned(writer, offset_bytes);
}

/**
 * Writes an `if` that traps, so that the instructions before it, which leave a 32-bit integer
 * that is not zero when something is wrong, stop the program instead of going on.
 */
static void wasm_structured_trap(Wasm_Builder *builder) {
    wasm_structured_begin(builder, WASM_OPCODE_IF);
    wasm_write_byte(&builder->writer, WASM_OPCODE_UNREACHABLE);
    wasm_write_byte(&builder->writer, WASM_OPCODE_END);
}

/**
 * Returns the first child of `expression` that is part of its value, which is its first child
 * unless it is an array literal. The first child of an array literal is its type, which is
 * not code.
 */
static Ast_Node_Index wasm_expression_child_first(Wasm_Builder const *builder,
                                                  Ast_Node_Index expression) {
    assert(expression < builder->node_count);

    Ast_Node const *node = &builder->nodes[expression];
    if (node->node_type == AST_NODE_TYPE_ARRAY_LITERAL) {
        assert(node->first_child != AST_NODE_INDEX_NONE);
        return builder->nodes[node->first_child].next_sibling;
    }
    // A field access, a struct literal and the address of something are written as a whole by their
    // own functions, which write their children as they need them, and not in the order of the
    // tree: the operand of `%` is a place, and is not loaded.
    if (node->node_type == AST_NODE_TYPE_ADDRESS_OF ||
        node->node_type == AST_NODE_TYPE_FIELD_ACCESS ||
        node->node_type == AST_NODE_TYPE_STRUCT_LITERAL) {
        return AST_NODE_INDEX_NONE;
    }
    return node->first_child;
}

/**
 * Returns whether the procedure has anything that uses the locals of `Wasm_Scratch`: an array
 * literal, a subscript, a slice, a for statement over the elements of an array, or a pointer that
 * is followed, also by a field access, or whose address is taken, which are loads and stores of
 * memory like the others, and need the frame for the variables that have an address.
 */
static bool wasm_procedure_uses_arrays(Wasm_Builder const *builder, Ast_Node_Index procedure) {
    assert(procedure < builder->node_count);

    Ast_Node_Index end = ast_procedure_end(builder->nodes, builder->node_count, procedure);
    for (Ast_Node_Index node = procedure + 1; node < end; node += 1) {
        Ast_Node_Type node_type = builder->nodes[node].node_type;
        if (node_type == AST_NODE_TYPE_ARRAY_LITERAL || node_type == AST_NODE_TYPE_SLICE ||
            node_type == AST_NODE_TYPE_SUBSCRIPT || node_type == AST_NODE_TYPE_ADDRESS_OF ||
            node_type == AST_NODE_TYPE_DEREFERENCE) {
            return true;
        }
        // A field of a pointer to a struct is in memory, though no `^` is written.
        if (node_type == AST_NODE_TYPE_FIELD_ACCESS &&
            wasm_field_path(builder, node).is_through_pointer) {
            return true;
        }
        if (node_type == AST_NODE_TYPE_FOR_STATEMENT && wasm_for_is_each(builder, node)) {
            return true;
        }
    }

    return false;
}

/**
 * Returns the number of locals that the parameters and the variables of `procedure` take, which
 * is the index of the first local after them. The types of both must be known.
 */
static size_t wasm_variable_locals_end(Wasm_Builder const *builder, Ast_Node_Index procedure) {
    assert(procedure < builder->node_count);

    size_t local_index = wasm_parameter_slot_count(builder, procedure);
    Ast_Node_Index end = ast_procedure_end(builder->nodes, builder->node_count, procedure);
    for (Ast_Node_Index statement = procedure + 1; statement < end; statement += 1) {
        if (builder->nodes[statement].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION ||
            builder->nodes[statement].node_type == AST_NODE_TYPE_FOR_STATEMENT) {
            local_index += wasm_variable_slot_count(builder, statement);
        }
    }

    return local_index;
}

/**
 * Returns the index of the local `scratch` of `procedure`, which comes after its parameters and
 * its variables. The procedure must use arrays.
 */
static size_t wasm_scratch_local_index(Wasm_Builder const *builder, Ast_Node_Index procedure,
                                       Wasm_Scratch scratch) {
    assert(wasm_procedure_uses_arrays(builder, procedure));

    return wasm_variable_locals_end(builder, procedure) + (size_t)scratch;
}

/**
 * Returns whether the procedure needs a frame: it has an array literal, or a variable whose address
 * is taken, because both are in memory.
 */
static bool wasm_frame_is_needed(Wasm_Builder const *builder, Ast_Node_Index procedure) {
    assert(procedure < builder->node_count);

    Ast_Node_Index end = ast_procedure_end(builder->nodes, builder->node_count, procedure);
    for (Ast_Node_Index node = procedure + 1; node < end; node += 1) {
        if (builder->nodes[node].node_type == AST_NODE_TYPE_ARRAY_LITERAL ||
            wasm_variable_is_in_memory(builder, node)) {
            return true;
        }
    }

    return false;
}

/**
 * Returns the size in bytes of the frame of `procedure`: the elements of its array literals
 * together, and then the variables whose address is taken. Each literal has room for its elements,
 * whether it is a variable or only a value that is used once, and a literal that is evaluated
 * again, for example in a loop, is put in the same place again. Each variable has a place of its
 * own, also when it is in a block, see `wasm_variable_frame_offset`.
 */
static size_t wasm_frame_size_bytes(Wasm_Builder const *builder, Ast_Node_Index procedure) {
    assert(procedure < builder->node_count);

    Ast_Node_Index end = ast_procedure_end(builder->nodes, builder->node_count, procedure);
    return wasm_variable_frame_offset(builder, procedure, end);
}

/**
 * Returns whether `variable` is kept in the frame of its procedure and not in locals, which it is
 * when it is a variable definition whose address is taken, as in `%x`. The analysis found out, see
 * `Semantic_Binding`. A WebAssembly local has no address, so such a variable is in memory like the
 * elements of an array. The iterator of a for statement is never, because it cannot be assigned
 * to, and so its address is not taken.
 */
static bool wasm_variable_is_in_memory(Wasm_Builder const *builder, Ast_Node_Index variable) {
    assert(variable < builder->node_count);

    return builder->nodes[variable].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION &&
           builder->bindings[variable].is_address_taken;
}

/**
 * Returns the offset from the start of the frame of `procedure` of the variable `variable`, which
 * must be kept in the frame, see `wasm_variable_is_in_memory`. The variables come after the
 * elements of the array literals, one after the other in the order of the source, also the ones in
 * blocks, and take as many bytes as `wasm_type_stride_bytes` says. `variable` can be the end of
 * the procedure, which is the size of the frame.
 */
static size_t wasm_variable_frame_offset(Wasm_Builder const *builder, Ast_Node_Index procedure,
                                         Ast_Node_Index variable) {
    assert(procedure < builder->node_count);
    assert(variable > procedure);
    assert(variable <= builder->node_count);

    Ast_Node_Index end = ast_procedure_end(builder->nodes, builder->node_count, procedure);
    assert(variable <= end);

    size_t offset_bytes = wasm_frame_literal_offset(builder, procedure, end);
    for (Ast_Node_Index node = procedure + 1; node < variable; node += 1) {
        if (wasm_variable_is_in_memory(builder, node) == false) {
            continue;
        }
        // A type that the target does not have takes none, since it is rejected when it is written.
        Wasm_Type type = wasm_variable_type(builder, node);
        if (type.kind == WASM_VALUE_TYPE_NONE) {
            continue;
        }
        offset_bytes += wasm_type_stride_bytes(builder, type);
    }

    return offset_bytes;
}

/**
 * Writes the instructions that push the address of the variable `variable` of `procedure`, which is
 * kept in the frame, plus `offset_bytes`: the frame, and the offset of the variable in it.
 */
static void wasm_variable_address_write(Wasm_Builder *builder, Ast_Node_Index procedure,
                                        Ast_Node_Index variable, size_t offset_bytes) {
    assert(procedure < builder->node_count);
    assert(wasm_variable_is_in_memory(builder, variable));
    assert(offset_bytes <= WASM_STACK_SIZE_BYTES);

    size_t total_bytes = wasm_variable_frame_offset(builder, procedure, variable) + offset_bytes;
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET,
                                 wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_FRAME));
    if (total_bytes > 0) {
        wasm_write_i32_const(&builder->writer, (int64_t)total_bytes);
        wasm_write_byte(&builder->writer, WASM_OPCODE_I32_ADD);
    }
}

/**
 * Writes the instructions that trap when the pointer on top of the stack is null, which is zero and
 * the value of a pointer that was not given one, and leave it on the stack otherwise. The address
 * zero is where the strings start, so without the check a null pointer would read them instead of
 * stopping the program. The pointer is held in a scratch local for the test, between instructions
 * that follow each other only.
 */
static void wasm_pointer_check_write(Wasm_Builder *builder, Ast_Node_Index procedure) {
    assert(procedure < builder->node_count);
    assert(wasm_procedure_uses_arrays(builder, procedure));

    size_t held = wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_TEMPORARY_32);

    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_TEE, held);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_EQZ);
    wasm_structured_trap(builder);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, held);
}

/**
 * Returns the offset from the start of the frame of `procedure` of the elements of the array
 * literal `literal`, which is the size of the array literals before it. `literal` can be the end of
 * the procedure, which is the size of all of them. An array literal takes room for each of its
 * elements, and an element takes as many bytes as `wasm_type_stride_bytes` says. A literal with an
 * element type that the target does not have takes none, since it is rejected when it is written.
 */
static size_t wasm_frame_literal_offset(Wasm_Builder const *builder, Ast_Node_Index procedure,
                                        Ast_Node_Index literal) {
    assert(procedure < builder->node_count);
    assert(literal > procedure);
    assert(literal <= builder->node_count);

    size_t offset_bytes = 0;
    for (Ast_Node_Index node = procedure + 1; node < literal; node += 1) {
        if (builder->nodes[node].node_type != AST_NODE_TYPE_ARRAY_LITERAL) {
            continue;
        }
        Wasm_Type array_type = wasm_type_of_node(builder, builder->nodes[node].first_child);
        if (array_type.kind == WASM_VALUE_TYPE_NONE) {
            continue;
        }

        // The first child is the type, which is not an element.
        size_t element_count = wasm_children_count(builder, node) - 1;
        offset_bytes += element_count *
                        wasm_type_stride_bytes(builder, wasm_type_element(array_type));
    }

    return offset_bytes;
}

/**
 * Writes the start of a procedure that has a frame: it makes room for the elements of all the array
 * literals and for the variables whose address is taken by moving the stack pointer down, and
 * keeps where they are in a local, so that the address of an array literal or of a variable is that
 * plus its offset. A recursion that goes too deep would move
 * the pointer into the data, so the new pointer is compared with the end of the stack, as a signed
 * number because a frame larger than the pointer gives a negative one, and traps if it is lower.
 * The pointer goes back when the procedure returns, see `wasm_frame_leave`. A procedure without
 * array literals and without variables whose address is taken writes nothing.
 */
static void wasm_frame_enter(Wasm_Builder *builder, Ast_Node_Index procedure) {
    assert(procedure < builder->node_count);

    if (wasm_frame_is_needed(builder, procedure) == false) {
        return;
    }
    assert(builder->has_stack);

    size_t frame_size_bytes = wasm_frame_size_bytes(builder, procedure);
    assert(frame_size_bytes <= WASM_STACK_SIZE_BYTES);
    size_t frame_local = wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_FRAME);

    wasm_write_byte(&builder->writer, WASM_OPCODE_GLOBAL_GET);
    wasm_write_unsigned(&builder->writer, wasm_stack_global_index(builder));
    wasm_write_i32_const(&builder->writer, (int64_t)frame_size_bytes);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_SUB);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_TEE, frame_local);
    wasm_write_i32_const(&builder->writer, (int64_t)builder->stack_limit_bytes);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_LT_S);
    wasm_structured_trap(builder);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, frame_local);
    wasm_write_byte(&builder->writer, WASM_OPCODE_GLOBAL_SET);
    wasm_write_unsigned(&builder->writer, wasm_stack_global_index(builder));
}

/**
 * Writes the instructions that give the frame of `procedure` back, by moving the stack pointer up
 * to where it was. They leave the stack of values as it is, so they can come after the value of a
 * `return`. A procedure without a frame writes nothing.
 */
static void wasm_frame_leave(Wasm_Builder *builder, Ast_Node_Index procedure) {
    assert(procedure < builder->node_count);

    if (wasm_frame_is_needed(builder, procedure) == false) {
        return;
    }
    assert(builder->has_stack);

    size_t frame_local = wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_FRAME);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, frame_local);
    wasm_write_i32_const(&builder->writer, (int64_t)wasm_frame_size_bytes(builder, procedure));
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_ADD);
    wasm_write_byte(&builder->writer, WASM_OPCODE_GLOBAL_SET);
    wasm_write_unsigned(&builder->writer, wasm_stack_global_index(builder));
}

/**
 * Returns the number of elements of the array that `expression` is, which is an array literal or a
 * name that stands for a variable that was given one. The length of an array is not part of its
 * value at run time, only of the program, so the code that needs it, for example for a bounds
 * check, writes it as a constant. The variable was checked by `wasm_variables_check`.
 */
static size_t wasm_array_length(Wasm_Builder const *builder, Ast_Node_Index expression) {
    assert(expression < builder->node_count);

    Ast_Node_Index current = expression;
    // Each step follows a name to the value of its variable, which is above it, so there are fewer
    // steps than nodes.
    for (size_t i = 0; i < builder->node_count; i += 1) {
        if (builder->nodes[current].node_type == AST_NODE_TYPE_ARRAY_LITERAL) {
            // The first child is the type, which is not an element.
            return wasm_children_count(builder, current) - 1;
        }

        assert(builder->nodes[current].node_type == AST_NODE_TYPE_IDENTIFIER);
        Semantic_Binding binding = builder->bindings[current];
        assert(binding.kind == SEMANTIC_BINDING_KIND_VARIABLE);
        assert(builder->nodes[binding.target].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION);
        current = builder->nodes[binding.target].last_child;
    }

    // Unreachable: a chain of variables is shorter than the number of nodes.
    assert(false);
    return 0;
}

/**
 * Writes the length of `expression` if it is an array, after its address, so that the two are an
 * array slice of all of its elements. Everything that works on the elements takes an array slice,
 * so an array is made into one where it is given to something that does.
 */
static void wasm_array_decay(Wasm_Builder *builder, Ast_Node_Index expression) {
    assert(expression < builder->node_count);

    if (wasm_expression_type(builder, expression).kind == WASM_VALUE_TYPE_ARRAY) {
        wasm_write_i32_const(&builder->writer, (int64_t)wasm_array_length(builder, expression));
    }
}

/**
 * Writes the instructions that leave the address of an element, from the array slice and the index
 * that are on the stack: the address of the first element, its length as a 32-bit integer, and
 * the index as a 64-bit one. An index that is not below the length, which is also any negative
 * index, traps. The comparison is unsigned and 64-bit, so that an index that does not fit in 32
 * bits is not cut down to one that does. The address is moved up by the index times
 * `stride_bytes`, the size of an element.
 */
static void wasm_element_address_write(Wasm_Builder *builder, Ast_Node_Index procedure,
                                       size_t stride_bytes) {
    size_t low = wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_LOW);

    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_SET, low);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I64_EXTEND_I32_U);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, low);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I64_LE_U);
    wasm_structured_trap(builder);

    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, low);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_WRAP_I64);
    wasm_scale_write(builder, stride_bytes);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_ADD);
}

/**
 * Writes the instructions that make a new array slice from the one and the two indexes that are
 * on the stack: the address of the first element, its length, the first index and the end index,
 * which is not included. The new one starts at the first index and ends before the end index.
 * It traps unless the first index is not above the end index, and the end index is not above the
 * length. Both comparisons are unsigned, which also makes a negative index too large. The
 * address moves up by the first index times `stride_bytes`, the size of an element.
 */
static void wasm_slice_write(Wasm_Builder *builder, Ast_Node_Index procedure,
                             size_t stride_bytes) {
    size_t low = wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_LOW);
    size_t high = wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_HIGH);
    size_t length = wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_FIRST);

    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_SET, high);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_SET, low);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_SET, length);

    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, low);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, high);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I64_GT_U);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, high);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, length);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I64_EXTEND_I32_U);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I64_GT_U);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_OR);
    wasm_structured_trap(builder);

    // The address moves up by the first index, and the length is the distance between the indexes.
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, low);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_WRAP_I64);
    wasm_scale_write(builder, stride_bytes);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_ADD);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, high);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, low);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I64_SUB);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_WRAP_I64);
}

/**
 * Writes the instructions for the array literal `literal`, whose elements were written already,
 * each stored at its place in the frame by `wasm_build_code_section_body_child`. What is left is
 * the address of the first element, which is the frame plus the offset of the literal. The number
 * of elements must be the length of the type when it gives one, and the type of the elements must
 * be one that the target has. Returns false after it recorded an error.
 */
static bool wasm_build_code_section_body_array_literal(Wasm_Builder *builder,
                                                       Ast_Node_Index definition,
                                                       Ast_Node_Index literal) {
    assert(literal < builder->node_count);
    assert(builder->nodes[literal].node_type == AST_NODE_TYPE_ARRAY_LITERAL);

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index type = builder->nodes[literal].first_child;
    assert(builder->nodes[type].node_type == AST_NODE_TYPE_ARRAY_TYPE);
    assert(builder->nodes[type].payload.array_type.length_kind != AST_ARRAY_LENGTH_OMITTED);

    if (wasm_type_of_node(builder, type).kind == WASM_VALUE_TYPE_NONE) {
        return wasm_fail(builder, WASM_BUILD_STATUS_TYPE_UNSUPPORTED,
                         wasm_type_subject(builder, type));
    }

    size_t element_count = wasm_children_count(builder, literal) - 1;
    Ast_Array_Length_Kind length_kind = builder->nodes[type].payload.array_type.length_kind;
    if (length_kind == AST_ARRAY_LENGTH_FIXED &&
        (size_t)builder->nodes[type].payload.array_type.length != element_count) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_LENGTH_MISMATCH,
                         wasm_type_subject(builder, type));
    }

    size_t offset_bytes = wasm_frame_literal_offset(builder, procedure, literal);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET,
                                 wasm_scratch_local_index(builder, procedure,
                                                          WASM_SCRATCH_FRAME));
    wasm_write_i32_const(&builder->writer, (int64_t)offset_bytes);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_ADD);
    return true;
}

/**
 * Writes what the parent of an expression needs after the expression `child` was written. For an
 * array literal that is storing the element at its place in the frame. An `Int` element had the
 * address pushed before it, see `wasm_build_code_section_body_expression`, so it is one store, and
 * the other kinds of element are stored slot by slot. For the arguments of a call and the operands
 * of a subscript or a slice it is making an array into an array slice, see `wasm_array_decay`,
 * because those take array slices. Returns false after it recorded an error.
 */
static bool wasm_build_code_section_body_child(Wasm_Builder *builder, Ast_Node_Index definition,
                                               Ast_Node_Index parent, Ast_Node_Index child) {
    assert(parent < builder->node_count);
    assert(child < builder->node_count);

    Ast_Node_Type parent_type = builder->nodes[parent].node_type;
    if (parent_type == AST_NODE_TYPE_ARRAY_LITERAL) {
        Ast_Node_Index type = builder->nodes[parent].first_child;
        Wasm_Type array_type = wasm_type_of_node(builder, type);
        if (array_type.kind == WASM_VALUE_TYPE_NONE) {
            return wasm_fail(builder, WASM_BUILD_STATUS_TYPE_UNSUPPORTED,
                             wasm_type_subject(builder, type));
        }
        Wasm_Type element = wasm_type_element(array_type);
        if (wasm_type_equals(element, wasm_expression_type(builder, child)) == false) {
            return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_ELEMENT_TYPE_MISMATCH,
                             wasm_expression_subject(builder, definition, child));
        }

        // The position among the children, where the first is the type.
        size_t position = 0;
        Ast_Node_Index sibling = builder->nodes[parent].first_child;
        for (size_t i = 0; i < builder->node_count && sibling != child; i += 1) {
            position += 1;
            sibling = builder->nodes[sibling].next_sibling;
        }
        assert(sibling == child);
        assert(position >= 1);

        Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
        size_t offset_bytes = wasm_frame_literal_offset(builder, procedure, parent) +
                              (position - 1) * wasm_type_stride_bytes(builder, element);
        if (element.kind == WASM_VALUE_TYPE_INT) {
            wasm_write_memory_instruction(&builder->writer, WASM_OPCODE_I64_STORE, offset_bytes);
        } else {
            wasm_slots_store(builder, procedure, element,
                             wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_FRAME),
                             offset_bytes);
        }
        return true;
    }

    if (parent_type == AST_NODE_TYPE_CALL || parent_type == AST_NODE_TYPE_SLICE ||
        parent_type == AST_NODE_TYPE_SUBSCRIPT) {
        wasm_array_decay(builder, child);
    }
    return true;
}

/**
 * Writes the instructions for the subscript `subscript`, such as `a[i]`, whose array and index
 * were written already: the address of the element, and the load of its slots. Returns false after
 * it recorded an error.
 */
static bool wasm_build_code_section_body_subscript(Wasm_Builder *builder,
                                                   Ast_Node_Index definition,
                                                   Ast_Node_Index subscript) {
    assert(subscript < builder->node_count);
    assert(builder->nodes[subscript].node_type == AST_NODE_TYPE_SUBSCRIPT);

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index base = builder->nodes[subscript].first_child;
    Ast_Node_Index index = builder->nodes[subscript].last_child;

    Wasm_Type base_type = wasm_expression_type(builder, base);
    if (wasm_type_is_sequence(base_type) == false) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_EXPECTED,
                         wasm_expression_subject(builder, definition, base));
    }
    if (wasm_expression_type(builder, index).kind != WASM_VALUE_TYPE_INT) {
        return wasm_fail(builder, WASM_BUILD_STATUS_INDEX_TYPE_INVALID,
                         wasm_expression_subject(builder, definition, index));
    }

    Wasm_Type element = wasm_type_element(base_type);
    wasm_element_address_write(builder, procedure, wasm_type_stride_bytes(builder, element));
    wasm_slots_load_from_stack(builder, procedure, element, 0);
    return true;
}

/**
 * Writes the instructions that leave the address of the element that the subscript `subscript`
 * stands for, such as `a[i]`: the array and the index are written, and the address is computed from
 * them like the code of `wasm_build_code_section_body_subscript` does, with the same trap for an
 * index outside of the array, but nothing is loaded. Returns false after it recorded an error.
 */
static bool wasm_build_code_section_body_element_address(Wasm_Builder *builder,
                                                         Ast_Node_Index definition,
                                                         Ast_Node_Index subscript) {
    assert(subscript < builder->node_count);
    assert(builder->nodes[subscript].node_type == AST_NODE_TYPE_SUBSCRIPT);

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index base = builder->nodes[subscript].first_child;
    Ast_Node_Index index = builder->nodes[subscript].last_child;

    if (wasm_build_code_section_body_expression(builder, definition, base, false) == false) {
        return false;
    }
    Wasm_Type base_type = wasm_expression_type(builder, base);
    if (wasm_type_is_sequence(base_type) == false) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_EXPECTED,
                         wasm_expression_subject(builder, definition, base));
    }
    wasm_array_decay(builder, base);

    if (wasm_build_code_section_body_expression(builder, definition, index, false) == false) {
        return false;
    }
    if (wasm_expression_type(builder, index).kind != WASM_VALUE_TYPE_INT) {
        return wasm_fail(builder, WASM_BUILD_STATUS_INDEX_TYPE_INVALID,
                         wasm_expression_subject(builder, definition, index));
    }
    wasm_element_address_write(builder, procedure,
                               wasm_type_stride_bytes(builder, wasm_type_element(base_type)));
    return true;
}

/**
 * Writes the instructions that push the value of `pointer`, which must be a pointer. With
 * `is_checked` they trap when it is null, see `wasm_pointer_check_write`, which is what a pointer
 * that is followed needs, and not one that is only passed on, such as the operand of `%p^`. Returns
 * false after it recorded an error.
 */
static bool wasm_build_code_section_body_pointer(Wasm_Builder *builder, Ast_Node_Index definition,
                                                 Ast_Node_Index pointer, bool is_checked) {
    assert(pointer < builder->node_count);

    if (wasm_build_code_section_body_expression(builder, definition, pointer, false) == false) {
        return false;
    }
    if (wasm_expression_type(builder, pointer).kind != WASM_VALUE_TYPE_POINTER) {
        return wasm_fail(builder, WASM_BUILD_STATUS_POINTER_EXPECTED,
                         wasm_expression_subject(builder, definition, pointer));
    }
    if (is_checked) {
        wasm_pointer_check_write(builder, wasm_procedure_of(builder, definition));
    }
    return true;
}

/**
 * Writes the instructions for the dereference `dereference`, such as `p^`, whose pointer was
 * written already: the null check, and the load of the slots of what it points to. Returns false
 * after it recorded an error.
 */
static bool wasm_build_code_section_body_dereference(Wasm_Builder *builder,
                                                     Ast_Node_Index definition,
                                                     Ast_Node_Index dereference) {
    assert(dereference < builder->node_count);
    assert(builder->nodes[dereference].node_type == AST_NODE_TYPE_DEREFERENCE);

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index pointer = builder->nodes[dereference].first_child;

    Wasm_Type pointer_type = wasm_expression_type(builder, pointer);
    if (pointer_type.kind != WASM_VALUE_TYPE_POINTER) {
        return wasm_fail(builder, WASM_BUILD_STATUS_POINTER_EXPECTED,
                         wasm_expression_subject(builder, definition, pointer));
    }

    wasm_pointer_check_write(builder, procedure);
    wasm_slots_load_from_stack(builder, procedure, wasm_type_pointee(pointer_type), 0);
    return true;
}

/**
 * Writes the instructions of an assignment to what a pointer points to, such as `p^ = 5` or
 * `p^ += 1`. The pointer is written first, and kept in a local while the value is evaluated, like
 * the address of an element is, see `wasm_build_code_section_body_place_store`. Returns false after
 * it recorded an error.
 */
static bool wasm_build_code_section_body_dereference_assignment(Wasm_Builder *builder,
                                                                Ast_Node_Index definition,
                                                                Ast_Node_Index assignment) {
    assert(assignment < builder->node_count);

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index target = builder->nodes[assignment].first_child;
    assert(builder->nodes[target].node_type == AST_NODE_TYPE_DEREFERENCE);
    Ast_Node_Index pointer = builder->nodes[target].first_child;

    if (wasm_build_code_section_body_pointer(builder, definition, pointer, true) == false) {
        return false;
    }
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_SET,
                                 wasm_scratch_local_index(builder, procedure,
                                                          WASM_SCRATCH_ADDRESS));

    Wasm_Type pointee = wasm_type_pointee(wasm_expression_type(builder, pointer));
    return wasm_build_code_section_body_place_store(builder, definition, assignment, pointee, 0);
}

/**
 * Writes the instructions for the address-of `address_of`, such as `%x`: the address of the
 * operand, which is a place in memory. Only a few operands have one:
 *
 * - A variable, or a field of one, such as `x` and `x.a.b`. A variable whose address is taken is in
 *   the frame of its procedure and not in locals, and its address is the frame plus its offset.
 *   A parameter, the iterator of a for statement and a global variable have none.
 * - An element of an array or an array slice, or a field of one, such as `a[i]` and `a[i].age`:
 *   the address that a subscript computes, with the same trap for an index outside of the array.
 * - What a pointer points to, or a field of it, such as `p^` and `p^.age`: `%p^` is `p`, and the
 *   address of the field is `p` plus the offset of the field.
 *
 * An operand that is none of these is a value without a place, such as a number or a call, and the
 * type of it must be one that can be pointed to, see `wasm_type_pointer_to`. Returns false after it
 * recorded an error.
 */
static bool wasm_build_code_section_body_address_of(Wasm_Builder *builder,
                                                    Ast_Node_Index definition,
                                                    Ast_Node_Index address_of) {
    assert(address_of < builder->node_count);
    assert(builder->nodes[address_of].node_type == AST_NODE_TYPE_ADDRESS_OF);

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index operand = builder->nodes[address_of].first_child;
    Str subject = wasm_expression_subject(builder, definition, operand);

    if (wasm_type_pointer_to(wasm_expression_type(builder, operand)).kind ==
        WASM_VALUE_TYPE_NONE) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ADDRESS_INVALID, subject);
    }

    // `%p^` is the pointer, and following it is not needed to know where it points to.
    if (builder->nodes[operand].node_type == AST_NODE_TYPE_DEREFERENCE) {
        return wasm_build_code_section_body_pointer(builder, definition,
                                                    builder->nodes[operand].first_child, false);
    }

    // What the address is the address of, and how far into it the operand is: a field is some
    // slots after the start of the struct that it is a part of.
    Ast_Node_Index root = operand;
    size_t offset_bytes = 0;
    if (builder->nodes[operand].node_type == AST_NODE_TYPE_FIELD_ACCESS) {
        Wasm_Field_Path path = wasm_field_path(builder, operand);
        if (path.status != WASM_BUILD_STATUS_OK) {
            return wasm_fail(builder, path.status, path.subject);
        }
        root = path.root;
        offset_bytes = path.slot_offset * WASM_ELEMENT_SIZE_BYTES;

        // The address of a field of what a pointer points to follows the pointer, so it is a null
        // pointer that is reported and not a small address.
        if (path.is_through_pointer) {
            if (wasm_build_code_section_body_pointer(builder, definition, root, true) == false) {
                return false;
            }
            if (offset_bytes > 0) {
                wasm_write_i32_const(&builder->writer, (int64_t)offset_bytes);
                wasm_write_byte(&builder->writer, WASM_OPCODE_I32_ADD);
            }
            return true;
        }
    }

    switch (builder->nodes[root].node_type) {
    case AST_NODE_TYPE_IDENTIFIER: {
        Semantic_Binding binding = builder->bindings[root];
        if (binding.kind != SEMANTIC_BINDING_KIND_VARIABLE ||
            builder->nodes[binding.target].node_type != AST_NODE_TYPE_VARIABLE_DEFINITION) {
            return wasm_fail(builder, WASM_BUILD_STATUS_ADDRESS_INVALID,
                             builder->nodes[root].payload.identifier.name);
        }
        // The analysis marked the variable, which is how it came to be in the frame.
        wasm_variable_address_write(builder, procedure, binding.target, offset_bytes);
        return true;
    }
    case AST_NODE_TYPE_SUBSCRIPT:
        if (wasm_build_code_section_body_element_address(builder, definition, root) == false) {
            return false;
        }
        break;
    case AST_NODE_TYPE_DEREFERENCE:
    case AST_NODE_TYPE_ENUM_LITERAL:
    case AST_NODE_TYPE_ENUM_MEMBER:
    case AST_NODE_TYPE_ENUM_TYPE:
    case AST_NODE_TYPE_ADDITION:
    case AST_NODE_TYPE_ADDITION_ASSIGNMENT:
    case AST_NODE_TYPE_ADDRESS_OF:
    case AST_NODE_TYPE_AND:
    case AST_NODE_TYPE_ARRAY_LITERAL:
    case AST_NODE_TYPE_ARRAY_TYPE:
    case AST_NODE_TYPE_ASSIGNMENT:
    case AST_NODE_TYPE_ATTRIBUTE:
    case AST_NODE_TYPE_BLOCK:
    case AST_NODE_TYPE_BOOL_LITERAL:
    case AST_NODE_TYPE_BREAK_STATEMENT:
    case AST_NODE_TYPE_CALL:
    case AST_NODE_TYPE_CALL_STATEMENT:
    case AST_NODE_TYPE_COMPARISON:
    case AST_NODE_TYPE_CONSTANT_DEFINITION:
    case AST_NODE_TYPE_CONTINUE_STATEMENT:
    case AST_NODE_TYPE_DIVISION:
    case AST_NODE_TYPE_DIVISION_ASSIGNMENT:
    case AST_NODE_TYPE_FIELD:
    case AST_NODE_TYPE_FIELD_ACCESS:
    case AST_NODE_TYPE_FIELD_INITIALIZER:
    case AST_NODE_TYPE_FOR_STATEMENT:
    case AST_NODE_TYPE_FOREIGN_BODY:
    case AST_NODE_TYPE_FOREIGN_IMPORT:
    case AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION:
    case AST_NODE_TYPE_IF_STATEMENT:
    case AST_NODE_TYPE_INTEGER_LITERAL:
    case AST_NODE_TYPE_LABEL:
    case AST_NODE_TYPE_LABELED_STATEMENT:
    case AST_NODE_TYPE_LOOP_STATEMENT:
    case AST_NODE_TYPE_MODULE:
    case AST_NODE_TYPE_MULTIPLICATION:
    case AST_NODE_TYPE_MULTIPLICATION_ASSIGNMENT:
    case AST_NODE_TYPE_NIL_LITERAL:
    case AST_NODE_TYPE_NOT:
    case AST_NODE_TYPE_OR:
    case AST_NODE_TYPE_PARAMETER:
    case AST_NODE_TYPE_POINTER_TYPE:
    case AST_NODE_TYPE_PROCEDURE:
    case AST_NODE_TYPE_PROCEDURE_TYPE:
    case AST_NODE_TYPE_RANGE:
    case AST_NODE_TYPE_RETURN_STATEMENT:
    case AST_NODE_TYPE_SCOPE_STATEMENT:
    case AST_NODE_TYPE_SLICE:
    case AST_NODE_TYPE_STRING_LITERAL:
    case AST_NODE_TYPE_STRUCT_LITERAL:
    case AST_NODE_TYPE_STRUCT_TYPE:
    case AST_NODE_TYPE_SUBTRACTION:
    case AST_NODE_TYPE_SUBTRACTION_ASSIGNMENT:
    case AST_NODE_TYPE_TYPE_NAME:
    case AST_NODE_TYPE_VARIABLE_DEFINITION:
        // A value without a place, such as a number, a call or the result of an operator.
        return wasm_fail(builder, WASM_BUILD_STATUS_ADDRESS_INVALID, subject);
    }

    // The address of an element or of what a pointer points to is on the stack, and the field is
    // some slots into it.
    if (offset_bytes > 0) {
        wasm_write_i32_const(&builder->writer, (int64_t)offset_bytes);
        wasm_write_byte(&builder->writer, WASM_OPCODE_I32_ADD);
    }
    return true;
}

/**
 * Writes the instructions for the slice `slice`, such as `a[1:4]`, whose array, first index and end
 * index were written already: the array slice that they make. Returns false after it recorded an
 * error.
 */
static bool wasm_build_code_section_body_slice(Wasm_Builder *builder, Ast_Node_Index definition,
                                               Ast_Node_Index slice) {
    assert(slice < builder->node_count);
    assert(builder->nodes[slice].node_type == AST_NODE_TYPE_SLICE);

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index base = builder->nodes[slice].first_child;
    Ast_Node_Index first = builder->nodes[base].next_sibling;
    Ast_Node_Index end = builder->nodes[slice].last_child;
    assert(first != end);

    Wasm_Type base_type = wasm_expression_type(builder, base);
    if (wasm_type_is_sequence(base_type) == false) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_EXPECTED,
                         wasm_expression_subject(builder, definition, base));
    }
    Ast_Node_Index indexes[2] = {first, end};
    for (size_t i = 0; i < 2; i += 1) {
        if (wasm_expression_type(builder, indexes[i]).kind != WASM_VALUE_TYPE_INT) {
            return wasm_fail(builder, WASM_BUILD_STATUS_INDEX_TYPE_INVALID,
                             wasm_expression_subject(builder, definition, indexes[i]));
        }
    }

    wasm_slice_write(builder, procedure,
                     wasm_type_stride_bytes(builder, wasm_type_element(base_type)));
    return true;
}

/**
 * Writes the instructions of an assignment whose target is an element, such as `a[i] = 1` or
 * `a[i] += 1`, or a range of elements, such as `a[1:3] = b`. The address of an element is computed
 * first and kept in a local while the value is evaluated, see
 * `wasm_build_code_section_body_place_store`. A range of elements is copied from the array or the
 * array slice on the right, which must have as many elements of the same type, or the program
 * traps. The copy is a `memory.copy`, which is right when the two overlap. Returns false after it
 * recorded an error.
 */
static bool wasm_build_code_section_body_element_assignment(Wasm_Builder *builder,
                                                            Ast_Node_Index definition,
                                                            Ast_Node_Index assignment) {
    assert(assignment < builder->node_count);
    assert(ast_node_type_is_assignment(builder->nodes[assignment].node_type));

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index target = builder->nodes[assignment].first_child;
    Ast_Node_Index value = builder->nodes[assignment].last_child;
    bool is_plain = builder->nodes[assignment].node_type == AST_NODE_TYPE_ASSIGNMENT;

    if (builder->nodes[target].node_type == AST_NODE_TYPE_SLICE) {
        // Only the plain assignment copies. An operator such as `+=` is for one `Int`.
        if (is_plain == false) {
            return wasm_fail(builder, WASM_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID,
                             wasm_expression_subject(builder, definition, target));
        }

        if (wasm_build_code_section_body_expression(builder, definition, target, false) == false) {
            return false;
        }
        if (wasm_build_code_section_body_expression(builder, definition, value, false) == false) {
            return false;
        }
        Wasm_Type target_type = wasm_expression_type(builder, target);
        Wasm_Type value_type = wasm_expression_type(builder, value);
        if (wasm_type_is_sequence(value_type) == false) {
            return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_EXPECTED,
                             wasm_expression_subject(builder, definition, value));
        }
        if (wasm_type_equals(wasm_type_element(target_type), wasm_type_element(value_type)) ==
            false) {
            return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_ELEMENT_TYPE_MISMATCH,
                             wasm_expression_subject(builder, definition, value));
        }
        wasm_array_decay(builder, value);

        // The stack has the address and the length of the target, and of the value. The lengths
        // must be the same. Then the elements are copied, as many bytes as they take.
        size_t source_length = wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_FIRST);
        size_t source_address = wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_SECOND);
        wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_SET, source_length);
        wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_SET, source_address);
        wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, source_length);
        wasm_write_byte(&builder->writer, WASM_OPCODE_I32_NE);
        wasm_structured_trap(builder);
        wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, source_address);
        wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, source_length);
        wasm_scale_write(builder,
                         wasm_type_stride_bytes(builder, wasm_type_element(value_type)));
        wasm_write_byte(&builder->writer, WASM_OPCODE_PREFIX_MISC);
        wasm_write_unsigned(&builder->writer, WASM_OPCODE_MISC_MEMORY_COPY);
        // Both memory operands, the destination and the source, are memory 0.
        wasm_write_byte(&builder->writer, 0x00);
        wasm_write_byte(&builder->writer, 0x00);
        return true;
    }

    assert(builder->nodes[target].node_type == AST_NODE_TYPE_SUBSCRIPT);
    Ast_Node_Index base = builder->nodes[target].first_child;
    Ast_Node_Index index = builder->nodes[target].last_child;

    if (wasm_build_code_section_body_expression(builder, definition, base, false) == false) {
        return false;
    }
    Wasm_Type base_type = wasm_expression_type(builder, base);
    if (wasm_type_is_sequence(base_type) == false) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_EXPECTED,
                         wasm_expression_subject(builder, definition, base));
    }
    wasm_array_decay(builder, base);

    if (wasm_build_code_section_body_expression(builder, definition, index, false) == false) {
        return false;
    }
    if (wasm_expression_type(builder, index).kind != WASM_VALUE_TYPE_INT) {
        return wasm_fail(builder, WASM_BUILD_STATUS_INDEX_TYPE_INVALID,
                         wasm_expression_subject(builder, definition, index));
    }
    Wasm_Type element = wasm_type_element(base_type);
    wasm_element_address_write(builder, procedure, wasm_type_stride_bytes(builder, element));
    wasm_write_local_instruction(
        &builder->writer, WASM_OPCODE_LOCAL_SET,
        wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_ADDRESS));

    return wasm_build_code_section_body_place_store(builder, definition, assignment, element, 0);
}

/**
 * Writes the start of a for statement that goes through the elements of an array or an array
 * slice: the elements are written, and the address of the first element and the address after the
 * last one are stored in the two locals after the iterator, which has the locals of an element.
 * Then come a `block` that a finished loop leaves and a `loop` that is repeated, as for a range,
 * and the test that leaves the block when the address of the next element is the end. The
 * iterator is loaded from that address, and the address moves on to the next element before the
 * statements of the block, which are in a `block` of their own that `continue` leaves. The loop
 * is closed by `wasm_build_code_section_body_for_end`, which has nothing to step. Returns false
 * after it recorded an error.
 */
static bool wasm_build_code_section_body_for_each(Wasm_Builder *builder, Ast_Node_Index definition,
                                                  Ast_Node_Index statement) {
    assert(statement < builder->node_count);
    assert(wasm_for_is_each(builder, statement));

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index elements = builder->nodes[statement].first_child;
    Wasm_Type element = wasm_variable_type(builder, statement);
    size_t stride_bytes = wasm_type_stride_bytes(builder, element);
    size_t iterator = wasm_variable_local_index(builder, procedure, statement);
    size_t cursor = iterator + wasm_type_slot_count(builder, element);
    size_t end = cursor + 1;

    if (wasm_build_code_section_body_expression(builder, definition, elements, false) == false) {
        return false;
    }
    if (wasm_type_is_sequence(wasm_expression_type(builder, elements)) == false) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_EXPECTED,
                         wasm_expression_subject(builder, definition, elements));
    }
    wasm_array_decay(builder, elements);

    // The stack has the address and the length. The end is the address plus the size of the
    // elements.
    wasm_scale_write(builder, stride_bytes);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_SET, end);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_TEE, cursor);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, end);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_ADD);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_SET, end);

    wasm_structured_begin(builder, WASM_OPCODE_BLOCK);
    wasm_structured_begin(builder, WASM_OPCODE_LOOP);

    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, cursor);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, end);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_GE_U);
    // Label 0 is the loop, and label 1 is the block around it, which is left.
    wasm_write_byte(&builder->writer, WASM_OPCODE_BR_IF);
    wasm_write_unsigned(&builder->writer, 1);

    wasm_slots_load(builder, element, cursor, 0);
    wasm_slots_set(builder, element, WASM_OPCODE_LOCAL_SET, iterator);

    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, cursor);
    wasm_write_i32_const(&builder->writer, (int64_t)stride_bytes);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_ADD);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_SET, cursor);

    wasm_structured_begin(builder, WASM_OPCODE_BLOCK);
    return true;
}

/**
 * Returns a type of the kind `kind` that has nothing else to say, which is any kind but a struct,
 * a pointer, an array and an array slice.
 */
static Wasm_Type wasm_type_make(Wasm_Value_Type kind) {
    return (Wasm_Type){
        .kind = kind,
        .element_kind = WASM_VALUE_TYPE_NONE,
        .pointee_kind = WASM_VALUE_TYPE_NONE,
        .element_pointee_kind = WASM_VALUE_TYPE_NONE,
        .definition = AST_NODE_INDEX_NONE,
        .element_definition = AST_NODE_INDEX_NONE,
        .depth = 0,
        .element_depth = 0,
    };
}

/**
 * Returns the type of a pointer to a value of the type `target`, which is a pointer with one more
 * caret when `target` is a pointer already. Only an `Int`, a `Bool`, a `Str`, a struct, a pointer
 * or a pointer to a procedure is pointed to: there is no pointer to an array or to an array slice,
 * so those, and a type that the target does not have, give a type of the kind
 * `WASM_VALUE_TYPE_NONE`.
 */
static Wasm_Type wasm_type_pointer_to(Wasm_Type target) {
    assert(target.kind != WASM_VALUE_TYPE_POINTER || target.depth >= 1);
    assert(target.kind == WASM_VALUE_TYPE_POINTER || target.depth == 0);

    switch (target.kind) {
    case WASM_VALUE_TYPE_BOOL:
    case WASM_VALUE_TYPE_ENUM:
    case WASM_VALUE_TYPE_INT:
    case WASM_VALUE_TYPE_PROCEDURE:
    case WASM_VALUE_TYPE_STR:
    case WASM_VALUE_TYPE_STRUCT: {
        Wasm_Type pointer = wasm_type_make(WASM_VALUE_TYPE_POINTER);
        pointer.pointee_kind = target.kind;
        pointer.definition = target.definition;
        pointer.depth = 1;
        return pointer;
    }
    case WASM_VALUE_TYPE_POINTER: {
        Wasm_Type pointer = target;
        assert(pointer.depth < UINT32_MAX);
        pointer.depth += 1;
        return pointer;
    }
    case WASM_VALUE_TYPE_ARRAY:
    case WASM_VALUE_TYPE_ENUM_LITERAL:
    case WASM_VALUE_TYPE_NIL:
    case WASM_VALUE_TYPE_NONE:
    case WASM_VALUE_TYPE_SLICE:
        break;
    }

    return wasm_type_make(WASM_VALUE_TYPE_NONE);
}

/**
 * Returns the type that the pointer type `pointer` points to: the pointer type with one caret less,
 * or the type at the end of the carets when it has one.
 */
static Wasm_Type wasm_type_pointee(Wasm_Type pointer) {
    assert(pointer.kind == WASM_VALUE_TYPE_POINTER);
    assert(pointer.depth >= 1);

    if (pointer.depth > 1) {
        Wasm_Type inner = pointer;
        inner.depth -= 1;
        return inner;
    }

    Wasm_Type pointee = wasm_type_make(pointer.pointee_kind);
    pointee.definition = pointer.definition;
    return pointee;
}

/**
 * Returns the struct called `name`, which is the `AST_NODE_TYPE_STRUCT_TYPE` that is the value of
 * the constant of that name, or `AST_NODE_INDEX_NONE` when the module has no struct of that name.
 */
static Ast_Node_Index wasm_struct_find(Wasm_Builder const *builder, Str name) {
    assert(name.bytes != NULL);

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
 * Returns the enum called `name`, which is the `AST_NODE_TYPE_ENUM_TYPE` that is the value of the
 * constant of that name, or `AST_NODE_INDEX_NONE` when the module has no enum of that name.
 */
static Ast_Node_Index wasm_enum_find(Wasm_Builder const *builder, Str name) {
    assert(name.bytes != NULL);

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
 * Finds the member called `name` of the enum `enum_node`. Returns whether there is one, and then
 * sets `*number` to its number, which is the count of the members before it.
 */
static bool wasm_enum_member_find(Wasm_Builder const *builder, Ast_Node_Index enum_node, Str name,
                                  size_t *number) {
    assert(builder->nodes[enum_node].node_type == AST_NODE_TYPE_ENUM_TYPE);

    Ast_Node_Index member = builder->nodes[enum_node].first_child;
    // Every member is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && member != AST_NODE_INDEX_NONE; i += 1) {
        if (str_equals(builder->nodes[member].payload.enum_member.name, name)) {
            *number = i;
            return true;
        }
        member = builder->nodes[member].next_sibling;
    }

    return false;
}

/**
 * Returns the enum that `access`, an `AST_NODE_TYPE_FIELD_ACCESS`, is a member of, as in
 * `Color.RED`: the name before the dot is a constant that is an enum. Returns
 * `AST_NODE_INDEX_NONE` when it is the access of a field, which is the usual meaning of the dot.
 */
static Ast_Node_Index wasm_enum_of_access(Wasm_Builder const *builder, Ast_Node_Index access) {
    assert(builder->nodes[access].node_type == AST_NODE_TYPE_FIELD_ACCESS);

    Ast_Node_Index base = builder->nodes[access].first_child;
    if (builder->nodes[base].node_type != AST_NODE_TYPE_IDENTIFIER ||
        builder->bindings[base].kind != SEMANTIC_BINDING_KIND_CONSTANT) {
        return AST_NODE_INDEX_NONE;
    }

    Ast_Node_Index value = builder->nodes[builder->bindings[base].target].first_child;
    if (builder->nodes[value].node_type != AST_NODE_TYPE_ENUM_TYPE) {
        return AST_NODE_INDEX_NONE;
    }
    return value;
}

/**
 * Returns the node that has `child` as one of its children, or `AST_NODE_INDEX_NONE` when there is
 * none, which is the case for the root of the tree. The nodes do not know their parents, and this
 * is only asked for the few nodes whose meaning depends on the place they are in.
 */
static Ast_Node_Index wasm_parent_find(Wasm_Builder const *builder, Ast_Node_Index child) {
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
 * Returns the type of the struct literal `literal`: the struct that it names, or for a literal
 * without a name the type of the field that it is the value of, which is found from the literal
 * around it. The kind is `WASM_VALUE_TYPE_NONE` when there is none.
 */
static Wasm_Type wasm_struct_literal_type(Wasm_Builder const *builder, Ast_Node_Index literal) {
    assert(builder->nodes[literal].node_type == AST_NODE_TYPE_STRUCT_LITERAL);

    // The names of the fields from the literal outwards, up to the literal that has a name.
    Str names[AST_BLOCK_DEPTH_MAX];
    size_t name_count = 0;
    Wasm_Type type = wasm_type_make(WASM_VALUE_TYPE_NONE);
    Ast_Node_Index current = literal;
    for (size_t i = 0; i <= AST_BLOCK_DEPTH_MAX; i += 1) {
        Str name = builder->nodes[current].payload.struct_literal.name;
        if (name.bytes != NULL) {
            type = wasm_type_of_name(builder, name);
            break;
        }

        Ast_Node_Index initializer = wasm_parent_find(builder, current);
        if (initializer == AST_NODE_INDEX_NONE ||
            builder->nodes[initializer].node_type != AST_NODE_TYPE_FIELD_INITIALIZER ||
            name_count == AST_BLOCK_DEPTH_MAX) {
            return wasm_type_make(WASM_VALUE_TYPE_NONE);
        }
        names[name_count] = builder->nodes[initializer].payload.field_initializer.name;
        name_count += 1;

        current = wasm_parent_find(builder, initializer);
        if (current == AST_NODE_INDEX_NONE ||
            builder->nodes[current].node_type != AST_NODE_TYPE_STRUCT_LITERAL) {
            return wasm_type_make(WASM_VALUE_TYPE_NONE);
        }
    }

    for (size_t i = name_count; i > 0; i -= 1) {
        size_t slot_offset = 0;
        if (type.kind != WASM_VALUE_TYPE_STRUCT ||
            wasm_struct_field_find(builder, type.definition, names[i - 1], &slot_offset, &type) ==
                false) {
            return wasm_type_make(WASM_VALUE_TYPE_NONE);
        }
    }
    return type;
}

/**
 * Returns the enum that the member `literal`, an `AST_NODE_TYPE_ENUM_LITERAL` such as `.RED`, is
 * a member of, which is the one that the place it is in wants: the type of the variable that it is
 * the value of, of what it is assigned to, of the result of the procedure it is returned from, of
 * the parameter or the field or the element that it is given to, or of what it is compared with.
 * Returns `AST_NODE_INDEX_NONE` when the place does not say, which is an error. `definition` is the
 * constant that the literal is in.
 */
static Ast_Node_Index wasm_enum_literal_expected(Wasm_Builder const *builder,
                                                 Ast_Node_Index definition,
                                                 Ast_Node_Index literal) {
    assert(builder->nodes[literal].node_type == AST_NODE_TYPE_ENUM_LITERAL);

    Ast_Node_Index parent = wasm_parent_find(builder, literal);
    if (parent == AST_NODE_INDEX_NONE) {
        return AST_NODE_INDEX_NONE;
    }

    Ast_Node const *parent_node = &builder->nodes[parent];
    Wasm_Type expected = wasm_type_make(WASM_VALUE_TYPE_NONE);
    switch (parent_node->node_type) {
    case AST_NODE_TYPE_RETURN_STATEMENT: {
        Str return_type = ast_procedure_return_type(
            &builder->nodes[wasm_procedure_of(builder, definition)]);
        if (return_type.bytes != NULL) {
            expected = wasm_type_of_name(builder, return_type);
        }
        break;
    }
    case AST_NODE_TYPE_VARIABLE_DEFINITION:
        // The type is the first child when the definition writes it, and the literal is the last.
        if (parent_node->first_child != literal &&
            wasm_node_is_type(builder, parent_node->first_child)) {
            expected = wasm_type_of_node(builder, parent_node->first_child);
        }
        break;
    case AST_NODE_TYPE_ASSIGNMENT:
        if (parent_node->last_child == literal) {
            expected = wasm_expression_type(builder, parent_node->first_child);
        }
        break;
    case AST_NODE_TYPE_CALL: {
        Ast_Node_Index callee = wasm_call_signature(builder, parent);
        size_t position = 0;
        Ast_Node_Index argument = parent_node->first_child;
        while (argument != literal) {
            position += 1;
            argument = builder->nodes[argument].next_sibling;
        }
        if (callee != AST_NODE_INDEX_NONE &&
            position < wasm_signature_parameter_count(builder, callee)) {
            expected = wasm_type_of_node(builder,
                                         wasm_signature_parameter_type(builder, callee, position));
        }
        break;
    }
    case AST_NODE_TYPE_COMPARISON: {
        Ast_Node_Index other = parent_node->first_child;
        if (other == literal) {
            other = parent_node->last_child;
        }
        expected = wasm_expression_type(builder, other);
        break;
    }
    case AST_NODE_TYPE_ARRAY_LITERAL:
        // The first child is the type of the array, and the others are its elements.
        if (parent_node->first_child != literal) {
            Wasm_Type array_type = wasm_type_of_node(builder, parent_node->first_child);
            if (wasm_type_is_sequence(array_type)) {
                expected = wasm_type_element(array_type);
            }
        }
        break;
    case AST_NODE_TYPE_FIELD_INITIALIZER: {
        Ast_Node_Index struct_literal = wasm_parent_find(builder, parent);
        if (struct_literal != AST_NODE_INDEX_NONE) {
            Wasm_Type struct_type = wasm_struct_literal_type(builder, struct_literal);
            size_t slot_offset = 0;
            if (struct_type.kind == WASM_VALUE_TYPE_STRUCT) {
                (void)wasm_struct_field_find(builder, struct_type.definition,
                                             parent_node->payload.field_initializer.name,
                                             &slot_offset, &expected);
            }
        }
        break;
    }
    default:
        break;
    }

    if (expected.kind != WASM_VALUE_TYPE_ENUM) {
        return AST_NODE_INDEX_NONE;
    }
    return expected.definition;
}

/**
 * Writes the instruction that pushes the number of the member `member_name` of the enum
 * `enum_node`, which is a 32-bit constant. Returns false after it recorded an error when the enum
 * has no such member, or when it is not known, which is `AST_NODE_INDEX_NONE`.
 */
static bool wasm_enum_member_write(Wasm_Builder *builder, Ast_Node_Index enum_node,
                                   Str member_name) {
    if (enum_node == AST_NODE_INDEX_NONE) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ENUM_TYPE_UNKNOWN, member_name);
    }

    size_t number = 0;
    if (wasm_enum_member_find(builder, enum_node, member_name, &number) == false) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ENUM_MEMBER_UNKNOWN, member_name);
    }
    wasm_write_i32_const(&builder->writer, (int64_t)number);
    return true;
}

/**
 * Returns the type that `type_name` stands for, which is `Bool`, `Int`, `Str` or the name of a
 * struct, or a type of the kind `WASM_VALUE_TYPE_NONE` if the target does not have a type of that
 * name. The text can have the carets of a pointer type before the name, such as `^Int`, which is
 * how a return type is kept, see `Ast_Node.payload.procedure`, and then the type is a pointer to
 * that. Spaces after a caret are skipped, because the source can have them.
 */
static Wasm_Type wasm_type_of_name(Wasm_Builder const *builder, Str type_name) {
    assert(type_name.bytes != NULL);
    assert(type_name.length > 0);

    size_t start = 0;
    size_t depth = 0;
    // Every step consumes a byte, so there are at most as many steps as bytes.
    for (size_t i = 0; i < type_name.length; i += 1) {
        char character = type_name.bytes[start];
        if (character == '^') {
            depth += 1;
        } else if (character != ' ' && character != '\t') {
            break;
        }
        start += 1;
    }
    if (start == type_name.length) {
        return wasm_type_make(WASM_VALUE_TYPE_NONE);
    }

    Wasm_Type base = wasm_type_of_plain_name(
        builder, str_make(type_name.bytes + start, type_name.length - start));
    if (depth == 0) {
        return base;
    }

    // A pointer to a pointer is one with one more caret, so the type is made from the base once
    // and the carets, which were counted, are set.
    assert(depth <= UINT32_MAX);
    Wasm_Type pointer = wasm_type_pointer_to(base);
    if (pointer.kind == WASM_VALUE_TYPE_POINTER) {
        pointer.depth = (uint32_t)depth;
    }
    return pointer;
}

/**
 * Returns the type that the name `type_name` stands for, which has no carets before it: `Bool`,
 * `Int`, `Str` or the name of a struct, or a type of the kind `WASM_VALUE_TYPE_NONE` if the target
 * does not have a type of that name.
 */
static Wasm_Type wasm_type_of_plain_name(Wasm_Builder const *builder, Str type_name) {
    assert(type_name.bytes != NULL);
    assert(type_name.length > 0);

    if (str_equals(type_name, STR("Bool"))) {
        return wasm_type_make(WASM_VALUE_TYPE_BOOL);
    }
    if (str_equals(type_name, STR("Int"))) {
        return wasm_type_make(WASM_VALUE_TYPE_INT);
    }
    if (str_equals(type_name, STR("Str"))) {
        return wasm_type_make(WASM_VALUE_TYPE_STR);
    }

    Ast_Node_Index enum_definition = wasm_enum_find(builder, type_name);
    if (enum_definition != AST_NODE_INDEX_NONE) {
        Wasm_Type enum_type = wasm_type_make(WASM_VALUE_TYPE_ENUM);
        enum_type.definition = enum_definition;
        return enum_type;
    }

    Ast_Node_Index definition = wasm_struct_find(builder, type_name);
    if (definition == AST_NODE_INDEX_NONE) {
        return wasm_type_make(WASM_VALUE_TYPE_NONE);
    }
    Wasm_Type type = wasm_type_make(WASM_VALUE_TYPE_STRUCT);
    type.definition = definition;
    return type;
}

/**
 * Returns the type of `depth` carets before the procedure type `procedure_type`: the type of a
 * pointer to a procedure for one caret, which is what a procedure type is always written with, and
 * a pointer to it for each caret more. The kind is `WASM_VALUE_TYPE_NONE` when a type in the
 * signature is not one that the target has.
 */
static Wasm_Type wasm_type_of_procedure(Wasm_Builder const *builder,
                                        Ast_Node_Index procedure_type, uint32_t depth) {
    assert(builder->nodes[procedure_type].node_type == AST_NODE_TYPE_PROCEDURE_TYPE);
    assert(depth >= 1);

    size_t parameter_count = wasm_signature_parameter_count(builder, procedure_type);
    for (size_t i = 0; i < parameter_count; i += 1) {
        Ast_Node_Index parameter = wasm_signature_parameter_type(builder, procedure_type, i);
        if (wasm_type_of_node(builder, parameter).kind == WASM_VALUE_TYPE_NONE) {
            return wasm_type_make(WASM_VALUE_TYPE_NONE);
        }
    }
    Str return_type = ast_procedure_return_type(&builder->nodes[procedure_type]);
    if (return_type.bytes != NULL &&
        wasm_type_of_name(builder, return_type).kind == WASM_VALUE_TYPE_NONE) {
        return wasm_type_make(WASM_VALUE_TYPE_NONE);
    }

    Wasm_Type procedure = wasm_type_make(WASM_VALUE_TYPE_PROCEDURE);
    procedure.definition = wasm_signature_canonical(builder, procedure_type);
    if (depth == 1) {
        return procedure;
    }

    Wasm_Type pointer = wasm_type_pointer_to(procedure);
    assert(pointer.kind == WASM_VALUE_TYPE_POINTER);
    pointer.depth = depth - 1;
    return pointer;
}

/**
 * Returns the type that the type `type` stands for, or a type of the kind `WASM_VALUE_TYPE_NONE`
 * if the target does not have it. The elements of an array and of an array slice can be an `Int`,
 * a `Bool`, a `Str`, a struct or a pointer, and not an array. An array type is an array slice when
 * it has no length. A struct type that is not a name is the type of a field that is a struct of
 * its own.
 */
static Wasm_Type wasm_type_of_node(Wasm_Builder const *builder, Ast_Node_Index type) {
    assert(wasm_node_is_type(builder, type));

    Ast_Node const *type_node = &builder->nodes[type];
    if (type_node->node_type == AST_NODE_TYPE_TYPE_NAME) {
        return wasm_type_of_name(builder, type_node->payload.type_name.name);
    }
    if (type_node->node_type == AST_NODE_TYPE_STRUCT_TYPE) {
        Wasm_Type nested = wasm_type_make(WASM_VALUE_TYPE_STRUCT);
        nested.definition = type;
        return nested;
    }
    if (type_node->node_type == AST_NODE_TYPE_POINTER_TYPE) {
        // Each caret is a node with the rest of the type as its child, down to the name.
        uint32_t depth = 0;
        Ast_Node_Index current = type;
        // Each step goes down one level, so there are fewer steps than nodes.
        for (size_t i = 0; i < builder->node_count &&
                           builder->nodes[current].node_type == AST_NODE_TYPE_POINTER_TYPE;
             i += 1) {
            depth += 1;
            current = builder->nodes[current].first_child;
        }
        assert(builder->nodes[current].node_type == AST_NODE_TYPE_TYPE_NAME ||
               builder->nodes[current].node_type == AST_NODE_TYPE_PROCEDURE_TYPE);
        if (builder->nodes[current].node_type == AST_NODE_TYPE_PROCEDURE_TYPE) {
            return wasm_type_of_procedure(builder, current, depth);
        }

        Wasm_Type pointer = wasm_type_pointer_to(
            wasm_type_of_name(builder, builder->nodes[current].payload.type_name.name));
        if (pointer.kind == WASM_VALUE_TYPE_POINTER) {
            pointer.depth = depth;
        }
        return pointer;
    }

    Ast_Node_Index element_node = type_node->first_child;
    assert(element_node != AST_NODE_INDEX_NONE);
    assert(builder->nodes[element_node].node_type == AST_NODE_TYPE_TYPE_NAME ||
           builder->nodes[element_node].node_type == AST_NODE_TYPE_POINTER_TYPE);
    Wasm_Type element = wasm_type_of_node(builder, element_node);
    if (element.kind == WASM_VALUE_TYPE_NONE) {
        return element;
    }

    Wasm_Type sequence = wasm_type_make(WASM_VALUE_TYPE_ARRAY);
    if (type_node->payload.array_type.length_kind == AST_ARRAY_LENGTH_OMITTED) {
        sequence.kind = WASM_VALUE_TYPE_SLICE;
    }
    sequence.element_kind = element.kind;
    sequence.element_definition = element.definition;
    sequence.element_pointee_kind = element.pointee_kind;
    sequence.element_depth = element.depth;
    return sequence;
}

/**
 * Returns the type of the elements of the array or array slice type `sequence`.
 */
static Wasm_Type wasm_type_element(Wasm_Type sequence) {
    assert(wasm_type_is_sequence(sequence));

    Wasm_Type element = wasm_type_make(sequence.element_kind);
    element.definition = sequence.element_definition;
    element.pointee_kind = sequence.element_pointee_kind;
    element.depth = sequence.element_depth;
    return element;
}

/**
 * Walks the values of the struct `struct_node`, which are its fields one after the other, where a
 * field that is a struct takes the values of its own fields in its place. Each value is a slot:
 * an `Int` is one, a `Bool` is one and a `Str` is two. The struct is walked without calling
 * itself, with an array of the fields that are next in each struct that is open, as deep as
 * `WASM_STRUCT_DEPTH_MAX` allows. Returns the number of slots, or `WASM_SLOT_COUNT_INVALID` when
 * the structs are inside each other too deep, which happens for a struct that contains itself.
 * When `kind` is not NULL and `wanted` is the index of a slot, `*kind` is set to the WebAssembly
 * type of that slot, `WASM_TYPE_I32` or `WASM_TYPE_I64`. A field with a type that the target does
 * not have counts as one slot, because `wasm_check_structs` rejects it.
 */
static size_t wasm_struct_slots(Wasm_Builder const *builder, Ast_Node_Index struct_node,
                                size_t wanted, byte *kind) {
    assert(struct_node < builder->node_count);
    assert(builder->nodes[struct_node].node_type == AST_NODE_TYPE_STRUCT_TYPE);

    Ast_Node_Index next_fields[WASM_STRUCT_DEPTH_MAX];
    size_t depth = 1;
    next_fields[0] = builder->nodes[struct_node].first_child;

    size_t count = 0;
    for (size_t step = 0; depth > 0; step += 1) {
        if (step == WASM_STRUCT_STEP_MAX) {
            return WASM_SLOT_COUNT_INVALID;
        }

        Ast_Node_Index field = next_fields[depth - 1];
        if (field == AST_NODE_INDEX_NONE) {
            depth -= 1;
            continue;
        }
        next_fields[depth - 1] = builder->nodes[field].next_sibling;

        Ast_Node_Index type_node = builder->nodes[field].first_child;
        Ast_Node_Index nested = AST_NODE_INDEX_NONE;
        // A field with a type that is not named here is one 32-bit value, which is what a pointer
        // is. The struct it points to is not walked, so a struct can have a pointer to itself.
        Wasm_Value_Type leaf = WASM_VALUE_TYPE_BOOL;
        if (builder->nodes[type_node].node_type == AST_NODE_TYPE_STRUCT_TYPE) {
            nested = type_node;
        } else if (builder->nodes[type_node].node_type == AST_NODE_TYPE_TYPE_NAME) {
            Str name = builder->nodes[type_node].payload.type_name.name;
            if (str_equals(name, STR("Int"))) {
                leaf = WASM_VALUE_TYPE_INT;
            } else if (str_equals(name, STR("Str"))) {
                leaf = WASM_VALUE_TYPE_STR;
            } else if (str_equals(name, STR("Bool")) == false &&
                       wasm_enum_find(builder, name) == AST_NODE_INDEX_NONE) {
                // An enum is one 32-bit value, like a `Bool`.
                nested = wasm_struct_find(builder, name);
            }
        }

        if (nested != AST_NODE_INDEX_NONE) {
            if (depth == WASM_STRUCT_DEPTH_MAX) {
                return WASM_SLOT_COUNT_INVALID;
            }
            next_fields[depth] = builder->nodes[nested].first_child;
            depth += 1;
            continue;
        }

        size_t leaf_slot_count = 1;
        if (leaf == WASM_VALUE_TYPE_STR) {
            leaf_slot_count = 2;
        }
        byte leaf_kind = WASM_TYPE_I32;
        if (leaf == WASM_VALUE_TYPE_INT) {
            leaf_kind = WASM_TYPE_I64;
        }
        for (size_t slot = 0; slot < leaf_slot_count; slot += 1) {
            if (kind != NULL && count == wanted) {
                *kind = leaf_kind;
            }
            count += 1;
        }
    }

    return count;
}

/**
 * Returns the number of WebAssembly values that a value of `type` takes. `Int` is a 64-bit integer,
 * so that every integer literal, which the tokenizer accepts up to the largest signed 64-bit value,
 * fits. `Bool` is one 32-bit integer, which is 0 for `false` and 1 for `true`, like the result of a
 * WebAssembly comparison. `Str` is the address and the length of its bytes, both 32-bit, because
 * addresses are. An array is the address of its first element, and an array slice the address of
 * its first element and its length in elements. A pointer is the address of what it points to,
 * which is a 32-bit integer like any address. A struct is the values of its fields, see
 * `wasm_struct_slots`. The struct types must have been checked by `wasm_check_structs`.
 */
static size_t wasm_type_slot_count(Wasm_Builder const *builder, Wasm_Type type) {
    switch (type.kind) {
    case WASM_VALUE_TYPE_ARRAY:
    case WASM_VALUE_TYPE_BOOL:
    case WASM_VALUE_TYPE_INT:
    case WASM_VALUE_TYPE_ENUM:
    case WASM_VALUE_TYPE_ENUM_LITERAL:
    case WASM_VALUE_TYPE_NIL:
    case WASM_VALUE_TYPE_POINTER:
    case WASM_VALUE_TYPE_PROCEDURE:
        return 1;
    case WASM_VALUE_TYPE_SLICE:
    case WASM_VALUE_TYPE_STR:
        return 2;
    case WASM_VALUE_TYPE_STRUCT: {
        size_t slot_count = wasm_struct_slots(builder, type.definition, SIZE_MAX, NULL);
        assert(slot_count != WASM_SLOT_COUNT_INVALID);
        return slot_count;
    }
    case WASM_VALUE_TYPE_NONE:
        break;
    }

    // Unreachable: a type that the target does not have takes no place anywhere.
    assert(false);
    return 0;
}

/**
 * Returns the WebAssembly type of the slot at index `slot` of a value of `type`: `WASM_TYPE_I64`
 * for an `Int`, and `WASM_TYPE_I32` for everything else.
 */
static byte wasm_type_slot_kind(Wasm_Builder const *builder, Wasm_Type type, size_t slot) {
    assert(slot < wasm_type_slot_count(builder, type));

    switch (type.kind) {
    case WASM_VALUE_TYPE_INT:
        return WASM_TYPE_I64;
    case WASM_VALUE_TYPE_ARRAY:
    case WASM_VALUE_TYPE_BOOL:
    case WASM_VALUE_TYPE_ENUM:
    case WASM_VALUE_TYPE_ENUM_LITERAL:
    case WASM_VALUE_TYPE_NIL:
    case WASM_VALUE_TYPE_POINTER:
    case WASM_VALUE_TYPE_PROCEDURE:
    case WASM_VALUE_TYPE_SLICE:
    case WASM_VALUE_TYPE_STR:
        return WASM_TYPE_I32;
    case WASM_VALUE_TYPE_STRUCT: {
        byte kind = WASM_TYPE_I32;
        (void)wasm_struct_slots(builder, type.definition, slot, &kind);
        return kind;
    }
    case WASM_VALUE_TYPE_NONE:
        break;
    }

    // Unreachable: a type that the target does not have takes no place anywhere.
    assert(false);
    return WASM_TYPE_I32;
}

/**
 * Returns the number of bytes that an element of `element` type takes in memory: 8 for each slot,
 * whether it is a 32-bit one or a 64-bit one, so that every slot is aligned and the offset of a
 * slot is its index times 8.
 */
static size_t wasm_type_stride_bytes(Wasm_Builder const *builder, Wasm_Type element) {
    return wasm_type_slot_count(builder, element) * WASM_ELEMENT_SIZE_BYTES;
}

/**
 * Writes the WebAssembly types that a value of `type` is made of, one for each slot.
 */
static void wasm_write_type(Wasm_Builder *builder, Wasm_Type type) {
    size_t slot_count = wasm_type_slot_count(builder, type);
    for (size_t slot = 0; slot < slot_count; slot += 1) {
        wasm_write_byte(&builder->writer, wasm_type_slot_kind(builder, type, slot));
    }
}

/**
 * Returns the number of groups that the locals of a variable of `type` are written as. A group is a
 * count and a WebAssembly type, and neighbors of one type are one group.
 */
static size_t wasm_type_group_count(Wasm_Builder const *builder, Wasm_Type type) {
    size_t slot_count = wasm_type_slot_count(builder, type);
    size_t group_count = 0;
    for (size_t slot = 0; slot < slot_count; slot += 1) {
        if (slot == 0 || wasm_type_slot_kind(builder, type, slot) !=
                             wasm_type_slot_kind(builder, type, slot - 1)) {
            group_count += 1;
        }
    }

    return group_count;
}

/**
 * Writes the groups of locals of a variable of `type`, see `wasm_type_group_count`.
 */
static void wasm_write_type_groups(Wasm_Builder *builder, Wasm_Type type) {
    size_t slot_count = wasm_type_slot_count(builder, type);
    size_t slot = 0;
    // Each step takes one group, which is at least one slot.
    while (slot < slot_count) {
        byte kind = wasm_type_slot_kind(builder, type, slot);
        size_t run = 1;
        while (slot + run < slot_count && wasm_type_slot_kind(builder, type, slot + run) == kind) {
            run += 1;
        }
        wasm_write_unsigned(&builder->writer, run);
        wasm_write_byte(&builder->writer, kind);
        slot += run;
    }
}

/**
 * Returns whether the elements of the array or array slice types `first` and `second` are of the
 * same type: the same kind, and the same struct or the same pointer type when they have one.
 */
static bool wasm_type_elements_equal(Wasm_Type first, Wasm_Type second) {
    return first.element_kind == second.element_kind &&
           first.element_definition == second.element_definition &&
           first.element_pointee_kind == second.element_pointee_kind &&
           first.element_depth == second.element_depth;
}

/**
 * Returns whether the two types are the same type. Two structs are the same when they are the same
 * definition, a pointer is the same as another when it has as many carets and points to the same
 * type at the end of them, and an array or an array slice is the same as another when the elements
 * are. An enum is the same as another when it is the same definition, and the member of an enum
 * that is written without it is the same as every enum, like `nil` is for a pointer. A pointer to a
 * procedure is the same as another when they have the same signature, which the definition of the
 * type says, see `wasm_signature_canonical`. `nil` is the same as every pointer, because it is a
 * pointer of whatever type is wanted.
 */
static bool wasm_type_equals(Wasm_Type first, Wasm_Type second) {
    if (first.kind != second.kind) {
        bool is_first_pointer = first.kind == WASM_VALUE_TYPE_POINTER ||
                                first.kind == WASM_VALUE_TYPE_PROCEDURE;
        bool is_second_pointer = second.kind == WASM_VALUE_TYPE_POINTER ||
                                 second.kind == WASM_VALUE_TYPE_PROCEDURE;
        return (is_first_pointer && second.kind == WASM_VALUE_TYPE_NIL) ||
               (first.kind == WASM_VALUE_TYPE_NIL && is_second_pointer) ||
               (first.kind == WASM_VALUE_TYPE_ENUM &&
                second.kind == WASM_VALUE_TYPE_ENUM_LITERAL) ||
               (first.kind == WASM_VALUE_TYPE_ENUM_LITERAL &&
                second.kind == WASM_VALUE_TYPE_ENUM);
    }

    switch (first.kind) {
    case WASM_VALUE_TYPE_ENUM:
    case WASM_VALUE_TYPE_PROCEDURE:
    case WASM_VALUE_TYPE_STRUCT:
        return first.definition == second.definition;
    case WASM_VALUE_TYPE_POINTER:
        return first.depth == second.depth && first.pointee_kind == second.pointee_kind &&
               first.definition == second.definition;
    case WASM_VALUE_TYPE_ARRAY:
    case WASM_VALUE_TYPE_SLICE:
        return wasm_type_elements_equal(first, second);
    case WASM_VALUE_TYPE_BOOL:
    case WASM_VALUE_TYPE_ENUM_LITERAL:
    case WASM_VALUE_TYPE_INT:
    case WASM_VALUE_TYPE_NIL:
    case WASM_VALUE_TYPE_NONE:
    case WASM_VALUE_TYPE_STR:
        return true;
    }

    // Unreachable: every kind is handled above.
    assert(false);
    return false;
}

/**
 * Returns whether a value of the type `value` can be given where `target` is wanted: the same
 * type, or an array where an array slice of the same elements is, because the slice then views all
 * of its elements.
 */
static bool wasm_type_accepts(Wasm_Type target, Wasm_Type value) {
    if (wasm_type_equals(target, value)) {
        return true;
    }
    return target.kind == WASM_VALUE_TYPE_SLICE && value.kind == WASM_VALUE_TYPE_ARRAY &&
           wasm_type_elements_equal(target, value);
}

/**
 * Returns the number of parameters of `signature`, which is an `AST_NODE_TYPE_PROCEDURE` or an
 * `AST_NODE_TYPE_PROCEDURE_TYPE`.
 */
static size_t wasm_signature_parameter_count(Wasm_Builder const *builder,
                                             Ast_Node_Index signature) {
    assert(signature < builder->node_count);

    if (builder->nodes[signature].node_type == AST_NODE_TYPE_PROCEDURE) {
        return wasm_parameter_count(builder, signature);
    }
    assert(builder->nodes[signature].node_type == AST_NODE_TYPE_PROCEDURE_TYPE);
    // The children of a procedure type are the types of its parameters, and nothing else.
    return wasm_children_count(builder, signature);
}

/**
 * Returns the type of the parameter at `position` of `signature`, which is an
 * `AST_NODE_TYPE_PROCEDURE` or an `AST_NODE_TYPE_PROCEDURE_TYPE`, as the node of the type. The
 * parameter of a procedure is a node that has the type as its child, and the parameter of a
 * procedure type is the node of its type.
 */
static Ast_Node_Index wasm_signature_parameter_type(Wasm_Builder const *builder,
                                                    Ast_Node_Index signature, size_t position) {
    assert(position < wasm_signature_parameter_count(builder, signature));

    Ast_Node_Index parameter = builder->nodes[signature].first_child;
    for (size_t i = 0; i < position; i += 1) {
        assert(parameter != AST_NODE_INDEX_NONE);
        parameter = builder->nodes[parameter].next_sibling;
    }

    assert(parameter != AST_NODE_INDEX_NONE);
    if (builder->nodes[parameter].node_type == AST_NODE_TYPE_PARAMETER) {
        return builder->nodes[parameter].first_child;
    }
    return parameter;
}

/**
 * Returns whether the type `type` has a procedure type at the end of its carets, or of the carets
 * of the type of its elements. A type like that is not compared by what it is made of, see
 * `wasm_signature_equals`.
 */
static bool wasm_type_node_has_procedure(Wasm_Builder const *builder, Ast_Node_Index type) {
    Ast_Node_Index current = type;
    // Each step goes down one level, so there are fewer steps than nodes.
    for (size_t i = 0; i < builder->node_count &&
                       (builder->nodes[current].node_type == AST_NODE_TYPE_ARRAY_TYPE ||
                        builder->nodes[current].node_type == AST_NODE_TYPE_POINTER_TYPE);
         i += 1) {
        current = builder->nodes[current].first_child;
    }
    return builder->nodes[current].node_type == AST_NODE_TYPE_PROCEDURE_TYPE;
}

/**
 * Returns whether the signatures `first` and `second`, each an `AST_NODE_TYPE_PROCEDURE` or an
 * `AST_NODE_TYPE_PROCEDURE_TYPE`, have the same types of parameters and the same return type, or
 * both have none. The names of the parameters do not matter. A parameter that is a pointer to a
 * procedure itself is not compared by what that is made of, so such a signature is the same as
 * itself only: that keeps the comparison from asking for the type of a signature while it is
 * working out which signature that is, and a procedure type cannot be written with one anyway.
 */
static bool wasm_signature_equals(Wasm_Builder const *builder, Ast_Node_Index first,
                                  Ast_Node_Index second) {
    if (first == second) {
        return true;
    }

    size_t parameter_count = wasm_signature_parameter_count(builder, first);
    if (parameter_count != wasm_signature_parameter_count(builder, second)) {
        return false;
    }
    for (size_t i = 0; i < parameter_count; i += 1) {
        Ast_Node_Index first_type = wasm_signature_parameter_type(builder, first, i);
        Ast_Node_Index second_type = wasm_signature_parameter_type(builder, second, i);
        if (wasm_type_node_has_procedure(builder, first_type) ||
            wasm_type_node_has_procedure(builder, second_type)) {
            return false;
        }
        if (wasm_type_equals(wasm_type_of_node(builder, first_type),
                             wasm_type_of_node(builder, second_type)) == false) {
            return false;
        }
    }

    Str first_return = ast_procedure_return_type(&builder->nodes[first]);
    Str second_return = ast_procedure_return_type(&builder->nodes[second]);
    if (first_return.bytes == NULL || second_return.bytes == NULL) {
        return first_return.bytes == second_return.bytes;
    }
    return wasm_type_equals(wasm_type_of_name(builder, first_return),
                            wasm_type_of_name(builder, second_return));
}

/**
 * Returns the first node, by index, that is an `AST_NODE_TYPE_PROCEDURE` or an
 * `AST_NODE_TYPE_PROCEDURE_TYPE` with the signature of `signature`, which is one of them as well.
 * Every node with one signature has the same answer, so it stands for the signature, and two types
 * of a pointer to a procedure are the same when their answers are, see `wasm_type_equals`.
 */
static Ast_Node_Index wasm_signature_canonical(Wasm_Builder const *builder,
                                               Ast_Node_Index signature) {
    assert(signature < builder->node_count);

    for (Ast_Node_Index candidate = 0; candidate < signature; candidate += 1) {
        Ast_Node_Type node_type = builder->nodes[candidate].node_type;
        if ((node_type == AST_NODE_TYPE_PROCEDURE || node_type == AST_NODE_TYPE_PROCEDURE_TYPE) &&
            wasm_signature_equals(builder, candidate, signature)) {
            return candidate;
        }
    }

    return signature;
}

/**
 * Returns the index of the function type that `call_indirect` has to be given to call a procedure
 * that has the signature `signature`, which is the answer of `wasm_signature_canonical`. A
 * procedure has the type of its function, whose index is the index of the function. A procedure
 * type has its own, after the types of the functions, in the order of the nodes.
 */
static size_t wasm_signature_type_index(Wasm_Builder const *builder, Ast_Node_Index signature) {
    assert(signature < builder->node_count);
    assert(wasm_signature_canonical(builder, signature) == signature);

    if (builder->nodes[signature].node_type == AST_NODE_TYPE_PROCEDURE) {
        Ast_Node_Index definition = builder->nodes[builder->module].first_child;
        // Every definition is a distinct node, so there cannot be more than there are nodes.
        for (size_t i = 0; i < builder->node_count && definition != AST_NODE_INDEX_NONE; i += 1) {
            if (builder->nodes[definition].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION &&
                builder->nodes[definition].first_child == signature) {
                return wasm_function_index(builder, definition);
            }
            definition = builder->nodes[definition].next_sibling;
        }
        // Unreachable: a procedure is the value of a constant of the module.
        assert(false);
        return 0;
    }

    size_t index = builder->foreign_count + builder->definition_count;
    for (Ast_Node_Index node = 0; node < signature; node += 1) {
        if (builder->nodes[node].node_type == AST_NODE_TYPE_PROCEDURE_TYPE &&
            wasm_signature_canonical(builder, node) == node) {
            index += 1;
        }
    }
    return index;
}

/**
 * Returns the signature, an `AST_NODE_TYPE_PROCEDURE` or an `AST_NODE_TYPE_PROCEDURE_TYPE`, of
 * what the call `call` calls, or `AST_NODE_INDEX_NONE` when it calls a name that is not a pointer
 * to a procedure. A constant is called by its name, and a variable, a parameter or a global
 * variable is called through the pointer it holds, whose type says the signature.
 */
static Ast_Node_Index wasm_call_signature(Wasm_Builder const *builder, Ast_Node_Index call) {
    assert(call < builder->node_count);
    assert(builder->nodes[call].node_type == AST_NODE_TYPE_CALL);

    Semantic_Binding binding = builder->bindings[call];
    if (binding.kind == SEMANTIC_BINDING_KIND_CONSTANT) {
        return wasm_procedure_of(builder, binding.target);
    }

    Wasm_Type type = wasm_binding_value_type(builder, binding);
    if (type.kind != WASM_VALUE_TYPE_PROCEDURE) {
        return AST_NODE_INDEX_NONE;
    }
    return type.definition;
}

/**
 * Returns whether a value of `type` has elements that can be indexed, which an array and an array
 * slice do.
 */
static bool wasm_type_is_sequence(Wasm_Type type) {
    return type.kind == WASM_VALUE_TYPE_ARRAY || type.kind == WASM_VALUE_TYPE_SLICE;
}

/**
 * Finds the field called `name` in the struct `struct_node`. Returns whether there is one, and then
 * sets `*slot_offset` to the number of slots before the field in a value of the struct, and `*type`
 * to the type of the field. The field types must have been checked by `wasm_check_structs`.
 */
static bool wasm_struct_field_find(Wasm_Builder const *builder, Ast_Node_Index struct_node,
                                   Str name, size_t *slot_offset, Wasm_Type *type) {
    assert(struct_node < builder->node_count);
    assert(slot_offset != NULL);
    assert(type != NULL);

    size_t offset = 0;
    Ast_Node_Index field = builder->nodes[struct_node].first_child;
    // Every field is a distinct node, so there cannot be more than there are nodes.
    for (size_t i = 0; i < builder->node_count && field != AST_NODE_INDEX_NONE; i += 1) {
        Wasm_Type field_type = wasm_type_of_node(builder, builder->nodes[field].first_child);
        if (str_equals(builder->nodes[field].payload.field.name, name)) {
            *slot_offset = offset;
            *type = field_type;
            return true;
        }
        offset += wasm_type_slot_count(builder, field_type);
        field = builder->nodes[field].next_sibling;
    }

    return false;
}

/**
 * Checks the structs: the type of every field must be one that the target has and not an array,
 * and no struct can be inside itself, since it would never end. Returns false after it recorded
 * an error. What comes after can ask for the slots of a struct and assume that they are known.
 */
static bool wasm_check_structs(Wasm_Builder *builder) {
    for (size_t i = 0; i < builder->node_count; i += 1) {
        if (builder->nodes[i].node_type != AST_NODE_TYPE_FIELD) {
            continue;
        }

        Ast_Node_Index type = builder->nodes[i].first_child;
        if (builder->nodes[type].node_type == AST_NODE_TYPE_ARRAY_TYPE) {
            return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_TYPE_MISPLACED,
                             wasm_type_subject(builder, type));
        }
        // A pointer type is checked too: a struct can have a pointer to a struct that is not
        // defined, but not to a type that the target does not have.
        if ((builder->nodes[type].node_type == AST_NODE_TYPE_TYPE_NAME ||
             builder->nodes[type].node_type == AST_NODE_TYPE_POINTER_TYPE) &&
            wasm_type_of_node(builder, type).kind == WASM_VALUE_TYPE_NONE) {
            return wasm_fail(builder, WASM_BUILD_STATUS_TYPE_UNSUPPORTED,
                             wasm_type_subject(builder, type));
        }
    }

    for (size_t i = 0; i < builder->node_count; i += 1) {
        if (builder->nodes[i].node_type != AST_NODE_TYPE_STRUCT_TYPE) {
            continue;
        }
        if (wasm_struct_slots(builder, (Ast_Node_Index)i, SIZE_MAX, NULL) ==
            WASM_SLOT_COUNT_INVALID) {
            Ast_Node_Index first_field = builder->nodes[i].first_child;
            return wasm_fail(builder, WASM_BUILD_STATUS_STRUCT_RECURSIVE,
                             builder->nodes[first_field].payload.field.name);
        }
    }

    return true;
}

/**
 * Writes the instructions that push the default value of `type`: zero for an `Int`, `false` for a
 * `Bool`, the empty string for a `Str`, an empty array slice for an array slice, and the default of
 * each of its fields for a struct. Every slot of all of them is the number zero, which is why it is
 * one loop over the slots.
 */
static void wasm_default_write(Wasm_Builder *builder, Wasm_Type type) {
    size_t slot_count = wasm_type_slot_count(builder, type);
    for (size_t slot = 0; slot < slot_count; slot += 1) {
        if (wasm_type_slot_kind(builder, type, slot) == WASM_TYPE_I64) {
            wasm_write_byte(&builder->writer, WASM_OPCODE_I64_CONST);
            wasm_write_signed(&builder->writer, 0);
        } else {
            wasm_write_i32_const(&builder->writer, 0);
        }
    }
}

/**
 * Returns the instruction that loads a slot of the WebAssembly type `kind` from memory.
 */
static byte wasm_load_opcode(byte kind) {
    if (kind == WASM_TYPE_I64) {
        return WASM_OPCODE_I64_LOAD;
    }
    return WASM_OPCODE_I32_LOAD;
}

/**
 * Returns the instruction that stores a slot of the WebAssembly type `kind` to memory.
 */
static byte wasm_store_opcode(byte kind) {
    if (kind == WASM_TYPE_I64) {
        return WASM_OPCODE_I64_STORE;
    }
    return WASM_OPCODE_I32_STORE;
}

/**
 * Writes the instructions that push the slots of a value of `type` that is in memory, from the
 * local `address` plus `offset_bytes`. Each slot is 8 bytes there, see `wasm_type_stride_bytes`.
 */
static void wasm_slots_load(Wasm_Builder *builder, Wasm_Type type, size_t address,
                            size_t offset_bytes) {
    size_t slot_count = wasm_type_slot_count(builder, type);
    for (size_t slot = 0; slot < slot_count; slot += 1) {
        wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, address);
        wasm_write_memory_instruction(
            &builder->writer, wasm_load_opcode(wasm_type_slot_kind(builder, type, slot)),
            offset_bytes + slot * WASM_ELEMENT_SIZE_BYTES);
    }
}

/**
 * Writes the instructions that replace an address on the stack with the slots of a value of
 * `type` that is in memory there, plus `offset_bytes`. A value of one slot is a load. A value of
 * more is loaded from the address kept in a scratch local, which is used between instructions
 * that follow each other only.
 */
static void wasm_slots_load_from_stack(Wasm_Builder *builder, Ast_Node_Index procedure,
                                       Wasm_Type type, size_t offset_bytes) {
    if (wasm_type_slot_count(builder, type) == 1) {
        wasm_write_memory_instruction(
            &builder->writer, wasm_load_opcode(wasm_type_slot_kind(builder, type, 0)),
            offset_bytes);
        return;
    }

    size_t address = wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_SECOND);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_SET, address);
    wasm_slots_load(builder, type, address, offset_bytes);
}

/**
 * Writes the instructions that store the slots of a value of `type` that are on the stack to
 * memory, at the local `address` plus `offset_bytes`. The last slot is on top, so the slots are
 * stored from the last to the first, and each one is held in a scratch local while the address is
 * pushed below it, because a store takes the address first.
 */
static void wasm_slots_store(Wasm_Builder *builder, Ast_Node_Index procedure, Wasm_Type type,
                             size_t address, size_t offset_bytes) {
    size_t slot_count = wasm_type_slot_count(builder, type);
    for (size_t slot = slot_count; slot > 0; slot -= 1) {
        byte kind = wasm_type_slot_kind(builder, type, slot - 1);
        size_t held = wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_TEMPORARY_32);
        if (kind == WASM_TYPE_I64) {
            held = wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_TEMPORARY_64);
        }
        wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_SET, held);
        wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, address);
        wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, held);
        wasm_write_memory_instruction(&builder->writer, wasm_store_opcode(kind),
                                      offset_bytes + (slot - 1) * WASM_ELEMENT_SIZE_BYTES);
    }
}

/**
 * Writes the instructions that move the slots of a value of `type` that are on the stack into the
 * locals or the globals that start at index `first`, with the instruction `opcode`, which is
 * `local.set` or `global.set`. The last slot is on top, so it goes first.
 */
static void wasm_slots_set(Wasm_Builder *builder, Wasm_Type type, byte opcode, size_t first) {
    assert(opcode == WASM_OPCODE_LOCAL_SET || opcode == WASM_OPCODE_GLOBAL_SET);

    size_t slot_count = wasm_type_slot_count(builder, type);
    for (size_t slot = slot_count; slot > 0; slot -= 1) {
        wasm_write_byte(&builder->writer, opcode);
        wasm_write_unsigned(&builder->writer, first + slot - 1);
    }
}

/**
 * Writes the instructions that multiply the 32-bit integer on top of the stack by the size of an
 * element, which is a shift when that is 8 bytes, as it is for the elements of one slot.
 */
static void wasm_scale_write(Wasm_Builder *builder, size_t stride_bytes) {
    assert(stride_bytes > 0);
    assert(stride_bytes <= INT32_MAX);

    if (stride_bytes == WASM_ELEMENT_SIZE_BYTES) {
        wasm_write_i32_const(&builder->writer, WASM_ELEMENT_SIZE_SHIFT);
        wasm_write_byte(&builder->writer, WASM_OPCODE_I32_SHL);
    } else {
        wasm_write_i32_const(&builder->writer, (int64_t)stride_bytes);
        wasm_write_byte(&builder->writer, WASM_OPCODE_I32_MUL);
    }
}

/**
 * Returns the address in the data of the string literal `literal`, which is the length of the
 * string literals before it, because the data has them in the order of the nodes.
 */
static size_t wasm_string_address(Wasm_Builder const *builder, Ast_Node_Index literal) {
    assert(literal < builder->node_count);
    assert(builder->nodes[literal].node_type == AST_NODE_TYPE_STRING_LITERAL);

    size_t address = 0;
    for (Ast_Node_Index node = 0; node < literal; node += 1) {
        if (builder->nodes[node].node_type == AST_NODE_TYPE_STRING_LITERAL) {
            address += builder->nodes[node].payload.string_literal.text.length;
        }
    }

    return address;
}

/**
 * Finds where the field of the field access `access` is. The accesses are followed down to what
 * they are made through, which is a name, an element of an array or what a pointer points to, and
 * then up again, from the struct that has the first field to the last field. A field that is
 * accessed through a pointer to a struct, written `p.age` or `p^.age`, is in memory: `is_through_
 * pointer` is set, `root` is the expression whose value is that pointer, and `slot_offset` counts
 * from the start of what it points to. It is the last pointer that the fields are accessed
 * through, because a field can be a pointer again, as in `node.next.value`. The chain is kept in
 * an array, so the walk does not call itself. An access through anything else, such as the result
 * of a call, is wrong: its fields would have to be picked out of the values on the stack.
 */
static Wasm_Field_Path wasm_field_path(Wasm_Builder const *builder, Ast_Node_Index access) {
    assert(access < builder->node_count);
    assert(builder->nodes[access].node_type == AST_NODE_TYPE_FIELD_ACCESS);

    Ast_Node_Index chain[WASM_STRUCT_DEPTH_MAX];
    size_t chain_count = 0;
    Ast_Node_Index current = access;
    while (builder->nodes[current].node_type == AST_NODE_TYPE_FIELD_ACCESS &&
           chain_count < WASM_STRUCT_DEPTH_MAX) {
        chain[chain_count] = current;
        chain_count += 1;
        current = builder->nodes[current].first_child;
    }

    Wasm_Field_Path path = {
        .subject = builder->nodes[access].payload.field_access.name,
        .type = wasm_type_make(WASM_VALUE_TYPE_NONE),
        .slot_offset = 0,
        .root = current,
        .status = WASM_BUILD_STATUS_OK,
        .is_through_pointer = false,
    };
    if (builder->nodes[current].node_type == AST_NODE_TYPE_FIELD_ACCESS) {
        path.status = WASM_BUILD_STATUS_EXPRESSION_TOO_DEEP;
        return path;
    }
    if (builder->nodes[current].node_type != AST_NODE_TYPE_IDENTIFIER &&
        builder->nodes[current].node_type != AST_NODE_TYPE_SUBSCRIPT &&
        builder->nodes[current].node_type != AST_NODE_TYPE_DEREFERENCE) {
        path.status = WASM_BUILD_STATUS_FIELD_BASE_INVALID;
        return path;
    }

    Wasm_Type current_type = wasm_expression_type(builder, current);
    Str before = builder->nodes[chain[chain_count - 1]].payload.field_access.name;
    if (builder->nodes[current].node_type == AST_NODE_TYPE_IDENTIFIER) {
        before = builder->nodes[current].payload.identifier.name;
    }

    // `p^.age` is a field of what the pointer `p` points to: the pointer is the root, and the
    // fields come after it.
    if (builder->nodes[current].node_type == AST_NODE_TYPE_DEREFERENCE) {
        Ast_Node_Index pointer = builder->nodes[current].first_child;
        Wasm_Type pointer_type = wasm_expression_type(builder, pointer);
        if (pointer_type.kind != WASM_VALUE_TYPE_POINTER) {
            path.status = WASM_BUILD_STATUS_POINTER_EXPECTED;
            path.subject = before;
            return path;
        }
        path.root = pointer;
        path.is_through_pointer = true;
        current_type = wasm_type_pointee(pointer_type);
    }

    for (size_t i = chain_count; i > 0; i -= 1) {
        Str name = builder->nodes[chain[i - 1]].payload.field_access.name;

        // A field of a pointer to a struct is a field of the struct, as if the pointer was
        // followed: `p.age` is `p^.age`. The value before this field, which is the base for the
        // first field and the field access before it for the others, is the pointer that is
        // followed, and the offset starts over at the start of what it points to. A pointer to a
        // pointer is not followed more than once, so `pp.age` is an error and `pp^.age` is not.
        if (current_type.kind == WASM_VALUE_TYPE_POINTER && current_type.depth == 1 &&
            current_type.pointee_kind == WASM_VALUE_TYPE_STRUCT) {
            if (i == chain_count) {
                path.root = current;
            } else {
                path.root = chain[i];
            }
            path.is_through_pointer = true;
            path.slot_offset = 0;
            current_type = wasm_type_pointee(current_type);
        }
        if (current_type.kind != WASM_VALUE_TYPE_STRUCT) {
            path.status = WASM_BUILD_STATUS_STRUCT_EXPECTED;
            path.subject = before;
            return path;
        }

        size_t slot_offset = 0;
        if (wasm_struct_field_find(builder, current_type.definition, name, &slot_offset,
                                   &current_type) == false) {
            path.status = WASM_BUILD_STATUS_STRUCT_FIELD_UNKNOWN;
            path.subject = name;
            return path;
        }
        path.slot_offset += slot_offset;
        before = name;
    }

    path.type = current_type;
    return path;
}

/**
 * Writes the instructions for the field access `access`, such as `p.name`, `a[1].age` or `q^.age`.
 * The field of a variable or a parameter is in locals that follow each other, so its slots are
 * pushed one by one, unless the address of the variable is taken, which puts it in the frame. The
 * field of what a pointer points to is in memory at the address that the pointer is, plus the
 * offset of the field. The field of an element of an array is in memory, at the address of the
 * element plus the offset of the field, so the array and the index are written, the address is
 * computed, and the slots of the field are loaded. Returns false after it recorded an error.
 */
static bool wasm_build_code_section_body_field_access(Wasm_Builder *builder,
                                                      Ast_Node_Index definition,
                                                      Ast_Node_Index access) {
    assert(access < builder->node_count);
    assert(builder->nodes[access].node_type == AST_NODE_TYPE_FIELD_ACCESS);

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Wasm_Field_Path path = wasm_field_path(builder, access);
    if (path.status != WASM_BUILD_STATUS_OK) {
        return wasm_fail(builder, path.status, path.subject);
    }

    // The field is of what a pointer points to, as in `p.age` and `p^.age`, which is in memory at
    // the address that the pointer is, plus the offset of the field.
    if (path.is_through_pointer) {
        if (wasm_build_code_section_body_pointer(builder, definition, path.root, true) == false) {
            return false;
        }
        wasm_slots_load_from_stack(builder, procedure, path.type,
                                   path.slot_offset * WASM_ELEMENT_SIZE_BYTES);
        return true;
    }

    if (builder->nodes[path.root].node_type == AST_NODE_TYPE_IDENTIFIER) {
        Semantic_Binding binding = builder->bindings[path.root];
        size_t first = 0;
        if (binding.kind == SEMANTIC_BINDING_KIND_PARAMETER) {
            first = wasm_parameter_local_index(builder, procedure, binding.position);
        } else {
            assert(binding.kind == SEMANTIC_BINDING_KIND_VARIABLE);
            // A variable whose address is taken is in the frame, and its field is loaded from
            // there, at the offset of the variable plus the offset of the field.
            if (wasm_variable_is_in_memory(builder, binding.target)) {
                wasm_slots_load(builder, path.type,
                                wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_FRAME),
                                wasm_variable_frame_offset(builder, procedure, binding.target) +
                                    path.slot_offset * WASM_ELEMENT_SIZE_BYTES);
                return true;
            }
            first = wasm_variable_local_index(builder, procedure, binding.target);
        }
        first += path.slot_offset;

        size_t slot_count = wasm_type_slot_count(builder, path.type);
        for (size_t slot = 0; slot < slot_count; slot += 1) {
            wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, first + slot);
        }
        return true;
    }

    // The field is of an element, which is in memory.
    assert(builder->nodes[path.root].node_type == AST_NODE_TYPE_SUBSCRIPT);
    Ast_Node_Index base = builder->nodes[path.root].first_child;
    Ast_Node_Index index = builder->nodes[path.root].last_child;
    if (wasm_build_code_section_body_expression(builder, definition, base, false) == false) {
        return false;
    }
    Wasm_Type base_type = wasm_expression_type(builder, base);
    if (wasm_type_is_sequence(base_type) == false) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_EXPECTED,
                         wasm_expression_subject(builder, definition, base));
    }
    wasm_array_decay(builder, base);

    if (wasm_build_code_section_body_expression(builder, definition, index, false) == false) {
        return false;
    }
    if (wasm_expression_type(builder, index).kind != WASM_VALUE_TYPE_INT) {
        return wasm_fail(builder, WASM_BUILD_STATUS_INDEX_TYPE_INVALID,
                         wasm_expression_subject(builder, definition, index));
    }
    wasm_element_address_write(builder, procedure,
                               wasm_type_stride_bytes(builder, wasm_type_element(base_type)));
    wasm_slots_load_from_stack(builder, procedure, path.type,
                               path.slot_offset * WASM_ELEMENT_SIZE_BYTES);
    return true;
}

/**
 * Writes the instructions for the struct literal `literal`, such as `Person { name = "Bob" }`: the
 * values of the fields of the struct, in the order of the struct, which is the order of the slots
 * of its type, whatever the order of the literal is. A field that is not given a value gets the
 * default value of its type. `expected` is the struct that a literal without a name is the value
 * of, which is the type of the field it is given to, and is `AST_NODE_INDEX_NONE` for a literal
 * with a name. Every field must be one of the struct and be given a value once, and the value must
 * be of the type of the field. Returns false after it recorded an error.
 */
static bool wasm_build_code_section_body_struct_literal(Wasm_Builder *builder,
                                                        Ast_Node_Index definition,
                                                        Ast_Node_Index literal,
                                                        Ast_Node_Index expected) {
    assert(literal < builder->node_count);
    assert(builder->nodes[literal].node_type == AST_NODE_TYPE_STRUCT_LITERAL);

    Ast_Node_Index struct_node = expected;
    Str literal_name = builder->nodes[literal].payload.struct_literal.name;
    if (literal_name.bytes != NULL) {
        struct_node = wasm_struct_find(builder, literal_name);
        if (struct_node == AST_NODE_INDEX_NONE) {
            return wasm_fail(builder, WASM_BUILD_STATUS_TYPE_UNSUPPORTED, literal_name);
        }
    }
    // The parser only makes a literal without a name as the value of a field.
    assert(struct_node != AST_NODE_INDEX_NONE);

    // Every field that is given a value must be a field of the struct, and be given one once.
    Ast_Node_Index initializer = builder->nodes[literal].first_child;
    for (size_t i = 0; i < builder->node_count && initializer != AST_NODE_INDEX_NONE; i += 1) {
        Str name = builder->nodes[initializer].payload.field_initializer.name;
        size_t slot_offset = 0;
        Wasm_Type field_type = wasm_type_make(WASM_VALUE_TYPE_NONE);
        if (wasm_struct_field_find(builder, struct_node, name, &slot_offset, &field_type) ==
            false) {
            return wasm_fail(builder, WASM_BUILD_STATUS_STRUCT_FIELD_UNKNOWN, name);
        }

        Ast_Node_Index later = builder->nodes[initializer].next_sibling;
        for (size_t j = 0; j < builder->node_count && later != AST_NODE_INDEX_NONE; j += 1) {
            if (str_equals(builder->nodes[later].payload.field_initializer.name, name)) {
                return wasm_fail(builder, WASM_BUILD_STATUS_STRUCT_FIELD_DUPLICATE,
                                 builder->nodes[later].payload.field_initializer.name);
            }
            later = builder->nodes[later].next_sibling;
        }
        initializer = builder->nodes[initializer].next_sibling;
    }

    Ast_Node_Index field = builder->nodes[struct_node].first_child;
    for (size_t i = 0; i < builder->node_count && field != AST_NODE_INDEX_NONE; i += 1) {
        Str field_name = builder->nodes[field].payload.field.name;
        Wasm_Type field_type = wasm_type_of_node(builder, builder->nodes[field].first_child);

        Ast_Node_Index given = builder->nodes[literal].first_child;
        for (size_t j = 0; j < builder->node_count && given != AST_NODE_INDEX_NONE; j += 1) {
            if (str_equals(builder->nodes[given].payload.field_initializer.name, field_name)) {
                break;
            }
            given = builder->nodes[given].next_sibling;
        }

        if (given == AST_NODE_INDEX_NONE) {
            wasm_default_write(builder, field_type);
        } else {
            Ast_Node_Index value = builder->nodes[given].first_child;
            Str given_name = builder->nodes[given].payload.field_initializer.name;
            if (builder->nodes[value].node_type == AST_NODE_TYPE_STRUCT_LITERAL &&
                builder->nodes[value].payload.struct_literal.name.bytes == NULL) {
                if (field_type.kind != WASM_VALUE_TYPE_STRUCT ||
                    wasm_build_code_section_body_struct_literal(builder, definition, value,
                                                                field_type.definition) == false) {
                    if (builder->status == WASM_BUILD_STATUS_OK) {
                        return wasm_fail(builder, WASM_BUILD_STATUS_STRUCT_FIELD_TYPE_MISMATCH,
                                         given_name);
                    }
                    return false;
                }
            } else {
                if (wasm_build_code_section_body_expression(builder, definition, value, false) ==
                    false) {
                    return false;
                }
                if (wasm_type_equals(field_type, wasm_expression_type(builder, value)) == false) {
                    return wasm_fail(builder, WASM_BUILD_STATUS_STRUCT_FIELD_TYPE_MISMATCH,
                                     given_name);
                }
            }
        }

        field = builder->nodes[field].next_sibling;
    }

    return true;
}

/**
 * Records the first error and returns false, so that a caller can report failure in one line.
 */
static bool wasm_fail(Wasm_Builder *builder, Wasm_Build_Status status, Str subject) {
    // Only the first error is kept, and callers return right after it.
    assert(builder->status == WASM_BUILD_STATUS_OK);
    assert(status != WASM_BUILD_STATUS_OK);

    builder->status = status;
    builder->subject = subject;
    return false;
}

/**
 * Writes `count` bytes, or sets the overflow flag when they do not fit.
 */
static void wasm_write_bytes(Wasm_Writer *writer, byte const *bytes, size_t count) {
    assert(writer->length <= writer->capacity);
    assert(bytes != NULL || count == 0);

    if (writer->is_overflowed) {
        return;
    }
    if (count > writer->capacity - writer->length) {
        writer->is_overflowed = true;
        return;
    }

    if (count > 0) {
        memcpy(writer->bytes + writer->length, bytes, count);
    }
    writer->length += count;
}

static void wasm_write_byte(Wasm_Writer *writer, byte content) {
    assert(writer->bytes != NULL);
    assert(writer->length <= writer->capacity);

    wasm_write_bytes(writer, &content, 1);
}

static void wasm_write_unsigned(Wasm_Writer *writer, uint64_t number) {
    assert(writer->bytes != NULL);

    byte encoded[WASM_LEB128_64_BYTES_MAX] = {0};
    size_t size_bytes = wasm_leb128_unsigned_encode(number, encoded);
    assert(size_bytes <= WASM_LEB128_64_BYTES_MAX);

    wasm_write_bytes(writer, encoded, size_bytes);
}

static void wasm_write_signed(Wasm_Writer *writer, int64_t number) {
    assert(writer->bytes != NULL);

    byte encoded[WASM_LEB128_64_BYTES_MAX] = {0};
    size_t size_bytes = wasm_leb128_signed_encode(number, encoded);
    assert(size_bytes <= WASM_LEB128_64_BYTES_MAX);

    wasm_write_bytes(writer, encoded, size_bytes);
}

/**
 * Writes a name: its size in bytes, then the bytes. Mialu identifiers are ASCII, which is valid
 * UTF-8 as WebAssembly requires.
 */
static void wasm_write_text(Wasm_Writer *writer, Str text) {
    assert(text.bytes != NULL);

    wasm_write_unsigned(writer, text.length);
    wasm_write_bytes(writer, (byte const *)text.bytes, text.length);
}

/**
 * Starts a part of the module that is preceded by its own size, which is not known until the
 * part is written. Room for the largest size is reserved, and `wasm_block_end` closes the gap,
 * so that the size takes as few bytes as it can, as the format wants. Returns where the block
 * starts, to pass to `wasm_block_end`. Blocks may nest.
 */
static size_t wasm_block_begin(Wasm_Writer *writer) {
    assert(writer->length <= writer->capacity);

    size_t block_start = writer->length;
    if (writer->is_overflowed == false) {
        if (writer->capacity - writer->length < WASM_LEB128_32_BYTES_MAX) {
            writer->is_overflowed = true;
        } else {
            writer->length += WASM_LEB128_32_BYTES_MAX;
        }
    }

    return block_start;
}

static void wasm_block_end(Wasm_Writer *writer, size_t block_start) {
    assert(writer->bytes != NULL);
    assert(block_start <= writer->length);

    if (writer->is_overflowed) {
        return;
    }
    assert(writer->length >= block_start + WASM_LEB128_32_BYTES_MAX);

    size_t content_start = block_start + WASM_LEB128_32_BYTES_MAX;
    size_t content_size_bytes = writer->length - content_start;
    assert(content_size_bytes <= UINT32_MAX);

    byte encoded[WASM_LEB128_64_BYTES_MAX] = {0};
    size_t size_field_bytes = wasm_leb128_unsigned_encode(content_size_bytes, encoded);
    assert(size_field_bytes <= WASM_LEB128_32_BYTES_MAX);

    // Moves the content down over the unused part of the reserved room.
    memmove(writer->bytes + block_start + size_field_bytes, writer->bytes + content_start,
            content_size_bytes);
    memcpy(writer->bytes + block_start, encoded, size_field_bytes);
    writer->length = block_start + size_field_bytes + content_size_bytes;
}

/**
 * A section is the id of its kind followed by a block, which `wasm_block_end` closes.
 */
static size_t wasm_section_begin(Wasm_Writer *writer, byte section_id) {
    wasm_write_byte(writer, section_id);
    return wasm_block_begin(writer);
}

/**
 * Encodes `number` in unsigned LEB128, 7 bits per byte with the high bit set on all but the
 * last, least significant bits first. Returns the number of bytes written to `encoded`, which has
 * room for the longest encoding.
 */
static size_t wasm_leb128_unsigned_encode(uint64_t number, byte *encoded) {
    assert(encoded != NULL);

    size_t size_bytes = 0;
    uint64_t rest = number;
    // 64 bits need at most 10 groups of 7 bits.
    for (size_t i = 0; i < WASM_LEB128_64_BYTES_MAX; i += 1) {
        byte group = (byte)(rest & 0x7F);
        rest >>= 7;
        if (rest != 0) {
            group = (byte)(group | 0x80);
        }
        encoded[size_bytes] = group;
        size_bytes += 1;
        if (rest == 0) {
            break;
        }
    }

    assert(size_bytes >= 1);
    assert(rest == 0);
    return size_bytes;
}

/**
 * Encodes `number` in signed LEB128: like the unsigned form, but it stops when the remaining
 * bits are all copies of the sign bit of the last group. Shifts an unsigned copy and fills in the
 * sign by hand, because right shifts of negative signed numbers are implementation-defined in C.
 */
static size_t wasm_leb128_signed_encode(int64_t number, byte *encoded) {
    assert(encoded != NULL);

    bool is_negative = number < 0;
    uint64_t rest = (uint64_t)number;
    uint64_t const sign_fill = ~(UINT64_MAX >> 7);

    size_t size_bytes = 0;
    bool is_done = false;
    for (size_t i = 0; i < WASM_LEB128_64_BYTES_MAX && is_done == false; i += 1) {
        byte group = (byte)(rest & 0x7F);
        rest >>= 7;
        if (is_negative) {
            rest |= sign_fill;
        }

        bool is_sign_bit_set = (group & 0x40) != 0;
        is_done = (rest == 0 && is_sign_bit_set == false) ||
                  (rest == UINT64_MAX && is_sign_bit_set);
        if (is_done == false) {
            group = (byte)(group | 0x80);
        }
        encoded[size_bytes] = group;
        size_bytes += 1;
    }

    assert(is_done);
    return size_bytes;
}
