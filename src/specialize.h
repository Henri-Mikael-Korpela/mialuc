#ifndef MIALU_SPECIALIZE_H
#define MIALU_SPECIALIZE_H

#include <stdbool.h>
#include <stdint.h>

#include <mialu/array_slice.h>
#include <mialu/ast.h>
#include <mialu/semantic.h>
#include <mialu/str.h>

/**
 * Specialization makes a program without polymorphic definitions of one that has them, so that the
 * targets never meet a type parameter, see `ast_definition_is_polymorphic`. Each call of a
 * polymorphic procedure gets a copy of the procedure, a specialization, in which every type
 * parameter is the type that the call gives it, and each use of a polymorphic struct with type
 * arguments, such as `Pair(Int)`, gets a copy of the struct in which the type parameters are the
 * arguments. Calls and uses that give the same types share one copy. The name of a copy is the name
 * of the definition and the types between parentheses, such as `add(Int)` and `Pair(Int)`, which no
 * name of the program can be, so the copies are definitions of the program like any other, which
 * the analysis and the targets know nothing more about.
 *
 * The types that a call gives are deduced from its arguments: the type of each argument, which is
 * worked out as text, is matched with the type of its parameter, in which a type parameter stands
 * for any one type. `sum(values)` with `values: []Int` and `sum :: proc(values: []$T) T` gives `T`
 * the type `Int`. A call can give the type parameters that are given as types, `$T: type_id`, as
 * types instead, when it gives every parameter, as in `zero(Int)`. The type of an argument that is
 * a call of a polymorphic procedure is only known once that call is specialized, and the copies of
 * the procedures have calls of their own, so the work is done in rounds: each round finds what is
 * new, makes a new tree with the copies, and analyzes it, until nothing is new.
 */

// Alphabetical, because nothing depends on the order.
typedef enum {
    // The call of a polymorphic procedure has neither as many arguments as the procedure has
    // parameters, nor as many as it has parameters that are not types. The subject is the name of
    // the call.
    SPECIALIZE_STATUS_ARGUMENT_COUNT_MISMATCH,
    // An argument whose type is not of the form of the type of its parameter, such as an `Int`
    // for `[]$T`. The subject is the name of the call.
    SPECIALIZE_STATUS_ARGUMENT_TYPE_MISMATCH,
    // The program needs more copies, or a larger tree, than the compiler makes, which a procedure
    // that calls itself with ever larger types does. The subject is the name of the definition
    // whose copy did not fit, and `number` the limit.
    SPECIALIZE_STATUS_LIMIT,
    SPECIALIZE_STATUS_OK,
    // The analysis of the program with its copies found an error, which `semantic` has.
    SPECIALIZE_STATUS_SEMANTIC,
    // A polymorphic procedure is used as a value, which has no type, because it is many
    // procedures. The subject is its name.
    SPECIALIZE_STATUS_PROCEDURE_NOT_CALLED,
    // A struct that has no type parameters is given type arguments. The subject is the type.
    SPECIALIZE_STATUS_STRUCT_NOT_POLYMORPHIC,
    // A polymorphic struct is given another number of type arguments than it has type parameters,
    // which can be none. The subject is the type, and `number` the number of type parameters.
    SPECIALIZE_STATUS_STRUCT_TYPE_ARGUMENT_COUNT_MISMATCH,
    // The type that a type parameter is given is deduced as one type from one argument and as
    // another from another. The subject is the name of the call, `detail` the type parameter, and
    // `other` and `target` the two types.
    SPECIALIZE_STATUS_TYPE_PARAMETER_CONFLICT,
    // A type parameter is declared twice by one procedure or struct. The subject is the second.
    SPECIALIZE_STATUS_TYPE_PARAMETER_DUPLICATE,
    // A name with `$` before it outside of the type of a parameter of a procedure, which is where
    // a type parameter is declared. The subject is the name.
    SPECIALIZE_STATUS_TYPE_PARAMETER_MISPLACED,
    // The type of a type parameter cannot be deduced from the arguments of a call, because no
    // argument says it, or an argument has no type that is known. The subject is the name of the
    // call, and `detail` the type parameter.
    SPECIALIZE_STATUS_TYPE_PARAMETER_UNDEDUCED,
} Specialize_Status;

/**
 * Where a text that the specialization wrote came from: `text` is in the memory of the texts, and
 * `source` is the text of a source that it is made of, which an error about the text points at,
 * see `specialize_source_of`.
 */
typedef struct {
    Str text;
    Str source;
} Specialize_Text;

/**
 * The texts that the specialization writes: the names of the copies, the names that their calls
 * are given, and the types that are written as text with their type parameters replaced, all in
 * `bytes`, which has room for `size_bytes`, of which `length` are used. `entries` has room for
 * `entry_count_max` of `Specialize_Text`, one for each text, of which `entry_count` are used. The
 * texts must live as long as the tree that has them, so the memory is the caller's.
 */
typedef struct {
    char *bytes;
    Specialize_Text *entries;
    size_t size_bytes;
    size_t length;
    size_t entry_count;
    size_t entry_count_max;
} Specialize_Texts;

/**
 * The memory that the specialization works in, which the caller allocates once. Each holds as many
 * elements as the node buffer that the program was parsed into: `nodes` and `spare_nodes` are
 * `Ast_Node` elements, which the trees of the rounds alternate between, `origins` and
 * `spare_origins` are `Ast_Node_Index` elements, one for each node of the tree in the buffer of the
 * same name, `bindings` and `constants` are the `Semantic_Binding` and `Constant_Value` elements of
 * the analysis of each round, `text` holds the text of the strings that `++` makes in it,
 * `positions`, `ends` and `marks` are `Ast_Node_Index`, `Ast_Node_Index` and `uint32_t` elements,
 * one more than the nodes, for the copying of a tree, and `rewrites` holds `Str` elements, one for
 * each node.
 */
typedef struct {
    Array_Slice nodes;
    Array_Slice spare_nodes;
    Array_Slice origins;
    Array_Slice spare_origins;
    Array_Slice bindings;
    Array_Slice constants;
    Array_Slice text;
    Array_Slice positions;
    Array_Slice ends;
    Array_Slice marks;
    Array_Slice rewrites;
} Specialize_Buffers;

/**
 * What the specialization made. On `SPECIALIZE_STATUS_OK`, `nodes` is the tree without polymorphic
 * definitions, whose module is `root` and whose first node of the program is `imported_end`, and
 * `semantic` is its analysis, which the targets build from. `origins` holds an `Ast_Node_Index` for
 * each node of it: the node of the tree that was given that it is a copy of, or
 * `AST_NODE_INDEX_NONE` for a node of a specialization, which is a node of a polymorphic
 * definition. A program without polymorphic definitions is the tree that was given, with its own
 * analysis, and its nodes are their own origins. Otherwise the result says what is wrong, see
 * `Specialize_Status`: `semantic` has the error of the analysis of a round, and the others have
 * `subject`, a view of a source or of `texts`, see `specialize_source_of`, and the other texts and
 * `number` that the status says.
 */
typedef struct {
    Array_Slice nodes;
    Array_Slice origins;
    Semantic_Result semantic;
    Str subject;
    Str detail;
    Str other;
    Str target;
    int64_t number;
    Ast_Node_Index root;
    size_t imported_end;
    Specialize_Status status;
} Specialize_Result;

/**
 * Makes the specializations of the program whose tree is `nodes`, with its module at `root` and its
 * first node of the program at `imported_end`, which `analyzed` is the successful analysis of. The
 * tree that is given is not changed, so that it can still be shown as it is written, see
 * `mialuc dump-types`. The memory is `buffers`, and the texts that are written go to `texts`.
 */
Specialize_Result specialize(Array_Slice nodes, Ast_Node_Index root, size_t imported_end,
                             Semantic_Result analyzed, Specialize_Buffers buffers,
                             Specialize_Texts *texts);

/**
 * Returns the text of a source that `subject` stands for: `subject` itself when it is not one of
 * the texts that the specialization wrote, and otherwise the source that the text is made of, such
 * as the name of the call in the source for the name that the call of a copy is given, so that an
 * error about it points at the source. `texts` can be NULL.
 */
Str specialize_source_of(Specialize_Texts const *texts, Str subject);

#endif
