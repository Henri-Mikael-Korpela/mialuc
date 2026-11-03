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
// name that is not a type that the target has.
typedef enum {
    WASM_VALUE_TYPE_INT,
    WASM_VALUE_TYPE_NONE,
    WASM_VALUE_TYPE_STR,
} Wasm_Value_Type;

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
 * is the last literal that got one, which is checked to keep the two orders the same.
 */
typedef struct {
    Wasm_Writer writer;
    Ast_Node const *nodes;
    Semantic_Binding const *bindings;
    Str subject;
    size_t node_count;
    size_t data_size_bytes;
    size_t data_cursor_bytes;
    Ast_Node_Index data_last_literal;
    Wasm_Build_Status status;
    // Whether the module has a memory, which it has when there is a string literal to put in it.
    bool has_memory;
} Wasm_Builder;

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
static bool            wasm_build_code_section_body_expression(Wasm_Builder *builder,
                                                               Ast_Node_Index definition,
                                                               Ast_Node_Index expression);
static void            wasm_build_code_section_body_identifier(Wasm_Builder *builder,
                                                               Ast_Node_Index procedure,
                                                               Semantic_Binding binding);
static bool            wasm_build_code_section_body_procedure (Wasm_Builder *builder,
                                                               Ast_Node_Index definition);
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
static bool            wasm_check_signatures                  (Wasm_Builder *builder,
                                                               Ast_Node_Index module,
                                                               size_t definition_count);
static size_t          wasm_children_count                    (Wasm_Builder const *builder,
                                                               Ast_Node_Index parent);
static bool            wasm_data_measure                      (Wasm_Builder *builder);
static Wasm_Value_Type wasm_expression_type                   (Wasm_Builder const *builder,
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
static void            wasm_write_byte                        (Wasm_Writer *writer, byte content);
static void            wasm_write_bytes                       (Wasm_Writer *writer,
                                                               byte const *bytes, size_t count);
static void            wasm_write_signed                      (Wasm_Writer *writer, int64_t number);
static void            wasm_write_text                        (Wasm_Writer *writer, Str text);
static void            wasm_write_unsigned                    (Wasm_Writer *writer,
                                                               uint64_t number);
static void            wasm_write_value_type                  (Wasm_Writer *writer,
                                                               Wasm_Value_Type value_type);

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
        .data_size_bytes = 0,
        .data_cursor_bytes = 0,
        .data_last_literal = AST_NODE_INDEX_NONE,
        .status = WASM_BUILD_STATUS_OK,
        .has_memory = false,
    };

    size_t definition_count = wasm_children_count(&builder, root);
    wasm_write_bytes(&builder.writer, WASM_MODULE_HEADER, sizeof(WASM_MODULE_HEADER));

    // The types are checked first, so that the sections below can rely on every type being known.
    // The size of the data is needed before the code is written, because the memory has to be
    // big enough for it and comes before the code.
    bool is_ok = wasm_data_measure(&builder);
    if (is_ok) {
        is_ok = wasm_check_signatures(&builder, root, definition_count);
    }

    // The sections must come in this order, and each function refers to its type and to its body
    // by the index of its definition, so one count describes the type, function and code sections.
    if (is_ok) {
        wasm_build_type_section(&builder, root, definition_count);
        wasm_build_function_section(&builder, definition_count);
        if (builder.has_memory) {
            wasm_build_memory_section(&builder);
        }
        is_ok = wasm_build_export_section(&builder, root, definition_count);
    }
    if (is_ok) {
        is_ok = wasm_build_code_section(&builder, root, definition_count);
    }
    if (is_ok && builder.has_memory) {
        // The code gave every literal its address, and the data must be what those addresses
        // were counted from.
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
 * Measures the bytes of all the string literals, and decides from them whether the module has a
 * memory. Returns false after it recorded an error when they do not fit in the memory.
 */
static bool wasm_data_measure(Wasm_Builder *builder) {
    assert(builder->data_size_bytes == 0);
    assert(builder->has_memory == false);

    size_t size_bytes = 0;
    bool has_literal = false;
    for (size_t i = 0; i < builder->node_count; i += 1) {
        if (builder->nodes[i].node_type == AST_NODE_TYPE_STRING_LITERAL) {
            has_literal = true;
            // Each literal is shorter than 4 GiB and there are fewer than 4 Gi of them, so the
            // sum fits in 64 bits.
            size_bytes += builder->nodes[i].payload.string_literal.text.length;
        }
    }

    // Addresses and lengths are written as `i32.const`, whose immediate is signed, so they stay
    // below 2 GiB. A module that big would not fit in the output anyway, so the same status says
    // it.
    if (size_bytes > INT32_MAX) {
        return wasm_fail(builder, WASM_BUILD_STATUS_OUTPUT_TOO_LARGE, STR_EMPTY);
    }

    builder->data_size_bytes = size_bytes;
    builder->has_memory = has_literal;
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

    Ast_Node_Index definition = builder->nodes[module].first_child;
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

                Str type_name = builder->nodes[parameter_node->first_child].payload.type_name.name;
                if (wasm_value_type_of_name(type_name) == WASM_VALUE_TYPE_NONE) {
                    return wasm_fail(builder, WASM_BUILD_STATUS_TYPE_UNSUPPORTED, type_name);
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

        definition = builder->nodes[definition].next_sibling;
    }

    return true;
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

    Ast_Node_Index definition = builder->nodes[module].first_child;
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

                Str type_name = builder->nodes[parameter_node->first_child].payload.type_name.name;
                wasm_write_value_type(&builder->writer, wasm_value_type_of_name(type_name));
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

        definition = builder->nodes[definition].next_sibling;
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
 * 64-bit value, fits. `Str` is the address and the length of its bytes, both 32-bit, because
 * addresses are.
 */
static size_t wasm_value_type_slot_count(Wasm_Value_Type value_type) {
    assert(value_type != WASM_VALUE_TYPE_NONE);

    if (value_type == WASM_VALUE_TYPE_STR) {
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

    if (value_type == WASM_VALUE_TYPE_STR) {
        wasm_write_byte(writer, WASM_TYPE_I32);
        wasm_write_byte(writer, WASM_TYPE_I32);
    } else {
        wasm_write_byte(writer, WASM_TYPE_I64);
    }
}

/**
 * Writes the memory section, which declares the one memory of the module. It is just big enough
 * for the data, and at least one page, so that the memory exists with nothing in it as well.
 */
static void wasm_build_memory_section(Wasm_Builder *builder) {
    assert(builder->has_memory);
    assert(builder->data_size_bytes <= INT32_MAX);

    size_t page_count = (builder->data_size_bytes + WASM_MEMORY_PAGE_SIZE_BYTES - 1) /
                        WASM_MEMORY_PAGE_SIZE_BYTES;
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

    Ast_Node_Index definition = builder->nodes[module].first_child;
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

        definition = builder->nodes[definition].next_sibling;
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

    Ast_Node_Index definition = builder->nodes[module].first_child;
    for (size_t i = 0; i < definition_count; i += 1) {
        assert(definition != AST_NODE_INDEX_NONE);

        if (wasm_build_code_section_body(builder, definition) == false) {
            return false;
        }
        definition = builder->nodes[definition].next_sibling;
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

    // The parameters are the locals of the function already, so there are no other locals.
    wasm_write_unsigned(&builder->writer, 0);

    Ast_Node_Index value = builder->nodes[definition].first_child;
    if (builder->nodes[value].node_type == AST_NODE_TYPE_STRING_LITERAL) {
        // The string is the result, and is left on the stack for the `end` to return.
        wasm_build_code_section_body_string(builder, value);
    } else {
        if (wasm_build_code_section_body_procedure(builder, definition) == false) {
            return false;
        }
    }

    // After a `return` the code is unreachable, and WebAssembly accepts anything there, so the
    // body ends with the `end` instruction even though no value is left on the stack.
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
 * Writes the statements of a procedure. Each statement is a `return`, which becomes the value
 * followed by the `return` instruction.
 */
static bool wasm_build_code_section_body_procedure(Wasm_Builder *builder,
                                                   Ast_Node_Index definition) {
    assert(definition < builder->node_count);

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    Ast_Node_Index child = builder->nodes[procedure].first_child;
    for (size_t i = 0; i < builder->node_count && child != AST_NODE_INDEX_NONE; i += 1) {
        Ast_Node const *child_node = &builder->nodes[child];

        if (child_node->node_type == AST_NODE_TYPE_RETURN_STATEMENT) {
            if (builder->nodes[procedure].payload.procedure.return_type.bytes == NULL) {
                Str name = builder->nodes[definition].payload.constant_definition.name;
                return wasm_fail(builder, WASM_BUILD_STATUS_RETURN_TYPE_MISSING, name);
            }

            Ast_Node_Index value = child_node->first_child;
            if (wasm_build_code_section_body_expression(builder, definition, value) == false) {
                return false;
            }

            // The value on the stack must be what the procedure says that it returns. The
            // expression was written already, so every name in it is known.
            Str return_type_name = builder->nodes[procedure].payload.procedure.return_type;
            Wasm_Value_Type return_type = wasm_value_type_of_name(return_type_name);
            if (wasm_expression_type(builder, value) != return_type) {
                Str name = builder->nodes[definition].payload.constant_definition.name;
                return wasm_fail(builder, WASM_BUILD_STATUS_RETURN_TYPE_MISMATCH, name);
            }

            wasm_write_byte(&builder->writer, WASM_OPCODE_RETURN);
        } else {
            // Parameters come first and are not code. The parser makes nothing else yet.
            assert(child_node->node_type == AST_NODE_TYPE_PARAMETER);
        }

        child = child_node->next_sibling;
    }

    return true;
}

/**
 * Returns the type of the value of `expression`. The expression must have been written already,
 * so that every name in it is known to have a value. Only the root matters: an addition is an
 * `Int` because its operands were checked to be, as they were written.
 */
static Wasm_Value_Type wasm_expression_type(Wasm_Builder const *builder,
                                            Ast_Node_Index expression) {
    assert(expression < builder->node_count);

    Ast_Node const *node = &builder->nodes[expression];
    switch (node->node_type) {
    case AST_NODE_TYPE_ADDITION:
    case AST_NODE_TYPE_INTEGER_LITERAL:
        return WASM_VALUE_TYPE_INT;
    case AST_NODE_TYPE_IDENTIFIER: {
        Semantic_Binding binding = builder->bindings[expression];
        Wasm_Value_Type value_type = wasm_binding_value_type(builder, binding);
        assert(value_type != WASM_VALUE_TYPE_NONE);
        return value_type;
    }
    case AST_NODE_TYPE_STRING_LITERAL:
        return WASM_VALUE_TYPE_STR;
    case AST_NODE_TYPE_CONSTANT_DEFINITION:
    case AST_NODE_TYPE_MODULE:
    case AST_NODE_TYPE_PARAMETER:
    case AST_NODE_TYPE_PROCEDURE:
    case AST_NODE_TYPE_RETURN_STATEMENT:
    case AST_NODE_TYPE_TYPE_NAME:
        // None of these is part of an expression.
        assert(false);
        break;
    }

    return WASM_VALUE_TYPE_NONE;
}

/**
 * Writes the instructions of the expression below `expression`, which leave its value on the
 * stack. WebAssembly is a stack machine, so an operator comes after its operands: the nodes are
 * written depth first, each node after all of its children. The walk keeps the path from the
 * root of the expression in an array instead of calling itself.
 */
static bool wasm_build_code_section_body_expression(Wasm_Builder *builder,
                                                    Ast_Node_Index definition,
                                                    Ast_Node_Index expression) {
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
            if (node->first_child != AST_NODE_INDEX_NONE) {
                if (path_count == WASM_EXPRESSION_DEPTH_MAX) {
                    // The tree has no source positions, so the definition is the closest place.
                    Str name = builder->nodes[definition].payload.constant_definition.name;
                    return wasm_fail(builder, WASM_BUILD_STATUS_EXPRESSION_TOO_DEEP, name);
                }
                path[path_count] = current;
                path_count += 1;
                current = node->first_child;
                continue;
            }
        }

        switch (node->node_type) {
        case AST_NODE_TYPE_ADDITION:
            wasm_write_byte(&builder->writer, WASM_OPCODE_I64_ADD);
            break;
        case AST_NODE_TYPE_CONSTANT_DEFINITION:
            // The parser only makes additions, integer literals and names in an expression.
            assert(false);
            break;
        case AST_NODE_TYPE_IDENTIFIER: {
            Str name = node->payload.identifier.name;
            Semantic_Binding binding = builder->bindings[current];
            Wasm_Value_Type value_type = wasm_binding_value_type(builder, binding);
            if (value_type == WASM_VALUE_TYPE_NONE) {
                return wasm_fail(builder, WASM_BUILD_STATUS_IDENTIFIER_NOT_VALUE, name);
            }
            // The only operator is the addition of integers, and an operand has a parent.
            if (path_count > 0 && value_type != WASM_VALUE_TYPE_INT) {
                return wasm_fail(builder, WASM_BUILD_STATUS_ADDITION_OPERAND_TYPE_INVALID, name);
            }

            wasm_build_code_section_body_identifier(builder, procedure, binding);
            break;
        }
        case AST_NODE_TYPE_INTEGER_LITERAL:
            wasm_write_byte(&builder->writer, WASM_OPCODE_I64_CONST);
            wasm_write_signed(&builder->writer, node->payload.integer_literal.value);
            break;
        case AST_NODE_TYPE_STRING_LITERAL:
            if (path_count > 0) {
                return wasm_fail(builder, WASM_BUILD_STATUS_ADDITION_OPERAND_TYPE_INVALID,
                                 node->payload.string_literal.text);
            }
            wasm_build_code_section_body_string(builder, current);
            break;
        case AST_NODE_TYPE_MODULE:
        case AST_NODE_TYPE_PARAMETER:
        case AST_NODE_TYPE_PROCEDURE:
        case AST_NODE_TYPE_RETURN_STATEMENT:
        case AST_NODE_TYPE_TYPE_NAME:
            // Same as above: none of these is part of an expression.
            assert(false);
            break;
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
    case SEMANTIC_BINDING_KIND_NONE:
        // A name that is used was bound by the analysis.
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
    case SEMANTIC_BINDING_KIND_NONE:
        assert(false);
        break;
    case SEMANTIC_BINDING_KIND_PARAMETER: {
        Ast_Node_Index type = builder->nodes[binding.target].first_child;
        return wasm_value_type_of_name(builder->nodes[type].payload.type_name.name);
    }
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

        Str type_name = builder->nodes[parameter_node->first_child].payload.type_name.name;
        slot_count += wasm_value_type_slot_count(wasm_value_type_of_name(type_name));
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

        Str type_name = builder->nodes[parameter_node->first_child].payload.type_name.name;
        local_index += wasm_value_type_slot_count(wasm_value_type_of_name(type_name));
        parameter = parameter_node->next_sibling;
    }

    return local_index;
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
