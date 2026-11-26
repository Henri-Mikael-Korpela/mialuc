package constant_casts

/// A `Rune` that is known when the program is compiled is cast to an integer type in a constant,
/// and the constant has the type that is named.
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
LENGTH :: cast(Int)'\x08'

space :: proc() Int ->
    return SPACE

letter :: proc() Int ->
    return cast(Int)LETTER

latin :: proc() Int ->
    return cast(Int)LATIN

wide :: proc() Int ->
    return cast(Int)WIDE

emoji :: proc() Int ->
    return cast(Int)EMOJI

small :: proc() Int ->
    return cast(Int)SMALL

long :: proc() Int ->
    return cast(Int)LONG

next :: proc() Int ->
    return NEXT

first_number :: proc() Int ->
    return FIRST_NUMBER

array_length :: proc() Int ->
    values := [LENGTH]Byte { .. = 0 }
    return length_of(values)

/// A match on the constants.
class_of :: proc(number: Int) Int ->
    match number ->
        case SPACE ->
            return 1
        case FIRST_NUMBER ->
            return 2
        case ->
            return 0
