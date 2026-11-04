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
};

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
// array slice is two 32-bit values, the address of its first element and its length, like `Str`.
// The elements are always `Int`.
typedef enum {
    WASM_VALUE_TYPE_ARRAY,
    WASM_VALUE_TYPE_BOOL,
    WASM_VALUE_TYPE_INT,
    WASM_VALUE_TYPE_NONE,
    WASM_VALUE_TYPE_SLICE,
    WASM_VALUE_TYPE_STR,
} Wasm_Value_Type;

// The locals that a procedure with arrays has after its parameters and variables, for the code that
// is written for subscripts, slices and the like to hold values in. The order is the order of the
// locals: the first two are 64-bit, and the others 32-bit. A scratch local is only used between
// two instructions that follow each other, never while an expression is evaluated in between, so
// that one is enough for any nesting of expressions.
typedef enum {
    WASM_SCRATCH_LOW,
    WASM_SCRATCH_HIGH,
    WASM_SCRATCH_FIRST,
    WASM_SCRATCH_SECOND,
    // The address of the array literals of the procedure, see `wasm_frame_enter`.
    WASM_SCRATCH_FRAME,
} Wasm_Scratch;

/**
 * The state shared by the building functions. A building function returns false after it recorded
 * an error here, and its caller then returns false too without recording anything more. So only
 * the first error is kept. `bindings` says what each name in the tree stands for, so the builder
 * never searches for a name itself.
 *
 * The bytes of all string literals are written to one data segment at address zero, in the order
 * of the literal nodes. The parser makes the nodes of an expression from left to right, which is
 * the order that the code is written in, so the code can tell the address of a literal while
 * it is written, before the data. `data_cursor_bytes` is that address, and `data_last_literal`
 * is the last literal that got one, which is checked to keep the two orders the same. The strings
 * of global variables are in the data too, but no procedure writes them, so they are passed over when
 * the next literal gets its address, see `wasm_data_skip_globals`.
 */
typedef struct {
    Wasm_Writer writer;
    Ast_Node const *nodes;
    Semantic_Binding const *bindings;
    Str subject;
    size_t node_count;
    Ast_Node_Index module;
    size_t data_size_bytes;
    size_t data_cursor_bytes;
    // Where the stack of the arrays ends at the low side, which is after the data, and where it
    // starts. The stack grows down from the top, which is the end of the memory.
    size_t stack_limit_bytes;
    size_t stack_top_bytes;
    Ast_Node_Index data_last_literal;
    Wasm_Build_Status status;
    // Whether the module has a memory, which it has when there is a string literal to put in it, or
    // an array type, because arrays and array slices are in memory.
    bool has_memory;
    // Whether there is a data section, which there is when there is a string literal.
    bool has_data;
    // Whether the module has a stack in its memory, and a global that points to it, which it has
    // when there is an array literal.
    bool has_stack;
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

static Wasm_Value_Type wasm_binding_value_type                (Wasm_Builder const *builder,
                                                               Semantic_Binding binding);
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
static size_t          wasm_constant_count                    (Wasm_Builder const *builder,
                                                               Ast_Node_Index module);
static void            wasm_data_skip_globals                 (Wasm_Builder *builder,
                                                               Ast_Node_Index until);
static Ast_Node_Index  wasm_global_value                      (Wasm_Builder const *builder,
                                                               Ast_Node_Index global);
static Wasm_Value_Type wasm_global_type                       (Wasm_Builder const *builder,
                                                               Ast_Node_Index global);
static size_t          wasm_global_slot_index                 (Wasm_Builder const *builder,
                                                               Ast_Node_Index global);
static size_t          wasm_global_slot_count                 (Wasm_Builder const *builder);
static void            wasm_build_global_section              (Wasm_Builder *builder);
static bool            wasm_data_measure                      (Wasm_Builder *builder);
static Wasm_Value_Type wasm_expression_type                   (Wasm_Builder const *builder,
                                                               Ast_Node_Index expression);
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
static Wasm_Value_Type wasm_value_type_of_name                (Str type_name);
static size_t          wasm_value_type_slot_count             (Wasm_Value_Type value_type);
static size_t          wasm_variable_local_index              (Wasm_Builder const *builder,
                                                               Ast_Node_Index procedure,
                                                               Ast_Node_Index variable);
static Wasm_Value_Type wasm_variable_type                     (Wasm_Builder const *builder,
                                                               Ast_Node_Index variable);
static bool            wasm_variables_check                   (Wasm_Builder *builder,
                                                               Ast_Node_Index procedure);
static void            wasm_variables_write_locals            (Wasm_Builder *builder,
                                                               Ast_Node_Index procedure);
static void            wasm_write_byte                        (Wasm_Writer *writer, byte content);
static void            wasm_write_bytes                       (Wasm_Writer *writer,
                                                               byte const *bytes, size_t count);
static void            wasm_write_signed                      (Wasm_Writer *writer, int64_t number);
static void            wasm_write_text                        (Wasm_Writer *writer, Str text);
static void            wasm_write_unsigned                    (Wasm_Writer *writer,
                                                               uint64_t number);
static void            wasm_write_value_type                  (Wasm_Writer *writer,
                                                               Wasm_Value_Type value_type);
static size_t          wasm_array_length                      (Wasm_Builder const *builder,
                                                               Ast_Node_Index expression);
static void            wasm_array_decay                       (Wasm_Builder *builder,
                                                               Ast_Node_Index expression);
static bool            wasm_build_code_section_body_array_literal(Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index literal);
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
static void            wasm_element_address_write             (Wasm_Builder *builder,
                                                               Ast_Node_Index procedure);
static Ast_Node_Index  wasm_expression_child_first            (Wasm_Builder const *builder,
                                                               Ast_Node_Index expression);
static void            wasm_frame_enter                       (Wasm_Builder *builder,
                                                               Ast_Node_Index procedure);
static bool            wasm_frame_has_literal                 (Wasm_Builder const *builder,
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
static void            wasm_slice_write                       (Wasm_Builder *builder,
                                                               Ast_Node_Index procedure);
static size_t          wasm_stack_global_index                (Wasm_Builder const *builder);
static void            wasm_structured_trap                   (Wasm_Builder *builder);
static Str             wasm_type_subject                      (Wasm_Builder const *builder,
                                                               Ast_Node_Index type);
static bool            wasm_type_accepts                      (Wasm_Value_Type target,
                                                               Wasm_Value_Type value);
static bool            wasm_type_is_sequence                  (Wasm_Value_Type value_type);
static Wasm_Value_Type wasm_value_type_of_node                (Wasm_Builder const *builder,
                                                               Ast_Node_Index type);
static size_t          wasm_variable_locals_end               (Wasm_Builder const *builder,
                                                               Ast_Node_Index procedure);
static void            wasm_write_i32_const                   (Wasm_Writer *writer, int64_t number);
static void            wasm_write_local_instruction           (Wasm_Writer *writer, byte opcode,
                                                               size_t local_index);
static void            wasm_write_memory_instruction          (Wasm_Writer *writer, byte opcode,
                                                               uint64_t offset_bytes);

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
        .data_size_bytes = 0,
        .data_cursor_bytes = 0,
        .stack_limit_bytes = 0,
        .stack_top_bytes = 0,
        .data_last_literal = AST_NODE_INDEX_NONE,
        .status = WASM_BUILD_STATUS_OK,
        .has_memory = false,
        .has_data = false,
        .has_stack = false,
    };

    // Only the constant definitions are functions. A global variable is a global of the module.
    size_t definition_count = wasm_constant_count(&builder, root);
    wasm_write_bytes(&builder.writer, WASM_MODULE_HEADER, sizeof(WASM_MODULE_HEADER));

    // The types are checked first, so that the sections below can rely on every type being known.
    // The size of the data is needed before the code is written, because the memory has to be
    // big enough for it and comes before the code.
    bool is_ok = wasm_data_measure(&builder);
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
        wasm_build_function_section(&builder, definition_count);
        if (builder.has_memory) {
            wasm_build_memory_section(&builder);
        }
        if (wasm_global_slot_count(&builder) > 0 || builder.has_stack) {
            wasm_build_global_section(&builder);
        }
        is_ok = wasm_build_export_section(&builder, root, definition_count);
    }
    if (is_ok) {
        is_ok = wasm_build_code_section(&builder, root, definition_count);
    }
    if (is_ok && builder.has_data) {
        // The code gave every literal its address, except for the ones of global variables, which
        // are after the last one that the code wrote, and the data must be what those addresses
        // were counted from.
        wasm_data_skip_globals(&builder, (Ast_Node_Index)builder.node_count);
        assert(builder.data_cursor_bytes == builder.data_size_bytes);
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
    for (size_t i = 0; i < builder->node_count; i += 1) {
        if (builder->nodes[i].node_type == AST_NODE_TYPE_STRING_LITERAL) {
            has_literal = true;
            // Each literal is shorter than 4 GiB and there are fewer than 4 Gi of them, so the
            // sum fits in 64 bits.
            size_bytes += builder->nodes[i].payload.string_literal.text.length;
        } else if (builder->nodes[i].node_type == AST_NODE_TYPE_ARRAY_TYPE) {
            has_array_type = true;
        } else if (builder->nodes[i].node_type == AST_NODE_TYPE_ARRAY_LITERAL) {
            has_array_literal = true;
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
    // needs a memory, also when it has no array of its own.
    builder->has_memory = has_literal || has_array_type;
    builder->has_stack = has_array_literal;
    builder->stack_limit_bytes = stack_limit_bytes;
    builder->stack_top_bytes = stack_top_bytes;
    return true;
}

/**
 * Checks the types of the signature of every procedure: its parameters, and its return type when
 * it has one. Returns false after it recorded an error. What comes after can ask for the type of
 * a parameter or of a result and assume that it is known.
 */
static bool wasm_check_signatures(Wasm_Builder *builder, Ast_Node_Index module,
                                  size_t definition_count) {
    assert(module < builder->node_count);
    assert(builder->nodes[module].node_type == AST_NODE_TYPE_MODULE);

    Ast_Node_Index definition = wasm_constant_find(builder, builder->nodes[module].first_child);
    for (size_t i = 0; i < definition_count; i += 1) {
        assert(definition != AST_NODE_INDEX_NONE);

        // A string has no signature to check: it has no parameters, and its type is known.
        Ast_Node_Index value = builder->nodes[definition].first_child;
        if (builder->nodes[value].node_type == AST_NODE_TYPE_PROCEDURE) {
            size_t parameter_count = wasm_parameter_count(builder, value);
            Ast_Node_Index parameter = builder->nodes[value].first_child;
            for (size_t j = 0; j < parameter_count; j += 1) {
                assert(parameter != AST_NODE_INDEX_NONE);
                Ast_Node const *parameter_node = &builder->nodes[parameter];
                assert(parameter_node->node_type == AST_NODE_TYPE_PARAMETER);

                Ast_Node_Index type = parameter_node->first_child;
                Wasm_Value_Type parameter_type = wasm_value_type_of_node(builder, type);
                if (parameter_type == WASM_VALUE_TYPE_NONE) {
                    return wasm_fail(builder, WASM_BUILD_STATUS_TYPE_UNSUPPORTED,
                                     wasm_type_subject(builder, type));
                }
                // The elements of an array are in the frame of the procedure that has it, and a
                // parameter would have to be a copy of them, so an array slice is passed instead.
                if (parameter_type == WASM_VALUE_TYPE_ARRAY) {
                    return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_TYPE_MISPLACED,
                                     wasm_type_subject(builder, type));
                }

                parameter = parameter_node->next_sibling;
            }

            Str return_type = builder->nodes[value].payload.procedure.return_type;
            if (return_type.bytes != NULL) {
                if (wasm_value_type_of_name(return_type) == WASM_VALUE_TYPE_NONE) {
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
        if (builder->nodes[current].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION) {
            return current;
        }
        current = builder->nodes[current].next_sibling;
    }

    return AST_NODE_INDEX_NONE;
}

/**
 * Returns the number of constant definitions of `module`, which is the number of functions.
 */
static size_t wasm_constant_count(Wasm_Builder const *builder, Ast_Node_Index module) {
    assert(module < builder->node_count);

    size_t count = 0;
    Ast_Node_Index definition = wasm_constant_find(builder, builder->nodes[module].first_child);
    for (size_t i = 0; i < builder->node_count && definition != AST_NODE_INDEX_NONE; i += 1) {
        count += 1;
        definition = wasm_constant_find(builder, builder->nodes[definition].next_sibling);
    }

    return count;
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
 * child after the attributes, or else the type of its value. Returns `WASM_VALUE_TYPE_NONE` when
 * the type that the definition gives is not one that the target has, which `wasm_check_globals`
 * rejects.
 */
static Wasm_Value_Type wasm_global_type(Wasm_Builder const *builder, Ast_Node_Index global) {
    assert(global < builder->node_count);
    assert(builder->nodes[global].node_type == AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION);

    Ast_Node_Index type = wasm_global_type_node(builder, global);
    if (type != AST_NODE_INDEX_NONE) {
        return wasm_value_type_of_node(builder, type);
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
            Wasm_Value_Type global_type = wasm_global_type(builder, global);
            Ast_Node_Index value = wasm_global_value(builder, global);
            if (global_type == WASM_VALUE_TYPE_NONE) {
                // The only way to have no type is a type name that the target does not have, and
                // it is the child before the value.
                Ast_Node_Index type = wasm_global_type_node(builder, global);
                assert(type != AST_NODE_INDEX_NONE);
                return wasm_fail(builder, WASM_BUILD_STATUS_TYPE_UNSUPPORTED,
                                 wasm_type_subject(builder, type));
            }
            // A global is set once, from a literal, and no literal makes an array or a slice.
            if (global_type == WASM_VALUE_TYPE_ARRAY || global_type == WASM_VALUE_TYPE_SLICE) {
                Ast_Node_Index type = wasm_global_type_node(builder, global);
                assert(type != AST_NODE_INDEX_NONE);
                return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_TYPE_MISPLACED,
                                 wasm_type_subject(builder, type));
            }
            if (wasm_expression_type(builder, value) != global_type) {
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
            slot_index += wasm_value_type_slot_count(wasm_global_type(builder, current));
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
            slot_count += wasm_value_type_slot_count(wasm_global_type(builder, current));
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
            Wasm_Value_Type value_type = wasm_global_type(builder, global);
            Ast_Node_Index value = wasm_global_value(builder, global);
            Ast_Node const *value_node = &builder->nodes[value];

            // Both are written as `i32.const` or `i64.const` followed by the end of the expression.
            if (value_type == WASM_VALUE_TYPE_STR) {
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
            } else if (value_type == WASM_VALUE_TYPE_BOOL) {
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
 * Adds the length of the strings whose nodes are after the last literal that the code wrote and
 * before `until` to the address that the next literal gets. Those are the strings of global
 * variables, which are in the data because the data has every string literal in the order of the
 * nodes, but which no procedure writes.
 */
static void wasm_data_skip_globals(Wasm_Builder *builder, Ast_Node_Index until) {
    assert(until <= builder->node_count);

    size_t from = 0;
    if (builder->data_last_literal != AST_NODE_INDEX_NONE) {
        from = (size_t)builder->data_last_literal + 1;
    }
    for (size_t i = from; i < until; i += 1) {
        if (builder->nodes[i].node_type == AST_NODE_TYPE_STRING_LITERAL) {
            builder->data_cursor_bytes += builder->nodes[i].payload.string_literal.text.length;
        }
    }
}

/**
 * Writes the type section, which has one function type per definition: the parameters and the
 * result of its procedure. Types are not shared, because that would save only a few bytes. The
 * types in the signatures must have been checked by `wasm_check_signatures`.
 */
static void wasm_build_type_section(Wasm_Builder *builder, Ast_Node_Index module,
                                    size_t definition_count) {
    assert(module < builder->node_count);
    assert(builder->nodes[module].node_type == AST_NODE_TYPE_MODULE);

    size_t section = wasm_section_begin(&builder->writer, WASM_SECTION_ID_TYPE);
    wasm_write_unsigned(&builder->writer, definition_count);

    Ast_Node_Index definition = wasm_constant_find(builder, builder->nodes[module].first_child);
    for (size_t i = 0; i < definition_count; i += 1) {
        assert(definition != AST_NODE_INDEX_NONE);

        wasm_write_byte(&builder->writer, WASM_TYPE_FUNCTION);

        Ast_Node_Index value = builder->nodes[definition].first_child;
        if (builder->nodes[value].node_type == AST_NODE_TYPE_STRING_LITERAL) {
            // A constant string is a function without parameters that returns the string.
            wasm_write_unsigned(&builder->writer, 0);
            wasm_write_unsigned(&builder->writer, wasm_value_type_slot_count(WASM_VALUE_TYPE_STR));
            wasm_write_value_type(&builder->writer, WASM_VALUE_TYPE_STR);
        } else {
            Ast_Node_Index procedure = wasm_procedure_of(builder, definition);

            // A `Str` is two WebAssembly values, so the count is of values and not of parameters.
            wasm_write_unsigned(&builder->writer, wasm_parameter_slot_count(builder, procedure));
            Ast_Node_Index parameter = builder->nodes[procedure].first_child;
            size_t parameter_count = wasm_parameter_count(builder, procedure);
            for (size_t j = 0; j < parameter_count; j += 1) {
                assert(parameter != AST_NODE_INDEX_NONE);
                Ast_Node const *parameter_node = &builder->nodes[parameter];

                wasm_write_value_type(
                    &builder->writer,
                    wasm_value_type_of_node(builder, parameter_node->first_child));
                parameter = parameter_node->next_sibling;
            }

            Str return_type_name = builder->nodes[procedure].payload.procedure.return_type;
            if (return_type_name.bytes == NULL) {
                wasm_write_unsigned(&builder->writer, 0);
            } else {
                Wasm_Value_Type return_type = wasm_value_type_of_name(return_type_name);
                wasm_write_unsigned(&builder->writer, wasm_value_type_slot_count(return_type));
                wasm_write_value_type(&builder->writer, return_type);
            }
        }

        definition = wasm_constant_find(builder, builder->nodes[definition].next_sibling);
    }

    wasm_block_end(&builder->writer, section);
}

/**
 * Returns the type that `type_name` stands for, or `WASM_VALUE_TYPE_NONE` if the target does not
 * have a type of that name.
 */
static Wasm_Value_Type wasm_value_type_of_name(Str type_name) {
    assert(type_name.bytes != NULL);
    assert(type_name.length > 0);

    if (str_equals(type_name, STR("Bool"))) {
        return WASM_VALUE_TYPE_BOOL;
    }
    if (str_equals(type_name, STR("Int"))) {
        return WASM_VALUE_TYPE_INT;
    }
    if (str_equals(type_name, STR("Str"))) {
        return WASM_VALUE_TYPE_STR;
    }
    return WASM_VALUE_TYPE_NONE;
}

/**
 * Returns the number of WebAssembly values that a value of `value_type` takes. `Int` is a 64-bit
 * integer, so that every integer literal, which the tokenizer accepts up to the largest signed
 * 64-bit value, fits. `Bool` is one 32-bit integer, which is 0 for `false` and 1 for `true`, like
 * the result of a WebAssembly comparison. `Str` is the address and the length of its bytes, both
 * 32-bit, because addresses are. An array is the address of its first element, and an array slice
 * the address of its first element and its length in elements.
 */
static size_t wasm_value_type_slot_count(Wasm_Value_Type value_type) {
    assert(value_type != WASM_VALUE_TYPE_NONE);

    if (value_type == WASM_VALUE_TYPE_STR || value_type == WASM_VALUE_TYPE_SLICE) {
        return 2;
    }
    return 1;
}

/**
 * Writes the WebAssembly types that a value of `value_type` is made of, which are as many as
 * `wasm_value_type_slot_count` says.
 */
static void wasm_write_value_type(Wasm_Writer *writer, Wasm_Value_Type value_type) {
    assert(value_type != WASM_VALUE_TYPE_NONE);

    if (value_type == WASM_VALUE_TYPE_STR || value_type == WASM_VALUE_TYPE_SLICE) {
        wasm_write_byte(writer, WASM_TYPE_I32);
        wasm_write_byte(writer, WASM_TYPE_I32);
    } else if (value_type == WASM_VALUE_TYPE_BOOL || value_type == WASM_VALUE_TYPE_ARRAY) {
        wasm_write_byte(writer, WASM_TYPE_I32);
    } else {
        wasm_write_byte(writer, WASM_TYPE_I64);
    }
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
 * Writes the function section, which says which type each function has. The function at index
 * `i` has the type at index `i`, because the type section has one type per definition.
 */
static void wasm_build_function_section(Wasm_Builder *builder, size_t definition_count) {
    assert(definition_count <= builder->node_count);

    size_t section = wasm_section_begin(&builder->writer, WASM_SECTION_ID_FUNCTION);
    wasm_write_unsigned(&builder->writer, definition_count);
    for (size_t i = 0; i < definition_count; i += 1) {
        wasm_write_unsigned(&builder->writer, i);
    }
    wasm_block_end(&builder->writer, section);
}

/**
 * Writes the export section, which makes every function callable from outside the module under
 * the name of its definition, and the memory, if there is one, readable under the name `memory`.
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

    Ast_Node_Index definition = wasm_constant_find(builder, builder->nodes[module].first_child);
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
        wasm_write_unsigned(&builder->writer, i);

        definition = wasm_constant_find(builder, builder->nodes[definition].next_sibling);
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

    Ast_Node_Index definition = wasm_constant_find(builder, builder->nodes[module].first_child);
    for (size_t i = 0; i < definition_count; i += 1) {
        assert(definition != AST_NODE_INDEX_NONE);

        if (wasm_build_code_section_body(builder, definition) == false) {
            return false;
        }
        definition = wasm_constant_find(builder, builder->nodes[definition].next_sibling);
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
    } else {
        // The parameters are the first locals of the function already, and the variables come
        // after them.
        if (wasm_variables_check(builder, value) == false) {
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
 * The address is where the string will be in the data section, which is written last, in the
 * order of the nodes, see `Wasm_Builder`.
 */
static void wasm_build_code_section_body_string(Wasm_Builder *builder, Ast_Node_Index literal) {
    assert(literal < builder->node_count);
    assert(builder->nodes[literal].node_type == AST_NODE_TYPE_STRING_LITERAL);
    assert(builder->has_memory);

    // The data is in the order of the nodes, so a literal written out of order would be given an
    // address that is somebody else's string.
    assert(builder->data_last_literal == AST_NODE_INDEX_NONE ||
           literal > builder->data_last_literal);

    // The strings of global variables are in the data as well, but the code of a procedure does not
    // write them, so they are passed here.
    wasm_data_skip_globals(builder, literal);

    size_t length_bytes = builder->nodes[literal].payload.string_literal.text.length;
    assert(builder->data_cursor_bytes + length_bytes <= builder->data_size_bytes);

    // Both are below 2 GiB, because `wasm_data_measure` checked the total.
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_CONST);
    wasm_write_signed(&builder->writer, (int64_t)builder->data_cursor_bytes);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_CONST);
    wasm_write_signed(&builder->writer, (int64_t)length_bytes);

    builder->data_cursor_bytes += length_bytes;
    builder->data_last_literal = literal;
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
                if (wasm_expression_type(builder, child) != WASM_VALUE_TYPE_BOOL) {
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
        if (wasm_expression_type(builder, ends[i]) != WASM_VALUE_TYPE_INT) {
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
 * definition, and for the iterator of a for statement two, which also keeps the end of its range,
 * or three when it goes through the elements of an array, which keeps where it is and where it
 * ends.
 */
static size_t wasm_variable_slot_count(Wasm_Builder const *builder, Ast_Node_Index variable) {
    assert(variable < builder->node_count);

    if (builder->nodes[variable].node_type == AST_NODE_TYPE_FOR_STATEMENT) {
        if (wasm_for_is_each(builder, variable)) {
            return 3;
        }
        return 2;
    }
    return wasm_value_type_slot_count(wasm_variable_type(builder, variable));
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

    Wasm_Value_Type value_type = wasm_expression_type(builder, call);
    if (value_type != WASM_VALUE_TYPE_NONE) {
        size_t slot_count = wasm_value_type_slot_count(value_type);
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

    Semantic_Binding binding = builder->bindings[call];
    assert(binding.kind == SEMANTIC_BINDING_KIND_CONSTANT);
    Ast_Node_Index callee = wasm_procedure_of(builder, binding.target);
    Str name = builder->nodes[call].payload.call.name;

    if (wasm_children_count(builder, call) != wasm_parameter_count(builder, callee)) {
        return wasm_fail(builder, WASM_BUILD_STATUS_CALL_ARGUMENT_COUNT_MISMATCH, name);
    }

    Ast_Node_Index argument = builder->nodes[call].first_child;
    Ast_Node_Index parameter = builder->nodes[callee].first_child;
    size_t argument_count = wasm_children_count(builder, call);
    for (size_t i = 0; i < argument_count; i += 1) {
        assert(argument != AST_NODE_INDEX_NONE && parameter != AST_NODE_INDEX_NONE);

        Ast_Node_Index type = builder->nodes[parameter].first_child;
        Wasm_Value_Type parameter_type = wasm_value_type_of_node(builder, type);
        // An array is passed as an array slice, see `wasm_array_decay`.
        if (wasm_type_accepts(parameter_type, wasm_expression_type(builder, argument)) == false) {
            return wasm_fail(builder, WASM_BUILD_STATUS_CALL_ARGUMENT_TYPE_MISMATCH, name);
        }
        argument = builder->nodes[argument].next_sibling;
        parameter = builder->nodes[parameter].next_sibling;
    }

    bool has_result = builder->nodes[callee].payload.procedure.return_type.bytes != NULL;
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
    Wasm_Value_Type return_type = wasm_value_type_of_name(return_type_name);
    if (wasm_expression_type(builder, value) != return_type) {
        Str name = builder->nodes[definition].payload.constant_definition.name;
        return wasm_fail(builder, WASM_BUILD_STATUS_RETURN_TYPE_MISMATCH, name);
    }

    // The value is on the stack already, and giving the frame back does not touch it.
    wasm_frame_leave(builder, procedure);
    wasm_write_byte(&builder->writer, WASM_OPCODE_RETURN);
    return true;
}

/**
 * Writes the instructions of `assignment`, such as `x += 2`: the value of the variable is pushed,
 * then the value of the assignment, then the operator is applied to them, and the result is stored
 * in the local of the variable, or in the global when it is a global variable. Only an `Int` can be
 * assigned this way, so a variable of any other type, or a value of any other type, is rejected.
 * Returns false after it recorded an error.
 */
static bool wasm_build_code_section_body_assignment(Wasm_Builder *builder,
                                                    Ast_Node_Index definition,
                                                    Ast_Node_Index assignment) {
    assert(assignment < builder->node_count);
    assert(ast_node_type_is_assignment(builder->nodes[assignment].node_type));

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index target = builder->nodes[assignment].first_child;
    Ast_Node_Index value = builder->nodes[assignment].last_child;

    // An element, or a range of elements, is in memory, and is not stored like a variable.
    if (builder->nodes[target].node_type != AST_NODE_TYPE_IDENTIFIER) {
        return wasm_build_code_section_body_element_assignment(builder, definition, assignment);
    }

    // The analysis checked that the name stands for a variable or a global variable. A global is
    // read and stored with the global instructions, and a variable with the local ones.
    Semantic_Binding binding = builder->bindings[target];
    assert(binding.kind == SEMANTIC_BINDING_KIND_VARIABLE ||
           binding.kind == SEMANTIC_BINDING_KIND_GLOBAL);
    bool is_global = binding.kind == SEMANTIC_BINDING_KIND_GLOBAL;
    Str target_name = builder->nodes[target].payload.identifier.name;
    if (wasm_binding_value_type(builder, binding) != WASM_VALUE_TYPE_INT) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID, target_name);
    }

    size_t storage_index = 0;
    if (is_global) {
        storage_index = wasm_global_slot_index(builder, binding.target);
        wasm_write_byte(&builder->writer, WASM_OPCODE_GLOBAL_GET);
    } else {
        storage_index = wasm_variable_local_index(builder, procedure, binding.target);
        wasm_write_byte(&builder->writer, WASM_OPCODE_LOCAL_GET);
    }
    wasm_write_unsigned(&builder->writer, storage_index);

    if (wasm_build_code_section_body_expression(builder, definition, value, false) == false) {
        return false;
    }

    if (wasm_expression_type(builder, value) != WASM_VALUE_TYPE_INT) {
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
 * Writes the instructions of the definition of `variable`: the initial value is left on the stack
 * and then stored in the locals of the variable. The last local is stored first, because the
 * value of a `Str` has its length on top of the stack.
 */
static bool wasm_build_code_section_body_variable(Wasm_Builder *builder,
                                                  Ast_Node_Index definition,
                                                  Ast_Node_Index variable) {
    assert(variable < builder->node_count);
    assert(builder->nodes[variable].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION);

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index value = builder->nodes[variable].last_child;
    if (wasm_build_code_section_body_expression(builder, definition, value, false) == false) {
        return false;
    }

    // The value was written, so every name in it is known. It must be of the type of the variable,
    // which is the type that was written when the definition has one, and deduced otherwise.
    Wasm_Value_Type variable_type = wasm_variable_type(builder, variable);
    if (wasm_type_accepts(variable_type, wasm_expression_type(builder, value)) == false) {
        Str name = builder->nodes[variable].payload.variable_definition.name;
        return wasm_fail(builder, WASM_BUILD_STATUS_VARIABLE_TYPE_MISMATCH, name);
    }

    // An array whose type gives the length must have exactly that many elements.
    Ast_Node_Index declared = builder->nodes[variable].first_child;
    if (variable_type == WASM_VALUE_TYPE_ARRAY &&
        builder->nodes[declared].node_type == AST_NODE_TYPE_ARRAY_TYPE &&
        builder->nodes[declared].payload.array_type.length_kind == AST_ARRAY_LENGTH_FIXED &&
        (size_t)builder->nodes[declared].payload.array_type.length !=
            wasm_array_length(builder, value)) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_LENGTH_MISMATCH,
                         wasm_type_subject(builder, declared));
    }

    // A slice is given an array by viewing all of its elements.
    if (variable_type == WASM_VALUE_TYPE_SLICE) {
        wasm_array_decay(builder, value);
    }

    size_t local_index = wasm_variable_local_index(builder, procedure, variable);
    size_t slot_count = wasm_value_type_slot_count(variable_type);
    for (size_t slot = slot_count; slot > 0; slot -= 1) {
        wasm_write_byte(&builder->writer, WASM_OPCODE_LOCAL_SET);
        wasm_write_unsigned(&builder->writer, local_index + slot - 1);
    }

    return true;
}

/**
 * Checks the type of every variable of `procedure`, also the ones in its blocks, so that what
 * comes after can ask for the type of a variable and assume that it is known. A variable has the
 * type that its definition gives, or else the type of its value. Returns false after it recorded
 * an error.
 */
static bool wasm_variables_check(Wasm_Builder *builder, Ast_Node_Index procedure) {
    assert(procedure < builder->node_count);
    assert(builder->nodes[procedure].node_type == AST_NODE_TYPE_PROCEDURE);

    Ast_Node_Index end = ast_procedure_end(builder->nodes, builder->node_count, procedure);
    for (Ast_Node_Index statement = procedure + 1; statement < end; statement += 1) {
        Ast_Node const *statement_node = &builder->nodes[statement];

        if (statement_node->node_type == AST_NODE_TYPE_VARIABLE_DEFINITION) {
            Ast_Node const *first = &builder->nodes[statement_node->first_child];
            if (wasm_node_is_type(builder, statement_node->first_child)) {
                if (wasm_value_type_of_node(builder, statement_node->first_child) ==
                    WASM_VALUE_TYPE_NONE) {
                    return wasm_fail(builder, WASM_BUILD_STATUS_TYPE_UNSUPPORTED,
                                     wasm_type_subject(builder, statement_node->first_child));
                }
            } else if (wasm_variable_type(builder, statement) == WASM_VALUE_TYPE_NONE) {
                // The only values without a type are a name that is a procedure, and a call of a
                // procedure that returns nothing.
                if (first->node_type == AST_NODE_TYPE_CALL) {
                    return wasm_fail(builder, WASM_BUILD_STATUS_CALL_RESULT_MISSING,
                                     first->payload.call.name);
                }
                assert(first->node_type == AST_NODE_TYPE_IDENTIFIER);
                return wasm_fail(builder, WASM_BUILD_STATUS_IDENTIFIER_NOT_VALUE,
                                 first->payload.identifier.name);
            }

            // The elements of an array are put in the frame of the procedure by the array literal
            // that makes them, so an array variable is made by one, and is not a copy of another.
            Ast_Node_Index value = statement_node->last_child;
            if (wasm_variable_type(builder, statement) == WASM_VALUE_TYPE_ARRAY &&
                builder->nodes[value].node_type != AST_NODE_TYPE_ARRAY_LITERAL) {
                return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_INITIALIZER_INVALID,
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
 * Writes the locals of the variables of `procedure`, which come after the parameters: a count of
 * groups, and for each variable the number of locals it takes and their type. The variables of the
 * blocks are included, in the order of the source: a variable has its own local, also when
 * another variable in another block has the same name. The types must have been checked by
 * `wasm_variables_check`. A procedure that uses arrays has the locals of `Wasm_Scratch` after the
 * variables.
 */
static void wasm_variables_write_locals(Wasm_Builder *builder, Ast_Node_Index procedure) {
    assert(procedure < builder->node_count);

    Ast_Node_Index end = ast_procedure_end(builder->nodes, builder->node_count, procedure);

    // A for statement over the elements of an array has locals of two types, so it is two groups.
    bool uses_arrays = wasm_procedure_uses_arrays(builder, procedure);
    size_t group_count = 0;
    for (Ast_Node_Index statement = procedure + 1; statement < end; statement += 1) {
        if (builder->nodes[statement].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION) {
            group_count += 1;
        } else if (builder->nodes[statement].node_type == AST_NODE_TYPE_FOR_STATEMENT) {
            group_count += 1;
            if (wasm_for_is_each(builder, statement)) {
                group_count += 1;
            }
        }
    }
    if (uses_arrays) {
        group_count += 2;
    }
    wasm_write_unsigned(&builder->writer, group_count);

    for (Ast_Node_Index statement = procedure + 1; statement < end; statement += 1) {
        if (builder->nodes[statement].node_type == AST_NODE_TYPE_FOR_STATEMENT) {
            if (wasm_for_is_each(builder, statement)) {
                // The iterator is an `Int`, and the address of the next element and the address
                // after the last one are 32-bit.
                wasm_write_unsigned(&builder->writer, 1);
                wasm_write_byte(&builder->writer, WASM_TYPE_I64);
                wasm_write_unsigned(&builder->writer, 2);
                wasm_write_byte(&builder->writer, WASM_TYPE_I32);
            } else {
                // The iterator and the end of the range, which are both `Int`.
                wasm_write_unsigned(&builder->writer, wasm_variable_slot_count(builder, statement));
                wasm_write_byte(&builder->writer, WASM_TYPE_I64);
            }
        } else if (builder->nodes[statement].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION) {
            Wasm_Value_Type value_type = wasm_variable_type(builder, statement);
            wasm_write_unsigned(&builder->writer, wasm_value_type_slot_count(value_type));
            // A `Str` is two locals of one type, so one group holds all the locals of a variable.
            if (value_type == WASM_VALUE_TYPE_STR || value_type == WASM_VALUE_TYPE_BOOL ||
                value_type == WASM_VALUE_TYPE_ARRAY || value_type == WASM_VALUE_TYPE_SLICE) {
                wasm_write_byte(&builder->writer, WASM_TYPE_I32);
            } else {
                wasm_write_byte(&builder->writer, WASM_TYPE_I64);
            }
        }
    }

    // The order of `Wasm_Scratch`: two 64-bit locals, and then three 32-bit ones.
    if (uses_arrays) {
        wasm_write_unsigned(&builder->writer, 2);
        wasm_write_byte(&builder->writer, WASM_TYPE_I64);
        wasm_write_unsigned(&builder->writer, 3);
        wasm_write_byte(&builder->writer, WASM_TYPE_I32);
    }
}

/**
 * Returns the type of `variable`: the one its definition gives, or else the type of its value. A
 * name as the value has the type of what it stands for, so the search follows a chain of variables
 * that were defined from each other, which ends because a variable can only use the ones above
 * it. Returns `WASM_VALUE_TYPE_NONE` when the type is not one that the target has, or the value
 * is a procedure.
 */
static Wasm_Value_Type wasm_variable_type(Wasm_Builder const *builder, Ast_Node_Index variable) {
    assert(variable < builder->node_count);
    assert(builder->nodes[variable].node_type == AST_NODE_TYPE_VARIABLE_DEFINITION ||
           builder->nodes[variable].node_type == AST_NODE_TYPE_FOR_STATEMENT);

    Ast_Node_Index current = variable;
    // Each step goes to a variable above, so there are fewer steps than nodes.
    for (size_t i = 0; i < builder->node_count; i += 1) {
        Ast_Node const *current_node = &builder->nodes[current];
        if (current_node->node_type == AST_NODE_TYPE_FOR_STATEMENT) {
            // The iterator counts numbers.
            return WASM_VALUE_TYPE_INT;
        }
        if (wasm_node_is_type(builder, current_node->first_child)) {
            return wasm_value_type_of_node(builder, current_node->first_child);
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
    return WASM_VALUE_TYPE_NONE;
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
    return local_index;
}

/**
 * Returns the type of the value of `expression`. The expression must have been written already,
 * so that every name in it is known to have a value. Only the root matters: an arithmetic
 * operation is an `Int` and a comparison or a logical operation is a `Bool`, because their operands
 * were checked, as they were written.
 */
static Wasm_Value_Type wasm_expression_type(Wasm_Builder const *builder,
                                            Ast_Node_Index expression) {
    assert(expression < builder->node_count);

    Ast_Node const *node = &builder->nodes[expression];
    switch (node->node_type) {
    case AST_NODE_TYPE_ADDITION:
    case AST_NODE_TYPE_DIVISION:
    case AST_NODE_TYPE_INTEGER_LITERAL:
    case AST_NODE_TYPE_MULTIPLICATION:
    case AST_NODE_TYPE_SUBTRACTION:
        return WASM_VALUE_TYPE_INT;
    case AST_NODE_TYPE_IDENTIFIER: {
        Semantic_Binding binding = builder->bindings[expression];
        Wasm_Value_Type value_type = wasm_binding_value_type(builder, binding);
        assert(value_type != WASM_VALUE_TYPE_NONE);
        return value_type;
    }
    case AST_NODE_TYPE_AND:
    case AST_NODE_TYPE_BOOL_LITERAL:
    case AST_NODE_TYPE_COMPARISON:
    case AST_NODE_TYPE_NOT:
    case AST_NODE_TYPE_OR:
        return WASM_VALUE_TYPE_BOOL;
    case AST_NODE_TYPE_CALL: {
        // The type of what the procedure returns, which is none for a procedure without a result.
        Semantic_Binding binding = builder->bindings[expression];
        Ast_Node_Index callee = wasm_procedure_of(builder, binding.target);
        Str return_type = builder->nodes[callee].payload.procedure.return_type;
        if (return_type.bytes == NULL) {
            return WASM_VALUE_TYPE_NONE;
        }
        return wasm_value_type_of_name(return_type);
    }
    case AST_NODE_TYPE_STRING_LITERAL:
        return WASM_VALUE_TYPE_STR;
    case AST_NODE_TYPE_ARRAY_LITERAL:
        return WASM_VALUE_TYPE_ARRAY;
    case AST_NODE_TYPE_SLICE:
        return WASM_VALUE_TYPE_SLICE;
    case AST_NODE_TYPE_SUBSCRIPT:
        return WASM_VALUE_TYPE_INT;
    case AST_NODE_TYPE_ADDITION_ASSIGNMENT:
    case AST_NODE_TYPE_ARRAY_TYPE:
    case AST_NODE_TYPE_ASSIGNMENT:
    case AST_NODE_TYPE_ATTRIBUTE:
    case AST_NODE_TYPE_BLOCK:
    case AST_NODE_TYPE_CALL_STATEMENT:
    case AST_NODE_TYPE_CONSTANT_DEFINITION:
    case AST_NODE_TYPE_DIVISION_ASSIGNMENT:
    case AST_NODE_TYPE_BREAK_STATEMENT:
    case AST_NODE_TYPE_CONTINUE_STATEMENT:
    case AST_NODE_TYPE_FOR_STATEMENT:
    case AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION:
    case AST_NODE_TYPE_IF_STATEMENT:
    case AST_NODE_TYPE_LABEL:
    case AST_NODE_TYPE_LABELED_STATEMENT:
    case AST_NODE_TYPE_LOOP_STATEMENT:
    case AST_NODE_TYPE_MODULE:
    case AST_NODE_TYPE_MULTIPLICATION_ASSIGNMENT:
    case AST_NODE_TYPE_PARAMETER:
    case AST_NODE_TYPE_PROCEDURE:
    case AST_NODE_TYPE_RANGE:
    case AST_NODE_TYPE_RETURN_STATEMENT:
    case AST_NODE_TYPE_SCOPE_STATEMENT:
    case AST_NODE_TYPE_SUBTRACTION_ASSIGNMENT:
    case AST_NODE_TYPE_TYPE_NAME:
    case AST_NODE_TYPE_VARIABLE_DEFINITION:
        // None of these is part of an expression.
        assert(false);
        break;
    }

    return WASM_VALUE_TYPE_NONE;
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
        if (wasm_expression_type(builder, left) != WASM_VALUE_TYPE_BOOL) {
            return wasm_fail(builder, WASM_BUILD_STATUS_LOGICAL_OPERAND_TYPE_INVALID,
                             wasm_expression_subject(builder, definition, left));
        }
        return true;
    }
    assert(right != left);

    Wasm_Value_Type left_type = wasm_expression_type(builder, left);
    Wasm_Value_Type right_type = wasm_expression_type(builder, right);

    if (node->node_type == AST_NODE_TYPE_AND || node->node_type == AST_NODE_TYPE_OR) {
        if (left_type != WASM_VALUE_TYPE_BOOL) {
            return wasm_fail(builder, WASM_BUILD_STATUS_LOGICAL_OPERAND_TYPE_INVALID,
                             wasm_expression_subject(builder, definition, left));
        }
        if (right_type != WASM_VALUE_TYPE_BOOL) {
            return wasm_fail(builder, WASM_BUILD_STATUS_LOGICAL_OPERAND_TYPE_INVALID,
                             wasm_expression_subject(builder, definition, right));
        }
        return true;
    }

    if (node->node_type != AST_NODE_TYPE_COMPARISON) {
        if (left_type != WASM_VALUE_TYPE_INT) {
            return wasm_fail(builder, WASM_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID,
                             wasm_expression_subject(builder, definition, left));
        }
        if (right_type != WASM_VALUE_TYPE_INT) {
            return wasm_fail(builder, WASM_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID,
                             wasm_expression_subject(builder, definition, right));
        }
        return true;
    }

    Ast_Comparison_Operator operator = node->payload.comparison.operator;
    bool is_equality = operator == AST_COMPARISON_OPERATOR_EQUAL ||
                       operator == AST_COMPARISON_OPERATOR_NOT_EQUAL;
    bool is_left_comparable = left_type == WASM_VALUE_TYPE_INT ||
                              (left_type == WASM_VALUE_TYPE_BOOL && is_equality);
    if (is_left_comparable == false) {
        return wasm_fail(builder, WASM_BUILD_STATUS_COMPARISON_OPERAND_TYPE_INVALID,
                         wasm_expression_subject(builder, definition, left));
    }
    if (right_type != left_type) {
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
                builder->nodes[path[path_count - 1]].node_type == AST_NODE_TYPE_ARRAY_LITERAL) {
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
        case AST_NODE_TYPE_CALL:
            // The function index of a procedure is the position of its definition.
            wasm_write_byte(&builder->writer, WASM_OPCODE_CALL);
            wasm_write_unsigned(&builder->writer, builder->bindings[current].position);
            break;
        case AST_NODE_TYPE_BOOL_LITERAL:
            wasm_write_byte(&builder->writer, WASM_OPCODE_I32_CONST);
            wasm_write_signed(&builder->writer, node->payload.bool_literal.value ? 1 : 0);
            break;
        case AST_NODE_TYPE_COMPARISON:
            // `Bool` values are compared as the 32-bit integers that they are.
            if (wasm_expression_type(builder, node->first_child) == WASM_VALUE_TYPE_BOOL) {
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
            Wasm_Value_Type value_type = wasm_binding_value_type(builder, binding);
            if (value_type == WASM_VALUE_TYPE_NONE) {
                return wasm_fail(builder, WASM_BUILD_STATUS_IDENTIFIER_NOT_VALUE, name);
            }

            wasm_build_code_section_body_identifier(builder, procedure, binding);
            break;
        }
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
        case AST_NODE_TYPE_SUBSCRIPT:
            if (wasm_build_code_section_body_subscript(builder, definition, current) == false) {
                return false;
            }
            break;
        case AST_NODE_TYPE_ADDITION_ASSIGNMENT:
        case AST_NODE_TYPE_ARRAY_TYPE:
        case AST_NODE_TYPE_ASSIGNMENT:
        case AST_NODE_TYPE_ATTRIBUTE:
        case AST_NODE_TYPE_BLOCK:
        case AST_NODE_TYPE_CALL_STATEMENT:
        case AST_NODE_TYPE_DIVISION_ASSIGNMENT:
        case AST_NODE_TYPE_BREAK_STATEMENT:
        case AST_NODE_TYPE_CONTINUE_STATEMENT:
        case AST_NODE_TYPE_FOR_STATEMENT:
        case AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION:
        case AST_NODE_TYPE_IF_STATEMENT:
        case AST_NODE_TYPE_LABEL:
        case AST_NODE_TYPE_LABELED_STATEMENT:
        case AST_NODE_TYPE_LOOP_STATEMENT:
        case AST_NODE_TYPE_MODULE:
        case AST_NODE_TYPE_MULTIPLICATION_ASSIGNMENT:
        case AST_NODE_TYPE_PARAMETER:
        case AST_NODE_TYPE_PROCEDURE:
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
 * locals of the function, and a constant string is the result of a function of its own, which is
 * called. The constant is the definition at `binding.position`, and the function at an index is
 * the definition at that index, see `wasm_build_function_section`.
 */
static void wasm_build_code_section_body_identifier(Wasm_Builder *builder,
                                                    Ast_Node_Index procedure,
                                                    Semantic_Binding binding) {
    assert(procedure < builder->node_count);

    Wasm_Value_Type value_type = wasm_binding_value_type(builder, binding);
    assert(value_type != WASM_VALUE_TYPE_NONE);

    switch (binding.kind) {
    case SEMANTIC_BINDING_KIND_CONSTANT:
        wasm_write_byte(&builder->writer, WASM_OPCODE_CALL);
        wasm_write_unsigned(&builder->writer, binding.position);
        break;
    case SEMANTIC_BINDING_KIND_GLOBAL: {
        // Every global of the value is pushed, which is two for a `Str`.
        size_t global_index = wasm_global_slot_index(builder, binding.target);
        size_t slot_count = wasm_value_type_slot_count(value_type);
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
        size_t slot_count = wasm_value_type_slot_count(value_type);
        for (size_t slot = 0; slot < slot_count; slot += 1) {
            wasm_write_byte(&builder->writer, WASM_OPCODE_LOCAL_GET);
            wasm_write_unsigned(&builder->writer, local_index + slot);
        }
        break;
    }
    case SEMANTIC_BINDING_KIND_VARIABLE: {
        size_t local_index = wasm_variable_local_index(builder, procedure, binding.target);
        size_t slot_count = wasm_value_type_slot_count(value_type);
        for (size_t slot = 0; slot < slot_count; slot += 1) {
            wasm_write_byte(&builder->writer, WASM_OPCODE_LOCAL_GET);
            wasm_write_unsigned(&builder->writer, local_index + slot);
        }
        break;
    }
    }
}

/**
 * Returns the type of the value that `binding` stands for, or `WASM_VALUE_TYPE_NONE` when the
 * target cannot use it as a value, which is the case for a procedure. The types of the parameters
 * must be known.
 */
static Wasm_Value_Type wasm_binding_value_type(Wasm_Builder const *builder,
                                               Semantic_Binding binding) {
    assert(binding.target < builder->node_count);

    switch (binding.kind) {
    case SEMANTIC_BINDING_KIND_CONSTANT: {
        Ast_Node_Index value = builder->nodes[binding.target].first_child;
        if (builder->nodes[value].node_type == AST_NODE_TYPE_STRING_LITERAL) {
            return WASM_VALUE_TYPE_STR;
        }
        // A procedure has no value that can be passed around yet.
        return WASM_VALUE_TYPE_NONE;
    }
    case SEMANTIC_BINDING_KIND_GLOBAL:
        return wasm_global_type(builder, binding.target);
    case SEMANTIC_BINDING_KIND_JUMP_TARGET:
    case SEMANTIC_BINDING_KIND_NONE:
        assert(false);
        break;
    case SEMANTIC_BINDING_KIND_PARAMETER: {
        Ast_Node_Index type = builder->nodes[binding.target].first_child;
        return wasm_value_type_of_node(builder, type);
    }
    case SEMANTIC_BINDING_KIND_VARIABLE:
        return wasm_variable_type(builder, binding.target);
    }

    return WASM_VALUE_TYPE_NONE;
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

        slot_count += wasm_value_type_slot_count(
            wasm_value_type_of_node(builder, parameter_node->first_child));
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

        local_index += wasm_value_type_slot_count(
            wasm_value_type_of_node(builder, parameter_node->first_child));
        parameter = parameter_node->next_sibling;
    }

    return local_index;
}

/**
 * Returns whether `node` is a type: the name of one, such as `Int`, or an array type, such as
 * `[3]Int`.
 */
static bool wasm_node_is_type(Wasm_Builder const *builder, Ast_Node_Index node) {
    assert(node < builder->node_count);

    return builder->nodes[node].node_type == AST_NODE_TYPE_TYPE_NAME ||
           builder->nodes[node].node_type == AST_NODE_TYPE_ARRAY_TYPE;
}

/**
 * Returns the type that the type `type` stands for, or `WASM_VALUE_TYPE_NONE` if the target does
 * not have it. The elements of an array and of an array slice can only be `Int` values, because
 * the code that reads and writes them knows only the size of that. An array type is an array
 * slice when it has no length.
 */
static Wasm_Value_Type wasm_value_type_of_node(Wasm_Builder const *builder, Ast_Node_Index type) {
    assert(wasm_node_is_type(builder, type));

    Ast_Node const *type_node = &builder->nodes[type];
    if (type_node->node_type == AST_NODE_TYPE_TYPE_NAME) {
        return wasm_value_type_of_name(type_node->payload.type_name.name);
    }

    Ast_Node_Index element = type_node->first_child;
    assert(element != AST_NODE_INDEX_NONE);
    assert(builder->nodes[element].node_type == AST_NODE_TYPE_TYPE_NAME);
    if (wasm_value_type_of_name(builder->nodes[element].payload.type_name.name) !=
        WASM_VALUE_TYPE_INT) {
        return WASM_VALUE_TYPE_NONE;
    }
    if (type_node->payload.array_type.length_kind == AST_ARRAY_LENGTH_OMITTED) {
        return WASM_VALUE_TYPE_SLICE;
    }
    return WASM_VALUE_TYPE_ARRAY;
}

/**
 * Returns the text in the source that an error about the type `type` points at: the name of the
 * type, or the name of the type of the elements for an array type.
 */
static Str wasm_type_subject(Wasm_Builder const *builder, Ast_Node_Index type) {
    assert(wasm_node_is_type(builder, type));

    if (builder->nodes[type].node_type == AST_NODE_TYPE_ARRAY_TYPE) {
        Ast_Node_Index element = builder->nodes[type].first_child;
        return builder->nodes[element].payload.type_name.name;
    }
    return builder->nodes[type].payload.type_name.name;
}

/**
 * Returns whether a value of the type `value` can be given where `target` is wanted: the same
 * type, or an array where an array slice is, because the slice then views all of its elements.
 */
static bool wasm_type_accepts(Wasm_Value_Type target, Wasm_Value_Type value) {
    return target == value ||
           (target == WASM_VALUE_TYPE_SLICE && value == WASM_VALUE_TYPE_ARRAY);
}

/**
 * Returns whether a value of `value_type` has elements that can be indexed, which an array and an
 * array slice do.
 */
static bool wasm_type_is_sequence(Wasm_Value_Type value_type) {
    return value_type == WASM_VALUE_TYPE_ARRAY || value_type == WASM_VALUE_TYPE_SLICE;
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
 * Writes the load or the store of an `Int`, which is 8 bytes, at the address on the stack plus
 * `offset_bytes`. The alignment is the size, as it always is, because the stack and the elements
 * are aligned.
 */
static void wasm_write_memory_instruction(Wasm_Writer *writer, byte opcode,
                                          uint64_t offset_bytes) {
    assert(opcode == WASM_OPCODE_I64_LOAD || opcode == WASM_OPCODE_I64_STORE);

    wasm_write_byte(writer, opcode);
    wasm_write_unsigned(writer, WASM_ELEMENT_SIZE_SHIFT);
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
    return node->first_child;
}

/**
 * Returns whether the procedure has anything that uses the locals of `Wasm_Scratch`: an array
 * literal, a subscript, a slice, or a for statement over the elements of an array.
 */
static bool wasm_procedure_uses_arrays(Wasm_Builder const *builder, Ast_Node_Index procedure) {
    assert(procedure < builder->node_count);

    Ast_Node_Index end = ast_procedure_end(builder->nodes, builder->node_count, procedure);
    for (Ast_Node_Index node = procedure + 1; node < end; node += 1) {
        Ast_Node_Type node_type = builder->nodes[node].node_type;
        if (node_type == AST_NODE_TYPE_ARRAY_LITERAL || node_type == AST_NODE_TYPE_SLICE ||
            node_type == AST_NODE_TYPE_SUBSCRIPT) {
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
 * Returns whether the procedure has an array literal, which means that it has a frame.
 */
static bool wasm_frame_has_literal(Wasm_Builder const *builder, Ast_Node_Index procedure) {
    assert(procedure < builder->node_count);

    Ast_Node_Index end = ast_procedure_end(builder->nodes, builder->node_count, procedure);
    for (Ast_Node_Index node = procedure + 1; node < end; node += 1) {
        if (builder->nodes[node].node_type == AST_NODE_TYPE_ARRAY_LITERAL) {
            return true;
        }
    }

    return false;
}

/**
 * Returns the size in bytes of the elements of the array literals of `procedure` together, which
 * is the size of its frame. Each literal has room for its elements, whether it is a variable or
 * only a value that is used once, and a literal that is evaluated again, for example in a loop,
 * is put in the same place again.
 */
static size_t wasm_frame_size_bytes(Wasm_Builder const *builder, Ast_Node_Index procedure) {
    assert(procedure < builder->node_count);

    Ast_Node_Index end = ast_procedure_end(builder->nodes, builder->node_count, procedure);
    return wasm_frame_literal_offset(builder, procedure, end);
}

/**
 * Returns the offset from the start of the frame of `procedure` of the elements of the array
 * literal `literal`, which is the size of the array literals before it. `literal` can be the end of
 * the procedure, which is the size of all of them.
 */
static size_t wasm_frame_literal_offset(Wasm_Builder const *builder, Ast_Node_Index procedure,
                                        Ast_Node_Index literal) {
    assert(procedure < builder->node_count);
    assert(literal > procedure);
    assert(literal <= builder->node_count);

    size_t offset_bytes = 0;
    for (Ast_Node_Index node = procedure + 1; node < literal; node += 1) {
        if (builder->nodes[node].node_type == AST_NODE_TYPE_ARRAY_LITERAL) {
            // The first child is the type, which is not an element.
            size_t element_count = wasm_children_count(builder, node) - 1;
            offset_bytes += element_count * WASM_ELEMENT_SIZE_BYTES;
        }
    }

    return offset_bytes;
}

/**
 * Writes the start of a procedure that has array literals: it makes room for the elements of all
 * of them by moving the stack pointer down, and keeps where they are in a local, so that the
 * address of an array literal is that plus its offset. A recursion that goes too deep would move
 * the pointer into the data, so the new pointer is compared with the end of the stack, as a signed
 * number because a frame larger than the pointer gives a negative one, and traps if it is lower.
 * The pointer goes back when the procedure returns, see `wasm_frame_leave`. A procedure without
 * array literals writes nothing.
 */
static void wasm_frame_enter(Wasm_Builder *builder, Ast_Node_Index procedure) {
    assert(procedure < builder->node_count);

    if (wasm_frame_has_literal(builder, procedure) == false) {
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
 * `return`. A procedure without array literals writes nothing.
 */
static void wasm_frame_leave(Wasm_Builder *builder, Ast_Node_Index procedure) {
    assert(procedure < builder->node_count);

    if (wasm_frame_has_literal(builder, procedure) == false) {
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

    if (wasm_expression_type(builder, expression) == WASM_VALUE_TYPE_ARRAY) {
        wasm_write_i32_const(&builder->writer, (int64_t)wasm_array_length(builder, expression));
    }
}

/**
 * Writes the instructions that leave the address of an element, from the array slice and the index
 * that are on the stack: the address of the first element, its length as a 32-bit integer, and
 * the index as a 64-bit one. An index that is not below the length, which is also any negative
 * index, traps. The comparison is unsigned and 64-bit, so that an index that does not fit in 32
 * bits is not cut down to one that does.
 */
static void wasm_element_address_write(Wasm_Builder *builder, Ast_Node_Index procedure) {
    size_t low = wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_LOW);

    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_SET, low);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I64_EXTEND_I32_U);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, low);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I64_LE_U);
    wasm_structured_trap(builder);

    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, low);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_WRAP_I64);
    wasm_write_i32_const(&builder->writer, WASM_ELEMENT_SIZE_SHIFT);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_SHL);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_ADD);
}

/**
 * Writes the instructions that make a new array slice from the one and the two indexes that are
 * on the stack: the address of the first element, its length, the first index and the end index,
 * which is not included. The new one starts at the first index and ends before the end index.
 * It traps unless the first index is not above the end index, and the end index is not above the
 * length. Both comparisons are unsigned, which also makes a negative index too large.
 */
static void wasm_slice_write(Wasm_Builder *builder, Ast_Node_Index procedure) {
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
    wasm_write_i32_const(&builder->writer, WASM_ELEMENT_SIZE_SHIFT);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_SHL);
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
 * of elements must be the length of the type when it gives one, and the elements of the type
 * must be `Int` values. Returns false after it recorded an error.
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

    if (wasm_value_type_of_node(builder, type) == WASM_VALUE_TYPE_NONE) {
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
 * array literal that is storing the element at its place, whose address is on the stack below it.
 * For the arguments of a call and the operands of a subscript or a slice it is making an array
 * into an array slice, see `wasm_array_decay`, because those take array slices. Returns false
 * after it recorded an error.
 */
static bool wasm_build_code_section_body_child(Wasm_Builder *builder, Ast_Node_Index definition,
                                               Ast_Node_Index parent, Ast_Node_Index child) {
    assert(parent < builder->node_count);
    assert(child < builder->node_count);

    Ast_Node_Type parent_type = builder->nodes[parent].node_type;
    if (parent_type == AST_NODE_TYPE_ARRAY_LITERAL) {
        if (wasm_expression_type(builder, child) != WASM_VALUE_TYPE_INT) {
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
                              (position - 1) * WASM_ELEMENT_SIZE_BYTES;
        wasm_write_memory_instruction(&builder->writer, WASM_OPCODE_I64_STORE, offset_bytes);
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
 * were written already: the address of the element, and the load of it. Returns false after it
 * recorded an error.
 */
static bool wasm_build_code_section_body_subscript(Wasm_Builder *builder,
                                                   Ast_Node_Index definition,
                                                   Ast_Node_Index subscript) {
    assert(subscript < builder->node_count);
    assert(builder->nodes[subscript].node_type == AST_NODE_TYPE_SUBSCRIPT);

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index base = builder->nodes[subscript].first_child;
    Ast_Node_Index index = builder->nodes[subscript].last_child;

    if (wasm_type_is_sequence(wasm_expression_type(builder, base)) == false) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_EXPECTED,
                         wasm_expression_subject(builder, definition, base));
    }
    if (wasm_expression_type(builder, index) != WASM_VALUE_TYPE_INT) {
        return wasm_fail(builder, WASM_BUILD_STATUS_INDEX_TYPE_INVALID,
                         wasm_expression_subject(builder, definition, index));
    }

    wasm_element_address_write(builder, procedure);
    wasm_write_memory_instruction(&builder->writer, WASM_OPCODE_I64_LOAD, 0);
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

    if (wasm_type_is_sequence(wasm_expression_type(builder, base)) == false) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_EXPECTED,
                         wasm_expression_subject(builder, definition, base));
    }
    Ast_Node_Index indexes[2] = {first, end};
    for (size_t i = 0; i < 2; i += 1) {
        if (wasm_expression_type(builder, indexes[i]) != WASM_VALUE_TYPE_INT) {
            return wasm_fail(builder, WASM_BUILD_STATUS_INDEX_TYPE_INVALID,
                             wasm_expression_subject(builder, definition, indexes[i]));
        }
    }

    wasm_slice_write(builder, procedure);
    return true;
}

/**
 * Writes the instructions of an assignment whose target is an element, such as `a[i] = 1` or
 * `a[i] += 1`, or a range of elements, such as `a[1:3] = b`. An element is stored at its address,
 * and for an operator such as `+=` it is first loaded from there, so the address is kept on the
 * stack below the loaded value and the result. A range of elements is copied from the array or the
 * array slice on the right, which must have as many elements, or the program traps. The copy is
 * a `memory.copy`, which is right when the two overlap. Returns false after it recorded an error.
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
        if (wasm_type_is_sequence(wasm_expression_type(builder, value)) == false) {
            return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_EXPECTED,
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
        wasm_write_i32_const(&builder->writer, WASM_ELEMENT_SIZE_SHIFT);
        wasm_write_byte(&builder->writer, WASM_OPCODE_I32_SHL);
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
    if (wasm_type_is_sequence(wasm_expression_type(builder, base)) == false) {
        return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_EXPECTED,
                         wasm_expression_subject(builder, definition, base));
    }
    wasm_array_decay(builder, base);

    if (wasm_build_code_section_body_expression(builder, definition, index, false) == false) {
        return false;
    }
    if (wasm_expression_type(builder, index) != WASM_VALUE_TYPE_INT) {
        return wasm_fail(builder, WASM_BUILD_STATUS_INDEX_TYPE_INVALID,
                         wasm_expression_subject(builder, definition, index));
    }
    wasm_element_address_write(builder, procedure);

    if (is_plain == false) {
        // The address is used twice, by the load and by the store. It is kept in a local only
        // between the two instructions that follow, and not while the value is evaluated.
        size_t address = wasm_scratch_local_index(builder, procedure, WASM_SCRATCH_FIRST);
        wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_TEE, address);
        wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, address);
        wasm_write_memory_instruction(&builder->writer, WASM_OPCODE_I64_LOAD, 0);
    }

    if (wasm_build_code_section_body_expression(builder, definition, value, false) == false) {
        return false;
    }
    if (wasm_expression_type(builder, value) != WASM_VALUE_TYPE_INT) {
        Str subject = wasm_expression_subject(builder, definition, value);
        if (is_plain) {
            return wasm_fail(builder, WASM_BUILD_STATUS_ARRAY_ELEMENT_TYPE_MISMATCH, subject);
        }
        return wasm_fail(builder, WASM_BUILD_STATUS_ARITHMETIC_OPERAND_TYPE_INVALID, subject);
    }

    if (is_plain == false) {
        wasm_write_byte(&builder->writer,
                        wasm_assignment_opcode(builder->nodes[assignment].node_type));
    }
    wasm_write_memory_instruction(&builder->writer, WASM_OPCODE_I64_STORE, 0);
    return true;
}

/**
 * Writes the start of a for statement that goes through the elements of an array or an array
 * slice: the elements are written, and the address of the first element and the address after the
 * last one are stored in the locals after the iterator. Then come a `block` that a finished loop
 * leaves and a `loop` that is repeated, as for a range, and the test that leaves the block when the
 * address of the next element is the end. The iterator is loaded from that address, and the
 * address moves on to the next element before the statements of the block, which are in a `block`
 * of their own that `continue` leaves. The loop is closed by
 * `wasm_build_code_section_body_for_end`, which has nothing to step. Returns false after it
 * recorded an error.
 */
static bool wasm_build_code_section_body_for_each(Wasm_Builder *builder, Ast_Node_Index definition,
                                                  Ast_Node_Index statement) {
    assert(statement < builder->node_count);
    assert(wasm_for_is_each(builder, statement));

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index elements = builder->nodes[statement].first_child;
    size_t iterator = wasm_variable_local_index(builder, procedure, statement);
    size_t cursor = iterator + 1;
    size_t end = iterator + 2;

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
    wasm_write_i32_const(&builder->writer, WASM_ELEMENT_SIZE_SHIFT);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_SHL);
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

    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, cursor);
    wasm_write_memory_instruction(&builder->writer, WASM_OPCODE_I64_LOAD, 0);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_SET, iterator);

    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_GET, cursor);
    wasm_write_i32_const(&builder->writer, WASM_ELEMENT_SIZE_BYTES);
    wasm_write_byte(&builder->writer, WASM_OPCODE_I32_ADD);
    wasm_write_local_instruction(&builder->writer, WASM_OPCODE_LOCAL_SET, cursor);

    wasm_structured_begin(builder, WASM_OPCODE_BLOCK);
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
