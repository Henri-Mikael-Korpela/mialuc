Color :: enum ->
    RED
    GREEN
    BLUE

DEFAULT_COLOR :: Color.RED

create_enum_variable :: proc() Color ->
    color := Color.RED
    return color

return_enum_constant_outside_the_current_proc :: proc() Color ->
    return DEFAULT_COLOR

auto_deduce_enum_by_return_type :: proc() Color ->
    return .GREEN

comparison_between_enum_values :: proc() Bool ->
    return Color.RED == Color.BLUE

Car_Model :: struct ->
    color: Color
    manufacturer: Str

enum_field_in_struct :: proc() Color ->
    car_model := Car_Model {
        color = Color.RED,
        manufacturer = "Bord"
    }
    return car_model.color

enum_field_type_auto_detected_in_struct :: proc() Color ->
    car_model := Car_Model {
        color = .RED,
        manufacturer = "Bord"
    }
    return car_model.color

Light :: enum ->
    RED
    YELLOW
    GREEN

DEFAULT_LIGHT :: Light.YELLOW

Weekday :: enum ->
    MONDAY
    TUESDAY
    WEDNESDAY
    THURSDAY
    FRIDAY
    SATURDAY
    SUNDAY

members_are_numbered_from_zero :: proc() Int ->
    first : Color = Color.RED
    last : Color = Color.BLUE
    count := 0
    if first == Color.RED ->
        count += 1
    if last == Color.BLUE ->
        count += 10
    return count

default_value_is_the_first_member :: proc() Bool ->
    color : Color
    return color == Color.RED

variable_with_written_type :: proc() Color ->
    color : Color = .BLUE
    return color

assign_enum_variable :: proc() Color ->
    color := Color.RED
    color = .GREEN
    return color

assign_enum_variable_with_qualified_member :: proc() Color ->
    color := Color.RED
    color = Color.BLUE
    return color

same_member_names_in_two_enums :: proc() Int ->
    color : Color = .GREEN
    light : Light = .GREEN
    result := 0
    if color == Color.GREEN ->
        result += 1
    if light == Light.GREEN ->
        result += 10
    if light not= Light.RED ->
        result += 100
    return result

literal_on_either_side_of_a_comparison :: proc() Int ->
    color := Color.GREEN
    result := 0
    if color == .GREEN ->
        result += 1
    if .GREEN == color ->
        result += 10
    if color not= .BLUE ->
        result += 100
    return result

not_equal_between_enum_values :: proc() Bool ->
    return Color.RED not= Color.GREEN

is_weekend :: proc(day: Weekday) Bool ->
    return day == .SATURDAY or day == .SUNDAY

weekend_and_weekdays :: proc() Int ->
    result := 0
    if is_weekend(.SUNDAY) ->
        result += 1
    if is_weekend(Weekday.WEDNESDAY) ->
        result += 10
    if is_weekend(.MONDAY) not= true ->
        result += 100
    return result

next_day :: proc(day: Weekday) Weekday ->
    if day == .MONDAY ->
        return .TUESDAY
    else if day == .TUESDAY ->
        return .WEDNESDAY
    else if day == .WEDNESDAY ->
        return .THURSDAY
    else if day == .THURSDAY ->
        return .FRIDAY
    else if day == .FRIDAY ->
        return .SATURDAY
    else if day == .SATURDAY ->
        return .SUNDAY
    return .MONDAY

three_days_after_friday :: proc() Weekday ->
    day := Weekday.FRIDAY
    for i in 0..<3 ->
        day = next_day(day)
    return day

enum_as_argument_and_result :: proc() Weekday ->
    return next_day(.SUNDAY)

enum_constant_of_another_enum :: proc() Light ->
    return DEFAULT_LIGHT

enum_constant_in_a_comparison :: proc() Bool ->
    return DEFAULT_COLOR == .RED and DEFAULT_LIGHT == .YELLOW

enum_constant_as_argument :: proc() Bool ->
    return is_weekend(.SATURDAY) and DEFAULT_COLOR == Color.RED

Pixel :: struct ->
    color: Color
    light: Light
    x: Int

enum_fields_are_set_by_default :: proc() Int ->
    pixel := Pixel { x = 5 }
    result := pixel.x
    if pixel.color == Color.RED ->
        result += 10
    if pixel.light == Light.RED ->
        result += 100
    return result

assign_enum_field :: proc() Color ->
    pixel := Pixel { color = .RED, light = .GREEN, x = 1 }
    pixel.color = .BLUE
    return pixel.color

compare_enum_field_with_literal :: proc() Bool ->
    pixel := Pixel { color = .GREEN, light = .RED, x = 1 }
    return pixel.color == .GREEN and pixel.light == .RED

Frame :: struct ->
    pixel: Pixel
    name: Str

enum_in_a_nested_struct_literal :: proc() Light ->
    frame := Frame {
        pixel = Pixel { color = .BLUE, light = .YELLOW, x = 2 },
        name = "frame",
    }
    return frame.pixel.light

enum_in_a_struct_literal_without_a_name :: proc() Color ->
    frame := Frame {
        pixel = { color = .GREEN, light = .RED, x = 2 },
        name = "frame",
    }
    return frame.pixel.color

set_color :: proc(target: ^Color, color: Color) ->
    target^ = color

pointer_to_an_enum :: proc() Color ->
    color := Color.RED
    set_color(%color, .BLUE)
    return color

assign_through_a_pointer_to_an_enum :: proc() Color ->
    color := Color.RED
    p := %color
    p^ = .GREEN
    return color

read_through_a_pointer_to_an_enum :: proc() Bool ->
    color := Color.BLUE
    p := %color
    return p^ == .BLUE

array_of_enums :: proc() Int ->
    colors := [const]Color { .RED, .GREEN, Color.BLUE, .GREEN }
    greens := 0
    for color in colors ->
        if color == .GREEN ->
            greens += 1
    return greens

count_color :: proc(colors: []Color, wanted: Color) Int ->
    count := 0
    for color in colors ->
        if color == wanted ->
            count += 1
    return count

slice_of_enums :: proc() Int ->
    colors := [const]Color { .RED, .GREEN, .BLUE, .GREEN, .GREEN }
    return count_color(colors, .GREEN) * 10 + count_color(colors[0:2], .GREEN)

assign_enum_element :: proc() Color ->
    colors := [const]Color { .RED, .RED }
    colors[1] = .BLUE
    return colors[1]

index_array_of_enums :: proc() Weekday ->
    days := [const]Weekday { .SATURDAY, .SUNDAY }
    index := 1
    return days[index]

rotate_color :: proc(color: Color) Color ->
    if color == .RED ->
        return .GREEN
    else if color == .GREEN ->
        return .BLUE
    return .RED

pointer_to_a_procedure_with_an_enum :: proc() Color ->
    f : ^proc(Color) Color = rotate_color
    return f(.GREEN)

pass_enum_to_a_callback :: proc(f: ^proc(Color) Color, color: Color) Color ->
    return f(f(color))

callback_with_an_enum :: proc() Color ->
    return pass_enum_to_a_callback(rotate_color, .RED)

enum_in_a_loop_condition :: proc() Int ->
    color := Color.RED
    steps := 0
    for ->
        if color == .BLUE ->
            break
        color = rotate_color(color)
        steps += 1
    return steps
