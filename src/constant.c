// This file works out the values that are known when a program is compiled: literals, the
// constants that are defined with them, and the operators that combine them.
//
// A literal is untyped. It has a kind, which is an integer, a truth value or a string, but no
// size, so the number 200 is neither an `Int` nor a `Byte` until it is used somewhere. The place
// decides: where a `Byte` is wanted it becomes one, if it fits, and where nothing says, it becomes
// its default type, `Int`. A constant that is defined without a type, such as `LIMIT :: 200`, is
// untyped in the same way, so it can be used wherever its value fits, and arithmetic of untyped
// values gives an untyped value. That is how a literal can be given to every type that can hold it
// without the program converting anything when it runs, and without a literal having a type that
// is wrong for most places, as an `int` literal is in C.
//
// The values of all the nodes are kept in a table that the caller owns, one entry per node, so
// that a constant that is used many times is worked out once.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

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
    Ast_Node const *nodes;
    Semantic_Binding const *bindings;
    Constant_Value const *values;
    size_t node_count;
} Constant_Evaluation;

static Constant_Value constant_add             (Constant_Value left, Constant_Value right,
                                                Ast_Node_Index operation);
static Constant_Value constant_arithmetic      (Ast_Node const *nodes, Ast_Node_Index operation,
                                                Constant_Value left, Constant_Value right);
static Constant_Value constant_comparison      (Ast_Node const *nodes, Ast_Node_Index comparison,
                                                Constant_Value left, Constant_Value right);
static Constant_Value constant_convert         (Ast_Node const *nodes, Ast_Node_Index definition,
                                                Constant_Value value);
static Constant_Value constant_divide          (Constant_Value left, Constant_Value right,
                                                Ast_Node_Index operation);
static Constant_Value constant_fail            (Constant_Status status, Ast_Node_Index node);
static Constant_Value constant_logical         (Ast_Node const *nodes, Ast_Node_Index operation,
                                                Constant_Value left, Constant_Value right);
static Constant_Value constant_make            (Constant_Type type, int64_t integer);
static Constant_Value constant_multiply        (Constant_Value left, Constant_Value right,
                                                Ast_Node_Index operation);
static Constant_Value constant_operand_failure (Constant_Value operand);
static Constant_Step  constant_step            (Constant_Evaluation const *evaluation,
                                                Ast_Node_Index node);
static Constant_Step  constant_step_done       (Constant_Value value);
static Constant_Step  constant_step_down       (Ast_Node_Index child);
static Constant_Value constant_subtract        (Constant_Value left, Constant_Value right,
                                                Ast_Node_Index operation);
static bool           constant_type_is_bool    (Constant_Type type);
static bool           constant_type_is_integer (Constant_Type type);
static bool           constant_type_is_untyped (Constant_Type type);

Constant_Value constant_evaluate(Ast_Node const *nodes, size_t node_count,
                                 Semantic_Binding const *bindings, Constant_Value *values,
                                 Ast_Node_Index expression) {
    assert(nodes != NULL);
    assert(bindings != NULL);
    assert(values != NULL);
    assert(expression < node_count);

    if (values[expression].status != CONSTANT_STATUS_UNKNOWN) {
        return values[expression];
    }

    Constant_Evaluation evaluation = {
        .nodes = nodes,
        .bindings = bindings,
        .values = values,
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

Ast_Node_Index constant_definition_type(Ast_Node const *nodes, Ast_Node_Index definition) {
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

Constant_Type constant_type_default(Constant_Type type) {
    switch (type) {
    case CONSTANT_TYPE_BOOL:
    case CONSTANT_TYPE_BYTE:
    case CONSTANT_TYPE_INT:
    case CONSTANT_TYPE_STR:
        return type;
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
 * order in which the nodes are finished is up to `constant_evaluate`.
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
    case AST_NODE_TYPE_INTEGER_LITERAL:
        return constant_step_done(constant_make(CONSTANT_TYPE_UNTYPED_INTEGER,
                                                current->payload.integer_literal.value));
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
            constant_definition_is_value(nodes, binding.target) == false) {
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
        if (constant_type_is_bool(value.type) == false) {
            return constant_step_done(constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, node));
        }
        // The result has the type of the operand, so `not` of an untyped value is untyped.
        return constant_step_done(constant_make(value.type, 1 - value.integer));
    }
    case AST_NODE_TYPE_ADDITION:
    case AST_NODE_TYPE_AND:
    case AST_NODE_TYPE_COMPARISON:
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
        if (current->node_type == AST_NODE_TYPE_AND || current->node_type == AST_NODE_TYPE_OR) {
            return constant_step_done(constant_logical(nodes, node, values[left], values[right]));
        }
        return constant_step_done(constant_arithmetic(nodes, node, values[left], values[right]));
    }
    case AST_NODE_TYPE_ADDRESS_OF:
    case AST_NODE_TYPE_ARRAY_LITERAL:
    case AST_NODE_TYPE_CALL:
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
    if (target == CONSTANT_TYPE_BYTE && constant_is_byte(value) == false) {
        return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, definition);
    }
    value.type = target;
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

    if (constant_type_is_integer(left.type) == false ||
        constant_type_is_integer(right.type) == false) {
        return constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, operation);
    }
    if (left.type == CONSTANT_TYPE_BYTE || right.type == CONSTANT_TYPE_BYTE) {
        return constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, operation);
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

    // The integers are untyped or `Int`, and an untyped one takes the type of the other.
    result.type = CONSTANT_TYPE_UNTYPED_INTEGER;
    if (left.type == CONSTANT_TYPE_INT || right.type == CONSTANT_TYPE_INT) {
        result.type = CONSTANT_TYPE_INT;
    }
    return result;
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
 * order, and strings are not compared, as in a running program.
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
        if (is_left_untyped == false && is_right_untyped == false && left.type != right.type) {
            return constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, comparison);
        }
        if (left.type == CONSTANT_TYPE_BYTE && is_right_untyped &&
            constant_is_byte(right) == false) {
            return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, comparison);
        }
        if (right.type == CONSTANT_TYPE_BYTE && is_left_untyped &&
            constant_is_byte(left) == false) {
            return constant_fail(CONSTANT_STATUS_VALUE_NOT_REPRESENTABLE, comparison);
        }
    } else if (constant_type_is_bool(left.type) && constant_type_is_bool(right.type)) {
        if (is_equality == false) {
            return constant_fail(CONSTANT_STATUS_OPERAND_TYPE_INVALID, comparison);
        }
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

    if (constant_type_is_bool(left.type) == false || constant_type_is_bool(right.type) == false) {
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

static bool constant_type_is_integer(Constant_Type type) {
    return type == CONSTANT_TYPE_INT || type == CONSTANT_TYPE_BYTE ||
           type == CONSTANT_TYPE_UNTYPED_INTEGER;
}

static bool constant_type_is_untyped(Constant_Type type) {
    return type == CONSTANT_TYPE_UNTYPED_BOOL || type == CONSTANT_TYPE_UNTYPED_INTEGER ||
           type == CONSTANT_TYPE_UNTYPED_STRING;
}

static Constant_Value constant_make(Constant_Type type, int64_t integer) {
    return (Constant_Value){
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
