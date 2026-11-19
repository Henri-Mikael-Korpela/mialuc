/// Guess my number: the program thinks of a number from 0 to 10, and you guess it. When you do, it
/// thinks of another one, so the game does not end, except when the input does, which is Ctrl+Z and
/// Enter on Windows.
///
/// Build and run it from the root of the project, on Windows:
///
///     mialuc run examples/guess-my-number/guess-my-number.mu
///     mialuc build --target x64-win32 -o guess.exe examples/guess-my-number/guess-my-number.mu

package guess_my_number

import "mu:filesystem"
import foreign libc

/// The procedures of the C library are declared with the types that C has: `int` is an `I32`, and
/// `time_t` is as large as an `Int`. A `^Byte` is the address of a byte, and `nil` is no address,
/// which `time` and `fflush` take to mean "nothing". The input is read with the standard library.
foreign libc ->
    fflush  :: proc(stream: ^Byte) I32 -> ...
    printf  :: proc(format: C_Str, number: Int) I32 -> ...
    puts    :: proc(content: C_Str) I32 -> ...
    rand    :: proc() I32 -> ...
    srand   :: proc(seed: I32) -> ...
    time    :: proc(timer: ^Byte) Int -> ...

/// The numbers that can be thought of are from LOWEST to HIGHEST.
LOWEST :: 0
HIGHEST :: 10

/// What `read_guess` gives when there is no guess to give. They are below LOWEST, so they cannot be
/// mistaken for a guess.
END_OF_INPUT :: -1
NOT_A_NUMBER :: -2

/// Makes the numbers that `think_of_number` gives differ from one run to the next: the seed is the
/// seconds of the clock, kept below a million so that it fits in an `I32`.
seed_random :: proc() ->
    seconds := time(nil)
    srand(int_to_i32(seconds - seconds / 1000000 * 1000000))

/// Gives a number from LOWEST to HIGHEST. `rand` gives one that is not negative, and what is left
/// when it is divided by the count of numbers is one of them.
think_of_number :: proc() Int ->
    count := HIGHEST - LOWEST + 1
    value := cast(Int)rand()
    return LOWEST + value - value / count * count

/// Reads a line from `input` and gives the number that is written on it, or END_OF_INPUT when there
/// is no line left, or NOT_A_NUMBER when the line is not a number. A line that does not fit in the
/// buffer is cut where the buffer ends, which no guess is long enough to be.
read_guess :: proc(input: mu.File) Int ->
    buffer := [64]Byte { .. = 0 }
    length, error := mu.file_read_line(input, buffer[:])
    if error not= .NONE ->
        return END_OF_INPUT

    text, is_text := utf8_decode(buffer[:length])
    if not is_text ->
        return NOT_A_NUMBER
    number, is_number := try_str_to_int(text)
    if not is_number ->
        return NOT_A_NUMBER
    return number

/// Tells what the guess is: not a guess at all, too low, too high, or right. Gives whether it is right.
respond_to :: proc(guess: Int, secret: Int) Bool ->
    if guess == NOT_A_NUMBER or guess < LOWEST or guess > HIGHEST ->
        puts("That is not a number from 0 to 10.")
        return false
    if guess < secret ->
        puts("Too low.")
        return false
    if guess > secret ->
        puts("Too high.")
        return false

    // The format has no line feed, because a string has no escape sequences, so `puts` of nothing
    // ends the line.
    printf("Yes, it was %lld!", secret)
    puts("")
    return true

/// Asks for a guess, and reads it. The prompt is written out before the program waits for the line.
ask_for_guess :: proc(input: mu.File) Int ->
    puts("Your guess?")
    fflush(nil)
    return read_guess(input)

main :: proc() Int ->
    seed_random()
    puts("Guess my number")

    input := mu.file_standard_input()
    secret := think_of_number()
    puts("")
    puts("I am thinking of a number from 0 to 10.")
    guess := ask_for_guess(input)
    for guess not= END_OF_INPUT ->
        if respond_to(guess, secret) ->
            secret = think_of_number()
            puts("")
            puts("I am thinking of another number from 0 to 10.")
        guess = ask_for_guess(input)
    puts("")
    puts("Bye!")

    return 0
