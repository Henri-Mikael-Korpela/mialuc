/// The program that is running: the command line that it was called with, which is its name and the
/// arguments that were given after it, such as `mialuc run program.mu one "two words"` gives to the
/// program. Like the rest of the standard library it takes the memory that it needs from its caller.
/// It works on Windows only for now.
package mu

import foreign libc
import foreign kernel32 "system:Kernel32.lib"

foreign libc ->
    memcpy :: proc(destination: ^Byte, source: C_Str, count: Int) ^Byte -> ...
    strlen :: proc(text: C_Str) Int -> ...

foreign kernel32 #(default_calling_convention("system")) ->
    GetCommandLineA :: proc() C_Str -> ...

/// Copies the command line of the program into `buffer`, which the caller owns, and gives how many
/// bytes it has. It is the name of the program, and the arguments, each one between double quotes
/// when it has a space or a double quote, with a backslash before each double quote in it, as the
/// runtime of C reads them. A command line that is longer than the buffer is cut where the buffer
/// ends.
process_command_line :: proc(buffer: []Byte) Int ->
    command_line := GetCommandLineA()
    length := strlen(command_line)
    if length > length_of(buffer) ->
        length = length_of(buffer)
    if length > 0 ->
        memcpy(%buffer[0], command_line, length)
    return length

/// Gives where the arguments start in `command_line`, which `process_command_line` made: after the
/// name of the program, which ends at the first space that is not between double quotes, and after the
/// spaces that follow it. It is the length of the command line when there are no arguments, so
/// `command_line[process_arguments_start(command_line):]` has the arguments as they were written,
/// with their quotes, separated by spaces.
process_arguments_start :: proc(command_line: []Byte) Int ->
    is_quoted := false
    index := 0
    for index < length_of(command_line) ->
        character := command_line[index]
        if character == '"' ->
            is_quoted = not is_quoted
        is_separator := character == ' ' or character == '\t'
        if is_separator and not is_quoted ->
            break
        index += 1

    for index < length_of(command_line) ->
        if command_line[index] not= ' ' ->
            break
        index += 1
    return index
