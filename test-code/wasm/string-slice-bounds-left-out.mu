// The bounds of the slice of a string can be left out as well: `s[:3]` starts at the first character,
// `s[3:]` goes to the last, and `s[:]` has all of them. The bounds count characters and not bytes,
// as when they are written, so each of the characters of "añ🚀é!" is one, though it is one to four
// bytes. A chunk of a string, `s[offset:offset + length]`, can be written `s[offset:][:length]` too.

package string_slice_bounds_left_out

/// The four ways to write the whole string, which are all the same string.
whole_string :: proc() Int ->
    s := "hello, world"
    wrong := 0
    if s[0:12] not= s ->
        wrong += 1
    if s[:12] not= s ->
        wrong += 10
    if s[0:] not= s ->
        wrong += 100
    if s[:] not= s ->
        wrong += 1000
    return wrong

start :: proc() Str ->
    s := "hello, world"
    return s[:5]

end :: proc() Str ->
    s := "hello, world"
    return s[7:]

/// A chunk of the string, in the two ways to write it.
chunk :: proc(offset: Int, length: Int) Str ->
    s := "hello, world"
    return s[offset:offset + length]

chunk_of_a_slice :: proc(offset: Int, length: Int) Str ->
    s := "hello, world"
    return s[offset:][:length]

/// The bounds count characters: t[:2] is añ, t[2:] is 🚀é!, and t[2:][:1] is 🚀.
characters :: proc() Str ->
    t := "añ🚀é!"
    return t[2:][:1]

character_count :: proc() Int ->
    t := "añ🚀é!"
    return length_of(t[2:]) * 10 + length_of(t[:2])

/// A low bound past the number of characters traps, as it does with the high bound written.
past_the_end :: proc(first: Int) Int ->
    s := "hello"
    return length_of(s[first:])
