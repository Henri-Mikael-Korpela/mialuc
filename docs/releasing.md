# Releasing

The script for versioning is a program of Mialu, and the one for releasing uses PowerShell, so they need nothing to install on Windows but the compiler of this project.

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

After the version is bumped and committed, `scripts/deploy-release.ps1` reads the version from the file `VERSION`, tags the commit of main as `v<version>` and pushes it, which starts the release workflow in [.github/workflows/release.yml](../.github/workflows/release.yml). It refuses to run with uncommitted changes, off main, behind `origin/main`, or when the tag exists, and asks before pushing unless it is given `-Yes`.

```
powershell -File scripts/deploy-release.ps1
```
