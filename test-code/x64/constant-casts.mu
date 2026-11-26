package constant_casts

import foreign libc

foreign libc ->
    printf :: proc(format: C_Str, number: Int) Int -> ...
    puts :: proc(content: C_Str) Int -> ...

/// A `Rune` that is known when the program is compiled is cast to an integer type in a constant,
/// and the constant has the type that is named. The exit code of the program is the number of wrong
/// results, 0.
SPACE :: cast(Int)' '
LETTER : Byte : cast(Byte)'a'
LATIN : Byte : cast(Byte)'é'
WIDE :: cast(I32)'芋'
EMOJI :: cast(U64)'🚀'
SMALL : U32 : cast(U32)'A'
LONG : I64 : cast(I64)'Z'
NEXT :: SPACE + 1
FIRST : Rune : 'A'
FIRST_NUMBER :: cast(Int)FIRST

/// The constant is used as a length of an array, and as a case of a match.
LENGTH :: cast(Int)'\x08'

check :: proc(format: C_Str, number: Int, expected: Int) Int ->
    printf(format, number)
    if number == expected ->
        puts("")
        return 0
    puts(" (wrong)")
    return 1

class_of :: proc(number: Int) Int ->
    match number ->
        case SPACE ->
            return 1
        case FIRST_NUMBER ->
            return 2
        case ->
            return 0

main :: proc() Int ->
    wrong := 0
    wrong += check("a space is %lld", SPACE, 32)
    wrong += check("a is the Byte %lld", cast(Int)LETTER, 97)
    wrong += check("é is the Byte %lld", cast(Int)LATIN, 233)
    wrong += check("芋 is the I32 %lld", cast(Int)WIDE, 33419)
    wrong += check("the rocket is the U64 %lld", cast(Int)EMOJI, 128640)
    wrong += check("A is the U32 %lld", cast(Int)SMALL, 65)
    wrong += check("Z is the I64 %lld", cast(Int)LONG, 90)
    wrong += check("a space and one is %lld", NEXT, 33)
    wrong += check("the number of a constant rune is %lld", FIRST_NUMBER, 65)
    values := [LENGTH]Byte { .. = 0 }
    wrong += check("an array of the length of a cast has %lld", length_of(values), 8)
    wrong += check("a match on the space is %lld", class_of(32), 1)
    wrong += check("a match on A is %lld", class_of(65), 2)
    wrong += check("a match on another is %lld", class_of(7), 0)
    return wrong
