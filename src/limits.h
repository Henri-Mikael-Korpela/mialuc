#ifndef MIALU_LIMITS_H
#define MIALU_LIMITS_H

/**
 * The limits of the compiler that an error tells. The compiler allocates what it needs once, when
 * it starts, so the size of what it reads and writes is bounded, and the errors that say that
 * something is too large say by how much. They are here, and not in the file that allocates, so
 * that the text of an error and the allocation cannot drift apart.
 */
enum {
    // Larger sources are rejected. The tokenizer works on one buffer that is allocated once at
    // startup, so a bound keeps memory use predictable.
    SOURCE_SIZE_BYTES_MAX = 1024 * 1024,
    // Bounds the token buffer, which is allocated once at startup. A token is at least one byte,
    // so this is far more tokens than a source of the maximum size is likely to contain.
    TOKEN_COUNT_MAX = 64 * 1024,
    // Bounds the module that the build mode writes. The module is allocated once at startup. It
    // is mostly a few bytes per token, so this is far more than a source of the maximum size is
    // likely to need.
    OUTPUT_SIZE_BYTES_MAX = 1024 * 1024,
    // Bounds the text of the strings that `++` makes when the program is compiled, which is
    // allocated once at startup. The text is part of the program, so it is never larger than the
    // module that the build mode writes.
    CONSTANT_TEXT_SIZE_BYTES_MAX = OUTPUT_SIZE_BYTES_MAX,
    // Bounds the text that the `wat` target writes. The text is allocated once at startup. The
    // largest growth is `i64.add`, one byte of the module that becomes 12 bytes of text, so 16
    // bytes of text per byte of the module is enough.
    OUTPUT_TEXT_SIZE_BYTES_MAX = 16 * OUTPUT_SIZE_BYTES_MAX,
    // Bounds the texts that the specialization of polymorphic definitions writes: the names of
    // the copies and of their calls, and the types that are made canonical, see `specialize`.
    // They are part of the program, so they are never larger than the module either. Each text is
    // made for a node, and there are no more nodes than tokens.
    SPECIALIZE_TEXT_SIZE_BYTES_MAX = OUTPUT_SIZE_BYTES_MAX,
    SPECIALIZE_TEXT_COUNT_MAX = TOKEN_COUNT_MAX,
};

#endif
