// `mu.find_first_index_of` of `mu:slices` is one polymorphic procedure that searches the elements of
// an array slice of any type that `==` compares, for the elements of `Int`, `Byte` and `Bool`. The
// exit code of the program is the number of wrong results, 0.

package slices

import "mu:slices"

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

main :: proc() Int ->
    wrong := 0
    numbers := [5]Int { 7, 3, 9, 3, 1 }
    wrong += check("the first 3 is at %lld", mu.find_first_index_of(numbers[:], 3), 1)
    wrong += check("the first 7 is at %lld", mu.find_first_index_of(numbers[:], 7), 0)
    wrong += check("the first 1 is at %lld", mu.find_first_index_of(numbers[:], 1), 4)
    wrong += check("an Int that is not there is at %lld", mu.find_first_index_of(numbers[:], 4), -1)
    wrong += check("the part after the first 3 has its 3 at %lld", mu.find_first_index_of(numbers[2:], 3), 1)
    wrong += check("no elements have no index: %lld", mu.find_first_index_of(numbers[0:0], 7), -1)

    bytes := [4]Byte { 200, 100, 200, 0 }
    wrong += check("the first 200 is at %lld", mu.find_first_index_of(bytes[:], 200), 0)
    wrong += check("the first 0 is at %lld", mu.find_first_index_of(bytes[:], 0), 3)
    wrong += check("a Byte that is not there is at %lld", mu.find_first_index_of(bytes[:], 50), -1)

    flags := [3]Bool { false, false, true }
    wrong += check("the first true is at %lld", mu.find_first_index_of(flags[:], true), 2)
    return wrong
