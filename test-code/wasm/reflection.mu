// Static reflection: `type_info_of` gives the name and the members of an enum while the
// program is compiled, and `length_of` counts the members.

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

/// `type_info_of` is evaluated while the program is compiled, so its result can be a global variable.
Color_Members #(static) := type_info_of(Color).members

Weekday_Name #(static) := type_info_of(Weekday).name

/// `length_of` of the members gives the number of members.
get_enum_members_count :: proc() Int ->
    return length_of(Color_Members)

members_of_another_enum :: proc() Int ->
    return length_of(type_info_of(Weekday).members)

members_in_a_variable :: proc() Int ->
    members := type_info_of(Color).members
    return length_of(members)

/// A member has a name and a number: this is the number.
member_number :: proc(index: Int) Int ->
    return Color_Members[index].value

/// And this is the name, as a string.
member_name :: proc(index: Int) Str ->
    return Color_Members[index].name

/// The name of the enum itself.
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

/// The members are in the order of the enum, so the last one is the last that is written.
name_of_last_member :: proc() Str ->
    members := type_info_of(Weekday).members
    return members[length_of(members) - 1].name

middle_slice_length :: proc() Int ->
    return length_of(Color_Members[1:3])
