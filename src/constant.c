// This file works out the values that are known when a program is compiled: literals, the constants
// that are defined with them, and the operators that combine them.
//
// A literal is untyped. It has a kind, which is an integer, a float, a truth value or a string, but
// no size, so the number 200 is neither an `Int` nor a `Byte` until it is used somewhere. The place
// decides: where a `Byte` is wanted it becomes one, if it fits, and where nothing says, it becomes
// its default type, `Int`. An integer is never a float: `1` and `1.0` are different kinds of
// literal, and neither is converted to the other without `cast`. A constant that is defined without
// a type, such as `LIMIT :: 200`, is untyped in the same way, so it can be used wherever its value
// fits, and arithmetic of untyped values gives an untyped value. That is how a literal can be given
// to every type that can hold it without the program converting anything when it runs, and without
// a literal having a type that is wrong for most places, as an `int` literal is in C.
//
// The values of all the nodes are kept in a table that the caller owns, one entry per node, so that
// a constant that is used many times is worked out once.
#include <assert.h>
#include <iso646.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <mialu/constant.h>

/**
 * What one step of the evaluation does with a node: go down to `child`, whose entry is not known
 * yet and is needed first, or, when `child` is `AST_NODE_INDEX_NONE`, finish the node with `value`.
 */
typedef struct {
    Constant_Value value;
    Ast_Node_Index child;
} Constant_Step;

/**
 * The state that the steps read: the tree, what its names stand for, and the table of what is known
 * about each node so far.
 */
typedef struct {
    // Not constant, because a concatenation writes the text of its result to the string literal
    // after it.
    Ast_Node *nodes;
    Semantic_Binding const *bindings;
    Constant_Value const *values;
    Constant_Text *text;
    size_t node_count;
} Constant_Evaluation;

static Constant_Value constant_add             (Constant_Value left, Constant_Value right,
                                                Ast_Node_Index operation);
static Constant_Value constant_arithmetic      (Ast_Node const *nodes, Ast_Node_Index operation,
                                                Constant_Value left, Constant_Value right);
static Constant_Value constant_comparison      (Ast_Node const *nodes, Ast_Node_Index comparison,
                                                Constant_Value left, Constant_Value right);
static Constant_Value constant_concatenate     (Constant_Evaluation const *evaluation,
                                                Ast_Node_Index concatenation,
                                                Constant_Value left, Constant_Value right);
static Constant_Value constant_int_to_str      (Constant_Evaluation const *evaluation,
                                                Ast_Node_Index call, Constant_Value number);
static Constant_Value constant_convert         (Ast_Node const *nodes, Ast_Node_Index definition,
                                                Constant_Value value);
static Ast_Node_Index constant_definition_type (Ast_Node const *nodes,
                                                Ast_Node_Index definition);
static Constant_Value constant_divide          (Constant_Value left, Constant_Value right,
                                                Ast_Node_Index operation);
static Constant_Value constant_fail            (Constant_Status status, Ast_Node_Index node);
static Constant_Value constant_logical         (Ast_Node const *nodes, Ast_Node_Index operation,
                                                Constant_Value left, Constant_Value right);
static Constant_Value constant_float_arithmetic(Ast_Node const *nodes, Ast_Node_Index operation,
                                                Constant_Value left, Constant_Value right);
static Constant_Type  constant_float_type      (Constant_Value left, Constant_Value right);
static Constant_Value constant_float_make      (Constant_Type type, double floating);
static Constant_Value constant_make            (Constant_Type type, int64_t integer);
static Constant_Value constant_multiply        (Constant_Value left, Constant_Value right,
                                                Ast_Node_Index operation);
static Constant_Value constant_operand_failure (Constant_Value operand);
static Constant_Step  constant_step            (Constant_Evaluation const *evaluation,
                                                Ast_Node_Index node);
static Constant_Step  constant_step_done       (Constant_Value value);
static Constant_Step  constant_step_down       (Ast_Node_Index child);
static size_t         constant_rune_encode     (uint32_t code_point, char *bytes);
static Constant_Value constant_subtract        (Constant_Value left, Constant_Value right,
                                                Ast_Node_Index operation);
static bool           constant_type_is_bool    (Constant_Type type);
static bool           constant_type_is_float   (Constant_Type type);
static bool           constant_type_is_integer (Constant_Type type);
static bool           constant_type_is_string  (Constant_Type type);
static bool           constant_type_is_untyped (Constant_Type type);

Constant_Value constant_evaluate(Ast_Node *nodes, size_t node_count,
                                 Semantic_Binding const *bindings, Constant_Value *values,
                                 Constant_Text *text, Ast_Node_Index expression) {
    assert(nodes != NULL);
    assert(bindings != NULL);
    assert(values != NULL);
    assert(text != NULL);
    assert(text->bytes != NULL);
    assert(text->length <= text->size_bytes);
    assert(expression < node_count);

    if (values[expression].status != CONSTANT_STATUS_UNKNOWN) {
        return values[expression];
    }

    Constant_Evaluation evaluation = {
        .nodes = nodes,
        .bindings = bindings,
        .values = values,
        .text = text,
        .node_count = node_count,
    };

    // The nodes on the path from `expression` to the current node, excluding the current node. A
    // node on it waits for the entry of a child.
    Ast_Node_Index path[CONSTANT_EXPRESSION_DEPTH_MAX] = {0};
    size_t path_count = 0;
    Ast_Node_Index current = expression;

    // A node is gone down to once, because its entry is filled when it is finished, and the path
    // never has a node twice: a definition that is met again on it is a cycle, which finishes the
    // name that leads there instead. A node with operands comes back once after each of them, so
    // with at most two operands it takes three steps, and four for each node is enough.
    size_t const step_count_max = 4 * node_count;
    for (size_t step = 0; step < step_count_max; step += 1) {
        Constant_Step next = constant_step(&evaluation, current);

        if (next.child != AST_NODE_INDEX_NONE) {
            if (path_count < CONSTANT_EXPRESSION_DEPTH_MAX) {
                // A definition is being worked out from here on, so that a name that leads back to
                // it before it is finished is known to be a cycle.
                if (nodes[current].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION) {
                    values[current].status = CONSTANT_STATUS_EVALUATING;
                }
                path[path_count] = current;
                path_count += 1;
                current = next.child;
                continue;
            }
            next = constant_step_done(constant_fail(CONSTANT_STATUS_DEPTH_EXCEEDED, current));
        }

        assert(next.value.status != CONSTANT_STATUS_UNKNOWN);
        assert(next.value.status != CONSTANT_STATUS_EVALUATING);
        values[current] = next.value;

        if (path_count == 0) {
            assert(current == expression);
            return values[current];
        }
        path_count -= 1;
        current = path[path_count];
    }

    // Unreachable: the step budget covers every node, see above.
    assert(false);
    return constant_fail(CONSTANT_STATUS_DEPTH_EXCEEDED, expression);
}

bool constant_definition_is_value(Ast_Node const *nodes, Ast_Node_Index definition) {
    assert(nodes != NULL);
    assert(nodes[definition].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION);

    Ast_Node_Index value = nodes[definition].first_child;
    assert(value != AST_NODE_INDEX_NONE);

    Ast_Node_Type value_type = nodes[value].node_type;
    if (value_type == AST_NODE_TYPE_ENUM_TYPE || value_type == AST_NODE_TYPE_PROCEDURE ||
        value_type == AST_NODE_TYPE_STRUCT_TYPE) {
        return false;
    }
    // `Color.RED` is the member of an enum, which has a type of its own and is not a number,
    // though it is known when the program is compiled too. Any other field is a value, which is
    // then not a constant.
    if (value_type == AST_NODE_TYPE_FIELD_ACCESS) {
        return nodes[nodes[value].first_child].node_type != AST_NODE_TYPE_IDENTIFIER;
    }
    return true;
}

/**
 * Returns the type that `definition`, an `AST_NODE_TYPE_CONSTANT_DEFINITION`, gives its constant,
 * which is its second child, or `AST_NODE_INDEX_NONE` when it gives none and the constant is
 * untyped.
 */
static Ast_Node_Index constant_definition_type(Ast_Node const *nodes, Ast_Node_Index definition) {
    assert(nodes != NULL);
    assert(nodes[definition].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION);

    Ast_Node_Index value = nodes[definition].first_child;
    assert(value != AST_NODE_INDEX_NONE);
    // The type comes after the value, so that the value is the first child of every definition.
    return nodes[value].next_sibling;
}

bool constant_is_byte(Constant_Value value) {
    assert(value.status == CONSTANT_STATUS_OK);

    return value.type == CONSTANT_TYPE_UNTYPED_INTEGER && value.integer >= 0 &&
           value.integer <= 255;
}

bool constant_is_i32(Constant_Value value) {
    assert(value.status == CONSTANT_STATUS_OK);

    return value.type == CONSTANT_TYPE_UNTYPED_INTEGER && value.integer >= INT32_MIN &&
           value.integer <= INT32_MAX;
}

bool constant_is_u32(Constant_Value value) {
    assert(value.status == CONSTANT_STATUS_OK);

    return value.type == CONSTANT_TYPE_UNTYPED_INTEGER && value.integer >= 0 &&
           value.integer <= UINT32_MAX;
}

bool constant_is_u64(Constant_Value value) {
    assert(value.status == CONSTANT_STATUS_OK);

    return value.type == CONSTANT_TYPE_UNTYPED_INTEGER && value.integer >= 0;
}

bool constant_is_f32(Constant_Value value) {
    assert(value.status == CONSTANT_STATUS_OK);

    // The largest `F32` is the bound, and a number above it has no `F32` to be rounded to.
    return value.type == CONSTANT_TYPE_UNTYPED_FLOAT && value.floating >= -3.4028234663852886e38 &&
           value.floating <= 3.4028234663852886e38;
}

Constant_Type constant_type_default(Constant_Type type) {
    switch (type) {
    case CONSTANT_TYPE_BOOL:
    case CONSTANT_TYPE_BYTE:
    case CONSTANT_TYPE_F32:
    case CONSTANT_TYPE_F64:
    case CONSTANT_TYPE_I32:
    case CONSTANT_TYPE_I64:
    case CONSTANT_TYPE_INT:
    case CONSTANT_TYPE_RUNE:
    case CONSTANT_TYPE_STR:
    case CONSTANT_TYPE_U32:
    case CONSTANT_TYPE_U64:
        return type;
    case CONSTANT_TYPE_UNTYPED_FLOAT:
        // The widest float, which holds every number that a literal can be, and every `F32`.
        return CONSTANT_TYPE_F64;
    case CONSTANT_TYPE_UNTYPED_BOOL:
        return CONSTANT_TYPE_BOOL;
    case CONSTANT_TYPE_UNTYPED_INTEGER:
        return CONSTANT_TYPE_INT;
    case CONSTANT_TYPE_UNTYPED_STRING:
        return CONSTANT_TYPE_STR;
    }

    assert(false);
    return type;
}

/**
 * Works on `node` once: either asks for the entry of a child that is not known yet, or finishes the
 * node from the entries of its children, which are known by then. It only reads the table, so the
 * order in which the nodes are finished is up to `constant_evaluate`. It writes nothing but the text
 * of a concatenation.
 */
static Constant_Step constant_step(Constant_Evaluation const *evaluation, Ast_Node_Index node) {
    assert(node < evaluation->node_count);
    assert(evaluation->values[node].status == CONSTANT_STATUS_UNKNOWN ||
           evaluation->values[node].status == CONSTANT_STATUS_EVALUATING);

    Ast_Node const *nodes = evaluation->nodes;
    Constant_Value const *values = evaluation->values;
    Ast_Node const *current = &nodes[node];

    switch (current->node_type) {
    case AST_NODE_TYPE_BOOL_LITERAL: {
        int64_t integer = 0;
        if (current->payload.bool_literal.value) {
            integer = 1;
        }
        return constant_step_done(constant_make(CONSTANT_TYPE_UNTYPED_BOOL, integer));
    }
    case AST_NODE_TYPE_FLOAT_LITERAL:
        return constant_step_done(constant_float_make(CONSTANT_TYPE_UNTYPED_FLOAT,
                                                      current->payload.float_literal.value));
    case AST_NODE_TYPE_INTEGER_LITERAL:
        return constant_step_done(constant_make(CONSTANT_TYPE_UNTYPED_INTEGER,
                                                current->payload.integer_literal.value));
    case AST_NODE_TYPE_RUNE_LITERAL:
        return constant_step_done(constant_make(CONSTANT_TYPE_RUNE,
                                                (int64_t)current->payload.rune_literal.value));
    case AST_NODE_TYPE_STRING_LITERAL: {
        Constant_Value string = constant_make(CONSTANT_TYPE_UNTYPED_STRING, 0);
        string.string = node;
        return constant_step_done(string);
    }
    case AST_NODE_TYPE_IDENTIFIER: {
        // Only the name of a constant is known when the program is compiled. A variable, a
        // parameter or a global variable has a value only when the program runs.
        Semantic_Binding binding = evaluation->bindings[node];
        if (binding.kind != SEMANTIC_BINDING_KIND_CONSTANT ||
            not constant_definition_is_value(nodes, binding.target)) {
            return constant_step_done(constant_fail(CONSTANT_STATUS_NOT_CONSTANT,
                                                    AST_NODE_INDEX_NONE));
        }
        Constant_Value definition = values[binding.target];
        if (definition.status == CONSTANT_STATUS_UNKNOWN) {
            return constant_step_down(binding.target);
        }
        if (definition.status == CONSTANT_STATUS_EVALUATING) {
            return constant_step_done(constant_fail(CONSTANT_STATUS_CYCLE, node));
        }
        return constant_step_done(definition);
    }
    case AST_NODE_TYPE_CONSTANT_DEFINITION: {
        Ast_Node_Index value = current->first_child;
        if (values[value].status == CONSTANT_STATUS_UNKNOWN) {
            return constant_step_down(value);
        }
        return constant_step_done(constant_convert(nodes, node, values[value]));
    }
    case AST_NODE_TYPE_NOT: {
        Ast_Node_Index operand = current->first_child;
        if (values[operand].status == CONSTANT_STATUS_UNKNOWN) {
            return constant_step_down(operand);
        }
        Constant_Value value = values[operand];
        if (value.status != CONSTANT_STATUS_OK) {
            return constant_step_done(constant_operand_failure(value));
        }
        if (not constant_type_is_bool(value.type)) {
            return constant_step_done(constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, node));
        }
        // The result has the type of the operand, so `not` of an untyped value is untyped.
        return constant_step_done(constant_make(value.type, 1 - value.integer));
    }
    case AST_NODE_TYPE_ADDITION:
    case AST_NODE_TYPE_AND:
    case AST_NODE_TYPE_COMPARISON:
    case AST_NODE_TYPE_CONCATENATION:
    case AST_NODE_TYPE_DIVISION:
    case AST_NODE_TYPE_MULTIPLICATION:
    case AST_NODE_TYPE_OR:
    case AST_NODE_TYPE_SUBTRACTION: {
        // The left operand first, and the right one only when the left one is a constant, so that
        // an expression that is not a constant is given up as early as possible.
        Ast_Node_Index left = current->first_child;
        Ast_Node_Index right = current->last_child;
        assert(left != right);

        if (values[left].status == CONSTANT_STATUS_UNKNOWN) {
            return constant_step_down(left);
        }
        if (values[left].status != CONSTANT_STATUS_OK) {
            return constant_step_done(constant_operand_failure(values[left]));
        }
        if (values[right].status == CONSTANT_STATUS_UNKNOWN) {
            return constant_step_down(right);
        }
        if (values[right].status != CONSTANT_STATUS_OK) {
            return constant_step_done(constant_operand_failure(values[right]));
        }

        if (current->node_type == AST_NODE_TYPE_COMPARISON) {
            return constant_step_done(
                constant_comparison(nodes, node, values[left], values[right]));
        }
        if (current->node_type == AST_NODE_TYPE_CONCATENATION) {
            return constant_step_done(
                constant_concatenate(evaluation, node, values[left], values[right]));
        }
        if (current->node_type == AST_NODE_TYPE_AND || current->node_type == AST_NODE_TYPE_OR) {
            return constant_step_done(constant_logical(nodes, node, values[left], values[right]));
        }
        return constant_step_done(constant_arithmetic(nodes, node, values[left], values[right]));
    }
    case AST_NODE_TYPE_CALL: {
        // Only the built-in `int_to_str` is a constant, when its argument is. Any other call
        // needs the program to run.
        if (evaluation->bindings[node].kind != SEMANTIC_BINDING_KIND_NONE ||
            not str_equals(current->payload.call.name, STR("int_to_str"))) {
            return constant_step_done(constant_fail(CONSTANT_STATUS_NOT_CONSTANT,
                                                    AST_NODE_INDEX_NONE));
        }
        Ast_Node_Index argument = current->first_child;
        // One argument, and no other.
        if (argument == AST_NODE_INDEX_NONE || nodes[argument].next_sibling != AST_NODE_INDEX_NONE) {
            return constant_step_done(constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, node));
        }
        if (values[argument].status == CONSTANT_STATUS_UNKNOWN) {
            return constant_step_down(argument);
        }
        if (values[argument].status != CONSTANT_STATUS_OK) {
            return constant_step_done(constant_operand_failure(values[argument]));
        }
        return constant_step_done(constant_int_to_str(evaluation, node, values[argument]));
    }
    case AST_NODE_TYPE_ADDRESS_OF:
    case AST_NODE_TYPE_ARRAY_LITERAL:
    case AST_NODE_TYPE_CAST:
    case AST_NODE_TYPE_DEREFERENCE:
    case AST_NODE_TYPE_ENUM_LITERAL:
    case AST_NODE_TYPE_FIELD_ACCESS:
    case AST_NODE_TYPE_NIL_LITERAL:
    case AST_NODE_TYPE_SLICE:
    case AST_NODE_TYPE_STRUCT_LITERAL:
    case AST_NODE_TYPE_SUBSCRIPT:
    case AST_NODE_TYPE_TYPE_INFO_OF:
        // These need the program to run, such as a call, or have no value of the kinds that a
        // constant has, such as `nil`, an array or the member of an enum.
        return constant_step_done(constant_fail(CONSTANT_STATUS_NOT_CONSTANT,
                                                AST_NODE_INDEX_NONE));
    case AST_NODE_TYPE_ADDITION_ASSIGNMENT:
    case AST_NODE_TYPE_ARRAY_TYPE:
    case AST_NODE_TYPE_ASSIGNMENT:
    case AST_NODE_TYPE_ATTRIBUTE:
    case AST_NODE_TYPE_BLOCK:
    case AST_NODE_TYPE_BREAK_STATEMENT:
    case AST_NODE_TYPE_CALL_STATEMENT:
    case AST_NODE_TYPE_CONTINUE_STATEMENT:
    case AST_NODE_TYPE_DEFER_STATEMENT:
    case AST_NODE_TYPE_DIVISION_ASSIGNMENT:
    case AST_NODE_TYPE_ENUM_MEMBER:
    case AST_NODE_TYPE_ENUM_TYPE:
    case AST_NODE_TYPE_FIELD:
    case AST_NODE_TYPE_FIELD_INITIALIZER:
    case AST_NODE_TYPE_FOR_STATEMENT:
    case AST_NODE_TYPE_FOREIGN_BODY:
    case AST_NODE_TYPE_FOREIGN_IMPORT:
    case AST_NODE_TYPE_IMPORT:
    case AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION:
    case AST_NODE_TYPE_IF_STATEMENT:
    case AST_NODE_TYPE_LABEL:
    case AST_NODE_TYPE_LABELED_STATEMENT:
    case AST_NODE_TYPE_LOOP_STATEMENT:
    case AST_NODE_TYPE_MATCH_CASE:
    case AST_NODE_TYPE_MATCH_STATEMENT:
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
    case AST_NODE_TYPE_UNION_CASE:
    case AST_NODE_TYPE_UNION_MATCH:
    case AST_NODE_TYPE_VARIABLE_DEFINITION:
        // None of these is an expression, and the analysis only evaluates expressions and the
        // definitions of constants.
        assert(false);
        break;
    }

    return constant_step_done(constant_fail(CONSTANT_STATUS_NOT_CONSTANT, AST_NODE_INDEX_NONE));
}

/**
 * Returns the value of the definition `definition` from `value`, the value of its expression: the
 * same when the constant is untyped, and otherwise the value converted to the type that the
 * definition gives, which an untyped value is when it fits, and a typed one only when it has that
 * type already.
 */
static Constant_Value constant_convert(Ast_Node const *nodes, Ast_Node_Index definition,
                                       Constant_Value value) {
    assert(nodes[definition].node_type == AST_NODE_TYPE_CONSTANT_DEFINITION);

    if (value.status != CONSTANT_STATUS_OK) {
        return constant_operand_failure(value);
    }
    Ast_Node_Index type = constant_definition_type(nodes, definition);
    if (type == AST_NODE_INDEX_NONE) {
        return value;
    }

    // A constant is one value, so only the types that are one value of a fixed kind can be given:
    // there is no constant pointer, array or struct.
    if (nodes[type].node_type != AST_NODE_TYPE_TYPE_NAME) {
        return constant_fail(CONSTANT_STATUS_TYPE_INVALID, definition);
    }
    Str name = nodes[type].payload.type_name.name;
    Constant_Type target = CONSTANT_TYPE_INT;
    Constant_Type untyped = CONSTANT_TYPE_UNTYPED_INTEGER;
    if (str_equals(name, STR("Int"))) {
        target = CONSTANT_TYPE_INT;
        untyped = CONSTANT_TYPE_UNTYPED_INTEGER;
    } else if (str_equals(name, STR("Byte"))) {
        target = CONSTANT_TYPE_BYTE;
        untyped = CONSTANT_TYPE_UNTYPED_INTEGER;
    } else if (str_equals(name, STR("I32"))) {
        target = CONSTANT_TYPE_I32;
        untyped = CONSTANT_TYPE_UNTYPED_INTEGER;
    } else if (str_equals(name, STR("I64"))) {
        // Every untyped integer fits, because it is a number of 64 bits already.
        target = CONSTANT_TYPE_I64;
        untyped = CONSTANT_TYPE_UNTYPED_INTEGER;
    } else if (str_equals(name, STR("U32"))) {
        target = CONSTANT_TYPE_U32;
        untyped = CONSTANT_TYPE_UNTYPED_INTEGER;
    } else if (str_equals(name, STR("U64"))) {
        target = CONSTANT_TYPE_U64;
        untyped = CONSTANT_TYPE_UNTYPED_INTEGER;
    } else if (str_equals(name, STR("F32"))) {
        target = CONSTANT_TYPE_F32;
        untyped = CONSTANT_TYPE_UNTYPED_FLOAT;
    } else if (str_equals(name, STR("F64"))) {
        target = CONSTANT_TYPE_F64;
        untyped = CONSTANT_TYPE_UNTYPED_FLOAT;
    } else if (str_equals(name, STR("Rune"))) {
        // No untyped constant is a rune, so only a rune literal or a rune constant is.
        target = CONSTANT_TYPE_RUNE;
        untyped = CONSTANT_TYPE_RUNE;
    } else if (str_equals(name, STR("Bool"))) {
        target = CONSTANT_TYPE_BOOL;
        untyped = CONSTANT_TYPE_UNTYPED_BOOL;
    } else if (str_equals(name, STR("Str"))) {
        target = CONSTANT_TYPE_STR;
        untyped = CONSTANT_TYPE_UNTYPED_STRING;
    } else {
        return constant_fail(CONSTANT_STATUS_TYPE_INVALID, definition);
    }

    if (value.type == target) {
        return value;
    }
    if (value.type != untyped) {
        return constant_fail(CONSTANT_STATUS_TYPE_MISMATCH, definition);
    }
    if (target == CONSTANT_TYPE_BYTE && not constant_is_byte(value)) {
        return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, definition);
    }
    if (target == CONSTANT_TYPE_I32 && not constant_is_i32(value)) {
        return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, definition);
    }
    if (target == CONSTANT_TYPE_U32 && not constant_is_u32(value)) {
        return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, definition);
    }
    if (target == CONSTANT_TYPE_U64 && not constant_is_u64(value)) {
        return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, definition);
    }
    if (target == CONSTANT_TYPE_F32) {
        if (not constant_is_f32(value)) {
            return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, definition);
        }
        // The constant has the number that an `F32` holds, not the digits that were written.
        value.floating = (double)(float)value.floating;
    }
    value.type = target;
    return value;
}

/**
 * Returns the number of bytes, from 1 to 4, that the UTF-8 encoding of `code_point` takes, and
 * writes them to `bytes`, which has room for four. The code point is a Unicode character, as a rune
 * is, so it is at most 0x10FFFF.
 */
static size_t constant_rune_encode(uint32_t code_point, char *bytes) {
    assert(code_point <= 0x10FFFF);

    if (code_point < 0x80) {
        bytes[0] = (char)code_point;
        return 1;
    }
    if (code_point < 0x800) {
        bytes[0] = (char)(0xC0 | (code_point >> 6));
        bytes[1] = (char)(0x80 | (code_point & 0x3F));
        return 2;
    }
    if (code_point < 0x10000) {
        bytes[0] = (char)(0xE0 | (code_point >> 12));
        bytes[1] = (char)(0x80 | ((code_point >> 6) & 0x3F));
        bytes[2] = (char)(0x80 | (code_point & 0x3F));
        return 3;
    }
    bytes[0] = (char)(0xF0 | (code_point >> 18));
    bytes[1] = (char)(0x80 | ((code_point >> 12) & 0x3F));
    bytes[2] = (char)(0x80 | ((code_point >> 6) & 0x3F));
    bytes[3] = (char)(0x80 | (code_point & 0x3F));
    return 4;
}

/**
 * Returns the string that `++` makes of two constants, which is the text of the left operand and
 * then that of the right one. One operand is a string and the other is a string or a rune, which
 * is its UTF-8 bytes. The result is a `Str` when an operand is one, and an untyped string
 * otherwise. The text is written to the memory of `evaluation`, and the string literal after
 * `concatenation` is given it, so that the result is a literal like any other string that is
 * known when the program is compiled.
 */
static Constant_Value constant_concatenate(Constant_Evaluation const *evaluation,
                                           Ast_Node_Index concatenation, Constant_Value left,
                                           Constant_Value right) {
    assert(evaluation->nodes[concatenation].node_type == AST_NODE_TYPE_CONCATENATION);
    assert(left.status == CONSTANT_STATUS_OK);
    assert(right.status == CONSTANT_STATUS_OK);

    bool is_left_string = constant_type_is_string(left.type);
    bool is_right_string = constant_type_is_string(right.type);
    bool is_left_rune = left.type == CONSTANT_TYPE_RUNE;
    bool is_right_rune = right.type == CONSTANT_TYPE_RUNE;
    if (not ((is_left_string and (is_right_string or is_right_rune)) or
             (is_left_rune and is_right_string))) {
        return constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, concatenation);
    }

    // The bytes of a rune are at hand, and those of a string are in its literal.
    char left_rune_bytes[4] = {0};
    char right_rune_bytes[4] = {0};
    Str left_text = STR("");
    Str right_text = STR("");
    if (is_left_string) {
        left_text = evaluation->nodes[left.string].payload.string_literal.text;
    } else {
        left_text = str_make(left_rune_bytes,
                             constant_rune_encode((uint32_t)left.integer, left_rune_bytes));
    }
    if (is_right_string) {
        right_text = evaluation->nodes[right.string].payload.string_literal.text;
    } else {
        right_text = str_make(right_rune_bytes,
                              constant_rune_encode((uint32_t)right.integer, right_rune_bytes));
    }

    Constant_Text *text = evaluation->text;
    size_t available = text->size_bytes - text->length;
    if (left_text.length > available || right_text.length > available - left_text.length) {
        return constant_fail(CONSTANT_STATUS_TEXT_TOO_LONG, concatenation);
    }

    // The literal right after the node, which the parser made for the result.
    Ast_Node_Index result = concatenation + 1;
    assert(result < evaluation->node_count);
    assert(evaluation->nodes[result].node_type == AST_NODE_TYPE_STRING_LITERAL);

    char *bytes = text->bytes + text->length;
    // A string can be empty, and `memcpy` takes no bytes from nothing.
    if (left_text.length > 0) {
        memcpy(bytes, left_text.bytes, left_text.length);
    }
    if (right_text.length > 0) {
        memcpy(bytes + left_text.length, right_text.bytes, right_text.length);
    }
    size_t length = left_text.length + right_text.length;
    text->length += length;
    evaluation->nodes[result].payload.string_literal.text = str_make(bytes, length);

    Constant_Type type = CONSTANT_TYPE_UNTYPED_STRING;
    if (left.type == CONSTANT_TYPE_STR || right.type == CONSTANT_TYPE_STR) {
        type = CONSTANT_TYPE_STR;
    }
    Constant_Value value = constant_make(type, 0);
    value.string = result;
    return value;
}

/**
 * Returns the value of the call `call` of `int_to_str`, which is the decimal text of `number`, an
 * integer: untyped or an `Int`, `I32` or `I64`, but not a `Byte`, a `Rune` or a truth value. The
 * text is a minus sign for a negative number and the digits. It is an untyped string, like a string
 * literal, and is written to the memory of `evaluation`, and the string literal after `call` is
 * given it, see `AST_NODE_TYPE_CALL`.
 */
static Constant_Value constant_int_to_str(Constant_Evaluation const *evaluation,
                                          Ast_Node_Index call, Constant_Value number) {
    assert(evaluation->nodes[call].node_type == AST_NODE_TYPE_CALL);
    assert(number.status == CONSTANT_STATUS_OK);

    if (not constant_type_is_integer(number.type) || number.type == CONSTANT_TYPE_BYTE) {
        return constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, call);
    }

    // The digits from the end, of the magnitude, which is unsigned so that the smallest number has
    // one too.
    char digits[24] = {0};
    size_t start = sizeof digits;
    uint64_t magnitude = (uint64_t)number.integer;
    if (number.integer < 0) {
        magnitude = 0 - magnitude;
    }
    do {
        start -= 1;
        digits[start] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude > 0);
    if (number.integer < 0) {
        start -= 1;
        digits[start] = '-';
    }
    size_t length = sizeof digits - start;

    Constant_Text *text = evaluation->text;
    if (length > text->size_bytes - text->length) {
        return constant_fail(CONSTANT_STATUS_TEXT_TOO_LONG, call);
    }

    // The literal right after the node, which the parser made for the result.
    Ast_Node_Index result = call + 1;
    assert(result < evaluation->node_count);
    assert(evaluation->nodes[result].node_type == AST_NODE_TYPE_STRING_LITERAL);

    char *bytes = text->bytes + text->length;
    memcpy(bytes, digits + start, length);
    text->length += length;
    evaluation->nodes[result].payload.string_literal.text = str_make(bytes, length);

    Constant_Value value = constant_make(CONSTANT_TYPE_UNTYPED_STRING, 0);
    value.string = result;
    return value;
}

/**
 * Returns the value of the arithmetic `operation` of two constants. Both are integers, and not a
 * typed `Byte`, which has no arithmetic. Two untyped integers give an untyped one, and an untyped
 * one with an `Int` gives an `Int`.
 */
static Constant_Value constant_arithmetic(Ast_Node const *nodes, Ast_Node_Index operation,
                                          Constant_Value left, Constant_Value right) {
    assert(left.status == CONSTANT_STATUS_OK);
    assert(right.status == CONSTANT_STATUS_OK);

    if (constant_type_is_float(left.type) && constant_type_is_float(right.type)) {
        return constant_float_arithmetic(nodes, operation, left, right);
    }
    if (not constant_type_is_integer(left.type) ||
        not constant_type_is_integer(right.type)) {
        return constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, operation);
    }
    if (left.type == CONSTANT_TYPE_BYTE || right.type == CONSTANT_TYPE_BYTE) {
        return constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, operation);
    }
    // An `I32` is worked with another `I32`, or an untyped integer that fits in it, and not with
    // an `Int`, because nothing is converted.
    bool is_i32 = left.type == CONSTANT_TYPE_I32 || right.type == CONSTANT_TYPE_I32;
    bool is_i64 = left.type == CONSTANT_TYPE_I64 || right.type == CONSTANT_TYPE_I64;
    bool is_u32 = left.type == CONSTANT_TYPE_U32 || right.type == CONSTANT_TYPE_U32;
    bool is_u64 = left.type == CONSTANT_TYPE_U64 || right.type == CONSTANT_TYPE_U64;
    // The sized types are not worked with each other, or with an `Int`.
    size_t sized_count = (size_t)is_i32 + (size_t)is_i64 + (size_t)is_u32 + (size_t)is_u64;
    if (sized_count > 1 ||
        (sized_count == 1 && (left.type == CONSTANT_TYPE_INT || right.type == CONSTANT_TYPE_INT))) {
        return constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, operation);
    }
    // An untyped operand of an unsigned type must be a number that the type holds.
    if ((is_u32 && ((left.type == CONSTANT_TYPE_UNTYPED_INTEGER && not constant_is_u32(left)) ||
                    (right.type == CONSTANT_TYPE_UNTYPED_INTEGER && not constant_is_u32(right)))) ||
        (is_u64 && ((left.type == CONSTANT_TYPE_UNTYPED_INTEGER && not constant_is_u64(left)) ||
                    (right.type == CONSTANT_TYPE_UNTYPED_INTEGER && not constant_is_u64(right))))) {
        return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, operation);
    }
    if (is_i32) {
        if (left.type == CONSTANT_TYPE_UNTYPED_INTEGER && not constant_is_i32(left)) {
            return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, operation);
        }
        if (right.type == CONSTANT_TYPE_UNTYPED_INTEGER && not constant_is_i32(right)) {
            return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, operation);
        }
    }

    Ast_Node_Type operator = nodes[operation].node_type;
    Constant_Value result = constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, operation);
    if (operator == AST_NODE_TYPE_ADDITION) {
        result = constant_add(left, right, operation);
    } else if (operator == AST_NODE_TYPE_DIVISION) {
        result = constant_divide(left, right, operation);
    } else if (operator == AST_NODE_TYPE_MULTIPLICATION) {
        result = constant_multiply(left, right, operation);
    } else {
        assert(operator == AST_NODE_TYPE_SUBTRACTION);
        result = constant_subtract(left, right, operation);
    }
    if (result.status != CONSTANT_STATUS_OK) {
        return result;
    }

    // The integers are untyped, `Int` or `I32`, and an untyped one takes the type of the other.
    // The result of an `I32` must fit in it, because a constant does not wrap around.
    result.type = CONSTANT_TYPE_UNTYPED_INTEGER;
    if (left.type == CONSTANT_TYPE_INT || right.type == CONSTANT_TYPE_INT) {
        result.type = CONSTANT_TYPE_INT;
    }
    if (is_i32) {
        if (result.integer < INT32_MIN || result.integer > INT32_MAX) {
            return constant_fail(CONSTANT_STATUS_OVERFLOW, operation);
        }
        result.type = CONSTANT_TYPE_I32;
    }
    if (is_i64) {
        result.type = CONSTANT_TYPE_I64;
    }
    // An unsigned constant does not wrap around either: a result below zero, or above what the
    // type holds, is an error.
    if (is_u32) {
        if (result.integer < 0 || result.integer > UINT32_MAX) {
            return constant_fail(CONSTANT_STATUS_OVERFLOW, operation);
        }
        result.type = CONSTANT_TYPE_U32;
    }
    if (is_u64) {
        if (result.integer < 0) {
            return constant_fail(CONSTANT_STATUS_OVERFLOW, operation);
        }
        result.type = CONSTANT_TYPE_U64;
    }
    return result;
}

/**
 * Returns the type of the arithmetic or the comparison of two constants that are floats: the type
 * of the operand that has one, which is `CONSTANT_TYPE_UNTYPED_FLOAT` when neither has. Two typed
 * floats must have the same type, and when they do not, the result is
 * `CONSTANT_TYPE_UNTYPED_INTEGER`, which no float has and the caller turns into an error.
 */
static Constant_Type constant_float_type(Constant_Value left, Constant_Value right) {
    assert(constant_type_is_float(left.type));
    assert(constant_type_is_float(right.type));

    if (left.type == CONSTANT_TYPE_UNTYPED_FLOAT) {
        return right.type;
    }
    if (right.type == CONSTANT_TYPE_UNTYPED_FLOAT || right.type == left.type) {
        return left.type;
    }
    return CONSTANT_TYPE_UNTYPED_INTEGER;
}

/**
 * Returns the value of the arithmetic `operation` of two constants that are floats. Two untyped
 * floats give an untyped one, an untyped one with a typed float gives that type, and two typed
 * floats must have the same type. The result is worked out as a `double` and rounded to the type,
 * which is the result that a running program has. A division by zero is an error, and so is a
 * result that is infinite, so that no constant is ever one.
 */
static Constant_Value constant_float_arithmetic(Ast_Node const *nodes, Ast_Node_Index operation,
                                                Constant_Value left, Constant_Value right) {
    assert(constant_type_is_float(left.type));
    assert(constant_type_is_float(right.type));

    Constant_Type type = constant_float_type(left, right);
    if (type == CONSTANT_TYPE_UNTYPED_INTEGER) {
        return constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, operation);
    }
    if (type == CONSTANT_TYPE_F32) {
        // An untyped operand takes the type of the typed one, so it has to fit in it.
        if (left.type == CONSTANT_TYPE_UNTYPED_FLOAT && not constant_is_f32(left)) {
            return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, operation);
        }
        if (right.type == CONSTANT_TYPE_UNTYPED_FLOAT && not constant_is_f32(right)) {
            return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, operation);
        }
    }

    // The operands of an `F32` are the numbers that an `F32` holds, which an untyped operand is
    // rounded to, as it is where it is used.
    double a = left.floating;
    double b = right.floating;
    if (type == CONSTANT_TYPE_F32) {
        a = (double)(float)a;
        b = (double)(float)b;
    }

    double result = 0.0;
    Ast_Node_Type operator = nodes[operation].node_type;
    if (operator == AST_NODE_TYPE_ADDITION) {
        result = a + b;
    } else if (operator == AST_NODE_TYPE_SUBTRACTION) {
        result = a - b;
    } else if (operator == AST_NODE_TYPE_MULTIPLICATION) {
        result = a * b;
    } else {
        assert(operator == AST_NODE_TYPE_DIVISION);
        if (b == 0.0) {
            return constant_fail(CONSTANT_STATUS_DIVISION_BY_ZERO, operation);
        }
        result = a / b;
    }
    if (type == CONSTANT_TYPE_F32) {
        // A result that is above the largest `F32` is infinite once it is rounded.
        result = (double)(float)result;
    }
    // The operands are finite, so the result is a number or an infinity, and never not a number.
    if (result > 1.7976931348623157e308 || result < -1.7976931348623157e308) {
        return constant_fail(CONSTANT_STATUS_OVERFLOW, operation);
    }
    return constant_float_make(type, result);
}

/**
 * Returns `left + right`, or an overflow when the sum does not fit in 64 bits. The check comes
 * before the addition, because signed overflow is undefined behavior in C.
 */
static Constant_Value constant_add(Constant_Value left, Constant_Value right,
                                   Ast_Node_Index operation) {
    int64_t a = left.integer;
    int64_t b = right.integer;
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
        return constant_fail(CONSTANT_STATUS_OVERFLOW, operation);
    }
    return constant_make(CONSTANT_TYPE_UNTYPED_INTEGER, a + b);
}

/**
 * Returns `left - right`, or an overflow when the difference does not fit in 64 bits.
 */
static Constant_Value constant_subtract(Constant_Value left, Constant_Value right,
                                        Ast_Node_Index operation) {
    int64_t a = left.integer;
    int64_t b = right.integer;
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) {
        return constant_fail(CONSTANT_STATUS_OVERFLOW, operation);
    }
    return constant_make(CONSTANT_TYPE_UNTYPED_INTEGER, a - b);
}

/**
 * Returns `left * right`, or an overflow when the product does not fit in 64 bits. Each pair of
 * signs has a bound of its own, which is found with a division, so that nothing overflows while it
 * is checked.
 */
static Constant_Value constant_multiply(Constant_Value left, Constant_Value right,
                                        Ast_Node_Index operation) {
    int64_t a = left.integer;
    int64_t b = right.integer;
    bool is_overflow = false;
    if (a > 0) {
        if (b > 0) {
            is_overflow = a > INT64_MAX / b;
        } else {
            is_overflow = b < INT64_MIN / a;
        }
    } else if (a < 0) {
        if (b > 0) {
            is_overflow = a < INT64_MIN / b;
        } else if (b < 0) {
            is_overflow = b < INT64_MAX / a;
        }
    }
    if (is_overflow) {
        return constant_fail(CONSTANT_STATUS_OVERFLOW, operation);
    }
    return constant_make(CONSTANT_TYPE_UNTYPED_INTEGER, a * b);
}

/**
 * Returns `left / right`, which truncates toward zero, as a running program does. Dividing by zero
 * has no result, and neither has the smallest `Int` divided by -1, which does not fit.
 */
static Constant_Value constant_divide(Constant_Value left, Constant_Value right,
                                      Ast_Node_Index operation) {
    int64_t a = left.integer;
    int64_t b = right.integer;
    if (b == 0) {
        return constant_fail(CONSTANT_STATUS_DIVISION_BY_ZERO, operation);
    }
    if (a == INT64_MIN && b == -1) {
        return constant_fail(CONSTANT_STATUS_OVERFLOW, operation);
    }
    return constant_make(CONSTANT_TYPE_UNTYPED_INTEGER, a / b);
}

/**
 * Returns whether the comparison `comparison` of two constants holds, as an untyped truth value.
 * Integers are compared with any operator, when an untyped one fits in the type of a typed one and
 * the typed ones have the same type, so a `Byte` is compared with an untyped number from 0 to 255
 * but not with an `Int`. Truth values are compared with `==` and `not=` only, because they have no
 * order, and strings are compared with `==` and `not=` by their bytes, and have no order either.
 */
static Constant_Value constant_comparison(Ast_Node const *nodes, Ast_Node_Index comparison,
                                          Constant_Value left, Constant_Value right) {
    assert(nodes[comparison].node_type == AST_NODE_TYPE_COMPARISON);
    assert(left.status == CONSTANT_STATUS_OK);
    assert(right.status == CONSTANT_STATUS_OK);

    Ast_Comparison_Operator operator = nodes[comparison].payload.comparison.operator;
    bool is_equality = operator == AST_COMPARISON_OPERATOR_EQUAL ||
                       operator == AST_COMPARISON_OPERATOR_NOT_EQUAL;

    if (constant_type_is_integer(left.type) && constant_type_is_integer(right.type)) {
        bool is_left_untyped = constant_type_is_untyped(left.type);
        bool is_right_untyped = constant_type_is_untyped(right.type);
        if (not is_left_untyped && not is_right_untyped && left.type != right.type) {
            return constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, comparison);
        }
        if (left.type == CONSTANT_TYPE_BYTE && is_right_untyped &&
            not constant_is_byte(right)) {
            return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, comparison);
        }
        if (right.type == CONSTANT_TYPE_BYTE && is_left_untyped &&
            not constant_is_byte(left)) {
            return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, comparison);
        }
        if (left.type == CONSTANT_TYPE_I32 && is_right_untyped && not constant_is_i32(right)) {
            return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, comparison);
        }
        if (right.type == CONSTANT_TYPE_I32 && is_left_untyped && not constant_is_i32(left)) {
            return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, comparison);
        }
        if (left.type == CONSTANT_TYPE_U32 && is_right_untyped && not constant_is_u32(right)) {
            return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, comparison);
        }
        if (right.type == CONSTANT_TYPE_U32 && is_left_untyped && not constant_is_u32(left)) {
            return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, comparison);
        }
        if (left.type == CONSTANT_TYPE_U64 && is_right_untyped && not constant_is_u64(right)) {
            return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, comparison);
        }
        if (right.type == CONSTANT_TYPE_U64 && is_left_untyped && not constant_is_u64(left)) {
            return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, comparison);
        }
    } else if (constant_type_is_float(left.type) && constant_type_is_float(right.type)) {
        bool is_left_untyped = left.type == CONSTANT_TYPE_UNTYPED_FLOAT;
        bool is_right_untyped = right.type == CONSTANT_TYPE_UNTYPED_FLOAT;
        if (constant_float_type(left, right) == CONSTANT_TYPE_UNTYPED_INTEGER) {
            return constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, comparison);
        }
        if (left.type == CONSTANT_TYPE_F32 && is_right_untyped && not constant_is_f32(right)) {
            return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, comparison);
        }
        if (right.type == CONSTANT_TYPE_F32 && is_left_untyped && not constant_is_f32(left)) {
            return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, comparison);
        }

        // An untyped operand is rounded to the type of the other, as it is where it is used, so
        // that the comparison holds exactly when it does in the running program.
        double a = left.floating;
        double b = right.floating;
        if (left.type == CONSTANT_TYPE_F32 || right.type == CONSTANT_TYPE_F32) {
            a = (double)(float)a;
            b = (double)(float)b;
        }
        bool holds = false;
        switch (operator) {
        case AST_COMPARISON_OPERATOR_EQUAL:
            holds = a == b;
            break;
        case AST_COMPARISON_OPERATOR_GREATER:
            holds = a > b;
            break;
        case AST_COMPARISON_OPERATOR_GREATER_EQUAL:
            holds = a >= b;
            break;
        case AST_COMPARISON_OPERATOR_LESS:
            holds = a < b;
            break;
        case AST_COMPARISON_OPERATOR_LESS_EQUAL:
            holds = a <= b;
            break;
        case AST_COMPARISON_OPERATOR_NOT_EQUAL:
            holds = a != b;
            break;
        }
        int64_t integer = 0;
        if (holds) {
            integer = 1;
        }
        return constant_make(CONSTANT_TYPE_UNTYPED_BOOL, integer);
    } else if ((left.type == CONSTANT_TYPE_BYTE && right.type == CONSTANT_TYPE_RUNE) ||
               (left.type == CONSTANT_TYPE_RUNE && right.type == CONSTANT_TYPE_BYTE)) {
        // A `Byte` and a rune are compared with `==` and `not=` when the rune is in the ASCII range,
        // which is one byte in UTF-8, by the number that both are.
        if (not is_equality) {
            return constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, comparison);
        }
        Constant_Value rune = left.type == CONSTANT_TYPE_RUNE ? left : right;
        if (rune.integer < 0 || rune.integer > 0x7F) {
            return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, comparison);
        }
    } else if (left.type == CONSTANT_TYPE_RUNE && right.type == CONSTANT_TYPE_RUNE) {
        // Runes are compared by their code points, with every operator, like numbers.
    } else if (constant_type_is_bool(left.type) && constant_type_is_bool(right.type)) {
        if (not is_equality) {
            return constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, comparison);
        }
    } else if (constant_type_is_string(left.type) && constant_type_is_string(right.type)) {
        // Two strings are the same when they have the same bytes, and there is no order.
        if (not is_equality) {
            return constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, comparison);
        }
        assert(left.string != AST_NODE_INDEX_NONE);
        assert(right.string != AST_NODE_INDEX_NONE);
        bool is_same = str_equals(nodes[left.string].payload.string_literal.text,
                                  nodes[right.string].payload.string_literal.text);
        bool holds = is_same == (operator == AST_COMPARISON_OPERATOR_EQUAL);
        return constant_make(CONSTANT_TYPE_UNTYPED_BOOL, holds ? 1 : 0);
    } else {
        return constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, comparison);
    }

    int64_t a = left.integer;
    int64_t b = right.integer;
    bool holds = false;
    switch (operator) {
    case AST_COMPARISON_OPERATOR_EQUAL:
        holds = a == b;
        break;
    case AST_COMPARISON_OPERATOR_GREATER:
        holds = a > b;
        break;
    case AST_COMPARISON_OPERATOR_GREATER_EQUAL:
        holds = a >= b;
        break;
    case AST_COMPARISON_OPERATOR_LESS:
        holds = a < b;
        break;
    case AST_COMPARISON_OPERATOR_LESS_EQUAL:
        holds = a <= b;
        break;
    case AST_COMPARISON_OPERATOR_NOT_EQUAL:
        holds = a != b;
        break;
    }

    int64_t integer = 0;
    if (holds) {
        integer = 1;
    }
    return constant_make(CONSTANT_TYPE_UNTYPED_BOOL, integer);
}

/**
 * Returns `left and right` or `left or right` of two truth values. Two untyped ones give an untyped
 * one, and a `Bool` makes the result a `Bool`.
 */
static Constant_Value constant_logical(Ast_Node const *nodes, Ast_Node_Index operation,
                                       Constant_Value left, Constant_Value right) {
    assert(left.status == CONSTANT_STATUS_OK);
    assert(right.status == CONSTANT_STATUS_OK);

    if (not constant_type_is_bool(left.type) || not constant_type_is_bool(right.type)) {
        return constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, operation);
    }

    bool holds = left.integer != 0 && right.integer != 0;
    if (nodes[operation].node_type == AST_NODE_TYPE_OR) {
        holds = left.integer != 0 || right.integer != 0;
    } else {
        assert(nodes[operation].node_type == AST_NODE_TYPE_AND);
    }

    Constant_Type type = CONSTANT_TYPE_UNTYPED_BOOL;
    if (left.type == CONSTANT_TYPE_BOOL || right.type == CONSTANT_TYPE_BOOL) {
        type = CONSTANT_TYPE_BOOL;
    }
    int64_t integer = 0;
    if (holds) {
        integer = 1;
    }
    return constant_make(type, integer);
}

/**
 * Returns what an operator whose operand is `operand`, which is not a constant or is wrong, is: not
 * a constant either, or the same error, which keeps the node it is about, so that it is reported
 * where it is, whichever of the nodes the caller looks at.
 */
static Constant_Value constant_operand_failure(Constant_Value operand) {
    assert(operand.status != CONSTANT_STATUS_OK);
    assert(operand.status != CONSTANT_STATUS_UNKNOWN);

    if (operand.status == CONSTANT_STATUS_NOT_CONSTANT) {
        return constant_fail(CONSTANT_STATUS_NOT_CONSTANT, AST_NODE_INDEX_NONE);
    }
    return operand;
}

static bool constant_type_is_bool(Constant_Type type) {
    return type == CONSTANT_TYPE_BOOL || type == CONSTANT_TYPE_UNTYPED_BOOL;
}

static bool constant_type_is_float(Constant_Type type) {
    return type == CONSTANT_TYPE_F32 || type == CONSTANT_TYPE_F64 ||
           type == CONSTANT_TYPE_UNTYPED_FLOAT;
}

static bool constant_type_is_integer(Constant_Type type) {
    return type == CONSTANT_TYPE_INT || type == CONSTANT_TYPE_BYTE ||
           type == CONSTANT_TYPE_I32 || type == CONSTANT_TYPE_I64 ||
           type == CONSTANT_TYPE_U32 || type == CONSTANT_TYPE_U64 ||
           type == CONSTANT_TYPE_UNTYPED_INTEGER;
}

static bool constant_type_is_string(Constant_Type type) {
    return type == CONSTANT_TYPE_STR || type == CONSTANT_TYPE_UNTYPED_STRING;
}

static bool constant_type_is_untyped(Constant_Type type) {
    return type == CONSTANT_TYPE_UNTYPED_BOOL || type == CONSTANT_TYPE_UNTYPED_FLOAT ||
           type == CONSTANT_TYPE_UNTYPED_INTEGER || type == CONSTANT_TYPE_UNTYPED_STRING;
}

static Constant_Value constant_float_make(Constant_Type type, double floating) {
    return (Constant_Value){
        .floating = floating,
        .integer = 0,
        .string = AST_NODE_INDEX_NONE,
        .error = AST_NODE_INDEX_NONE,
        .type = type,
        .status = CONSTANT_STATUS_OK,
    };
}

static Constant_Value constant_make(Constant_Type type, int64_t integer) {
    return (Constant_Value){
        .floating = 0.0,
        .integer = integer,
        .string = AST_NODE_INDEX_NONE,
        .error = AST_NODE_INDEX_NONE,
        .type = type,
        .status = CONSTANT_STATUS_OK,
    };
}

static Constant_Value constant_fail(Constant_Status status, Ast_Node_Index node) {
    assert(status != CONSTANT_STATUS_OK);

    return (Constant_Value){
        .floating = 0.0,
        .integer = 0,
        .string = AST_NODE_INDEX_NONE,
        .error = node,
        // Not read, because the value is not a constant.
        .type = CONSTANT_TYPE_UNTYPED_INTEGER,
        .status = status,
    };
}

static Constant_Step constant_step_done(Constant_Value value) {
    return (Constant_Step){
        .value = value,
        .child = AST_NODE_INDEX_NONE,
    };
}

static Constant_Step constant_step_down(Ast_Node_Index child) {
    assert(child != AST_NODE_INDEX_NONE);

    return (Constant_Step){
        .value = constant_fail(CONSTANT_STATUS_NOT_CONSTANT, AST_NODE_INDEX_NONE),
        .child = child,
    };
}
