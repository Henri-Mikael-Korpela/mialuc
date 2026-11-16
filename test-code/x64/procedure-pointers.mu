// Pointers to procedures: the name of a procedure that is not called is a pointer to it, which a
// variable, a parameter, a field and an element can hold and which is called like a procedure.
// Every procedure with the same types of parameters and result is of one type, and a pointer that
// is `nil` traps when it is called. The results are the ones that the language specification
// gives. The exit code of the program is the number of wrong results, 0.

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

add :: proc(a: Int, b: Int) Int ->
    return a + b

subtract :: proc(a: Int, b: Int) Int ->
    return a - b

double :: proc(n: Int) Int ->
    return n * 2

create_pointer_to_procedure :: proc() Int ->
    proc_p : ^proc(Int, Int) Int = add
    return proc_p(5, 15)

create_pointer_to_procedure2 :: proc() Int ->
    proc_p := add
    proc_p = subtract
    return proc_p(5, 15)

apply_twice :: proc(f: ^proc(Int) Int, n: Int) Int ->
    return f(f(n))

Operation :: struct ->
    apply: ^proc(Int, Int) Int
    left: Int

/// The pointers in an array and in a struct are stored in a variable before they are called.
operations_table :: proc() Int ->
    operations := [const]^proc(Int, Int) Int { add, subtract }
    f := operations[1]
    first := f(6, 3)
    operation := Operation { apply = add, left = 40 }
    g := operation.apply
    return first * 100 + g(operation.left, 2)

/// A pointer is nil when it is given no value, and is compared with nil and with other pointers.
compare_pointers :: proc() Int ->
    f : ^proc(Int) Int
    if f not= nil ->
        return 1
    f = double
    g := double
    if f == g and f not= nil ->
        return 0
    return 2

/// A pointer to a variable that holds a pointer to a procedure changes what the variable holds.
change_through_pointer :: proc(target: ^^proc(Int, Int) Int) ->
    target^ = subtract

pointer_to_pointer :: proc() Int ->
    f : ^proc(Int, Int) Int = add
    change_through_pointer(%f)
    return f(10, 4)

/// A foreign procedure is a pointer too: the address that the loader found for it.
call_foreign_through_pointer :: proc() Int ->
    show := puts
    show("puts called through a pointer")
    return 0

main :: proc() Int ->
    wrong := 0
    wrong += check("create_pointer_to_procedure() is %lld", create_pointer_to_procedure(), 20)
    difference := create_pointer_to_procedure2()
    wrong += check("create_pointer_to_procedure2() is %lld", difference, -10)
    wrong += check("apply_twice(double, 5) is %lld", apply_twice(double, 5), 20)
    wrong += check("operations_table() is %lld", operations_table(), 342)
    wrong += check("compare_pointers() is %lld", compare_pointers(), 0)
    wrong += check("pointer_to_pointer() is %lld", pointer_to_pointer(), 6)
    wrong += call_foreign_through_pointer()
    return wrong
