// `and` and `or` evaluate their right operand only when the left one does not decide the result: `and`
// when the left one is `true`, and `or` when it is `false`. Each procedure is checked against the
// result that the language gives, and the exit code of the program is the number of wrong results, 0.

package short_circuit

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

/// Counts that it was called, and gives `value`.
count_and_give :: proc(counter: ^Int, value: Bool) Bool ->
    counter^ += 1
    return value

/// The division is not done when the divisor is zero, which would stop the program.
guarded_division :: proc(n: Int, d: Int) Bool ->
    return d not= 0 and n / d > 1

/// The same with `or`: the division is only done when the divisor is not zero.
guarded_division_or :: proc(n: Int, d: Int) Bool ->
    return d == 0 or n / d > 1

/// An index that is checked before the element is read, which would stop the program when it is
/// outside of the elements.
guarded_index :: proc(values: []Int, index: Int) Bool ->
    return index >= 0 and index < length_of(values) and values[index] > 10

guarded_index_or :: proc(values: []Int, index: Int) Bool ->
    return index < 0 or index >= length_of(values) or values[index] > 10

/// The number of times that the right operand was evaluated, times 10, and 1 when `a and b` is true.
and_case :: proc(left: Bool, right: Bool) Int ->
    runs := 0
    result := left and count_and_give(%runs, right)
    if result ->
        return runs * 10 + 1
    return runs * 10

or_case :: proc(left: Bool, right: Bool) Int ->
    runs := 0
    result := left or count_and_give(%runs, right)
    if result ->
        return runs * 10 + 1
    return runs * 10

/// `a and b or c` is `(a and b) or c`: each of the operands is evaluated only when the ones before
/// it do not decide the result. The number is the count of the evaluated operands, times 10, and 1
/// when the result is true.
chain :: proc(a: Bool, b: Bool, c: Bool) Int ->
    runs := 0
    result := count_and_give(%runs, a) and count_and_give(%runs, b) or count_and_give(%runs, c)
    if result ->
        return runs * 10 + 1
    return runs * 10

/// `not` binds tighter than `and`, so only the operand of `and` that comes first decides.
negated :: proc(a: Bool, b: Bool) Int ->
    runs := 0
    result := not count_and_give(%runs, a) and count_and_give(%runs, b)
    if result ->
        return runs * 10 + 1
    return runs * 10

/// The bytes that are not spaces at the start, found with a loop that checks the index first.
leading_non_spaces :: proc(bytes: []Byte) Int ->
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

/// Prints the result of a procedure and says whether it is the one that the language gives.
check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

/// 1 for true and 0 for false, so that a `Bool` is checked as a number.
as_number :: proc(value: Bool) Int ->
    if value ->
        return 1
    return 0

main :: proc() Int ->
    wrong := 0
    wrong += check("guarded_division(10, 2) is %lld", as_number(guarded_division(10, 2)), 1)
    wrong += check("guarded_division(10, 0) is %lld", as_number(guarded_division(10, 0)), 0)
    wrong += check("guarded_division(1, 1) is %lld", as_number(guarded_division(1, 1)), 0)
    wrong += check("guarded_division_or(10, 2) is %lld", as_number(guarded_division_or(10, 2)), 1)
    wrong += check("guarded_division_or(10, 0) is %lld", as_number(guarded_division_or(10, 0)), 1)
    wrong += check("guarded_division_or(1, 1) is %lld", as_number(guarded_division_or(1, 1)), 0)

    values := [3]Int { 5, 20, 30 }
    wrong += check("guarded_index(values, 1) is %lld", as_number(guarded_index(values[:], 1)), 1)
    wrong += check("guarded_index(values, 0) is %lld", as_number(guarded_index(values[:], 0)), 0)
    wrong += check("guarded_index(values, 3) is %lld", as_number(guarded_index(values[:], 3)), 0)
    wrong += check("guarded_index(values, -1) is %lld", as_number(guarded_index(values[:], -1)), 0)
    wrong += check("guarded_index_or(values, 3) is %lld", as_number(guarded_index_or(values[:], 3)), 1)
    wrong += check("guarded_index_or(values, -1) is %lld", as_number(guarded_index_or(values[:], -1)), 1)
    wrong += check("guarded_index_or(values, 1) is %lld", as_number(guarded_index_or(values[:], 1)), 1)
    wrong += check("guarded_index_or(values, 0) is %lld", as_number(guarded_index_or(values[:], 0)), 0)

    wrong += check("and_case(false, false) is %lld", and_case(false, false), 0)
    wrong += check("and_case(false, true) is %lld", and_case(false, true), 0)
    wrong += check("and_case(true, false) is %lld", and_case(true, false), 10)
    wrong += check("and_case(true, true) is %lld", and_case(true, true), 11)
    wrong += check("or_case(false, false) is %lld", or_case(false, false), 10)
    wrong += check("or_case(false, true) is %lld", or_case(false, true), 11)
    wrong += check("or_case(true, false) is %lld", or_case(true, false), 1)
    wrong += check("or_case(true, true) is %lld", or_case(true, true), 1)

    wrong += check("chain(false, true, true) is %lld", chain(false, true, true), 21)
    wrong += check("chain(false, true, false) is %lld", chain(false, true, false), 20)
    wrong += check("chain(true, true, false) is %lld", chain(true, true, false), 21)
    wrong += check("chain(true, false, false) is %lld", chain(true, false, false), 30)
    wrong += check("chain(true, false, true) is %lld", chain(true, false, true), 31)

    wrong += check("negated(false, true) is %lld", negated(false, true), 21)
    wrong += check("negated(false, false) is %lld", negated(false, false), 20)
    wrong += check("negated(true, true) is %lld", negated(true, true), 10)

    text := [5]Byte { 97, 98, 32, 99, 100 }
    wrong += check("leading_non_spaces(text) is %lld", leading_non_spaces(text[:]), 2)
    wrong += check("leading_non_spaces(text[0:2]) is %lld", leading_non_spaces(text[0:2]), 2)
    wrong += check("leading_non_spaces(text[0:0]) is %lld", leading_non_spaces(text[0:0]), 0)

    wrong += check("kept_values(10, 2) is %lld", kept_values(10, 2), 917105)
    wrong += check("kept_values(10, 0) is %lld", kept_values(10, 0), 917095)
    wrong += check("kept_values(1, 1) is %lld", kept_values(1, 1), 907095)
    return wrong
