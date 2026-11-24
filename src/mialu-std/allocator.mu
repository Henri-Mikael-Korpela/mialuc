/// Allocators: ways to give out memory in pieces. They work on every target, as they only use the
/// language itself. An allocator does not own its memory: the program gives it a buffer, which it
/// made, such as with `make`, or which is an array of a procedure, and gives the buffer back itself
/// when nothing uses the allocator any more.
package mu

/// The alignment of the pieces that an arena gives out, in bytes. A piece starts at a multiple of
/// it from the start of the memory, which `make` gives at an address that is a multiple of it, so
/// that a piece can hold values of any type.
ARENA_ALIGNMENT_BYTES :: 8

/// An arena: memory that is given out from its start to its end, one piece after the other, and
/// that is given back all at once, with `arena_reset`, and never one piece at a time. Giving out a
/// piece is a few instructions, and nothing needs to remember the pieces, so it suits memory that
/// lives as long as one task, such as the answer to one request. It is made with a struct literal
/// of the memory that it gives out, which it does not own, and nothing of it given out:
/// `mu.Arena_Allocator { memory = buffer, used = 0 }`.
Arena_Allocator :: struct ->
    /// The memory that the pieces are given out of.
    memory: []Byte
    /// How many bytes of the memory are given out, from its start, which is where the next piece
    /// starts once it is rounded up to the alignment.
    used: Int

/// Gives out `size` bytes of `arena`, which are zero, at the next multiple of the alignment, and
/// whether there was room for them. When there is no room, or `size` is below zero, the bytes are
/// none and the arena is as it was. The bytes are good until the arena is reset, or its memory is
/// given back.
arena_allocate :: proc(arena: ^Arena_Allocator, size: Int) ([]Byte, Bool) ->
    start := arena_aligned(arena.used)
    if size < 0 or start > length_of(arena.memory) - size ->
        return arena.memory[0:0], false
    piece := arena.memory[start:start + size]
    // The memory may have been given out before the arena was reset, so it is made zero, which is
    // what `make` gives too.
    for i in 0..<size ->
        piece[i] = 0
    arena.used = start + size
    return piece, true

/// Gives back everything that `arena` gave out, so that the memory is given out again from its
/// start. What was given out must not be used any more.
arena_reset :: proc(arena: ^Arena_Allocator) ->
    arena.used = 0

/// Gives the number of bytes that `arena` can still give out in one piece.
arena_remaining :: proc(arena: ^Arena_Allocator) Int ->
    start := arena_aligned(arena.used)
    if start > length_of(arena.memory) ->
        return 0
    return length_of(arena.memory) - start

/// Gives `offset` rounded up to the next multiple of the alignment of an arena.
arena_aligned :: proc(offset: Int) Int ->
    last := offset + ARENA_ALIGNMENT_BYTES - 1
    return last / ARENA_ALIGNMENT_BYTES * ARENA_ALIGNMENT_BYTES
