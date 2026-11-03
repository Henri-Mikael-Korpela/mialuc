always_true :: proc() Bool ->
    return true

always_false :: proc() Bool ->
    flag : Bool = false
    return flag

is_positive :: proc(n: Int) Bool ->
    return n > 0

is_even :: proc(n: Int) Bool ->
    return n / 2 * 2 == n

in_range :: proc(n: Int, low: Int, high: Int) Bool ->
    at_least_low := n >= low
    if at_least_low ->
        return n <= high
    return false

negate :: proc(value: Bool) Bool ->
    return value == false

both :: proc(a: Bool, b: Bool) Bool ->
    if a ->
        return b
    return false

either :: proc(a: Bool, b: Bool) Bool ->
    if a ->
        return true
    return b

exclusive_or :: proc(a: Bool, b: Bool) Bool ->
    return a not= b

describe :: proc(flag: Bool) Int ->
    if flag ->
        return 1
    return 0

same_sign :: proc(a: Int, b: Int) Bool ->
    a_negative := a < 0
    b_negative := b < 0
    return a_negative == b_negative

grade :: proc(score: Int) Int ->
    passed := score >= 50
    excellent := score >= 90
    if excellent ->
        return 3
    else if passed ->
        return 2
    else ->
        return 1

is_leap_year :: proc(year: Int) Bool ->
    by_4 := year / 4 * 4 == year
    by_100 := year / 100 * 100 == year
    by_400 := year / 400 * 400 == year
    if by_400 ->
        return true
    if by_100 ->
        return false
    return by_4

logical_not :: proc(value: Bool) Bool ->
    return not value

logical_and :: proc(a: Bool, b: Bool) Bool ->
    return a and b

logical_or :: proc(a: Bool, b: Bool) Bool ->
    return a or b

not_not :: proc(value: Bool) Bool ->
    return not not value

exclusive_or_keywords :: proc(a: Bool, b: Bool) Bool ->
    return a and not b or not a and b

in_range_keywords :: proc(n: Int, low: Int, high: Int) Bool ->
    return n >= low and n <= high

out_of_range :: proc(n: Int, low: Int, high: Int) Bool ->
    return n < low or n > high

not_binds_looser_than_comparison :: proc(a: Int, b: Int) Bool ->
    return not a == b

and_binds_tighter_than_or :: proc(a: Bool, b: Bool, c: Bool) Bool ->
    return a or b and c

is_leap_year_keywords :: proc(year: Int) Bool ->
    return year / 4 * 4 == year and not year / 100 * 100 == year or year / 400 * 400 == year

fizz_or_buzz :: proc(n: Int) Bool ->
    return n / 3 * 3 == n or n / 5 * 5 == n

fizz_buzz_only :: proc(n: Int) Bool ->
    return n / 3 * 3 == n and not n / 5 * 5 == n

can_enter :: proc(age: Int, has_ticket: Bool, is_banned: Bool) Int ->
    if age >= 18 and has_ticket and not is_banned ->
        return 1
    else if age < 18 and has_ticket and not is_banned ->
        return 2
    return 0
