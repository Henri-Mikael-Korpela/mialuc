// Structs: struct objects and their fields, nested structs, default values, passing objects
// to procedures by value, and structs in arrays.

/// A struct with two fields. A struct object has all fields, and the ones that are left out are the default.
Person :: struct ->
    age: Int
    name: Str

create_struct_object :: proc() Str ->
    p := Person {
        age = 30,
        name = "Bob",
    }
    return p.name

assign_field_of_struct_object :: proc() Int ->
    p := Person {
        age = 30,
        name = "Bob",
    }
    p.age = 31
    return p.age

late_initialization_of_struct_object :: proc() Str ->
    x := 1
    p : Person
    if x == 1 ->
        p = Person {
            age = 30,
            name = "Bob",
        }
    else ->
        p = Person {
            age = 40,
            name = "Alice",
        }
    return p.name

return_struct_object :: proc() Person ->
    return Person { age = 24, name = "Sam", }

create_array_of_struct_objects :: proc() Int ->
    persons := [const]Person {
        Person { age = 30, name = "Bob", },
        Person { age = 25, name = "Alice", },
        Person { age = 40, name = "Charlie", },
    }
    return persons[0].age

/// A field can be a struct that is written inside its parent, which has no name of its own.
Info :: struct ->
    name: Str
    age: Int
    address: struct ->
        area_name: Str
        house_no: Int
        district: Str

create_nested_struct_object :: proc() Str ->
    info := Info {
        name = "Bob",
        age = 30,
        address = {
            area_name = "Downtown",
            house_no = 123,
            district = "Central",
        },
    }
    return info.address.area_name

/// The default of `Int` is 0, of `Bool` is false and of `Str` is the empty string.
default_values_of_struct_object :: proc() Int ->
    p := Person { }
    return p.age

default_str_of_struct_object :: proc() Str ->
    p : Person
    return p.name

partially_initialized_struct_object :: proc() Int ->
    p := Person { name = "Zed" }
    return p.age

order_of_initializers_is_free :: proc() Int ->
    p := Person { name = "Eve", age = 5 }
    q := Person { age = 5, name = "Eve" }
    return p.age - q.age + p.age

struct_object_without_trailing_comma :: proc() Int ->
    p := Person {
        age = 7,
        name = "Bob"
    }
    return p.age

initializers_are_expressions :: proc(n: Int) Int ->
    p := Person { age = n * 2 + 1, name = first_name() }
    return p.age

first_name :: proc() Str ->
    return "Ann"

assign_field_with_operators :: proc() Int ->
    p := Person { age = 10, name = "Bob" }
    p.age += 5
    p.age *= 2
    p.age -= 4
    p.age /= 2
    return p.age

/// Assigning a struct object copies it, so the two do not share fields.
struct_object_is_a_value :: proc() Int ->
    p := Person { age = 1, name = "Bob" }
    q := p
    q.age = 99
    return p.age * 100 + q.age

assign_whole_struct_object :: proc() Int ->
    p := Person { age = 1, name = "Bob" }
    q := Person { age = 2, name = "Amy" }
    p = q
    q.age = 3
    return p.age * 10 + q.age

/// A struct object is passed by value.
pass_struct_object_to_proc :: proc() Int ->
    p := Person { age = 41, name = "Bob" }
    return next_age(p) + next_age(Person { age = 9 })

next_age :: proc(person: Person) Int ->
    return person.age + 1

proc_changes_its_own_copy :: proc() Int ->
    p := Person { age = 5, name = "Bob" }
    older := birthday(p)
    return older.age * 10 + p.age

birthday :: proc(person: Person) Person ->
    older := person
    older.age += 1
    return older

use_returned_struct_object :: proc() Str ->
    p := birthday(Person { age = 1, name = "Kim" })
    return p.name

Point :: struct ->
    x: Int
    y: Int

/// A struct can contain other structs, but not itself.
Line :: struct ->
    start: Point
    end: Point
    label: Str

struct_in_struct :: proc() Int ->
    line := Line {
        start = Point { x = 1, y = 2 },
        end = { x = 4, y = 6 },
        label = "diagonal",
    }
    dx := line.end.x - line.start.x
    dy := line.end.y - line.start.y
    return dx * dx + dy * dy

default_of_nested_struct :: proc() Int ->
    line := Line { label = "empty" }
    return line.start.x + line.end.y

assign_nested_fields :: proc() Int ->
    line : Line
    line.end.y = 10
    line.start = Point { x = 3, y = 4 }
    line.end.x += line.start.x
    return line.start.x * 1000 + line.start.y * 100 + line.end.x * 10 + line.end.y

Flags :: struct ->
    enabled: Bool
    count: Int
    name: Str
    point: Point

mixed_field_types :: proc() Int ->
    f := Flags { count = 3 }
    if f.enabled ->
        return 100
    f.enabled = true
    f.point.y = 5
    if f.enabled and f.point.x == 0 ->
        return f.count + f.point.y
    return 0

struct_in_loop :: proc() Int ->
    total := 0
    for i in 1..<=3 ->
        p := Point { x = i, y = i * i }
        total += p.x + p.y
    return total

struct_in_branches :: proc(flag: Bool) Int ->
    p : Point
    if flag ->
        p = Point { x = 1, y = 2 }
    else ->
        p = Point { x = 10, y = 20 }
    return p.x + p.y

total_age :: proc(people: []Person) Int ->
    total := 0
    for p in people ->
        total += p.age
    return total

array_of_struct_objects :: proc() Int ->
    people := [const]Person {
        Person { age = 30, name = "Bob" },
        Person { age = 25, name = "Alice" },
        Person { age = 40, name = "Charlie" },
    }
    return total_age(people) * 10 + total_age(people[1:3])

assign_fields_of_elements :: proc() Int ->
    people := [const]Person {
        Person { age = 30, name = "Bob" },
        Person { age = 25, name = "Alice" },
    }
    people[0].age = 31
    people[1].age += 5
    return people[0].age * 100 + people[1].age

assign_element_that_is_a_struct :: proc() Int ->
    people := [const]Person { Person { age = 1 }, Person { age = 2 } }
    people[1] = Person { age = 20, name = "Zoe" }
    first := people[0]
    people[0] = people[1]
    people[1] = first
    return people[0].age * 100 + people[1].age

name_of_element :: proc(index: Int) Str ->
    people := [const]Person { Person { age = 1, name = "Ann" }, Person { age = 2, name = "Ben" } }
    return people[index].name

iterate_struct_objects :: proc() Int ->
    points := [const]Point { Point { x = 1, y = 2 }, Point { x = 3, y = 4 }, Point { x = 5, y = 6 } }
    total := 0
    for p in points ->
        if p.x == 3 ->
            continue
        total += p.x * p.y
    return total

copy_range_of_struct_objects :: proc() Int ->
    a := [const]Point { Point { x = 1, y = 1 }, Point { x = 2, y = 2 }, Point { x = 3, y = 3 } }
    b := [const]Point { Point { }, Point { }, Point { } }
    b[0:3] = a
    a[0:2] = a[1:3]
    return b[2].x * 1000 + b[0].y * 100 + a[0].x * 10 + a[2].x

nested_struct_in_array_element :: proc() Int ->
    lines := [const]Line { Line { end = Point { x = 7, y = 8 } }, Line { } }
    lines[1].end.y = lines[0].end.x + 1
    return lines[0].end.y * 10 + lines[1].end.y

/// Reading a field of an element that does not exist traps.
element_out_of_bounds_field :: proc(index: Int) Int ->
    people := [const]Person { Person { age = 1 } }
    return people[index].age