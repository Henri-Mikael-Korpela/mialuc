// Debugging of Mialu programs in VS Code. A debug configuration of the type `mialu` names a source
// file. Before the session starts, the file is built with `mialuc build --target x64-win32 --debug`,
// and the session is given to gdb, which has a debug adapter of its own since version 14:
// `gdb -i=dap` speaks the Debug Adapter Protocol that VS Code speaks, so nothing is in between. gdb
// reads the DWARF debug information that the compiler writes, which has the lines, the procedures,
// their parameters and variables, and the frames.
//
// The extension is plain JavaScript that uses only Node.js and the API of VS Code, so there is
// nothing to build and nothing to install with a package manager, like the language extension.

"use strict";

const childProcess = require("child_process");
const fs = require("fs");
const path = require("path");
const vscode = require("vscode");

const DEBUG_TYPE = "mialu";

/**
 * Registers the debug configurations of the type `mialu` and the debug adapter that runs them.
 */
function activate(context) {
    const output = vscode.window.createOutputChannel("Mialu Debug");
    context.subscriptions.push(
        output,
        vscode.debug.registerDebugConfigurationProvider(DEBUG_TYPE, new ConfigurationProvider(output)),
        vscode.debug.registerDebugAdapterDescriptorFactory(DEBUG_TYPE, new GdbAdapterFactory()),
    );
}

function deactivate() {}

/**
 * Fills in a debug configuration, and builds its program before the session starts.
 */
class ConfigurationProvider {
    constructor(output) {
        this.output = output;
    }

    /**
     * Starting to debug without a launch.json debugs the Mialu file that is open in the editor.
     */
    resolveDebugConfiguration(folder, configuration) {
        if (!configuration.type && !configuration.request && !configuration.name) {
            const editor = vscode.window.activeTextEditor;
            if (editor === undefined || editor.document.languageId !== "mialu") {
                vscode.window.showErrorMessage("Open a Mialu file (.mu) to debug it.");
                return undefined;
            }
            configuration.type = DEBUG_TYPE;
            configuration.request = "launch";
            configuration.name = "Debug Mialu file";
            configuration.program = "${file}";
        }
        if (!configuration.program) {
            vscode.window.showErrorMessage('The debug configuration has no "program", the Mialu file to debug.');
            return undefined;
        }
        return configuration;
    }

    /**
     * Builds the program, after the variables such as `${file}` were replaced, and gives gdb the
     * executable instead of the source. Returns undefined, which stops the session, when the build
     * fails.
     */
    async resolveDebugConfigurationWithSubstitutedVariables(folder, configuration) {
        const settings = vscode.workspace.getConfiguration("mialu.debug", folder);
        const workspaceDirectory = folder !== undefined ? folder.uri.fsPath : undefined;
        const source = path.resolve(workspaceDirectory ?? process.cwd(), configuration.program);
        const baseDirectory = workspaceDirectory ?? path.dirname(source);

        const compiler = configuration.compilerPath || settings.get("compilerPath") ||
            defaultCompilerPath(baseDirectory);
        const gdb = configuration.gdbPath || settings.get("gdbPath") || "gdb";
        const executable = configuration.output ||
            path.join(baseDirectory, "build", "debug", path.basename(source, ".mu") + ".exe");

        const arguments_ = ["build", "--target", "x64-win32", "--debug", "-o", executable, source];
        this.output.appendLine(`> ${compiler} ${arguments_.join(" ")}`);
        try {
            fs.mkdirSync(path.dirname(executable), { recursive: true });
            await run(compiler, arguments_, baseDirectory);
        } catch (error) {
            const message = String(error.message ?? error).trim();
            this.output.appendLine(message);
            const choice = await vscode.window.showErrorMessage(
                `Building ${path.basename(source)} failed: ${message.split("\n")[0]}`, "Show Output");
            if (choice === "Show Output") {
                this.output.show(true);
            }
            return undefined;
        }

        // These are the arguments of the launch request of gdb, which ignores the others. `start`,
        // which `stopAtBeginningOfMainSubprogram` asks for, stops at the first statement of `main`.
        return {
            ...configuration,
            program: executable,
            source,
            cwd: configuration.cwd || baseDirectory,
            args: configuration.args ?? [],
            stopAtBeginningOfMainSubprogram: configuration.stopAtEntry === true,
            gdbPath: gdb,
        };
    }
}

/**
 * Runs gdb with its debug adapter for a session.
 */
class GdbAdapterFactory {
    createDebugAdapterDescriptor(session) {
        return new vscode.DebugAdapterExecutable(session.configuration.gdbPath || "gdb", ["-i=dap"]);
    }
}

/**
 * Returns the compiler of the workspace when it was built there, which is where the project of the
 * compiler builds it, and otherwise the one on the PATH.
 */
function defaultCompilerPath(directory) {
    const built = path.join(directory, "build", "mialuc.exe");
    return fs.existsSync(built) ? built : "mialuc";
}

/**
 * Runs `command` with `arguments_` in `directory`, and rejects with what it printed when it fails.
 */
function run(command, arguments_, directory) {
    return new Promise((resolve, reject) => {
        childProcess.execFile(command, arguments_, { cwd: directory }, (error, stdout, stderr) => {
            if (error) {
                const printed = `${stderr}${stdout}`.trim();
                reject(new Error(printed || error.message));
                return;
            }
            resolve();
        });
    });
}

module.exports = { activate, deactivate };
