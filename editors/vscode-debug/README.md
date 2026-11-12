# Mialu Debug for VS Code

Debugging of Mialu programs in VS Code: breakpoints in `.mu` files, stepping, the call stack, and the parameters, variables and string constants of the procedures. It is an extension of its own next to the [language extension](../vscode/README.md), which it needs for the language `mialu`.

A debug session of the type `mialu` builds its source file with `mialuc build --target x64-win32 --debug`, which writes a program with DWARF debug information, see [mialuc](../../docs/mialu-cli.md#debugging), and then debugs the program with gdb. gdb has a debug adapter of its own since version 14, `gdb -i=dap`, which speaks the protocol that VS Code speaks, so the extension only builds the program and starts gdb, and needs no other extension.

## Requirements

- Windows on x64, which the programs of the `x64-win32` target run on.
- gdb 14 or newer with Python, such as the one of [MinGW-w64](https://github.com/niXman/mingw-builds-binaries/releases), on the `PATH` or in the setting `mialu.debug.gdbPath`.
    - Tested with GDB 17.2 (MinGW-Builds, `x86_64-win32-seh`).
- mialuc: `build/mialuc.exe` in the folder of the workspace when it is there, which is where the project of the compiler builds it, otherwise `mialuc` on the `PATH`, or the setting `mialu.debug.compilerPath`.

## Install

VS Code only loads extensions from its extensions folder, so link this folder into it with the script that links the language extension too:

```
powershell -File scripts/install_vscode_extension.ps1
```

Then run "Developer: Reload Window" in VS Code.

## Use

Open a `.mu` file and press F5: without a `launch.json` the file in the editor is debugged. Click to the left of a line number to set a breakpoint. A `launch.json` can name the file and more:

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "type": "mialu",
            "request": "launch",
            "name": "Debug hello",
            "program": "${workspaceFolder}/test-code/x64/foreign-functions-libc.mu",
            "stopAtEntry": true
        }
    ]
}
```

The attributes of a configuration:

| Attribute | Meaning |
|-----------|---------|
| `program` | The Mialu source file to build and debug. Required. |
| `args` | The command line arguments of the program. |
| `cwd` | The directory that the program runs in. The folder of the workspace by default. |
| `stopAtEntry` | Stop at the first statement of `main`. False by default. |
| `output` | Where the program is written. `build/debug/<name>.exe` in the folder of the workspace by default. |
| `compilerPath` | The path of mialuc, instead of the setting `mialu.debug.compilerPath`. |
| `gdbPath` | The path of gdb, instead of the setting `mialu.debug.gdbPath`. |

What the program prints is in the Debug Console. When the build fails, the session does not start, and the output of the compiler is in the output channel "Mialu Debug".

## What is shown

- The call stack has the procedures of the program, and `start`, the code of the compiler that calls `main`. A procedure of the C library is shown without a source.
- The Variables view has the parameters of the procedure under Arguments, its variables under Locals, and the constants that are strings under Globals. An `Int` is a number and a `C_Str` is its text. A variable has no value of its own before the line that defines it, so it shows what is in its place in the frame until then.
- An expression in the Watch view or the Debug Console is one of gdb, in the C language, because gdb has no Mialu, such as `count` or `message[0]`.
