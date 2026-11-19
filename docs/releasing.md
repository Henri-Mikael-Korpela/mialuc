# Releasing

Both scripts, the one for versioning and the one for releasing, are programs of Mialu, so they need nothing to install on Windows but the compiler of this project, and git for releasing.

## Versioning

The version of the compiler is written once, in the file [VERSION](../VERSION) in the root of the project: a semantic version of the form `MAJOR.MINOR.PATCH`, such as `0.4.0`, without zeros at the start, on a line of its own. Nothing else has it. CMake reads the file when it configures the project, and it stops with an error when the file says anything else. It gives the version to the compiler as a definition of the command line of GCC and Clang, `-DMIALU_VERSION_STRING="0.4.0"`, which is also in `compile_commands.json` for the tools that read it, so there is no header to keep in step, and `mialuc --version` prints it. A change of the file configures the project again by itself, and the compiler has the new version when it is built again.

`scripts/bump-version.mu` sets it. It is a program of Mialu, run with the compiler of this project from the root of the project, because the path of the file is relative to it, and the version is the argument that comes after the source file, which `mialuc run` gives to the program as if the built program had been called with it:

```
build\mialuc.exe run scripts/bump-version.mu 0.5.0
```

When there is no argument, the program asks for the version and reads a line of the standard input, so the version can also be typed, or piped:

```
build\mialuc.exe run scripts/bump-version.mu
echo 0.5.0| build\mialuc.exe run scripts/bump-version.mu
```

The file is not changed when what it is given is not a version of the form `MAJOR.MINOR.PATCH` without zeros at the start. Build the compiler again after the version is set, and commit the file.

## Releasing

After the version is bumped and committed, `scripts/deploy-release.mu` reads the version from the file `VERSION`, tags the commit of main as `v<version>` and pushes it, which starts the release workflow in [.github/workflows/release.yml](../.github/workflows/release.yml). It refuses to run with uncommitted changes, off main, or behind `origin/main`, shows the commit and asks before pushing unless it is given `--yes`, and when the tag exists it asks whether to replace it, which is the only way that a tag is replaced. Like the script for versioning it is run with the compiler of this project, from the root of the project, and it runs `git`, which must be on the path:

```
build\mialuc.exe run scripts/deploy-release.mu
build\mialuc.exe run scripts/deploy-release.mu --yes
```

The exit code is 0 when the release was pushed, 1 when it was cancelled or a check failed, and 2 when git or a file failed. The program has no way yet to read the output of a program that it runs, so it runs git through the shell of C with the output written to the file `deploy-release.tmp` in the directory it is run in, which it reads and deletes.
