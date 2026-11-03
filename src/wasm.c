#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <mialu/common_types.h>
#include <mialu/wasm.h>

// The constants of the binary format are in wasm.h, because wasm-text.c reads what this writes.

enum {
    // The deepest expression that can be written. The writer keeps the path from the root of the
    // expression in a fixed array instead of recursing. Left associative operators nest to the
    // left, so `a + b + c + ...` is as deep as it has operators. A longer chain is rejected.
    WASM_EXPRESSION_DEPTH_MAX = 1024,
};

// Means "no such parameter". It is not zero, because zero is the index of the first parameter.
static uint32_t const WASM_PARAMETER_INDEX_NONE = UINT32_MAX;

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

/**
 * The state shared by the building functions. A building function returns false after it recorded
 * an error here, and its caller then returns false too without recording anything more. So only
 * the first error is kept.
 */
typedef struct {
    Wasm_Writer writer;
    Ast_Node const *nodes;
    Str subject;
    size_t node_count;
    Wasm_Build_Status status;
} Wasm_Builder;

static size_t         wasm_block_begin                       (Wasm_Writer *writer);
static void           wasm_block_end                         (Wasm_Writer *writer,
                                                              size_t block_start);
static bool           wasm_build_code_section                (Wasm_Builder *builder,
                                                              Ast_Node_Index module,
                                                              size_t definition_count);
static bool           wasm_build_code_section_body           (Wasm_Builder *builder,
                                                              Ast_Node_Index definition);
static bool           wasm_build_code_section_body_expression(Wasm_Builder *builder,
                                                              Ast_Node_Index definition,
                                                              Ast_Node_Index expression);
static bool           wasm_build_export_section              (Wasm_Builder *builder,
                                                              Ast_Node_Index module,
                                                              size_t definition_count);
static void           wasm_build_function_section            (Wasm_Builder *builder,
                                                              size_t definition_count);
static bool           wasm_build_type_section                (Wasm_Builder *builder,
                                                              Ast_Node_Index module,
                                                              size_t definition_count);
static bool           wasm_build_type_section_value_type     (Wasm_Builder *builder, Str type_name);
static size_t         wasm_children_count                    (Wasm_Builder const *builder,
                                                              Ast_Node_Index parent);
static bool           wasm_fail                              (Wasm_Builder *builder,
                                                              Wasm_Build_Status status,
                                                              Str subject);
static size_t         wasm_leb128_signed_encode              (int64_t number, byte *encoded);
static size_t         wasm_leb128_unsigned_encode            (uint64_t number, byte *encoded);
static size_t         wasm_parameter_count                   (Wasm_Builder const *builder,
                                                              Ast_Node_Index procedure);
static uint32_t       wasm_parameter_find                    (Wasm_Builder const *builder,
                                                              Ast_Node_Index procedure, Str name);
static Ast_Node_Index wasm_procedure_of                      (Wasm_Builder const *builder,
                                                              Ast_Node_Index definition);
static size_t         wasm_section_begin                     (Wasm_Writer *writer, byte section_id);
static void           wasm_write_byte                        (Wasm_Writer *writer, byte content);
static void           wasm_write_bytes                       (Wasm_Writer *writer,
                                                              byte const *bytes, size_t count);
static void           wasm_write_signed                      (Wasm_Writer *writer, int64_t number);
static void           wasm_write_text                        (Wasm_Writer *writer, Str text);
static void           wasm_write_unsigned                    (Wasm_Writer *writer, uint64_t number);

Wasm_Build_Result wasm_build(Array_Slice nodes, Ast_Node_Index root, Array_Slice output_buffer) {
    assert(nodes.elements != NULL);
    assert(output_buffer.elements != NULL);
    assert(root < nodes.length);

    Ast_Node const *node_elements = nodes.elements;
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
        .subject = STR_EMPTY,
        .node_count = nodes.length,
        .status = WASM_BUILD_STATUS_OK,
    };

    size_t definition_count = wasm_children_count(&builder, root);
    wasm_write_bytes(&builder.writer, WASM_MODULE_HEADER, sizeof(WASM_MODULE_HEADER));

    // The sections must come in this order, and each function refers to its type and to its body
    // by the index of its definition, so one count describes all four.
    bool is_ok = wasm_build_type_section(&builder, root, definition_count);
    if (is_ok) {
        wasm_build_function_section(&builder, definition_count);
        is_ok = wasm_build_export_section(&builder, root, definition_count);
    }
    if (is_ok) {
        is_ok = wasm_build_code_section(&builder, root, definition_count);
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
 * Writes the type section, which has one function type per definition: the parameters and the
 * result of its procedure. Types are not shared, because that would save only a few bytes.
 */
static bool wasm_build_type_section(Wasm_Builder *builder, Ast_Node_Index module,
                                    size_t definition_count) {
    assert(module < builder->node_count);
    assert(builder->nodes[module].node_type == AST_NODE_TYPE_MODULE);

    size_t section = wasm_section_begin(&builder->writer, WASM_SECTION_ID_TYPE);
    wasm_write_unsigned(&builder->writer, definition_count);

    Ast_Node_Index definition = builder->nodes[module].first_child;
    for (size_t i = 0; i < definition_count; i += 1) {
        assert(definition != AST_NODE_INDEX_NONE);

        Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
        size_t parameter_count = wasm_parameter_count(builder, procedure);
        wasm_write_byte(&builder->writer, WASM_TYPE_FUNCTION);
        wasm_write_unsigned(&builder->writer, parameter_count);

        Ast_Node_Index parameter = builder->nodes[procedure].first_child;
        for (size_t j = 0; j < parameter_count; j += 1) {
            assert(parameter != AST_NODE_INDEX_NONE);
            Ast_Node const *parameter_node = &builder->nodes[parameter];
            assert(parameter_node->node_type == AST_NODE_TYPE_PARAMETER);

            Str name = parameter_node->payload.parameter.name;
            Str type_name = builder->nodes[parameter_node->first_child].payload.type_name.name;
            if (wasm_build_type_section_value_type(builder, type_name) == false) {
                return false;
            }

            // Two parameters with one name would make the name refer to two values.
            Ast_Node_Index earlier = builder->nodes[procedure].first_child;
            for (size_t k = 0; k < j; k += 1) {
                if (str_equals(builder->nodes[earlier].payload.parameter.name, name)) {
                    return wasm_fail(builder, WASM_BUILD_STATUS_PARAMETER_NAME_DUPLICATE, name);
                }
                earlier = builder->nodes[earlier].next_sibling;
            }

            parameter = parameter_node->next_sibling;
        }

        Str return_type = builder->nodes[procedure].payload.procedure.return_type;
        if (return_type.bytes == NULL) {
            wasm_write_unsigned(&builder->writer, 0);
        } else {
            wasm_write_unsigned(&builder->writer, 1);
            if (wasm_build_type_section_value_type(builder, return_type) == false) {
                return false;
            }
        }

        definition = builder->nodes[definition].next_sibling;
    }

    wasm_block_end(&builder->writer, section);
    return true;
}

/**
 * Writes the WebAssembly type that `type_name` stands for, and returns false after it recorded an
 * error if the name is not a type that the target has. `Int` is a 64-bit integer, so that every
 * integer literal, which the tokenizer accepts up to the largest signed 64-bit value, fits.
 */
static bool wasm_build_type_section_value_type(Wasm_Builder *builder, Str type_name) {
    assert(type_name.bytes != NULL);
    assert(type_name.length > 0);

    if (str_equals(type_name, STR("Int")) == false) {
        return wasm_fail(builder, WASM_BUILD_STATUS_TYPE_UNSUPPORTED, type_name);
    }

    wasm_write_byte(&builder->writer, WASM_TYPE_I64);
    return true;
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
 * the name of its definition.
 */
static bool wasm_build_export_section(Wasm_Builder *builder, Ast_Node_Index module,
                                      size_t definition_count) {
    assert(module < builder->node_count);
    assert(definition_count <= builder->node_count);

    size_t section = wasm_section_begin(&builder->writer, WASM_SECTION_ID_EXPORT);
    wasm_write_unsigned(&builder->writer, definition_count);

    Ast_Node_Index definition = builder->nodes[module].first_child;
    for (size_t i = 0; i < definition_count; i += 1) {
        assert(definition != AST_NODE_INDEX_NONE);
        Str name = builder->nodes[definition].payload.constant_definition.name;

        // Export names must be unique, and so must the names of constants.
        Ast_Node_Index earlier = builder->nodes[module].first_child;
        for (size_t j = 0; j < i; j += 1) {
            if (str_equals(builder->nodes[earlier].payload.constant_definition.name, name)) {
                return wasm_fail(builder, WASM_BUILD_STATUS_DEFINITION_NAME_DUPLICATE, name);
            }
            earlier = builder->nodes[earlier].next_sibling;
        }

        wasm_write_text(&builder->writer, name);
        wasm_write_byte(&builder->writer, WASM_EXPORT_KIND_FUNCTION);
        wasm_write_unsigned(&builder->writer, i);

        definition = builder->nodes[definition].next_sibling;
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
 * Writes one entry of the code section: the size of the body, then the body. Each statement is
 * a `return`, which becomes the value followed by the `return` instruction.
 */
static bool wasm_build_code_section_body(Wasm_Builder *builder, Ast_Node_Index definition) {
    assert(definition < builder->node_count);
    assert(builder->nodes[definition].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION);

    Ast_Node_Index procedure = wasm_procedure_of(builder, definition);
    size_t body = wasm_block_begin(&builder->writer);

    // The parameters are the locals of the function already, so there are no other locals.
    wasm_write_unsigned(&builder->writer, 0);

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
            wasm_write_byte(&builder->writer, WASM_OPCODE_RETURN);
        } else {
            // Parameters come first and are not code. The parser makes nothing else yet.
            assert(child_node->node_type == AST_NODE_TYPE_PARAMETER);
        }

        child = child_node->next_sibling;
    }

    // After a `return` the code is unreachable, and WebAssembly accepts anything there, so the
    // body ends with the `end` instruction even though no value is left on the stack.
    wasm_write_byte(&builder->writer, WASM_OPCODE_END);
    wasm_block_end(&builder->writer, body);
    return true;
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
            uint32_t index = wasm_parameter_find(builder, procedure, name);
            if (index == WASM_PARAMETER_INDEX_NONE) {
                return wasm_fail(builder, WASM_BUILD_STATUS_IDENTIFIER_UNKNOWN, name);
            }
            wasm_write_byte(&builder->writer, WASM_OPCODE_LOCAL_GET);
            wasm_write_unsigned(&builder->writer, index);
            break;
        }
        case AST_NODE_TYPE_INTEGER_LITERAL:
            wasm_write_byte(&builder->writer, WASM_OPCODE_I64_CONST);
            wasm_write_signed(&builder->writer, node->payload.integer_literal.value);
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
 * Returns the procedure that `definition` binds to its name. The parser only allows procedures as
 * the value of a definition for now.
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
 * Returns the index of the parameter of `procedure` called `name`, which is also the index of
 * the local that holds it, or `WASM_PARAMETER_INDEX_NONE` when there is no such parameter.
 */
static uint32_t wasm_parameter_find(Wasm_Builder const *builder, Ast_Node_Index procedure,
                                    Str name) {
    assert(name.bytes != NULL);

    size_t parameter_count = wasm_parameter_count(builder, procedure);
    assert(parameter_count < WASM_PARAMETER_INDEX_NONE);

    Ast_Node_Index parameter = builder->nodes[procedure].first_child;
    for (size_t i = 0; i < parameter_count; i += 1) {
        assert(parameter != AST_NODE_INDEX_NONE);
        if (str_equals(builder->nodes[parameter].payload.parameter.name, name)) {
            return (uint32_t)i;
        }
        parameter = builder->nodes[parameter].next_sibling;
    }

    return WASM_PARAMETER_INDEX_NONE;
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
