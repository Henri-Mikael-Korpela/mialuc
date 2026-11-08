#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <mialu/semantic.h>
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
    // The headers take 0x198 bytes, see `x64_headers_write`, so one unit of file alignment holds
    // them.
    X64_HEADERS_SIZE_BYTES = 0x200,
    // The address in memory, relative to the image, of the first section.
    X64_RDATA_RVA = 0x1000,

    X64_DOS_HEADER_SIZE_BYTES = 0x40,
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
    // What `sub rsp` takes for the number of bytes in use that a procedure with temporary strings
    // keeps when it starts, to go back to when it ends: the stack must stay a multiple of 16.
    X64_TEMPORARY_FRAME_SIZE_BYTES = 16,
};

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
 * What the build knows about a procedure of the module, kept at the position of the procedure
 * among the constant definitions, which is what the analysis binds a call to.
 */
typedef struct {
    Ast_Node_Index definition;
    // Where the code of the procedure starts, counted from the start of the code. Not set for a
    // foreign procedure, which has no code.
    size_t offset;
    // The place of the import of a foreign procedure in the table of imports, see
    // `x64_layout_make`. Not set for any other procedure.
    uint32_t import_slot;
    bool is_foreign;
} X64_Procedure;

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
 * The state shared by the building functions. A building function returns false after it recorded
 * an error here, and its caller then returns false too without recording anything more. So only
 * the first error is kept. The code is written twice, see `x64_build`, so everything here that the
 * code changes is reset between the two.
 */
typedef struct {
    X64_Writer writer;
    Ast_Node const *nodes;
    Semantic_Binding const *bindings;
    Str subject;
    X64_Layout layout;
    size_t node_count;
    // The place in the file of the start of the code, and of the data that precedes it.
    size_t text_start;
    size_t procedure_count;
    uint32_t text_rva;
    // Where the section of the temporary storage is in memory, see
    // `X64_TEMPORARY_STORAGE_SIZE_BYTES`.
    uint32_t data_rva;
    Ast_Node_Index module;
    // The definition of the procedure that is being written, which a message about it names.
    Ast_Node_Index definition;
    X64_Build_Status status;
    X64_Procedure procedures[X64_PROCEDURE_COUNT_MAX];
} X64_Builder;

static bool         x64_arguments_write          (X64_Builder *builder, Ast_Node_Index call,
                                                  Ast_Node_Index callee);
static bool         x64_call_is_str_to_c_str     (X64_Builder const *builder,
                                                  Ast_Node_Index expression);
static bool         x64_call_write               (X64_Builder *builder, Ast_Node_Index call,
                                                  bool is_result_used);
static void         x64_epilogue_write           (X64_Builder *builder, bool is_temporary_used);
static bool         x64_procedure_uses_temporary (X64_Builder const *builder,
                                                  Ast_Node_Index procedure);
static bool         x64_str_to_c_str_write       (X64_Builder *builder, Ast_Node_Index call,
                                                  uint8_t register_number);
static void         x64_write_move_from_r10      (X64_Builder *builder, uint8_t register_number);
static void         x64_write_rip_displacement   (X64_Builder *builder, uint64_t target_rva);
static bool         x64_code_write               (X64_Builder *builder);
static void         x64_dos_header_write         (byte *header);
static bool         x64_expression_load          (X64_Builder *builder, Ast_Node_Index expression,
                                                  uint8_t register_number);
static X64_Type     x64_expression_type          (X64_Builder const *builder,
                                                  Ast_Node_Index expression);
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
static bool         x64_procedures_collect       (X64_Builder *builder);
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
static void         x64_write_load_integer       (X64_Builder *builder, uint8_t register_number,
                                                  int64_t number);
static void         x64_write_load_slot          (X64_Builder *builder, uint8_t register_number,
                                                  size_t position);
static void         x64_write_store_slot         (X64_Builder *builder, uint8_t register_number,
                                                  size_t position);
static void         x64_store_u16                (byte *destination, uint16_t number);
static void         x64_store_u32                (byte *destination, uint32_t number);
static void         x64_store_u64                (byte *destination, uint64_t number);

// The registers that carry the arguments, in the order of the arguments.
static uint8_t const X64_ARGUMENT_REGISTERS[X64_PARAMETER_COUNT_MAX] = {
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
static char const X64_EXIT_NAME[] = "exit";

X64_Build_Result x64_build(Array_Slice nodes, Array_Slice bindings, Ast_Node_Index root,
                           Array_Slice output_buffer) {
    assert(nodes.elements != NULL);
    assert(bindings.elements != NULL);
    assert(output_buffer.elements != NULL);
    assert(root < nodes.length);
    // The bindings are of this tree, one for each node.
    assert(bindings.length == nodes.length);

    Ast_Node const *node_elements = nodes.elements;
    assert(node_elements[root].node_type == AST_NODE_TYPE_MODULE);

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
        .subject = STR_EMPTY,
        .layout = {0},
        .node_count = nodes.length,
        .text_start = 0,
        .procedure_count = 0,
        .text_rva = 0,
        .data_rva = 0,
        .module = root,
        .definition = AST_NODE_INDEX_NONE,
        .status = X64_BUILD_STATUS_OK,
        .procedures = {{0}},
    };

    bool is_ok = x64_procedures_collect(&builder);
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
    if (is_ok) {
        length_bytes = builder.writer.length;
    }

    assert(is_ok == (builder.status == X64_BUILD_STATUS_OK));
    assert(builder.writer.length <= output_buffer.length);
    return (X64_Build_Result){
        .bytes = array_slice_make(output_buffer.elements, length_bytes),
        .subject = builder.subject,
        .status = builder.status,
    };
}

/**
 * Finds the procedures of the module, and checks everything about them that the code does not
 * depend on the order for: that the module has nothing but procedures, that they are for this
 * target, and that their parameters and results have types that the target has. Fills
 * `builder->procedures`, in the order of the definitions, which is the position that the analysis
 * binds a call by. Returns false after it recorded an error.
 */
static bool x64_procedures_collect(X64_Builder *builder) {
    assert(builder->procedure_count == 0);

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
        if (builder->nodes[procedure].node_type != AST_NODE_TYPE_PROCEDURE) {
            // A string, a struct, an enum or the member of one is not built.
            return x64_fail(builder, X64_BUILD_STATUS_UNSUPPORTED, name);
        }
        if (builder->procedure_count == X64_PROCEDURE_COUNT_MAX) {
            return x64_fail(builder, X64_BUILD_STATUS_PROCEDURE_COUNT_EXCEEDED, name);
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

        builder->procedures[builder->procedure_count] = (X64_Procedure){
            .definition = definition,
            .offset = 0,
            .import_slot = 0,
            .is_foreign = is_foreign,
        };
        if (is_foreign) {
            builder->procedures[builder->procedure_count].import_slot = import_slot;
            import_slot += 1;
        }
        builder->procedure_count += 1;
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
    for (size_t i = 0; i < builder->procedure_count; i += 1) {
        Ast_Node_Index definition = builder->procedures[i].definition;
        Str name = builder->nodes[definition].payload.constant_definition.name;
        if (str_equals(name, STR("main")) == false) {
            continue;
        }

        Ast_Node_Index procedure = builder->nodes[definition].first_child;
        if (builder->procedures[i].is_foreign ||
            x64_parameter_count(builder, procedure) != 0 ||
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
    for (size_t i = 0; i < builder->procedure_count; i += 1) {
        if (builder->procedures[i].is_foreign) {
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
    for (size_t i = 0; i < builder->procedure_count; i += 1) {
        if (builder->procedures[i].is_foreign) {
            Str name = builder->nodes[builder->procedures[i].definition]
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

    x64_write_zeros(&builder->writer, X64_HEADERS_SIZE_BYTES);
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
        for (size_t i = 0; i < builder->procedure_count; i += 1) {
            if (builder->procedures[i].is_foreign) {
                Str name = builder->nodes[builder->procedures[i].definition]
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
    for (size_t i = 0; i < builder->procedure_count; i += 1) {
        if (builder->procedures[i].is_foreign) {
            Str name = builder->nodes[builder->procedures[i].definition]
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
           writer->length == X64_HEADERS_SIZE_BYTES + layout->rdata_size_bytes);
}

/**
 * Writes the code: the few instructions that the program starts with, and then every procedure
 * that has a body, in the order of the definitions. Returns false after it recorded an error.
 */
static bool x64_code_write(X64_Builder *builder) {
    assert(builder->writer.length == builder->text_start);

    X64_Writer *writer = &builder->writer;

    // The program starts here. The system enters it with the stack 8 bytes past a multiple of 16,
    // because it was entered like a procedure is, and a call must be made with a multiple of 16.
    // So 8 bytes make it one, and 32 more are the space that `main` expects to be able to keep its
    // arguments in, which is `sub rsp, 40`.
    static byte const START_PROLOGUE[] = {0x48, 0x83, 0xEC, 0x28};
    x64_write_bytes(writer, START_PROLOGUE, sizeof(START_PROLOGUE));

    size_t main_position = builder->procedure_count;
    for (size_t i = 0; i < builder->procedure_count; i += 1) {
        Str name = builder->nodes[builder->procedures[i].definition]
                       .payload.constant_definition.name;
        if (str_equals(name, STR("main"))) {
            main_position = i;
        }
    }
    assert(main_position < builder->procedure_count);
    x64_write_call_local(builder, builder->procedures[main_position].offset);

    // The result of `main` is the exit code. `exit` ends the process after it flushed what the
    // program printed with the C library, which ending it directly would not do. It does not
    // return, so the breakpoint after it is never reached; it is there so that a stray jump here
    // stops instead of running into the next procedure.
    static byte const START_EXIT[] = {0x89, 0xC1};
    x64_write_bytes(writer, START_EXIT, sizeof(START_EXIT));
    x64_write_call_import(builder, 0);
    x64_write_byte(writer, 0xCC);

    for (size_t i = 0; i < builder->procedure_count; i += 1) {
        if (builder->procedures[i].is_foreign) {
            continue;
        }
        builder->procedures[i].offset = writer->length - builder->text_start;
        if (x64_procedure_write(builder, i) == false) {
            return false;
        }
    }

    return true;
}

/**
 * Writes the procedure at `position` of the module: it keeps `rbp` and sets it to the stack
 * pointer, so that the arguments, which are kept above it, are found at the same distance from it
 * whatever the procedure pushes. It copies the registers that carry the arguments to the space
 * that the caller made for them. Then come the statements, which are calls and `return`.
 * Returns false after it recorded an error.
 */
static bool x64_procedure_write(X64_Builder *builder, size_t position) {
    assert(position < builder->procedure_count);
    assert(builder->procedures[position].is_foreign == false);

    X64_Writer *writer = &builder->writer;
    Ast_Node_Index definition = builder->procedures[position].definition;
    Ast_Node_Index procedure = builder->nodes[definition].first_child;
    builder->definition = definition;

    // `push rbp` and `mov rbp, rsp`. The stack is a multiple of 16 after this, because the call
    // that entered the procedure made it 8 past one and `push rbp` is 8 more. Nothing is pushed
    // after, so it is also what every call made by the procedure is made with.
    static byte const PROLOGUE[] = {0x55, 0x48, 0x89, 0xE5};
    x64_write_bytes(writer, PROLOGUE, sizeof(PROLOGUE));

    size_t parameter_count = x64_parameter_count(builder, procedure);
    for (size_t i = 0; i < parameter_count; i += 1) {
        x64_write_store_slot(builder, X64_ARGUMENT_REGISTERS[i], i);
    }

    // A procedure that makes temporary strings keeps how much of the storage was in use when it
    // started, so that it can give them all back when it ends: `sub rsp, 16`, `mov rax, [used]` and
    // `mov [rbp - 8], rax`. The stack is still a multiple of 16.
    bool is_temporary_used = x64_procedure_uses_temporary(builder, procedure);
    if (is_temporary_used) {
        static byte const FRAME_MAKE[] = {0x48, 0x83, 0xEC, X64_TEMPORARY_FRAME_SIZE_BYTES};
        x64_write_bytes(writer, FRAME_MAKE, sizeof(FRAME_MAKE));
        static byte const USED_LOAD[] = {0x48, 0x8B, 0x05};
        x64_write_bytes(writer, USED_LOAD, sizeof(USED_LOAD));
        x64_write_rip_displacement(builder, builder->data_rva);
        static byte const USED_KEEP[] = {0x48, 0x89, 0x45, 0xF8};
        x64_write_bytes(writer, USED_KEEP, sizeof(USED_KEEP));
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
        if (statement_node->node_type == AST_NODE_TYPE_CALL_STATEMENT) {
            if (x64_call_write(builder, statement_node->first_child, false) == false) {
                return false;
            }
        } else if (statement_node->node_type == AST_NODE_TYPE_RETURN_STATEMENT) {
            if (x64_return_write(builder, procedure, statement) == false) {
                return false;
            }
            is_last_return = true;
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
            x64_epilogue_write(builder, is_temporary_used);
        }
    }
    return true;
}

/**
 * Writes the end of a procedure: the temporary strings that it made are given back when it made
 * any, with `mov r11, [rbp - 8]` and `mov [used], r11`, which leave `rax`, the result, alone. Then
 * `leave` and `ret` give the stack back and return.
 */
static void x64_epilogue_write(X64_Builder *builder, bool is_temporary_used) {
    if (is_temporary_used) {
        static byte const USED_RESTORE[] = {0x4C, 0x8B, 0x5D, 0xF8};
        x64_write_bytes(&builder->writer, USED_RESTORE, sizeof(USED_RESTORE));
        static byte const USED_STORE[] = {0x4C, 0x89, 0x1D};
        x64_write_bytes(&builder->writer, USED_STORE, sizeof(USED_STORE));
        x64_write_rip_displacement(builder, builder->data_rva);
    }
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
    X64_Type value_type = x64_expression_type(builder, value);
    if (value_type == X64_TYPE_NONE && builder->nodes[value].node_type != AST_NODE_TYPE_CALL) {
        // An expression that the target does not build has no type.
        return x64_fail_unsupported(builder);
    }
    if (value_type != return_type) {
        // The call of a procedure that returns nothing is in here too.
        return x64_fail(builder, X64_BUILD_STATUS_RETURN_TYPE_MISMATCH, name);
    }
    // The temporary strings are given back by the return, so a pointer to one must not leave.
    if (x64_call_is_str_to_c_str(builder, value)) {
        return x64_fail_unsupported(builder);
    }
    if (x64_expression_load(builder, value, X64_REGISTER_RAX) == false) {
        return false;
    }

    x64_epilogue_write(builder, x64_procedure_uses_temporary(builder, procedure));
    return true;
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
    assert(binding.position < builder->procedure_count);
    assert(builder->procedures[binding.position].definition == binding.target);

    X64_Procedure const *callee = &builder->procedures[binding.position];
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
    if (callee->is_foreign) {
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
        X64_Type type = x64_expression_type(builder, argument);
        if (type == X64_TYPE_NONE) {
            return x64_fail_unsupported(builder);
        }
        if (type != x64_parameter_type(builder, callee, i)) {
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
 * The expression is an integer literal, a string literal, a parameter, or a call, whose result is
 * in `rax` and is moved from there. Returns false after it recorded an error.
 */
static bool x64_expression_load(X64_Builder *builder, Ast_Node_Index expression,
                                uint8_t register_number) {
    Ast_Node const *node = &builder->nodes[expression];
    if (node->node_type == AST_NODE_TYPE_INTEGER_LITERAL) {
        x64_write_load_integer(builder, register_number, node->payload.integer_literal.value);
        return true;
    }
    if (node->node_type == AST_NODE_TYPE_IDENTIFIER &&
        builder->bindings[expression].kind == SEMANTIC_BINDING_KIND_PARAMETER) {
        x64_write_load_slot(builder, register_number, builder->bindings[expression].position);
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
 * Returns the type of `expression`, or `X64_TYPE_NONE` if it has none, which is the case for a
 * call of a procedure that returns nothing and for everything that the target does not build.
 */
static X64_Type x64_expression_type(X64_Builder const *builder, Ast_Node_Index expression) {
    Ast_Node const *node = &builder->nodes[expression];
    if (node->node_type == AST_NODE_TYPE_INTEGER_LITERAL) {
        return X64_TYPE_INT;
    }
    if (node->node_type == AST_NODE_TYPE_IDENTIFIER &&
        builder->bindings[expression].kind == SEMANTIC_BINDING_KIND_PARAMETER) {
        Ast_Node_Index parameter = builder->bindings[expression].target;
        return x64_type_of_name(
            builder->nodes[builder->nodes[parameter].first_child].payload.type_name.name);
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
 * `procedure` is an `AST_NODE_TYPE_PROCEDURE`, which `x64_procedures_collect` checked.
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
 * `x64_epilogue_write`. The argument must be a string literal, because the target does not have
 * `Str` values, so the string and its size are known when the program is built, and the copy is a
 * few stores of 8 bytes instead of a loop. Only `rax`, `r10` and `r11` are used, which no argument
 * is in. A string that does not fit in what is left stops the program with a breakpoint. Returns
 * false after it recorded an error.
 */
static bool x64_str_to_c_str_write(X64_Builder *builder, Ast_Node_Index call,
                                   uint8_t register_number) {
    assert(x64_call_is_str_to_c_str(builder, call));

    Str name = builder->nodes[call].payload.call.name;
    Ast_Node_Index argument = builder->nodes[call].first_child;
    if (argument == AST_NODE_INDEX_NONE ||
        builder->nodes[argument].next_sibling != AST_NODE_INDEX_NONE) {
        return x64_fail(builder, X64_BUILD_STATUS_CALL_ARGUMENT_COUNT_MISMATCH, name);
    }
    if (builder->nodes[argument].node_type != AST_NODE_TYPE_STRING_LITERAL) {
        return x64_fail_unsupported(builder);
    }

    Str text = builder->nodes[argument].payload.string_literal.text;
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
 * Writes `mov register, r10`.
 */
static void x64_write_move_from_r10(X64_Builder *builder, uint8_t register_number) {
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
static void x64_write_load_integer(X64_Builder *builder, uint8_t register_number,
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
static void x64_write_load_slot(X64_Builder *builder, uint8_t register_number, size_t position) {
    assert(register_number < 16);
    assert(position < X64_PARAMETER_COUNT_MAX);

    x64_write_byte(&builder->writer, (byte)(0x48 | ((register_number >> 3) << 2)));
    x64_write_byte(&builder->writer, 0x8B);
    // The mode that is `rbp` plus a distance of one byte, with the register in the middle bits.
    x64_write_byte(&builder->writer, (byte)(0x45 | ((register_number & 7) << 3)));
    x64_write_byte(&builder->writer,
                   (byte)(X64_ARGUMENT_HOME_OFFSET_BYTES + position * X64_SLOT_SIZE_BYTES));
}

/**
 * Writes `mov [rbp + offset], register`, which keeps the argument at `position` in the space that
 * the caller made for it.
 */
static void x64_write_store_slot(X64_Builder *builder, uint8_t register_number, size_t position) {
    assert(register_number < 16);
    assert(position < X64_PARAMETER_COUNT_MAX);

    x64_write_byte(&builder->writer, (byte)(0x48 | ((register_number >> 3) << 2)));
    x64_write_byte(&builder->writer, 0x89);
    x64_write_byte(&builder->writer, (byte)(0x45 | ((register_number & 7) << 3)));
    x64_write_byte(&builder->writer,
                   (byte)(X64_ARGUMENT_HOME_OFFSET_BYTES + position * X64_SLOT_SIZE_BYTES));
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
    x64_store_u16(file + 2, 3);                           // The sections: .rdata, .data, .text.
    x64_store_u32(file + 4, 0);                           // No date, so the file is reproducible.
    x64_store_u32(file + 8, 0);                           // No table of symbols.
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
    x64_store_u32(optional + 60, X64_HEADERS_SIZE_BYTES);
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
    x64_store_u32(section + 20, X64_HEADERS_SIZE_BYTES);
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

// The numbers of the format are little-endian, whatever the machine that builds them is, so they
// are written one byte at a time.
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
