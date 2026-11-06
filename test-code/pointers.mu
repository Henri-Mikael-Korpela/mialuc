pointer_with_implicit_type :: proc() Int ->
    x := 4
    p := %x
    p^ += 1
    return x

pointer_with_explicit_type :: proc() Int ->
    x := 4
    p : ^Int = %x
    p^ += 1
    return x

read_through_pointer :: proc() Int ->
    x := 20
    p := %x
    return p^ + 1

assign_through_pointer :: proc() Int ->
    x := 1
    p := %x
    p^ = 42
    return x

see_changes_of_variable :: proc() Int ->
    x := 1
    p := %x
    x = 7
    before := p^
    x += 3
    return before * 100 + p^

operators_through_pointer :: proc() Int ->
    x := 10
    p := %x
    p^ += 5
    p^ *= 2
    p^ -= 4
    p^ /= 2
    return x

pointer_to_bool :: proc() Bool ->
    ready := false
    p := %ready
    p^ = true
    return ready

pointer_to_str :: proc() Str ->
    name := "Bob"
    p := %name
    p^ = "Alice"
    return p^

increment :: proc(p: ^Int) ->
    p^ += 1

pointer_as_parameter :: proc() Int ->
    x := 10
    increment(%x)
    increment(%x)
    return x

swap :: proc(a: ^Int, b: ^Int) ->
    temp := a^
    a^ = b^
    b^ = temp

swap_through_pointers :: proc() Int ->
    x := 3
    y := 7
    swap(%x, %y)
    return x * 10 + y

set_flag :: proc(flag: ^Bool) ->
    flag^ = true

flag_through_pointer :: proc() Bool ->
    flag := false
    set_flag(%flag)
    return flag

pass_pointer_on :: proc(p: ^Int) ->
    increment(p)
    increment(p)

pointer_passed_on :: proc() Int ->
    x := 0
    pass_pointer_on(%x)
    return x

is_big :: proc(p: ^Int) Bool ->
    return p^ > 100

condition_through_pointer :: proc() Int ->
    small := 5
    big := 500
    if is_big(%small) ->
        return 1
    if is_big(%big) ->
        return 2
    return 3

sum_to :: proc(n: Int, total: ^Int) ->
    if n > 0 ->
        total^ += n
        sum_to(n - 1, total)

recursion_with_pointer :: proc() Int ->
    total := 0
    sum_to(10, %total)
    return total

pointer_to_variable_in_block :: proc() Int ->
    total := 0
    for i in 1..<=3 ->
        square := i * i
        p := %square
        p^ += 1
        total += square
    return total

pointer_to_pointer :: proc() Int ->
    x := 1
    p := %x
    pp : ^^Int = %p
    pp^^ = 5
    pp^^ += 1
    return x

change_pointer :: proc(pp: ^^Int, target: ^Int) ->
    pp^ = target

change_where_pointer_points :: proc() Int ->
    a := 1
    b := 2
    p := %a
    change_pointer(%p, %b)
    p^ = 20
    return a * 100 + b

choose :: proc(a: ^Int, b: ^Int, first: Bool) ^Int ->
    if first ->
        return a
    return b

return_pointer :: proc() Int ->
    x := 1
    y := 2
    p := choose(%x, %y, false)
    p^ = 50
    return x * 100 + y

Person :: struct ->
    age: Int
    name: Str

read_field_through_pointer :: proc() Int ->
    person := Person { age = 30, name = "Bob" }
    p := %person
    return p^.age

assign_field_through_pointer :: proc() Int ->
    person := Person { age = 30, name = "Bob" }
    p := %person
    p^.age = 31
    p^.age += 1
    return person.age

str_field_through_pointer :: proc() Str ->
    person := Person { age = 30, name = "Bob" }
    p := %person
    p^.name = "Alice"
    return p^.name

assign_whole_struct_through_pointer :: proc() Int ->
    person := Person { age = 30, name = "Bob" }
    p := %person
    p^ = Person { age = 5, name = "Zed" }
    return person.age

read_whole_struct_through_pointer :: proc() Int ->
    person := Person { age = 30, name = "Bob" }
    p := %person
    copy := p^
    copy.age = 99
    return person.age * 100 + copy.age

birthday :: proc(p: ^Person) ->
    p^.age += 1

struct_as_pointer_parameter :: proc() Int ->
    person := Person { age = 40, name = "Eve" }
    birthday(%person)
    birthday(%person)
    return person.age

see_changes_of_struct :: proc() Int ->
    person := Person { age = 1, name = "Bob" }
    p := %person
    person.age = 8
    person.age += 1
    return p^.age

pointer_to_field :: proc() Int ->
    person := Person { age = 30, name = "Bob" }
    age := %person.age
    age^ += 12
    return person.age

Point :: struct ->
    x: Int
    y: Int

Line :: struct ->
    start: Point
    end: Point

pointer_to_nested_field :: proc() Int ->
    line := Line { start = { x = 1, y = 2 }, end = { x = 3, y = 4 } }
    y := %line.end.y
    y^ = 40
    return line.start.x + line.start.y + line.end.x + line.end.y

pointer_to_nested_struct :: proc() Int ->
    line := Line { start = { x = 1, y = 2 }, end = { x = 3, y = 4 } }
    end := %line.end
    end^ = Point { x = 30, y = 40 }
    end^.x += 1
    return line.end.x * 100 + line.end.y

pointer_to_field_through_pointer :: proc() Int ->
    line := Line { start = { x = 1, y = 2 }, end = { x = 3, y = 4 } }
    p := %line
    q := %p^.end.x
    q^ = 33
    return line.end.x

pointer_to_pointer_to_struct :: proc() Int ->
    person := Person { age = 1 }
    p := %person
    pp := %p
    pp^^.age = 77
    return person.age

address_of_dereference :: proc() Int ->
    x := 1
    p := %x
    q := %p^
    q^ = 9
    return x

pointer_to_element :: proc() Int ->
    a := [const]Int { 1, 2, 3 }
    p := %a[1]
    p^ = 20
    p^ += 1
    return a[0] * 10000 + a[1] * 100 + a[2]

pointer_to_slice_element :: proc() Int ->
    a := [const]Int { 1, 2, 3, 4, 5 }
    s := a[1:4]
    p := %s[2]
    p^ = 30
    return a[3]

pointer_to_field_of_element :: proc() Int ->
    people := [const]Person { Person { age = 1 }, Person { age = 2 }, Person { age = 3 } }
    p := %people[1].age
    p^ = 20
    q := %people[2]
    q^.age += 100
    return people[0].age * 10000 + people[1].age * 100 + people[2].age

largest :: proc(values: []Int) ^Int ->
    best := 0
    index := 0
    for value in values ->
        if value > values[best] ->
            best = index
        index += 1
    return %values[best]

double_the_largest :: proc() Int ->
    a := [const]Int { 3, 9, 4 }
    p := largest(a)
    p^ *= 2
    return a[0] * 10000 + a[1] * 100 + a[2]

sum_of_pointed_to :: proc() Int ->
    a := 1
    b := 20
    c := 300
    pointers := [const]^Int { %a, %b, %c }
    total := 0
    for p in pointers ->
        total += p^
    return total

change_all_pointed_to :: proc() Int ->
    a := 1
    b := 2
    pointers := [const]^Int { %a, %b }
    for p in pointers ->
        p^ *= 10
    pointers[0]^ += 5
    return a * 100 + b

add_to_all :: proc(targets: []^Int, amount: Int) ->
    for target in targets ->
        target^ += amount

slice_of_pointers :: proc() Int ->
    a := 1
    b := 2
    targets := [const]^Int { %a, %b }
    add_to_all(targets, 10)
    return a * 100 + b

Node :: struct ->
    value: Int
    next: ^Node

follow_pointers_in_structs :: proc() Int ->
    last := Node { value = 3 }
    middle := Node { value = 2, next = %last }
    first := Node { value = 1, next = %middle }
    return first.next^.value * 10 + first.next^.next^.value

assign_through_pointers_in_structs :: proc() Int ->
    last := Node { value = 3 }
    first := Node { value = 1, next = %last }
    first.next^.value = 30
    first.next^.value += 1
    return last.value

read_null_pointer :: proc() Int ->
    p : ^Int
    return p^

assign_null_pointer :: proc() ->
    p : ^Int
    p^ = 1

follow_null_pointer_in_struct :: proc() Int ->
    node := Node { value = 1 }
    return node.next^.value

nil_pointer_variable :: proc() Bool ->
    p : ^Int = nil
    return p == nil

default_pointer_is_nil :: proc() Bool ->
    p : ^Int
    return p == nil

pointer_is_not_nil :: proc() Bool ->
    x := 1
    p := %x
    return p not= nil

assign_nil_to_pointer :: proc() Int ->
    x := 1
    p := %x
    p = nil
    if p == nil ->
        return x + 10
    return x

compare_pointers :: proc() Int ->
    x := 1
    y := 1
    p := %x
    q := %x
    r := %y
    result := 0
    if p == q ->
        result += 1
    if p == r ->
        result += 10
    if p not= r ->
        result += 100
    return result

read_or_default :: proc(p: ^Int, default: Int) Int ->
    if p == nil ->
        return default
    return p^

nil_as_argument :: proc() Int ->
    x := 7
    return read_or_default(%x, 0) * 10 + read_or_default(nil, 5)

find_negative :: proc(values: []Int) ^Int ->
    index := 0
    for value in values ->
        if value < 0 ->
            return %values[index]
        index += 1
    return nil

return_nil_or_pointer :: proc() Int ->
    found := [const]Int { 3, 0 - 4, 5 }
    missing := [const]Int { 3, 4, 5 }
    p := find_negative(found)
    q := find_negative(missing)
    result := 0
    if p not= nil ->
        p^ = 0
        result += 10
    if q == nil ->
        result += 1
    return result * 100 + found[1]

count_non_nil :: proc() Int ->
    a := 1
    b := 2
    pointers := [const]^Int { %a, nil, %b, nil }
    count := 0
    for p in pointers ->
        if p not= nil ->
            count += 1
    return count

sum_list :: proc(first: ^Node) Int ->
    total := 0
    current := first
    for ->
        if current == nil ->
            break
        total += current^.value
        current = current^.next
    return total

walk_list_until_nil :: proc() Int ->
    third := Node { value = 3, next = nil }
    second := Node { value = 2, next = %third }
    first := Node { value = 1, next = %second }
    return sum_list(%first) * 10 + sum_list(nil)

read_explicit_nil_pointer :: proc() Int ->
    p : ^Int = nil
    return p^
