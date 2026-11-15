// Pointers: `%` takes the address of a variable, an element or what a pointer points to, `^`
// follows a pointer, which traps when it is `nil`, and pointers are compared with `==` and
// `not=`. The results are the ones that the language specification gives. The exit code of the
// program is the number of wrong results, 0.

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

pointer_with_explicit_type :: proc() Int ->
    x := 4
    p : ^Int = %x
    p^ += 1
    return x

see_changes_of_variable :: proc() Int ->
    x := 1
    p := %x
    x = 7
    before := p^
    x += 3
    return before * 100 + p^

swap :: proc(a: ^Int, b: ^Int) ->
    temp := a^
    a^ = b^
    b^ = temp

swap_through_pointers :: proc() Int ->
    x := 3
    y := 7
    swap(%x, %y)
    return x * 10 + y

read_or_default :: proc(p: ^Int, default: Int) Int ->
    if p == nil ->
        return default
    return p^

nil_as_argument :: proc() Int ->
    x := 7
    return read_or_default(%x, 0) * 10 + read_or_default(nil, 5)

choose :: proc(a: ^Int, b: ^Int, first: Bool) ^Int ->
    if first ->
        return a
    return b

/// A pointer to a pointer is followed twice to get to the value, and once to the pointer.
pointer_to_pointer :: proc() Int ->
    x := 1
    y := 2
    p := %x
    pp := %p
    pp^^ += 10
    pp^ = %y
    p^ += 20
    return x * 100 + y

/// The address of an element of an array, and an array of pointers.
pointers_to_elements :: proc() Int ->
    a := [const]Int { 1, 2, 3 }
    second := %a[1]
    second^ = 20
    b := 5
    c := 6
    pointers := [const]^Int { %b, %c, second }
    total := 0
    for p in pointers ->
        total += p^
    pointers[0]^ = 50
    return total * 1000 + a[1] * 10 + b / 10

main :: proc() Int ->
    wrong := 0
    wrong += check("pointer_with_explicit_type() is %lld", pointer_with_explicit_type(), 5)
    wrong += check("see_changes_of_variable() is %lld", see_changes_of_variable(), 710)
    wrong += check("swap_through_pointers() is %lld", swap_through_pointers(), 73)
    wrong += check("nil_as_argument() is %lld", nil_as_argument(), 75)
    x := 1
    y := 2
    chosen := choose(%x, %y, false)
    wrong += check("what choose returns points to %lld", chosen^, 2)
    same := chosen == %y
    other := chosen not= %x
    if same and other ->
        wrong += 0
    else ->
        wrong += 1
    wrong += check("pointer_to_pointer() is %lld", pointer_to_pointer(), 1122)
    wrong += check("pointers_to_elements() is %lld", pointers_to_elements(), 31205)
    return wrong
