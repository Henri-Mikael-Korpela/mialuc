forty :: proc() Int ->
    return 40

forty_two :: proc() Int ->
    return forty() + 2

double :: proc(n: Int) Int ->
    return n * 2

quadruple :: proc(n: Int) Int ->
    return double(double(n))

add :: proc(a: Int, b: Int) Int ->
    return a + b

add_three :: proc(a: Int, b: Int, c: Int) Int ->
    return add(add(a, b), c)

average :: proc(a: Int, b: Int) Int ->
    return add(a, b) / 2

is_even :: proc(n: Int) Bool ->
    return n / 2 * 2 == n

is_positive_even :: proc(n: Int) Bool ->
    return n > 0 and is_even(n)

describe :: proc(n: Int) Int ->
    if is_positive_even(n) ->
        return 2
    else if n > 0 ->
        return 1
    return 0

square :: proc(n: Int) Int ->
    return n * n

sum_of_squares :: proc(a: Int, b: Int) Int ->
    first := square(a)
    second := square(b)
    return first + second

factorial :: proc(n: Int) Int ->
    if n <= 1 ->
        return 1
    return n * factorial(n - 1)

fibonacci :: proc(n: Int) Int ->
    if n < 2 ->
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)

last_name :: proc() Str ->
    return "Korpela"

pick_name :: proc(use_last: Bool, first: Str, last: Str) Str ->
    if use_last ->
        return last
    return first

chosen_name :: proc(use_last: Bool) Str ->
    return pick_name(use_last, "Henri", last_name())

uses_later :: proc() Int ->
    return defined_later(20)

defined_later :: proc(n: Int) Int ->
    return n + 1

ignores_result :: proc(n: Int) Int ->
    double(n)
    return n
