// Parametric polymorphism: procedures whose parameters have type parameters, which each call gives
// a type, and structs with type parameters, which each use gives types. The compiler makes a copy of
// each for the types that it is used with, which is a function of the module of its own, whose name
// is the name of the procedure and the types, such as `add(Int)`.

package polymorphism

/// A type parameter that is declared as a type, `$T: type_id`, whose type is deduced from the other arguments.
add :: proc($T: type_id, a: T, b: T) T ->
    return a + b

/// A type parameter that is declared where it is first used, with `$`.
larger :: proc(a: $T, b: T) T ->
    if a > b ->
        return a
    return b

/// The type of the elements of an array slice.
sum :: proc(values: []$T) T ->
    total : T
    for value in values ->
        total += value
    return total

/// A pointer to a value of the type.
swap :: proc(a: ^$T, b: ^T) ->
    kept := a^
    a^ = b^
    b^ = kept

/// Two values of the type, which are returned together.
low_and_high :: proc(values: []$T) (T, T) ->
    low := values[0]
    high := values[0]
    for value in values ->
        if value < low ->
            low = value
        high = larger(value, high)
    return low, high

/// A type that is given, which nothing else would say, and the default value of the type.
zero :: proc($T: type_id) T ->
    nothing : T
    return nothing

/// A procedure that calls another polymorphic procedure with its own type parameter.
add_three :: proc($T: type_id, a: T, b: T, c: T) T ->
    return add(T, add(a, b), c)

/// A struct with a type parameter, which each use gives a type.
Pair :: struct($T: type_id) ->
    first: T
    second: T

/// A struct with two type parameters, one of which is a struct with a type parameter.
Labeled :: struct($K: type_id, $V: type_id) ->
    key: K
    value: V

pair_of :: proc(a: $T, b: T) Pair(T) ->
    return Pair(T) { first = a, second = b }

swapped :: proc(pair: Pair($T)) Pair(T) ->
    return Pair(T) { first = pair.second, second = pair.first }

/// The type of an argument that is the result of another polymorphic call.
nested_calls :: proc() Int ->
    first := add(1, 2)
    second := add(first, 10)
    return larger(second, first) + add_three(Int, 1, 2, 3)

integers_and_floats :: proc() Int ->
    integer := add(10, 20)
    float := add(1.5, 2.25)
    larger_float := larger(float, 3.5) * 4.0
    return integer + cast(Int)larger_float

slices_of_types :: proc() Int ->
    integers := [const]Int { 1, 2, 3, 4 }
    floats := [const]F64 { 0.5, 0.25 }
    total_floats := sum(floats[:]) * 100.0
    return sum(integers) + cast(Int)total_floats

pointers_of_types :: proc() Int ->
    a := 1
    b := 2
    swap(%a, %b)
    x := 1.5
    y := 2.5
    swap(%x, %y)
    return a * 10 + b + cast(Int)x * 100

values_together :: proc() Int ->
    values := [const]Int { 5, 3, 9, 1 }
    low, high := low_and_high(values)
    return low * 10 + high

given_types :: proc() Int ->
    number := zero(Int)
    flag := zero(Bool)
    if flag ->
        return 0 - 1
    return number + cast(Int)zero(F64) + 7

structs_of_types :: proc() Int ->
    integers := pair_of(1, 2)
    turned := swapped(integers)
    floats := Pair(F64) { first = 0.5, second = 1.5 }
    labeled := Labeled(Bool, Pair(Int)) { key = true, value = turned }
    doubled := floats.second * 2.0
    total := labeled.value.first * 10 + labeled.value.second + cast(Int)doubled
    if labeled.key ->
        total += 100
    return total
