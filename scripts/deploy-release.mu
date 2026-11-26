/// Releases the version that is in the file VERSION, MAJOR.MINOR.PATCH: pushes main and a tag `v`
/// and the version, such as v0.5.0, to origin, which makes the release workflow of GitHub build and
/// publish it. Bump the version first with scripts/bump-version.mu and commit it.
///
/// It checks that the release is of main, with nothing uncommitted and nothing of origin that main
/// does not have, shows the commit that is released, and asks before it pushes. A tag that exists
/// is replaced only when that is answered yes. It is run with the compiler of this project, from the
/// root of the project, because the path of the file is relative to it, and git is on the path:
///
///     build\mialuc.exe run scripts/deploy-release.mu
///     build\mialuc.exe run scripts/deploy-release.mu --yes
///
/// The argument `--yes` leaves out the question before the push, which is not asked when a tag is
/// replaced either, because that was answered already. The exit code is 0 when the release was
/// pushed, 1 when it was cancelled or a check failed, and 2 when git or a file failed.
//
// MISSING FEATURES: running a program with its output, so that the commands of git are run by the
// shell of C with the output written to the file deploy-release.tmp, which is read and deleted; and
// the directory of the source file, so that the file VERSION could be found from anywhere.

package deploy_release

import "mu:filesystem"
import "mu:process"
import "mu:version"
import foreign libc

foreign libc ->
    fflush :: proc(stream: ^Byte) I32 -> ...
    memcpy :: proc(destination: ^Byte, source: C_Str, count: Int) ^Byte -> ...
    printf :: proc(format: C_Str, text: C_Str) I32 -> ...
    puts   :: proc(content: C_Str) I32 -> ...
    remove :: proc(path: C_Str) I32 -> ...
    system :: proc(command: ^Byte) I32 -> ...

/// The path of the file of the version, relative to the root of the project.
VERSION_PATH :: "VERSION"
SPACE : Byte : cast(Byte)' '
QUOTE : Byte : cast(Byte)'"'

/// The file that the output of a command of git is written to, and read from.
OUTPUT_PATH :: "deploy-release.tmp"

COMMAND_BYTES :: 512

/// The memory that the output of a command is read into at first, and the most that it grows to,
/// as it doubles while the output does not fit in it.
OUTPUT_BYTES :: 1024
OUTPUT_BYTES_MAX :: 16777216

/// Appends the characters of `text`, which are one byte each, to the first `length` bytes of
/// `buffer`, and gives the new length. The buffer must have room for them.
text_append :: proc(buffer: []Byte, length: Int, text: Str) Int ->
    memcpy(%buffer[length], str_to_c_str(text), length_of(text))
    return length + length_of(text)

/// Appends one byte like `text_append`.
byte_append :: proc(buffer: []Byte, length: Int, value: Byte) Int ->
    buffer[length] = value
    return length + 1

/// Gives the length of `bytes` without the line feeds, carriage returns and spaces at its end.
trimmed_length :: proc(bytes: []Byte) Int ->
    end := length_of(bytes)
    for end > 0 ->
        last := bytes[end - 1]
        if last not= '\n' and last not= '\r' and last not= SPACE ->
            break
        end -= 1
    return end

/// Writes `prefix` and `value` and a line feed.
say :: proc(prefix: Str, value: Str) ->
    printf("%s", str_to_c_str(prefix))
    printf("%s", str_to_c_str(value))
    puts("")

/// Writes `message` and gives the exit code of a check that failed.
fail :: proc(message: Str) Int ->
    puts(str_to_c_str(message))
    return 1

/// Writes the question and gives whether the line that is read from the input starts with y.
ask :: proc(question: Str) Bool ->
    printf("%s ", str_to_c_str(question))
    fflush(nil)
    line := [16]Byte { .. = 0 }
    count, error := mu.file_read_line(mu.file_standard_input(), line[:])
    answer := line[0]
    is_yes := answer == 'y' or answer == 'Y'
    return error == .NONE and count > 0 and is_yes

/// Runs the command in the first `length` bytes of `command` through the shell, with its output
/// written to the file of the output, and gives the output without the line feeds at its end, and
/// whether the command ended well, which git says with the exit code 0. The output is in memory
/// that is made for it, with a zero after it, so the caller gives it back with `destroy`, also
/// when the command failed, and then it is empty.
git :: proc(command: []Byte, length: Int) ([]Byte, Bool) ->
    total := text_append(command, length, " > " ++ OUTPUT_PATH)
    command[total] = 0
    status := system(%command[0])
    if status not= 0 ->
        remove(str_to_c_str(OUTPUT_PATH))
        return make([]Byte, 0), false

    // The output is read again into memory that is twice as large while it fills the memory, as
    // there may be more of it.
    wide_path := [128]Byte { .. = 0 }
    size := OUTPUT_BYTES
    for ->
        output := make([]Byte, size)
        count, error := mu.file_read_all(OUTPUT_PATH, wide_path[:], output[:size - 1])
        if error not= .NONE ->
            destroy(output)
            remove(str_to_c_str(OUTPUT_PATH))
            return make([]Byte, 0), false
        if count < size - 1 or size >= OUTPUT_BYTES_MAX ->
            remove(str_to_c_str(OUTPUT_PATH))
            trimmed := trimmed_length(output[:count])
            output[trimmed] = 0
            return output[:trimmed], true
        destroy(output)
        size *= 2

/// Runs `git` with the arguments, which have no quotes, like `git`.
git_arguments :: proc(arguments: Str) ([]Byte, Bool) ->
    command := [COMMAND_BYTES]Byte { .. = 0 }
    length := text_append(command[:], 0, "git ")
    length = text_append(command[:], length, arguments)
    output, is_ok := git(command[:], length)
    return output, is_ok

/// Runs `git` with the arguments, then a space, `value` and `suffix`, like `git`.
git_with :: proc(arguments: Str, value: Str, suffix: Str) ([]Byte, Bool) ->
    command := [COMMAND_BYTES]Byte { .. = 0 }
    length := text_append(command[:], 0, "git ")
    length = text_append(command[:], length, arguments)
    length = byte_append(command[:], length, SPACE)
    length = text_append(command[:], length, value)
    length = text_append(command[:], length, suffix)
    output, is_ok := git(command[:], length)
    return output, is_ok

/// Makes the tag `tag` that has the message `Release ` and `version`, which is of the commit that is
/// checked out, and with `is_replacing` it is the tag that exists, made again. Gives whether it was.
tag_make :: proc(tag: Str, version: Str, is_replacing: Bool) Bool ->
    command := [COMMAND_BYTES]Byte { .. = 0 }
    length := text_append(command[:], 0, "git tag ")
    if is_replacing ->
        length = text_append(command[:], length, "-f ")
    length = text_append(command[:], length, "-a ")
    length = text_append(command[:], length, tag)
    length = text_append(command[:], length, " -m ")
    length = byte_append(command[:], length, QUOTE)
    length = text_append(command[:], length, "Release ")
    length = text_append(command[:], length, version)
    length = byte_append(command[:], length, QUOTE)
    output, is_ok := git(command[:], length)
    destroy(output)
    return is_ok

main :: proc() Int ->
    // What follows the name of the program in its command line is the arguments.
    command_line := [256]Byte { .. = 0 }
    command_length := mu.process_command_line(command_line[:])
    arguments_bytes := command_line[mu.process_arguments_start(command_line[:command_length]):command_length]
    arguments, is_arguments_text := utf8_decode(arguments_bytes)
    is_yes := arguments == "--yes"
    if length_of(arguments_bytes) > 0 and not is_yes ->
        return fail("The only argument is --yes, which leaves out the question before the push.")

    // The version is the one line of the file VERSION.
    version_bytes := [64]Byte { .. = 0 }
    wide_path := [128]Byte { .. = 0 }
    read_length, read_error := mu.file_read_all(VERSION_PATH, wide_path[:], version_bytes[:])
    if read_error not= .NONE ->
        puts("The file VERSION cannot be read: run this from the root of the project.")
        return 2
    version, is_version_text := utf8_decode(version_bytes[:trimmed_length(version_bytes[:read_length])])
    if not is_version_text or not mu.is_version(version) ->
        say("VERSION says that the version is ", version)
        return fail("That is not of the form MAJOR.MINOR.PATCH, such as 0.5.0.")

    tag_bytes := [80]Byte { .. = 0 }
    tag_length := text_append(tag_bytes[:], 0, "v")
    tag_length = text_append(tag_bytes[:], tag_length, version)
    tag, is_tag_text := utf8_decode(tag_bytes[:tag_length])

    // Each output is in its own memory, which is given back when the program ends, as the text that
    // is read from it views it.
    branch_output, is_ok := git_arguments("rev-parse --abbrev-ref HEAD")
    defer destroy(branch_output)
    if not is_ok ->
        return 2
    branch, is_branch_text := utf8_decode(branch_output)
    if branch not= "main" ->
        say("The current branch is ", branch)
        return fail("A release is made from main.")

    // What is not committed would not be in the release. Untracked files do not matter.
    status_output, is_ok1 := git_arguments("status --porcelain --untracked-files=no")
    defer destroy(status_output)
    if not is_ok1 ->
        return 2
    if length_of(status_output) > 0 ->
        puts("There are uncommitted changes. Commit them first:")
        changes, is_changes_text := utf8_decode(status_output)
        puts(str_to_c_str(changes))
        return 1

    fetch_output, is_ok2 := git_arguments("fetch origin main --tags")
    defer destroy(fetch_output)
    if not is_ok2 ->
        return 2

    behind_output, is_ok3 := git_arguments("rev-list --count HEAD..origin/main")
    defer destroy(behind_output)
    if not is_ok3 ->
        return 2
    behind, is_behind_text := utf8_decode(behind_output)
    if behind not= "0" ->
        say("Commits that origin/main has and main does not: ", behind)
        return fail("Pull first.")

    // The fetch above brought the tags of GitHub, so a tag that is only there is found too.
    is_replacing := false
    tags_output, is_ok4 := git_with("tag --list", tag, "")
    defer destroy(tags_output)
    if not is_ok4 ->
        return 2
    if length_of(tags_output) > 0 ->
        // `~0` is the commit of the tag, which is what a tag with a message points to through.
        tag_commit_output, is_ok5 := git_with("log -1 --oneline", tag, "~0")
        defer destroy(tag_commit_output)
        if not is_ok5 ->
            return 2
        tag_commit, is_tag_commit_text := utf8_decode(tag_commit_output)
        printf("The tag %s ", str_to_c_str(tag))
        say("already exists, on: ", tag_commit)
        if not ask("Replace the tag with a new release of the current commit? [y/n]") ->
            return fail("Cancelled. To release a new version, bump it first with scripts/bump-version.mu.")
        is_replacing = true

    ahead_output, is_ok6 := git_arguments("rev-list --count origin/main..HEAD")
    defer destroy(ahead_output)
    if not is_ok6 ->
        return 2
    ahead, is_ahead_text := utf8_decode(ahead_output)
    commit_output, is_ok7 := git_arguments("log -1 --oneline")
    defer destroy(commit_output)
    if not is_ok7 ->
        return 2
    commit, is_commit_text := utf8_decode(commit_output)
    say("Version: ", version)
    say("Commit:  ", commit)
    say("New commits of main to push: ", ahead)
    say("Tag to push: ", tag)

    // Replacing the tag was confirmed above, so it is not asked a second time.
    if not is_yes and not is_replacing ->
        if not ask("Create the tag and push? [y/N]") ->
            return fail("Cancelled.")

    // The commits go first, so that the tag is never on a commit that GitHub does not have on main.
    push_output, is_ok8 := git_arguments("push origin main")
    defer destroy(push_output)
    if not is_ok8 ->
        return 2
    if not tag_make(tag, version, is_replacing) ->
        return 2
    push_arguments := "push origin"
    if is_replacing ->
        push_arguments = "push --force origin"
    tag_push_output, is_ok9 := git_with(push_arguments, tag, "")
    defer destroy(tag_push_output)
    if not is_ok9 ->
        return 2

    say("Pushed ", tag)
    puts("The release workflow is running: see the Actions tab of the repository.")
    return 0
