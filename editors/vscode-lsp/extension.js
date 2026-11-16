// The client of the Mialu language server in VS Code. It starts server.js, which speaks the
// Language Server Protocol, keeps it told about the text of the Mialu documents, and shows the code
// lenses that it answers with, which are the sizes of the structs above their definitions.
//
// The extension is plain JavaScript that uses only Node.js and the API of VS Code, so there is
// nothing to build and nothing to install with a package manager. That is why it has a small
// client of its own, which does what this server needs, instead of the library vscode-languageclient.

"use strict";

const childProcess = require("child_process");
const fs = require("fs");
const path = require("path");
const vscode = require("vscode");

const LANGUAGE_ID = "mialu";

/**
 * A connection to a server that is a child process: requests that resolve to the result of the
 * answer, and notifications. The messages are framed with a header that has their length in bytes.
 */
class Connection {
    constructor(command, arguments_, options, output) {
        this.output = output;
        this.nextId = 1;
        this.waiting = new Map();
        this.pending = Buffer.alloc(0);
        this.process = childProcess.spawn(command, arguments_, options);
        this.process.stdout.on("data", (chunk) => this.receive(chunk));
        this.process.stderr.on("data", (chunk) => output.append(chunk.toString()));
        this.process.on("exit", (code) => {
            output.appendLine(`The language server ended with ${code}.`);
            for (const { reject } of this.waiting.values()) {
                reject(new Error("The language server ended."));
            }
            this.waiting.clear();
        });
    }

    request(method, params) {
        return new Promise((resolve, reject) => {
            const id = this.nextId++;
            this.waiting.set(id, { resolve, reject });
            this.write({ id, method, params });
        });
    }

    notify(method, params) {
        this.write({ method, params });
    }

    write(message) {
        const body = JSON.stringify({ jsonrpc: "2.0", ...message });
        this.process.stdin.write(`Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`);
    }

    receive(chunk) {
        this.pending = Buffer.concat([this.pending, chunk]);
        for (;;) {
            const headerEnd = this.pending.indexOf("\r\n\r\n");
            if (headerEnd === -1) {
                return;
            }
            const header = /Content-Length: *(\d+)/i.exec(this.pending.toString("ascii", 0, headerEnd));
            if (header === null) {
                this.pending = Buffer.alloc(0);
                return;
            }
            const bodyEnd = headerEnd + 4 + Number(header[1]);
            if (this.pending.length < bodyEnd) {
                return;
            }
            const message = JSON.parse(this.pending.toString("utf8", headerEnd + 4, bodyEnd));
            this.pending = this.pending.subarray(bodyEnd);

            const answered = this.waiting.get(message.id);
            if (answered === undefined) {
                // A request or a notification of the server. It asks for nothing that this client
                // can give, so it is not answered.
                continue;
            }
            this.waiting.delete(message.id);
            if (message.error) {
                answered.reject(new Error(message.error.message));
            } else {
                answered.resolve(message.result);
            }
        }
    }

    /**
     * Ends the session the way the protocol says, and then the process, which is killed when it
     * does not end by itself.
     */
    async stop() {
        try {
            await this.request("shutdown", null);
            this.notify("exit", null);
        } catch (error) {
            // The server is gone already.
        }
        setTimeout(() => this.process.kill(), 1000).unref();
    }
}

/**
 * Starts the server and the sync of the documents, and registers the code lens provider.
 */
async function activate(context) {
    const output = vscode.window.createOutputChannel("Mialu Language Server");
    context.subscriptions.push(output);

    const folder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    const settings = vscode.workspace.getConfiguration("mialu.lsp");
    const connection = new Connection(
        process.execPath, [path.join(__dirname, "server.js")],
        // The process of the extension host is Electron, which runs a script like Node.js with this.
        { env: { ...process.env, ELECTRON_RUN_AS_NODE: "1" }, windowsHide: true }, output);
    context.subscriptions.push({ dispose: () => connection.stop() });

    await connection.request("initialize", {
        processId: process.pid,
        rootUri: folder === undefined ? null : vscode.Uri.file(folder).toString(),
        capabilities: {},
        initializationOptions: { compilerPath: compilerPath(settings, folder) },
    });
    connection.notify("initialized", {});

    const lensesChanged = new vscode.EventEmitter();
    const isMialu = (document) => document.languageId === LANGUAGE_ID;
    const open = (document) => {
        if (isMialu(document)) {
            connection.notify("textDocument/didOpen", {
                textDocument: {
                    uri: document.uri.toString(),
                    languageId: LANGUAGE_ID,
                    version: document.version,
                    text: document.getText(),
                },
            });
        }
    };
    vscode.workspace.textDocuments.forEach(open);

    context.subscriptions.push(
        lensesChanged,
        vscode.workspace.onDidOpenTextDocument(open),
        vscode.workspace.onDidChangeTextDocument(({ document }) => {
            if (isMialu(document)) {
                connection.notify("textDocument/didChange", {
                    textDocument: { uri: document.uri.toString(), version: document.version },
                    contentChanges: [{ text: document.getText() }],
                });
                lensesChanged.fire();
            }
        }),
        vscode.workspace.onDidCloseTextDocument((document) => {
            if (isMialu(document)) {
                connection.notify("textDocument/didClose", {
                    textDocument: { uri: document.uri.toString() },
                });
            }
        }),
        vscode.workspace.onDidChangeConfiguration((event) => {
            if (event.affectsConfiguration("mialu.lsp.structSizes")) {
                lensesChanged.fire();
            }
        }),
        vscode.languages.registerCodeLensProvider({ language: LANGUAGE_ID }, {
            onDidChangeCodeLenses: lensesChanged.event,
            async provideCodeLenses(document) {
                if (!vscode.workspace.getConfiguration("mialu.lsp").get("structSizes", true)) {
                    return [];
                }
                try {
                    const lenses = await connection.request("textDocument/codeLens", {
                        textDocument: { uri: document.uri.toString() },
                    });
                    return lenses.map((lens) => new vscode.CodeLens(
                        new vscode.Range(lens.range.start.line, lens.range.start.character,
                            lens.range.end.line, lens.range.end.character),
                        // A command with no name is shown as text and does nothing.
                        { title: lens.command.title, command: "" }));
                } catch (error) {
                    output.appendLine(String(error.message ?? error));
                    return [];
                }
            },
        }),
    );
}

function deactivate() {}

/**
 * Returns the setting for the compiler, or the compiler of the workspace when it was built there,
 * which is where the project of the compiler builds it, or an empty string, which makes the server
 * use the one on the PATH.
 */
function compilerPath(settings, folder) {
    const configured = settings.get("compilerPath");
    if (configured) {
        return configured;
    }
    const built = folder === undefined ? undefined : path.join(folder, "build", "mialuc.exe");
    return built !== undefined && fs.existsSync(built) ? built : "";
}

module.exports = { activate, deactivate };
