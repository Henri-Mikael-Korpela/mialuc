// Procedures: parameters, results, calls inside calls, recursion, and calling a procedure
// that is defined further down in the file.

forty :: proc() Int ->
    return 40

/// A call is an expression, so it can be an operand.
forty_two :: proc() Int ->
    return forty() + 2

double :: proc(n: Int) Int ->
    return n * 2

/// Calls can be nested, the inner one first.
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

/// A procedure can return from inside an if statement.
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

/// A procedure can call itself.
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

/// A procedure can be called above where it is defined.
uses_later :: proc() Int ->
    return defined_later(20)

defined_later :: proc(n: Int) Int ->
    return n + 1

/// A call can be a statement, and then the result is dropped.
ignores_result :: proc(n: Int) Int ->
    double(n)
    return n
