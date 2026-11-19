// A program is called with the arguments that follow the source file in `mialuc run`, as if the
// built program had been called with them after its name: with
//
//     mialuc run test-code/x64/command-line-arguments.mu one "two words" 3
//
// it prints the arguments as they were written, `one "two words" 3`, and its exit code is the number
// of bytes that they take. With no arguments it prints an empty line and ends with 0. The standard
// library gives the command line of the program, which a program of Mialu has no arguments for yet.

package command_line_arguments

import "mu:process"
import foreign libc

foreign libc ->
    puts :: proc(content: C_Str) I32 -> ...

main :: proc() Int ->
    buffer := [1024]Byte { .. = 0 }
    length := mu.process_command_line(buffer[:])
    start := mu.process_arguments_start(buffer[:length])
    arguments, is_text := utf8_decode(buffer[start:length])
    if not is_text ->
        return 1
    puts(str_to_c_str(arguments))
    return length_of(arguments)
