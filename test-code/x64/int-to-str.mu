// `int_to_str` makes the decimal text of an integer that is known when the program is compiled, so
// the result is a constant string: it can be joined with `++`, compared and given to a constant. The
// exit code of the program is the number of wrong results, 0.

package int_to_str

import foreign libc

foreign libc ->
    puts :: proc(content: C_Str) Int -> ...

MAJOR :: 1
MINOR :: 20
PATCH : I32 : 300
LEAST :: -9223372036854775807 - 1

VERSION :: int_to_str(MAJOR) ++ '.' ++ int_to_str(MINOR) ++ '.' ++ int_to_str(PATCH)
SUM :: int_to_str(MAJOR + MINOR)

/// Prints the name of the check, and counts it as wrong when it did not hold.
check :: proc(name: C_Str, is_true: Bool) Int ->
    if is_true ->
        puts(name)
        return 0
    puts("wrong:")
    puts(name)
    return 1

main :: proc() Int ->
    wrong := 0
    wrong += check("zero", int_to_str(0) == "0")
    wrong += check("a literal", int_to_str(42) == "42")
    wrong += check("negative", int_to_str(-7) == "-7")
    wrong += check("the largest", int_to_str(9223372036854775807) == "9223372036854775807")
    wrong += check("the smallest", int_to_str(LEAST) == "-9223372036854775808")
    wrong += check("an I32", int_to_str(PATCH) == "300")
    wrong += check("arithmetic", SUM == "21")
    wrong += check("joined", VERSION == "1.20.300")
    wrong += check("length", length_of(VERSION) == 8)
    text := int_to_str(MINOR)
    wrong += check("a variable", text == "20")
    puts(str_to_c_str(VERSION))
    return wrong
