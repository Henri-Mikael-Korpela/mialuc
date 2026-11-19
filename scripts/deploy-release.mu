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

/// The file that the output of a command of git is written to, and read from.
OUTPUT_PATH :: "deploy-release.tmp"

COMMAND_BYTES :: 512
OUTPUT_BYTES :: 1024

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
        if last not= 10 and last not= 13 and last not= ' ' ->
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
/// written to the file of the output, and reads that into `output`, which has the output without
/// the line feeds at its end and a zero after it. Gives how many bytes the output has, and whether
/// the command ended well, which git says with the exit code 0.
git :: proc(command: []Byte, length: Int, output: []Byte) (Int, Bool) ->
    total := text_append(command, length, " > " ++ OUTPUT_PATH)
    command[total] = 0
    status := system(%command[0])
    if status not= 0 ->
        remove(str_to_c_str(OUTPUT_PATH))
        return 0, false

    wide_path := [128]Byte { .. = 0 }
    count, error := mu.file_read_all(OUTPUT_PATH, wide_path[:], output[:length_of(output) - 1])
    remove(str_to_c_str(OUTPUT_PATH))
    if error not= .NONE ->
        return 0, false
    trimmed := trimmed_length(output[:count])
    output[trimmed] = 0
    return trimmed, true

/// Runs `git` with the arguments, which have no quotes, like `git`.
git_arguments :: proc(arguments: Str, output: []Byte) (Int, Bool) ->
    command := [COMMAND_BYTES]Byte { .. = 0 }
    length := text_append(command[:], 0, "git ")
    length = text_append(command[:], length, arguments)
    count, is_ok := git(command[:], length, output)
    return count, is_ok

/// Runs `git` with the arguments, then a space, `value` and `suffix`, like `git`.
git_with :: proc(arguments: Str, value: Str, suffix: Str, output: []Byte) (Int, Bool) ->
    command := [COMMAND_BYTES]Byte { .. = 0 }
    length := text_append(command[:], 0, "git ")
    length = text_append(command[:], length, arguments)
    length = byte_append(command[:], length, 32)
    length = text_append(command[:], length, value)
    length = text_append(command[:], length, suffix)
    count, is_ok := git(command[:], length, output)
    return count, is_ok

/// Makes the tag `tag` that has the message `Release ` and `version`, which is of the commit that is
/// checked out, and with `is_replacing` it is the tag that exists, made again. Gives whether it was.
tag_make :: proc(tag: Str, version: Str, is_replacing: Bool, output: []Byte) Bool ->
    command := [COMMAND_BYTES]Byte { .. = 0 }
    length := text_append(command[:], 0, "git tag ")
    if is_replacing ->
        length = text_append(command[:], length, "-f ")
    length = text_append(command[:], length, "-a ")
    length = text_append(command[:], length, tag)
    length = text_append(command[:], length, " -m ")
    length = byte_append(command[:], length, 34)
    length = text_append(command[:], length, "Release ")
    length = text_append(command[:], length, version)
    length = byte_append(command[:], length, 34)
    count, is_ok := git(command[:], length, output)
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

    output := [OUTPUT_BYTES]Byte { .. = 0 }

    // The workflow only releases a tag that is on main.
    count, is_ok := git_arguments("rev-parse --abbrev-ref HEAD", output[:])
    if not is_ok ->
        return 2
    branch, is_branch_text := utf8_decode(output[:count])
    if branch not= "main" ->
        say("The current branch is ", branch)
        return fail("A release is made from main.")

    // What is not committed would not be in the release. Untracked files do not matter.
    count1, is_ok1 := git_arguments("status --porcelain --untracked-files=no", output[:])
    if not is_ok1 ->
        return 2
    if count1 > 0 ->
        puts("There are uncommitted changes. Commit them first:")
        changes, is_changes_text := utf8_decode(output[:count1])
        puts(str_to_c_str(changes))
        return 1

    count2, is_ok2 := git_arguments("fetch origin main --tags", output[:])
    if not is_ok2 ->
        return 2

    count3, is_ok3 := git_arguments("rev-list --count HEAD..origin/main", output[:])
    if not is_ok3 ->
        return 2
    behind, is_behind_text := utf8_decode(output[:count3])
    if behind not= "0" ->
        say("Commits that origin/main has and main does not: ", behind)
        return fail("Pull first.")

    // The fetch above brought the tags of GitHub, so a tag that is only there is found too.
    is_replacing := false
    count4, is_ok4 := git_with("tag --list", tag, "", output[:])
    if not is_ok4 ->
        return 2
    if count4 > 0 ->
        // `~0` is the commit of the tag, which is what a tag with a message points to through.
        count5, is_ok5 := git_with("log -1 --oneline", tag, "~0", output[:])
        if not is_ok5 ->
            return 2
        tag_commit, is_tag_commit_text := utf8_decode(output[:count5])
        printf("The tag %s ", str_to_c_str(tag))
        say("already exists, on: ", tag_commit)
        if not ask("Replace the tag with a new release of the current commit? [y/n]") ->
            return fail("Cancelled. To release a new version, bump it first with scripts/bump-version.mu.")
        is_replacing = true

    // The count is kept in its own memory, because the text is where the output was read to.
    ahead_output := [16]Byte { .. = 0 }
    count6, is_ok6 := git_arguments("rev-list --count origin/main..HEAD", ahead_output[:])
    if not is_ok6 ->
        return 2
    ahead, is_ahead_text := utf8_decode(ahead_output[:count6])
    count7, is_ok7 := git_arguments("log -1 --oneline", output[:])
    if not is_ok7 ->
        return 2
    commit, is_commit_text := utf8_decode(output[:count7])
    say("Version: ", version)
    say("Commit:  ", commit)
    say("New commits of main to push: ", ahead)
    say("Tag to push: ", tag)

    // Replacing the tag was confirmed above, so it is not asked a second time.
    if not is_yes and not is_replacing ->
        if not ask("Create the tag and push? [y/N]") ->
            return fail("Cancelled.")

    // The commits go first, so that the tag is never on a commit that GitHub does not have on main.
    count8, is_ok8 := git_arguments("push origin main", output[:])
    if not is_ok8 ->
        return 2
    if not tag_make(tag, version, is_replacing, output[:]) ->
        return 2
    push_arguments := "push origin"
    if is_replacing ->
        push_arguments = "push --force origin"
    count9, is_ok9 := git_with(push_arguments, tag, "", output[:])
    if not is_ok9 ->
        return 2

    say("Pushed ", tag)
    puts("The release workflow is running: see the Actions tab of the repository.")
    return 0
