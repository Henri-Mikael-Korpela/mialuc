// An arena allocator of the standard library, which gives out pieces of memory that the program
// gives it, made by `make`, and gives them all back at once. Each procedure is checked against the
// result that the language gives, and the exit code of the program is the number of wrong results,
// 0.

package arena

import foreign libc
import "mu:allocator"

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

/// Pieces are given out one after the other, at multiples of 8, and are zero.
pieces_are_aligned_and_zero :: proc() Int ->
    memory := make([]Byte, 64)
    defer destroy(memory)
    arena := mu.Arena_Allocator { memory = memory, used = 0 }
    first, is_first := mu.arena_allocate(%arena, 3)
    second, is_second := mu.arena_allocate(%arena, 5)
    if not is_first or not is_second ->
        return -1
    first[2] = 9
    second[0] = 4
    // The second piece starts at 8, after the 3 bytes of the first rounded up.
    used := arena.used * 1000
    return cast(Int)memory[2] * 100 + cast(Int)memory[8] * 10 + length_of(second) + used

/// A piece that does not fit is none, and leaves the arena as it was.
too_large :: proc() Int ->
    memory := make([]Byte, 16)
    defer destroy(memory)
    arena := mu.Arena_Allocator { memory = memory, used = 0 }
    _, is_fitting := mu.arena_allocate(%arena, 10)
    piece, is_too_large := mu.arena_allocate(%arena, 10)
    _, is_negative := mu.arena_allocate(%arena, -1)
    if not is_fitting or is_too_large or is_negative ->
        return -1
    return length_of(piece) * 100 + arena.used * 10 + mu.arena_remaining(%arena)

/// A reset gives all of the memory back, and the pieces that are given out again are zero.
reset_gives_back :: proc() Int ->
    memory := make([]Byte, 8)
    defer destroy(memory)
    arena := mu.Arena_Allocator { memory = memory, used = 0 }
    piece, _ := mu.arena_allocate(%arena, 8)
    piece[7] = 200
    mu.arena_reset(%arena)
    again, is_again := mu.arena_allocate(%arena, 8)
    if not is_again ->
        return -1
    return cast(Int)again[7] + mu.arena_remaining(%arena) * 10 + arena.used * 100

/// The memory of an arena can be an array of a procedure too, which needs no `make`.
array_memory :: proc() Int ->
    buffer := [32]Byte { .. = 0 }
    arena := mu.Arena_Allocator { memory = buffer[:], used = 0 }
    for i in 0..<4 ->
        piece, is_given := mu.arena_allocate(%arena, 8)
        if not is_given ->
            return -1
        piece[0] = int_to_byte(i + 1)
    return cast(Int)buffer[0] + cast(Int)buffer[8] * 10 + cast(Int)buffer[24] * 100

/// Prints the result of a procedure and says whether it is the one that the language gives.
check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

main :: proc() Int ->
    wrong := 0
    wrong += check("pieces_are_aligned_and_zero() is %lld", pieces_are_aligned_and_zero(), 13945)
    wrong += check("too_large() is %lld", too_large(), 100)
    wrong += check("reset_gives_back() is %lld", reset_gives_back(), 800)
    wrong += check("array_memory() is %lld", array_memory(), 421)
    return wrong
