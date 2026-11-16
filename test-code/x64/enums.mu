// Enums, whose members are their numbers counted from 0: members written with and without the enum,
// constants that are members, comparisons, match statements that have a case for every member or
// are partial, and the information about an enum from `type_info_of`, which is a table in the data
// of the program. The results are the ones that the language specification gives. The exit code of
// the program is the number of wrong results, 0.

package enum_members

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

Color :: enum Int ->
    RED
    GREEN
    BLUE

DEFAULT_COLOR :: Color.GREEN

Color_Members #(static) := type_info_of(Color).members

Car :: struct ->
    color: Color
    seats: Int

check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

color_code :: proc(color: Color) Int ->
    match color ->
        case .RED ->
            return 10
        case .GREEN ->
            return 20
        case Color.BLUE ->
            return 30

warm_code :: proc(color: Color) Int ->
    match #(partial) color ->
        case .RED ->
            return 1
    return 0

next_color :: proc(color: Color) Color ->
    if color == .RED ->
        return .GREEN
    else if color == Color.GREEN ->
        return .BLUE
    return .RED

sum_of_numbers :: proc() Int ->
    total := 0
    for member in type_info_of(Color).members ->
        total += member.value
    return total

main :: proc() Int ->
    wrong := 0
    color : Color = .BLUE
    wrong += check("color_code(.BLUE) is %lld", color_code(color), 30)
    wrong += check("color_code(DEFAULT_COLOR) is %lld", color_code(DEFAULT_COLOR), 20)
    wrong += check("warm_code(.RED) is %lld", warm_code(.RED), 1)
    wrong += check("warm_code(.GREEN) is %lld", warm_code(.GREEN), 0)
    wrong += check("next_color(.BLUE) is %lld", color_code(next_color(color)), 10)

    nothing : Color
    if nothing not= Color.RED ->
        wrong += 1
    car := Car { color = .GREEN, seats = 4 }
    if car.color == .GREEN ->
        car.color = .BLUE
    wrong += check("the car is %lld", color_code(car.color), 30)

    wrong += check("Color has %lld members", length_of(Color_Members), 3)
    wrong += check("sum_of_numbers() is %lld", sum_of_numbers(), 3)
    puts(str_to_c_str(Color_Members[1].name))
    puts(str_to_c_str(type_info_of(Color).name))
    return wrong
