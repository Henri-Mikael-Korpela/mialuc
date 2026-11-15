// Structs, whose fields are one word after the other: struct literals, fields that are read and
// assigned through variables, parameters, elements and pointers, structs inside structs, also
// without a name, structs in arrays, and structs as parameters and results, which are passed as
// their words. The results are the ones that the language specification gives. The exit code of
// the program is the number of wrong results, 0.

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

Person :: struct ->
    age: Int
    initial: Byte
    is_member: Bool

Point :: struct ->
    x: Int
    y: Int

Line :: struct ->
    start: Point
    end: Point

Info :: struct ->
    age: Int
    address: struct ->
        house_no: Int
        floor: Int

Node :: struct ->
    value: Int
    next: ^Node

birthday :: proc(p: ^Person) ->
    p.age += 1

struct_as_pointer_parameter :: proc() Int ->
    person := Person { age = 40, initial = 69, is_member = true }
    birthday(%person)
    birthday(%person)
    return person.age

/// A struct is passed as its words, and a field of a parameter is read where it is kept.
length_squared :: proc(line: Line) Int ->
    dx := line.end.x - line.start.x
    dy := line.end.y - line.start.y
    return dx * dx + dy * dy

/// A struct that is returned goes to the place that the caller made for it.
midpoint :: proc(line: Line) Point ->
    x := line.start.x + line.end.x
    y := line.start.y + line.end.y
    return Point { x = x / 2, y = y / 2 }

/// A struct is copied by a definition and an assignment, and by following a pointer.
copies_are_separate :: proc() Int ->
    a := Point { x = 1, y = 2 }
    b := a
    b.x = 10
    p := %a
    c := p^
    p^ = Point { x = 7, y = 8 }
    return a.x * 1000 + b.x * 10 + c.x

create_nested_struct_object :: proc() Int ->
    info := Info {
        age = 30,
        address = {
            house_no = 123,
            floor = 4,
        },
    }
    info.address.floor += 1
    return info.address.house_no * 10 + info.address.floor

follow_pointers_in_structs :: proc() Int ->
    last := Node { value = 3, next = nil }
    middle := Node { value = 2, next = %last }
    first := Node { value = 1, next = %middle }
    return first.next.value * 10 + first.next.next.value

sum_list :: proc(first: ^Node) Int ->
    total := 0
    current := first
    for ->
        if current == nil ->
            break
        total += current.value
        current = current.next
    return total

/// Structs in an array, which take their words each, and a field of an element.
oldest :: proc(people: []Person) Int ->
    oldest_age := 0
    for person in people ->
        if person.age > oldest_age ->
            oldest_age = person.age
    return oldest_age

structs_in_arrays :: proc() Int ->
    people := [const]Person {
        Person { age = 30, initial = 66, is_member = false },
        Person { age = 25, initial = 65, is_member = true },
        Person { age = 40, initial = 67, is_member = false },
    }
    people[1].age += 50
    members := 0
    for person in people ->
        if person.is_member ->
            members += 1
    return oldest(people) * 10 + members

/// A literal with empty braces gives each field its default value, and so does a variable with a
/// type and no value.
defaults :: proc() Int ->
    empty := Point {}
    nothing : Person
    if nothing.is_member ->
        return 1
    return empty.x + empty.y + nothing.age + byte_to_int(nothing.initial)

main :: proc() Int ->
    wrong := 0
    wrong += check("struct_as_pointer_parameter() is %lld", struct_as_pointer_parameter(), 42)
    line := Line { start = Point { x = 1, y = 2 }, end = Point { x = 4, y = 6 } }
    wrong += check("length_squared(line) is %lld", length_squared(line), 25)
    middle := midpoint(line)
    wrong += check("midpoint(line).x is %lld", middle.x, 2)
    wrong += check("midpoint(line).y is %lld", middle.y, 4)
    wrong += check("copies_are_separate() is %lld", copies_are_separate(), 7101)
    wrong += check("create_nested_struct_object() is %lld", create_nested_struct_object(), 1235)
    wrong += check("follow_pointers_in_structs() is %lld", follow_pointers_in_structs(), 23)
    last := Node { value = 30, next = nil }
    first := Node { value = 12, next = %last }
    wrong += check("sum_list of the two nodes is %lld", sum_list(%first), 42)
    wrong += check("structs_in_arrays() is %lld", structs_in_arrays(), 751)
    wrong += check("defaults() is %lld", defaults(), 0)
    return wrong
