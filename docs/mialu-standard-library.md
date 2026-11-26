# Mialu Standard Library

The standard library of Mialu is source code: a few files of Mialu that the compiler reads when a program imports them, and that are part of the program that is built. It is small on purpose, and it is as thin as the system under it: nothing in it allocates, every procedure takes the memory that it needs from its caller, and nothing is shared between threads unless the caller shares it. What Mialu itself does not have yet, such as sockets, is written in the program that needs it, and the missing features are noted there, see the comments at the start of [mialudoc.mu](../src/mialudoc/mialudoc.mu) for a program that has many of them.

## Using the library

A program imports a file of the library with the vendor `mu` and the name of the file, and uses its definitions with the name of their package, which is `mu` for every file of the library, see [Imports](mialu-programming-language.md#imports) and [Packages](mialu-programming-language.md#packages):

```
package program

import "mu:filesystem"

main :: proc() Int ->
    buffer := [64]Byte { .. = 0 }
    wide_path := [128]Byte { .. = 0 }
    count, error := mu.file_read_all("data.txt", wide_path[:], buffer[:])
    if error not= .NONE ->
        return 1
    return count
```

The definitions of an imported file are all part of the program, whether it calls them or not, so a file that uses the system, such as `filesystem` and `process`, which are for Windows, does not build for the WebAssembly targets. `allocator`, `math` and `version` only use the language itself, and so they work on every target.

The files are in the directory [src/mialu-std](../src/mialu-std) of the project, which a compiler that is built from the project reads, and in the directory `lib` next to `mialuc.exe` in a [release](releasing.md), which is looked in first. A file that is in neither is an error that says where it looked. See [Standard library](mialu-programming-language.md#standard-library) in the specification.

| File | Imported as | Targets | What it is for |
|---|---|---|---|
| [allocator.mu](../src/mialu-std/allocator.mu) | `import "mu:allocator"` | all | An arena, which gives out pieces of memory that the program gives it, and gives them back all at once. |
| [math.mu](../src/mialu-std/math.mu) | `import "mu:math"` | all | The smaller and the larger of two numbers, and a number held between two others. |
| [version.mu](../src/mialu-std/version.mu) | `import "mu:version"` | all | Whether a text is a version of the form `MAJOR.MINOR.PATCH`. |
| [filesystem.mu](../src/mialu-std/filesystem.mu) | `import "mu:filesystem"` | `x64-win32` | Files: opening, reading, writing and closing them, the standard input, and finding an entry of a directory by its name. |
| [process.mu](../src/mialu-std/process.mu) | `import "mu:process"` | `x64-win32` | The command line that the program was started with. |

## allocator

An allocator gives out memory in pieces. It does not own the memory: the program gives it a buffer, an array slice of bytes that it made, such as with [`make`](mialu-programming-language.md#make), or that is an array of a procedure, and gives the buffer back itself, after nothing uses the allocator any more. An allocator has no constructor and no destructor: it is a struct, made with a struct literal.

`Arena_Allocator` is an arena: it gives out its memory from the start to the end, one piece after the other, at multiples of `ARENA_ALIGNMENT_BYTES`, which is 8, so that a piece can hold values of any type when the memory starts at such a multiple, as what `make` makes does. It never gives back one piece, only all of them at once, so giving out a piece is a few instructions, and nothing needs to remember the pieces. That suits memory that lives as long as one task, such as the answer to one request.

| Field | What it is |
|---|---|
| `memory: []Byte` | The memory that the pieces are given out of. |
| `used: Int` | How many bytes of the memory are given out, which is 0 for an arena that has given out nothing. |

| Procedure | What it does |
|---|---|
| `arena_allocate(arena: ^Arena_Allocator, size: Int) ([]Byte, Bool)` | Gives out `size` bytes, which are zero, and whether there was room for them. When there is none, or `size` is below zero, the bytes are none and the arena is as it was. |
| `arena_reset(arena: ^Arena_Allocator)` | Gives back everything that the arena gave out, which must not be used any more. |
| `arena_remaining(arena: ^Arena_Allocator) Int` | Gives the number of bytes that the arena can still give out in one piece. |
| `arena_aligned(offset: Int) Int` | Gives `offset` rounded up to a multiple of `ARENA_ALIGNMENT_BYTES`. |

```
import "mu:allocator"

answer :: proc() Int ->
    memory := make([]Byte, 4096)
    defer destroy(memory)
    arena := mu.Arena_Allocator { memory = memory, used = 0 }
    line, is_given := mu.arena_allocate(%arena, 80)
    if not is_given ->
        return 1
    line[0] = 72
    return 0
```

## math

Numbers are `Int` values, and the procedures work on every target.

| Procedure | What it does |
|---|---|
| `min(a: Int, b: Int) Int` | Gives the smaller of `a` and `b`. |
| `max(a: Int, b: Int) Int` | Gives the larger of `a` and `b`. |
| `clamp(number: Int, lowest: Int, highest: Int) Int` | Gives `number`, or `lowest` when it is below it, or `highest` when it is above it. `lowest` is not above `highest`. |

```
import "mu:math"

index := mu.clamp(position, 0, length_of(items) - 1)
```

## version

The version of the compiler is written once, in the file `VERSION` of the project, as `MAJOR.MINOR.PATCH`, see [Releasing](releasing.md#versioning). This file is what the scripts of the project use to check it.

| Procedure | What it does |
|---|---|
| `is_version(text: Str) Bool` | Whether `text` is a version of the form `MAJOR.MINOR.PATCH`: three numbers with a dot between each two, each of one or more digits and with no zero at the start unless the number is only that, as semantic versioning says. |
| `is_version_part(part: Str) Bool` | Whether `part` is one such number. |

## filesystem

Access to the filesystem of the system, for Windows only for now. A `File` is the handle that the system gives, with no buffer of its own and no lock. Every procedure takes the memory that it needs from its caller: the system takes a path in UTF-16, so the procedures that open a file by its path take a `wide_path` buffer, which is made of `WIDE_PATH_BYTES_PER_BYTE`, 2, bytes for each byte of the path, and two more for the zero that ends it. It is not needed after the call, so it can be used again, or be on the stack.

### Types

| Type | What it is |
|---|---|
| `File` | An open file: the handle of the system, in its field `handle`. A file that is not open has the handle `INVALID_HANDLE_VALUE`, -1. A copy of it is the same file, so a file is closed once. |
| `File_Open_Mode` | How a file is opened: `READ` for an existing file, `WRITE` from its start, made when it is not there and emptied when it is, and `APPEND` at its end, made when it is not there. |
| `Directory_Entry` | What `find_entry_first` says of an entry of a directory: `is_directory`, and `size_bytes`, the size of a file. |
| `File_Error` | Why a procedure did not do what it was asked: `NONE` when it did, and `NOT_FOUND`, `ACCESS_DENIED`, `IN_USE`, `PATH_INVALID`, `BUFFER_TOO_SMALL`, `FILE_INVALID`, `END_OF_FILE` and `OTHER`. |

### Procedures

| Procedure | What it does |
|---|---|
| `file_open(path: Str, mode: File_Open_Mode, wide_path: []Byte) (File, File_Error)` | Opens the file at `path`, a path in UTF-8, in the mode. A file that is not open comes back with the error. Close the file with `file_close`. |
| `file_close(file: File) File_Error` | Closes the file. `FILE_INVALID` is the answer for a file that is not open. |
| `file_standard_input() File` | The file that the program reads its input from: the console, or what the input was redirected from. It is not opened by the program and is not closed by it. |
| `file_read(file: File, buffer: []Byte) (Int, File_Error)` | Reads as many bytes as the system gives at once, at most as many as the buffer has, and gives how many. Zero bytes is the end of the file. A console gives a line at a time. |
| `file_read_all(path: Str, wide_path: []Byte, buffer: []Byte) (Int, File_Error)` | Opens the file, reads it until its end or until the buffer is full, closes it, and gives how many bytes it has. What is left of a file that is larger than the buffer is not read. |
| `file_read_line(file: File, buffer: []Byte) (Int, File_Error)` | Reads a line, without its end, and gives how long it is. A line feed ends it, and the carriage return before it is not part of it. A line that does not fit is cut where the buffer ends. At the end of the file it is `END_OF_FILE`. It reads a byte at a time, so it takes no byte of the next line, but it is slow for a large file. |
| `find_entry_first(directory_path: Str, target_entry: Str) (Directory_Entry, File_Error)` | Finds the entry that has the name `target_entry` in the directory, and says whether it is a directory and how big it is. It is the name of one entry and not a pattern: a `*` or a `?` in either text is `PATH_INVALID`, and an entry that is not there is `NOT_FOUND`. An empty directory is the current one. The path is made on the stack, up to `FIND_PATH_BYTES_MAX`, 1024, bytes. |
| `file_write(file: File, buffer: []Byte) (Int, File_Error)` | Writes as many bytes as the system takes at once, and gives how many. |
| `file_write_byte(file: File, value: Byte) File_Error` | Writes one byte. |
| `file_write_all(path: Str, wide_path: []Byte, buffer: []Byte) (Int, File_Error)` | Opens the file for writing, writes the whole buffer, and closes it. |

```
import "mu:filesystem"

read_first_line :: proc(path: Str, line: []Byte) Int ->
    wide_path := [512]Byte { .. = 0 }
    file, open_error := mu.file_open(path, .READ, wide_path[:])
    if open_error not= .NONE ->
        return -1
    defer mu.file_close(file)

    count, error := mu.file_read_line(file, line)
    if error not= .NONE ->
        return -1
    return count
```

The file has other definitions that are for its own procedures, such as the constants of the system and `file_error_from`, which gives the `File_Error` of the code that the system gives. They are public as everything is, but a program does not need them.

## process

The program that is running, for Windows only for now.

| Procedure | What it does |
|---|---|
| `process_command_line(buffer: []Byte) Int` | Copies the command line of the program into the buffer, and gives how many bytes it has. It is the name of the program and its arguments, each one between double quotes when it has a space or a double quote, with a backslash before each double quote in it. A command line that is longer than the buffer is cut. |
| `process_arguments_start(command_line: []Byte) Int` | Where the arguments start in a command line that `process_command_line` made: after the name of the program and the spaces that follow it. It is the length of the command line when there are none. |

```
import "mu:process"

main :: proc() Int ->
    command_line := [512]Byte { .. = 0 }
    length := mu.process_command_line(command_line[:])
    arguments := command_line[mu.process_arguments_start(command_line[0:length]):length]
    return length_of(arguments)
```

## What the library does not have yet

The library has what the programs of the project needed, and no more. The programs that are written in Mialu, the [scripts](releasing.md) and `mialudoc`, show what is missing, and where it was needed they note it with `MISSING:` in a comment. The most needed are:

- Sockets and the starting of a process, which `mialudoc` declares itself from the system.
- Listing the files of a directory and the size of a file.
- Text: joining strings when the program runs and the text of a number.
- Programs that run on something other than Windows.
