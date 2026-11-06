Color :: enum Int ->
    RED
    GREEN
    BLUE

Weekday :: enum Int ->
    MONDAY
    TUESDAY
    WEDNESDAY
    THURSDAY
    FRIDAY

Color_Members #(static) := type_info_of(Color).members

Weekday_Name #(static) := type_info_of(Weekday).name

get_enum_members_count :: proc() Int ->
    return length_of(Color_Members)

members_of_another_enum :: proc() Int ->
    return length_of(type_info_of(Weekday).members)

members_in_a_variable :: proc() Int ->
    members := type_info_of(Color).members
    return length_of(members)

member_number :: proc(index: Int) Int ->
    return Color_Members[index].value

member_name :: proc(index: Int) Str ->
    return Color_Members[index].name

enum_name :: proc() Str ->
    return type_info_of(Color).name

enum_name_in_a_global :: proc() Str ->
    return Weekday_Name

sum_of_numbers :: proc() Int ->
    total := 0
    for member in Color_Members ->
        total += member.value
    return total

sum_of_weekday_numbers :: proc() Int ->
    total := 0
    for member in type_info_of(Weekday).members ->
        total += member.value
    return total

name_of_last_member :: proc() Str ->
    members := type_info_of(Weekday).members
    return members[length_of(members) - 1].name

middle_slice_length :: proc() Int ->
    return length_of(Color_Members[1:3])

slice_length :: proc(elements: []Int) Int ->
    return length_of(elements)

length_of_literal :: proc() Int ->
    return length_of([const]Int { 4, 5, 6, 7 })
