#ifndef MIALU_AST_H
#define MIALU_AST_H

#include <stdbool.h>
#include <stdint.h>

#include <mialu/array_slice.h>
#include <mialu/static_assert.h>
#include <mialu/str.h>

// Alphabetical, because nothing depends on the order.
typedef enum {
    AST_NODE_TYPE_ADDITION,
    AST_NODE_TYPE_ADDITION_ASSIGNMENT,
    AST_NODE_TYPE_ADDRESS_OF,
    AST_NODE_TYPE_AND,
    AST_NODE_TYPE_ARRAY_LITERAL,
    AST_NODE_TYPE_ARRAY_TYPE,
    AST_NODE_TYPE_ASSIGNMENT,
    AST_NODE_TYPE_ATTRIBUTE,
    AST_NODE_TYPE_BLOCK,
    AST_NODE_TYPE_BOOL_LITERAL,
    AST_NODE_TYPE_BREAK_STATEMENT,
    AST_NODE_TYPE_CALL,
    AST_NODE_TYPE_CALL_STATEMENT,
    AST_NODE_TYPE_COMPARISON,
    AST_NODE_TYPE_CONSTANT_DEFINITION,
    AST_NODE_TYPE_CONTINUE_STATEMENT,
    AST_NODE_TYPE_DEREFERENCE,
    AST_NODE_TYPE_DIVISION,
    AST_NODE_TYPE_DIVISION_ASSIGNMENT,
    AST_NODE_TYPE_ENUM_LITERAL,
    AST_NODE_TYPE_ENUM_MEMBER,
    AST_NODE_TYPE_ENUM_TYPE,
    AST_NODE_TYPE_FIELD,
    AST_NODE_TYPE_FIELD_ACCESS,
    AST_NODE_TYPE_FIELD_INITIALIZER,
    AST_NODE_TYPE_FOR_STATEMENT,
    AST_NODE_TYPE_FOREIGN_BODY,
    AST_NODE_TYPE_FOREIGN_IMPORT,
    AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION,
    AST_NODE_TYPE_IDENTIFIER,
    AST_NODE_TYPE_IF_STATEMENT,
    AST_NODE_TYPE_INTEGER_LITERAL,
    AST_NODE_TYPE_LABEL,
    AST_NODE_TYPE_LABELED_STATEMENT,
    AST_NODE_TYPE_LOOP_STATEMENT,
    AST_NODE_TYPE_MODULE,
    AST_NODE_TYPE_MULTIPLICATION,
    AST_NODE_TYPE_MULTIPLICATION_ASSIGNMENT,
    AST_NODE_TYPE_NIL_LITERAL,
    AST_NODE_TYPE_NOT,
    AST_NODE_TYPE_OR,
    AST_NODE_TYPE_PARAMETER,
    AST_NODE_TYPE_POINTER_TYPE,
    AST_NODE_TYPE_PROCEDURE,
    AST_NODE_TYPE_PROCEDURE_TYPE,
    AST_NODE_TYPE_RANGE,
    AST_NODE_TYPE_RETURN_STATEMENT,
    AST_NODE_TYPE_SCOPE_STATEMENT,
    AST_NODE_TYPE_SLICE,
    AST_NODE_TYPE_STRING_LITERAL,
    AST_NODE_TYPE_STRUCT_LITERAL,
    AST_NODE_TYPE_STRUCT_TYPE,
    AST_NODE_TYPE_SUBSCRIPT,
    AST_NODE_TYPE_SUBTRACTION,
    AST_NODE_TYPE_SUBTRACTION_ASSIGNMENT,
    AST_NODE_TYPE_TYPE_NAME,
    AST_NODE_TYPE_VARIABLE_DEFINITION,
} Ast_Node_Type;

// Alphabetical, because nothing depends on the order.
typedef enum {
    AST_COMPARISON_OPERATOR_EQUAL,
    AST_COMPARISON_OPERATOR_GREATER,
    AST_COMPARISON_OPERATOR_GREATER_EQUAL,
    AST_COMPARISON_OPERATOR_LESS,
    AST_COMPARISON_OPERATOR_LESS_EQUAL,
    AST_COMPARISON_OPERATOR_NOT_EQUAL,
} Ast_Comparison_Operator;

// How the length of an array type is given. Alphabetical, because nothing depends on the order.
typedef enum {
    // An integer literal between the brackets, as in `[5]Int`. It is in the payload.
    AST_ARRAY_LENGTH_FIXED,
    // The keyword `const` between the brackets, as in `[const]Int`: the length is the number of
    // elements of the array literal that the type is the start of.
    AST_ARRAY_LENGTH_INFERRED,
    // Nothing between the brackets, as in `[]Int`. That is not an array but an array slice, which
    // views the elements of an array and knows its length only when the program runs.
    AST_ARRAY_LENGTH_OMITTED,
} Ast_Array_Length_Kind;

enum {
    // The most blocks that can be inside each other in a procedure, not counting the body of the
    // procedure. The parser and the analysis keep the blocks that are open in a fixed array
    // instead of recursing, and a deeper nesting is rejected by the parser.
    AST_BLOCK_DEPTH_MAX = 32,
};

/**
 * The position of a node in the node buffer. Nodes refer to each other by index instead of by
 * pointer, so the buffer can be allocated once and the indexes stay valid. An index is 32 bits,
 * which is far more nodes than a source file can have.
 */
typedef uint32_t Ast_Node_Index;

// Means "no node". It is not zero, because zero is the index of the first node.
static Ast_Node_Index const AST_NODE_INDEX_NONE = UINT32_MAX;

/**
 * One node of the syntax tree. The children of a node form a linked list: `first_child` is the
 * head, `next_sibling` of each child points at the following one, and `last_child` makes appending
 * a child a constant-time operation. Which children a node has depends on `node_type`:
 *
 * - `AST_NODE_TYPE_ADDITION`: two children, the left and the right operand, in that order.
 * - `AST_NODE_TYPE_ADDITION_ASSIGNMENT`: two children, the `AST_NODE_TYPE_IDENTIFIER` of the
 *   variable that gets the sum, or the `AST_NODE_TYPE_SUBSCRIPT` of the element that gets it, and
 *   the value that is added to it, in that order. It is a statement of a procedure body. The other
 *   assignments that are made with an operator, such as `-=`, have the same two children.
 * - `AST_NODE_TYPE_ADDRESS_OF`: one child, the operand whose address is taken, such as `x` in
 *   `%x`. It is an expression, whose value is a pointer to the operand. Like a subscript it is made
 *   after its child. Which operands have an address is for the target to tell: a variable, a field,
 *   an element of an array and what a pointer points to do.
 * - `AST_NODE_TYPE_AND`: two children, the left and the right operand, in that order. It is an
 *   expression of type `Bool` and the target requires both operands to be `Bool`. It binds
 *   tighter than `AST_NODE_TYPE_OR` and looser than `AST_NODE_TYPE_NOT`.
 * - `AST_NODE_TYPE_ARRAY_LITERAL`: first the `AST_NODE_TYPE_ARRAY_TYPE` that starts the literal,
 *   whose length is not omitted, and then one child per element, in the order of the source, each
 *   one an expression. `[const]Int { 1, 2, 3 }` and `[3]Int { 1, 2, 3 }` are array literals. The
 *   nodes of the elements come after the node of the type, so the nodes are in the order of the
 *   source. It is an expression, and it can be followed by subscripts.
 * - `AST_NODE_TYPE_ARRAY_TYPE`: one child, the type of the elements, which is an
 *   `AST_NODE_TYPE_TYPE_NAME` or an `AST_NODE_TYPE_POINTER_TYPE`. It is a type like
 *   `AST_NODE_TYPE_TYPE_NAME`, and is found wherever a type is: it is
 *   the type of a parameter or of a variable, or it starts an array literal. How the length is
 *   given, and the length when it is a fixed one, is in the payload.
 * - `AST_NODE_TYPE_ASSIGNMENT`: two children, the target and the value, in that order. It is a
 *   plain `=`. The target is an `AST_NODE_TYPE_IDENTIFIER`, an `AST_NODE_TYPE_FIELD_ACCESS`, an
 *   `AST_NODE_TYPE_SUBSCRIPT`, an `AST_NODE_TYPE_DEREFERENCE` or, for a range of elements, an
 *   `AST_NODE_TYPE_SLICE`. It is a statement of a procedure body.
 * - `AST_NODE_TYPE_ATTRIBUTE`: no children. One attribute of a definition, such as `static` in
 *   `#(static)`, with the name between the parentheses in the payload. Which names are attributes
 *   that the language has is for the analysis to tell, not the parser.
 * - `AST_NODE_TYPE_BLOCK`: one child per statement. It is the body of a branch of an
 *   `AST_NODE_TYPE_IF_STATEMENT`, of a loop or of a scope statement, and it has its own scope: a
 *   variable defined in it cannot be used below it. Its payload has the end of the block, see
 *   below.
 * - `AST_NODE_TYPE_BOOL_LITERAL`: no children. It is `true` or `false`, which is in the payload.
 * - `AST_NODE_TYPE_BREAK_STATEMENT`: no children, or one `AST_NODE_TYPE_LABEL` when the statement
 *   names the statement to leave. Without a label it leaves the innermost loop or scope that it
 *   is in, which is an `AST_NODE_TYPE_FOR_STATEMENT`, an `AST_NODE_TYPE_LOOP_STATEMENT` or an
 *   `AST_NODE_TYPE_SCOPE_STATEMENT`, and the blocks of if statements are not among them. With a
 *   label it leaves the labeled statement with that label, which is a scope, a loop or an if
 *   statement. The word is in the payload. It is a statement of a procedure body.
 * - `AST_NODE_TYPE_CALL`: one child per argument, in the order of the source, each one an
 *   expression. It is a call of the procedure that the name in the payload stands for, which is a
 *   procedure, or a variable, a parameter or a global variable that holds a pointer to one, and an
 *   expression like the others. The node is made before its arguments. Whether it has a value
 *   depends on the procedure, which is for the target to tell.
 * - `AST_NODE_TYPE_CALL_STATEMENT`: one child, an `AST_NODE_TYPE_CALL` whose value, if it has
 *   one, is dropped. It is a statement of a procedure body.
 * - `AST_NODE_TYPE_COMPARISON`: two children, the left and the right operand, in that order. It is
 *   an expression of type `Bool` with the lowest precedence of the operators, so its operands are
 *   not comparisons. The operator is in the payload.
 * - `AST_NODE_TYPE_CONSTANT_DEFINITION`: one child, the value that is bound to the name. It is
 *   an `AST_NODE_TYPE_PROCEDURE`, an `AST_NODE_TYPE_STRING_LITERAL`, an
 *   `AST_NODE_TYPE_STRUCT_TYPE`, an `AST_NODE_TYPE_ENUM_TYPE` or the member of an enum, which is an
 *   `AST_NODE_TYPE_FIELD_ACCESS` of an `AST_NODE_TYPE_IDENTIFIER`, as in `DEFAULT :: Color.RED`.
 * - `AST_NODE_TYPE_CONTINUE_STATEMENT`: no children, or one `AST_NODE_TYPE_LABEL` when the
 *   statement names the loop to go on with. It ends the current pass of the innermost loop that it
 *   is in, or of the labeled loop, and goes on with the next one. The word is in the payload. It is
 *   a statement of a procedure body.
 * - `AST_NODE_TYPE_DEREFERENCE`: one child, the expression whose value is a pointer, such as `p`
 *   in `p^`. It is an expression, whose value is what the pointer points to, and it can be the
 *   target of an assignment. Like a subscript it is made after its child.
 * - `AST_NODE_TYPE_DIVISION`: two children, the dividend and the divisor, in that order.
 * - `AST_NODE_TYPE_DIVISION_ASSIGNMENT`: two children, the `AST_NODE_TYPE_IDENTIFIER` of the
 *   variable that is divided and gets the quotient, and the divisor, in that order. It is a
 *   statement of a procedure body.
 * - `AST_NODE_TYPE_ENUM_LITERAL`: no children. A member of an enum whose enum is not written, as
 *   in `.RED`, with the name of the member without the dot in the payload. The enum is the one
 *   that the place the literal is given to has, which is for the target to tell. It is an
 *   expression.
 * - `AST_NODE_TYPE_ENUM_MEMBER`: no children. One member of an enum, with its name in the payload.
 *   It is a child of an `AST_NODE_TYPE_ENUM_TYPE`.
 * - `AST_NODE_TYPE_ENUM_TYPE`: one `AST_NODE_TYPE_ENUM_MEMBER` for each member, in the order of the
 *   source, which is the order of their numbers, from zero. The name of the type of the numbers,
 *   such as `Int` in `enum Int ->`, is in the payload. It is always written, and which types an
 *   enum can have is for the target to tell. It is the value of an
 *   `AST_NODE_TYPE_CONSTANT_DEFINITION` that defines an enum.
 * - `AST_NODE_TYPE_FIELD`: one child, the type of the field, which is an `AST_NODE_TYPE_TYPE_NAME`,
 *   an `AST_NODE_TYPE_POINTER_TYPE` or, for a field that is a struct of its own, an
 *   `AST_NODE_TYPE_STRUCT_TYPE`. The name of the
 *   field is in the payload. It is a child of an `AST_NODE_TYPE_STRUCT_TYPE`.
 * - `AST_NODE_TYPE_FIELD_ACCESS`: one child, the expression whose field is read, such as `p` in
 *   `p.age`, or `p^` in `p^.age`. The name of the field is in the payload. It is an expression,
 *   and it can be the target of an assignment. Like a subscript it is made after its child.
 * - `AST_NODE_TYPE_FIELD_INITIALIZER`: one child, the value of the field, which is an expression
 *   or, for a field that is a struct, an `AST_NODE_TYPE_STRUCT_LITERAL` without a name. The name of
 *   the field is in the payload. It is a child of an `AST_NODE_TYPE_STRUCT_LITERAL`.
 * - `AST_NODE_TYPE_FOR_STATEMENT`: two children, an `AST_NODE_TYPE_RANGE` and the
 *   `AST_NODE_TYPE_BLOCK` that runs once for each number of the range, in that order. Instead of
 *   the range the first child can be an expression, the array or array slice that the block runs
 *   once for each element of. The name of the iterator is in the payload. The iterator is a
 *   variable of type `Int` that exists only in the block, and it cannot be assigned to. The nodes
 *   of the range come before the block, so the iterator is not in scope in the range. It is a
 *   statement of a procedure body.
 * - `AST_NODE_TYPE_FOREIGN_BODY`: no children. The `...` that takes the place of the statements of a
 *   procedure that is declared in a `foreign js ->` block: its code is not in the program but is
 *   provided from outside. It is the only child after the parameters of such an
 *   `AST_NODE_TYPE_PROCEDURE`, see `ast_procedure_is_foreign`.
 * - `AST_NODE_TYPE_FOREIGN_IMPORT`: no children. `import foreign js "functions"`: the program
 *   gets the foreign procedures that its `foreign js ->` blocks declare from the JavaScript object
 *   that has the name between the quotes, which is in the payload. `js` is the only target
 *   there is, so the node does not store it. It is a child of the `AST_NODE_TYPE_MODULE`, and not
 *   a definition: it has no name.
 * - `AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION`: a global variable, which is defined at file scope.
 *   Its children are one `AST_NODE_TYPE_ATTRIBUTE` for each attribute of its attribute list, in the
 *   order of the source, then the type, an `AST_NODE_TYPE_TYPE_NAME` or an
 *   `AST_NODE_TYPE_ARRAY_TYPE`, when the definition gives one, and last the initial value, which is
 *   an `AST_NODE_TYPE_INTEGER_LITERAL`, an `AST_NODE_TYPE_BOOL_LITERAL` or an
 *   `AST_NODE_TYPE_STRING_LITERAL`, and never another expression. The name is in the payload. It is
 *   a child of the `AST_NODE_TYPE_MODULE`, between the constant definitions.
 * - `AST_NODE_TYPE_IDENTIFIER`: no children. It is a use of a name, such as a parameter.
 * - `AST_NODE_TYPE_IF_STATEMENT`: a chain of `if`, any number of `else if` and an optional `else`,
 *   as the children in the order of the source: a condition, which is any expression (the target
 *   requires it to be a `Bool`), and the `AST_NODE_TYPE_BLOCK` that runs when it holds, for the
 *   `if` and for every `else if`, and then, when `has_else` is set in the payload, the last child
 *   is the `AST_NODE_TYPE_BLOCK` of the `else`.
 * - `AST_NODE_TYPE_INTEGER_LITERAL`: no children.
 * - `AST_NODE_TYPE_LABEL`: no children. The name of a label, without the `@` that it is written
 *   with, in the payload. It is the first child of an `AST_NODE_TYPE_SCOPE_STATEMENT` that has a
 *   label, which declares it, and the only child of an `AST_NODE_TYPE_BREAK_STATEMENT` that names
 *   one, which uses it. Labels are not names of variables, so the analysis does not bind them.
 * - `AST_NODE_TYPE_LABELED_STATEMENT`: one child, the `AST_NODE_TYPE_IF_STATEMENT`,
 *   `AST_NODE_TYPE_FOR_STATEMENT` or `AST_NODE_TYPE_LOOP_STATEMENT` that the label in the payload
 *   is given to, written `@name if ...` or `@name for ...`. It takes the place of that statement in
 *   its block, and the node of the statement comes right after it. A label of a scope statement is
 *   its child instead. It is a statement of a procedure body.
 * - `AST_NODE_TYPE_LOOP_STATEMENT`: one child, the `AST_NODE_TYPE_BLOCK` that runs again and again
 *   until a `break` or a `return` leaves it. It is a statement of a procedure body.
 * - `AST_NODE_TYPE_MODULE`: one child per top-level definition, which is an
 *   `AST_NODE_TYPE_CONSTANT_DEFINITION` or an `AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION`, and one
 *   per `import foreign`, which is an `AST_NODE_TYPE_FOREIGN_IMPORT`. A procedure that a
 *   `foreign js ->` block declares is a constant definition of the module like any other, so the
 *   block itself has no node. It is the root of the tree.
 * - `AST_NODE_TYPE_MULTIPLICATION`: two children, the left and the right operand, in that order.
 * - `AST_NODE_TYPE_MULTIPLICATION_ASSIGNMENT`: two children, the `AST_NODE_TYPE_IDENTIFIER` of the
 *   variable that gets the product, and the factor that it is multiplied by, in that order. It
 *   is a statement of a procedure body.
 * - `AST_NODE_TYPE_NIL_LITERAL`: no children. `nil`, the null pointer, which is a pointer of
 *   whatever pointer type the place that it is given to has.
 * - `AST_NODE_TYPE_NOT`: one child, the operand. It is an expression of type `Bool` and the target
 *   requires the operand to be `Bool`. It binds looser than a comparison, so `not a == b` is
 *   `not (a == b)`.
 * - `AST_NODE_TYPE_OR`: two children, the left and the right operand, in that order. It is an
 *   expression of type `Bool` with the lowest precedence of the operators, and the target
 *   requires both operands to be `Bool`.
 * - `AST_NODE_TYPE_PARAMETER`: one child, its type, which is a `AST_NODE_TYPE_TYPE_NAME`, an
 *   `AST_NODE_TYPE_POINTER_TYPE` or an `AST_NODE_TYPE_ARRAY_TYPE`.
 * - `AST_NODE_TYPE_POINTER_TYPE`: one child, the type that is pointed to, which is an
 *   `AST_NODE_TYPE_TYPE_NAME` or another `AST_NODE_TYPE_POINTER_TYPE`, so `^^Int` is a pointer type
 *   in a pointer type. It is a type like `AST_NODE_TYPE_TYPE_NAME`, found wherever a type is, and
 *   the type of the elements of an `AST_NODE_TYPE_ARRAY_TYPE` can be one.
 * - `AST_NODE_TYPE_PROCEDURE`: first one child per parameter, then one child per statement of the
 *   body, which are variable definitions, assignments, if statements, loops, jumps and return
 *   statements. A foreign procedure has one `AST_NODE_TYPE_FOREIGN_BODY` instead of statements. The
 *   return type is in the payload.
 * - `AST_NODE_TYPE_PROCEDURE_TYPE`: one child per parameter, in the order of the source, each one a
 *   type: an `AST_NODE_TYPE_TYPE_NAME` or an `AST_NODE_TYPE_POINTER_TYPE`, and never a procedure
 *   type, an array or an array slice. It is the `proc(Int, Int) Int` of `^proc(Int, Int) Int`, the
 *   type of a pointer to a procedure, and so it is always the end of the carets of an
 *   `AST_NODE_TYPE_POINTER_TYPE`: the first caret is the pointer, and each one more is a pointer to
 *   that. The parameters have no names, so they are types and not `AST_NODE_TYPE_PARAMETER`
 *   nodes. The return type is in the payload, like that of an `AST_NODE_TYPE_PROCEDURE`, except
 *   that a procedure type that returns nothing has the keyword `proc` there instead, because the
 *   node has no name that an error about it could point at and a return type is never a keyword:
 *   read the return type with `ast_procedure_return_type`. It is a type like
 *   `AST_NODE_TYPE_TYPE_NAME`, found wherever a type is, and the type of the elements of an
 *   `AST_NODE_TYPE_ARRAY_TYPE` can be one.
 * - `AST_NODE_TYPE_RANGE`: two children, the first number and the end of the range, in that order,
 *   each one an expression (the target requires them to be `Int`). Whether the end is part of the
 *   range is in the payload. It is the child of an `AST_NODE_TYPE_FOR_STATEMENT`, and not an
 *   expression itself.
 * - `AST_NODE_TYPE_RETURN_STATEMENT`: one child, the returned value.
 * - `AST_NODE_TYPE_SCOPE_STATEMENT`: an optional `AST_NODE_TYPE_LABEL`, and then the
 *   `AST_NODE_TYPE_BLOCK` that runs once, in a scope of its own. A `break` inside it leaves it.
 *   It is a statement of a procedure body.
 * - `AST_NODE_TYPE_SLICE`: three children, the array or array slice that is sliced, the first index
 *   and the end index, in that order, each one an expression. `a[1:4]` has the elements of `a` from
 *   the one at index 1 up to the one at index 3. It is an expression, and it can be the target of
 *   an `AST_NODE_TYPE_ASSIGNMENT`.
 * - `AST_NODE_TYPE_STRING_LITERAL`: no children.
 * - `AST_NODE_TYPE_STRUCT_LITERAL`: one `AST_NODE_TYPE_FIELD_INITIALIZER` for each field that is
 *   given a value, in the order of the source, which is not necessarily the order of the fields of
 *   the struct. `Person { age = 30 }` is a struct literal. The name of the struct is in the
 *   payload, with `bytes` NULL for the literal `{ ... }` of a field that is a struct, which gets
 *   its type from the field. It is an expression.
 * - `AST_NODE_TYPE_STRUCT_TYPE`: one `AST_NODE_TYPE_FIELD` for each field, in the order of the
 *   source. It is the value of an `AST_NODE_TYPE_CONSTANT_DEFINITION` that defines a struct, or the
 *   type of a field.
 * - `AST_NODE_TYPE_SUBSCRIPT`: two children, the array or array slice and the index of the element,
 *   in that order, each one an expression. `a[2]` is the element at index 2 of `a`. It is an
 *   expression, and it can be the target of an assignment.
 * - `AST_NODE_TYPE_SUBTRACTION`: two children, the minuend and the subtrahend, in that order.
 * - `AST_NODE_TYPE_SUBTRACTION_ASSIGNMENT`: two children, the `AST_NODE_TYPE_IDENTIFIER` of the
 *   variable that gets the difference, and the value that is subtracted from it, in that order.
 *   It is a statement of a procedure body.
 * - `AST_NODE_TYPE_TYPE_NAME`: no children.
 * - `AST_NODE_TYPE_VARIABLE_DEFINITION`: the initial value, preceded by the type, an
 *   `AST_NODE_TYPE_TYPE_NAME`, an `AST_NODE_TYPE_POINTER_TYPE` or an `AST_NODE_TYPE_ARRAY_TYPE`,
 *   when the definition gives one. A
 *   definition that gives a type can leave the value out, and then the type is its only child and
 *   the variable gets the default value of the type.
 *   Without it the type is deduced from the value, so the value is the only child. The name is in
 *   the payload.
 */
typedef struct {
    Ast_Node_Type node_type;
    Ast_Node_Index first_child;
    Ast_Node_Index last_child;
    Ast_Node_Index next_sibling;
    // This union is named, because unnamed unions are C11 and the project uses C99.
    // Only the member that matches `node_type` is meaningful.
    union {
        struct {
            // Only the member of the kind `AST_ARRAY_LENGTH_FIXED` is a length.
            int64_t length;
            Ast_Array_Length_Kind length_kind;
        } array_type;
        struct {
            // Views the source text, so it is valid only as long as the source is.
            Str name;
        } constant_definition;
        struct {
            // The name between the parentheses, as a view of the source text, like the other names.
            Str name;
        } attribute;
        struct {
            // Views the source text, like the other names.
            Str name;
        } global_variable_definition;
        struct {
            // Views the source text, like the other names.
            Str name;
        } variable_definition;
        struct {
            // The index after the last node that is in the block. The nodes of a block are the
            // ones from its own index up to this, because the parser makes the nodes in the order
            // of the source.
            Ast_Node_Index end;
        } block;
        struct {
            bool value;
        } bool_literal;
        struct {
            // Views the source text, like the other names.
            Str name;
        } call;
        struct {
            Ast_Comparison_Operator operator;
        } comparison;
        struct {
            // The name of the type of the numbers of the members, as a view of the source text.
            Str value_type;
        } enum_type;
        struct {
            // Views the source text, like the other names. Without the dot.
            Str name;
        } enum_literal;
        struct {
            // Views the source text, like the other names.
            Str name;
        } enum_member;
        struct {
            // Views the source text, like the other names.
            Str name;
        } field;
        struct {
            // Views the source text, like the other names.
            Str name;
        } field_access;
        struct {
            // Views the source text, like the other names.
            Str name;
        } field_initializer;
        struct {
            // Views the source text, like the other names.
            Str name;
        } for_statement;
        struct {
            // The bytes between the quotes, as a view of the source text: the name of the object
            // that the foreign procedures are properties of. It is valid UTF-8, and `bytes` is
            // never NULL.
            Str module_name;
        } foreign_import;
        struct {
            // Views the source text. `bytes` is NULL for a literal that has no name.
            Str name;
        } struct_literal;
        struct {
            // The word `break` or `continue`, as a view of the source text, so that an error can
            // point at it.
            Str keyword;
        } jump;
        struct {
            bool has_else;
        } if_statement;
        struct {
            // Whether the end is a number of the range: `..<=` is inclusive, `..<` is not.
            bool is_inclusive;
        } range;
        struct {
            // The return type, as a view of the source text from its first token to its last: a
            // name, or a name with the carets of a pointer type before it, such as `^Int`. Empty,
            // with `bytes` NULL, when the procedure does not declare one. A return type is never
            // an array or an array slice, so it is a name with carets before it; when types get
            // more structure, this should become a child node.
            Str return_type;
        } procedure;
        struct {
            // Views the source text, like the other names.
            Str name;
        } identifier;
        struct {
            int64_t value;
        } integer_literal;
        struct {
            // Views the source text, like the other names. Without the `@`.
            Str name;
        } label;
        struct {
            Str name;
        } parameter;
        struct {
            // The bytes between the quotes, as a view of the source text, valid UTF-8, and never
            // with `bytes` NULL.
            Str text;
        } string_literal;
        struct {
            Str name;
        } type_name;
    } payload;
} Ast_Node;

// Four 4-byte fields, followed by the 16-byte payload.
STATIC_ASSERT(sizeof(Ast_Node) == 32, ast_node_size_is_32_bytes);

/**
 * Returns whether nodes of `node_type` are assignments: statements that change the value of a
 * variable, such as `x += 1`. They all have the same shape, the variable and then the value, see
 * the children of each type above.
 */
bool ast_node_type_is_assignment(Ast_Node_Type node_type);

/**
 * Returns the return type of `procedure`, which must be an `AST_NODE_TYPE_PROCEDURE` or an
 * `AST_NODE_TYPE_PROCEDURE_TYPE`, as the text of the source, or a `Str` whose `bytes` is NULL when
 * it returns nothing. For a procedure type that is not what the payload holds, see above.
 */
Str ast_procedure_return_type(Ast_Node const *procedure);

/**
 * Returns whether `procedure`, an `AST_NODE_TYPE_PROCEDURE`, is declared in a `foreign js ->` block:
 * it has no statements but the `...` that stands for its body, which is the last child.
 */
bool ast_procedure_is_foreign(Ast_Node const *nodes, Ast_Node_Index procedure);

/**
 * Returns the name of `definition`, which must be an `AST_NODE_TYPE_CONSTANT_DEFINITION` or an
 * `AST_NODE_TYPE_GLOBAL_VARIABLE_DEFINITION`: the children of the module, which share one scope.
 */
Str ast_definition_name(Ast_Node const *definition);

/**
 * Returns the index after the last node of the procedure at index `procedure`, which is the index
 * of the next definition of the module, or `node_count` for the last one. The nodes of a procedure,
 * including the ones of its nested blocks, are the ones after its own, because the parser makes
 * the nodes in the order of the source.
 */
Ast_Node_Index ast_procedure_end(Ast_Node const *nodes, size_t node_count,
                                 Ast_Node_Index procedure);

/**
 * Prints the tree below `root`, one node per line, indented by depth. `nodes` holds `Ast_Node`
 * elements.
 */
void ast_print(Array_Slice nodes, Ast_Node_Index root);

#endif
