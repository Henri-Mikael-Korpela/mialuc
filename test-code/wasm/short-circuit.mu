// `and` and `or` evaluate their right operand only when the left one does not decide the result: `and`
// when the left one is `true`, and `or` when it is `false`. The operand that is not evaluated would
// stop the program, with a division by zero or an index that is outside of the elements, or it
// counts that it was called.

package short_circuit

/// Counts that it was called, and gives `value`.
count_and_give :: proc(counter: ^Int, value: Bool) Bool ->
    counter^ += 1
    return value

as_bool :: proc(number: Int) Bool ->
    return number == 1

as_number :: proc(value: Bool) Int ->
    if value ->
        return 1
    return 0

/// The division is not done when the divisor is zero, which would stop the program.
guarded_division :: proc(n: Int, d: Int) Int ->
    return as_number(d not= 0 and n / d > 1)

/// The same with `or`: the division is only done when the divisor is not zero.
guarded_division_or :: proc(n: Int, d: Int) Int ->
    return as_number(d == 0 or n / d > 1)

/// An index that is checked before the element is read, which would stop the program when it is
/// outside of the elements.
guarded_index :: proc(index: Int) Int ->
    values := [3]Int { 5, 20, 30 }
    slice := values[:]
    return as_number(index >= 0 and index < length_of(slice) and slice[index] > 10)

guarded_index_or :: proc(index: Int) Int ->
    values := [3]Int { 5, 20, 30 }
    slice := values[:]
    return as_number(index < 0 or index >= length_of(slice) or slice[index] > 10)

/// The number of times that the right operand was evaluated, times 10, and 1 when `a and b` is true.
and_case :: proc(left: Int, right: Int) Int ->
    runs := 0
    result := as_bool(left) and count_and_give(%runs, as_bool(right))
    return runs * 10 + as_number(result)

or_case :: proc(left: Int, right: Int) Int ->
    runs := 0
    result := as_bool(left) or count_and_give(%runs, as_bool(right))
    return runs * 10 + as_number(result)

/// `a and b or c` is `(a and b) or c`: each of the operands is evaluated only when the ones before
/// it do not decide the result. The number is the count of the evaluated operands, times 10, and 1
/// when the result is true.
chain :: proc(a: Int, b: Int, c: Int) Int ->
    runs := 0
    result := count_and_give(%runs, as_bool(a)) and count_and_give(%runs, as_bool(b)) or count_and_give(%runs, as_bool(c))
    return runs * 10 + as_number(result)

/// `not` binds tighter than `and`, so only the operand of `and` that comes first decides.
negated :: proc(a: Int, b: Int) Int ->
    runs := 0
    result := not count_and_give(%runs, as_bool(a)) and count_and_give(%runs, as_bool(b))
    return runs * 10 + as_number(result)

/// The bytes that are not spaces at the start, found with a loop that checks the index first.
leading_non_spaces :: proc(count: Int) Int ->
    text := [5]Byte { 97, 98, 32, 99, 100 }
    bytes := text[0:count]
    index := 0
    for index < length_of(bytes) and bytes[index] not= ' ' ->
        index += 1
    return index

/// The values that were worked out before the operator are not lost when the right operand is skipped
/// or evaluated, as an argument, and in the field of a struct.
choose :: proc(base: Int, flag: Bool, extra: Int) Int ->
    if flag ->
        return base + extra
    return base - extra

Verdict :: struct ->
    first: Int
    holds: Bool
    last: Int

kept_values :: proc(n: Int, d: Int) Int ->
    chosen := choose(100, d not= 0 and n / d > 1, 5)
    verdict := Verdict { first = 7, holds = d == 0 or n / d > 1, last = 9 }
    total := chosen + verdict.first * 1000 + verdict.last * 100000
    if verdict.holds ->
        total += 10000
    return total
