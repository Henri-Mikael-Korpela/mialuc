// Pointers to procedures: assigning a procedure to a variable, passing it to another
// procedure, keeping it in arrays and structs, and the traps when it is `nil`.

add :: proc(a: Int, b: Int) Int ->
    return a + b

subtract :: proc(a: Int, b: Int) Int ->
    return a - b

multiply :: proc(a: Int, b: Int) Int ->
    return a * b

double :: proc(n: Int) Int ->
    return n * 2

triple :: proc(n: Int) Int ->
    return n * 3

forty_two :: proc() Int ->
    return 42

is_even :: proc(n: Int) Bool ->
    return n / 2 * 2 == n

greet :: proc(name: Str) Str ->
    return name

increment :: proc(p: ^Int) ->
    p^ += 1

decrement :: proc(p: ^Int) ->
    p^ -= 1

/// The name of a procedure that is not called is a pointer to it.
pointer_with_deduced_type :: proc() Int ->
    f := double
    return f(21)

pointer_with_written_type :: proc() Int ->
    f : ^proc(Int) Int = triple
    return f(14)

pointer_to_procedure_without_parameters :: proc() Int ->
    f : ^proc() Int = forty_two
    return f() + 1

two_parameters :: proc() Int ->
    f := subtract
    return f(50, 8)

make_pointer_point_elsewhere :: proc() Int ->
    f := double
    first := f(5)
    f = triple
    second := f(5)
    return first * 100 + second

choose_by_condition :: proc(use_double: Bool) Int ->
    f : ^proc(Int) Int = triple
    if use_double ->
        f = double
    return f(7)

choose_in_a_loop :: proc() Int ->
    f := double
    total := 0
    for i in 1..<4 ->
        if i == 2 ->
            f = triple
        else ->
            f = double
        total += f(i)
    return total

/// Pointers to procedures with the same parameter types and the same result type can be assigned to each other.
the_same_signature_is_the_same_type :: proc() Int ->
    f : ^proc(Int, Int) Int = add
    f = subtract
    g : ^proc(Int, Int) Int = multiply
    f = g
    return f(6, 7)

/// Calls the procedure that it is given, which makes it a higher-order procedure.
apply :: proc(f: ^proc(Int) Int, n: Int) Int ->
    return f(n)

apply_twice :: proc(f: ^proc(Int) Int, n: Int) Int ->
    return f(f(n))

pass_procedure_as_argument :: proc() Int ->
    return apply(double, 10) * 100 + apply(triple, 10)

pass_procedure_pointer_on :: proc() Int ->
    f := triple
    return apply_twice(f, 2)

combine :: proc(operation: ^proc(Int, Int) Int, a: Int, b: Int) Int ->
    return operation(a, b)

pass_a_procedure_of_two_parameters :: proc() Int ->
    return combine(add, 20, 22) * 100 + combine(multiply, 6, 7)

call_parameter_in_a_condition :: proc(test: ^proc(Int) Bool, n: Int) Int ->
    if test(n) ->
        return 1
    return 0

procedure_returning_bool :: proc() Int ->
    even := call_parameter_in_a_condition(is_even, 10)
    odd := call_parameter_in_a_condition(is_even, 7)
    return even * 10 + odd

procedure_returning_str :: proc() Str ->
    f : ^proc(Str) Str = greet
    return f("Hello")

procedure_without_result :: proc() Int ->
    x := 10
    f : ^proc(^Int) = increment
    f(%x)
    f(%x)
    f = decrement
    f(%x)
    return x

call_in_an_expression :: proc() Int ->
    f := double
    g := triple
    return f(g(2)) + f(1) * g(1)

pointers_in_arguments_of_calls :: proc() Int ->
    f := add
    g := double
    return f(g(1), f(g(2), g(3)))

/// A pointer to a procedure can be compared with `nil`.
compare_with_nil :: proc() Int ->
    f : ^proc(Int) Int = nil
    g := double
    result := 0
    if f == nil ->
        result += 1
    if g not= nil ->
        result += 10
    f = g
    if f not= nil ->
        result += 100
    return result

default_is_nil :: proc() Bool ->
    f : ^proc(Int) Int
    return f == nil

compare_pointers :: proc() Int ->
    f := double
    g := double
    h := triple
    result := 0
    if f == g ->
        result += 1
    if f not= h ->
        result += 10
    return result

/// Calling `nil` traps, which stops the program.
call_nil :: proc() Int ->
    f : ^proc(Int) Int = nil
    return f(1)

/// A pointer to a procedure is `nil` until it is assigned.
call_default :: proc() Int ->
    f : ^proc(Int) Int
    return f(1)

nil_as_argument :: proc() Int ->
    return apply(nil, 1)

sum_with_callback :: proc(values: []Int, f: ^proc(Int) Int) Int ->
    total := 0
    for value in values ->
        total += f(value)
    return total

procedure_with_a_slice :: proc() Int ->
    values := [const]Int { 1, 2, 3 }
    return sum_with_callback(values, double) * 100 + sum_with_callback(values, triple)

array_of_procedures :: proc() Int ->
    operations := [const]^proc(Int, Int) Int { add, subtract, multiply }
    total := 0
    for operation in operations ->
        total += operation(6, 3)
    return total

index_array_of_procedures :: proc() Int ->
    operations := [const]^proc(Int, Int) Int { add, subtract, multiply }
    index := 2
    f := operations[index]
    return f(6, 7)

assign_to_element_of_array_of_procedures :: proc() Int ->
    operations := [const]^proc(Int, Int) Int { add, subtract, multiply }
    operations[0] = multiply
    f := operations[0]
    return f(6, 7)

run_all :: proc(functions: []^proc(Int) Int, n: Int) Int ->
    total := 0
    for f in functions ->
        total += f(n)
    return total

slice_of_procedures :: proc() Int ->
    functions := [const]^proc(Int) Int { double, triple, double }
    return run_all(functions, 5) * 100 + run_all(functions[1:3], 5)

/// A struct can have a field that is a pointer to a procedure.
Operation :: struct ->
    name: Str
    apply: ^proc(Int, Int) Int

struct_with_procedure_field :: proc() Int ->
    operation := Operation { name = "add", apply = add }
    f := operation.apply
    return f(40, 2)

assign_procedure_field :: proc() Int ->
    operation := Operation { name = "add", apply = add }
    operation.apply = multiply
    f := operation.apply
    return f(6, 7)

procedure_field_is_nil_by_default :: proc() Bool ->
    operation := Operation { name = "none" }
    return operation.apply == nil

use_struct_in_a_procedure :: proc(operation: Operation) Int ->
    f := operation.apply
    return f(8, 2)

struct_as_argument :: proc() Int ->
    return use_struct_in_a_procedure(Operation { name = "subtract", apply = subtract })

/// A pointer to a variable that holds a pointer to a procedure.
pointer_to_pointer_to_procedure :: proc() Int ->
    f : ^proc(Int) Int = double
    p := %f
    first := p^
    p^ = triple
    second := f
    return first(5) * 100 + second(5)

replace_procedure :: proc(target: ^^proc(Int) Int) ->
    target^ = triple

replace_through_a_pointer :: proc() Int ->
    f : ^proc(Int) Int = double
    replace_procedure(%f)
    return f(5)

call_through_a_pointer_to_pointer :: proc() Int ->
    f : ^proc(Int) Int = double
    p := %f
    g := p^
    return g(8)

recursion_with_procedure_pointer :: proc(f: ^proc(Int) Int, n: Int) Int ->
    if n == 0 ->
        return 0
    return f(n) + recursion_with_procedure_pointer(f, n - 1)

recurse :: proc() Int ->
    return recursion_with_procedure_pointer(triple, 4)

variable_defined_from_a_parameter :: proc(f: ^proc(Int) Int) Int ->
    g := f
    return g(4) + f(5)

use_variable_defined_from_a_parameter :: proc() Int ->
    return variable_defined_from_a_parameter(double)
