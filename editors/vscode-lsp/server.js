// A language server for Mialu, which speaks the Language Server Protocol over the standard input
// and output, so any editor that has a client for it can use it. It has two features. The first is
// a code lens above the definition of each struct that says how large the struct is in memory:
//
//     Size: 24 bytes | Alignment: 8 bytes
//
// The second is a hover: resting the pointer on the name of a constant, a global variable, a
// parameter or a variable shows its type, such as `LIMIT: untyped Int`.
//
// The server does not work any of that out itself. It asks the compiler, which knows the layouts
// and the types: `mialuc dump-struct-sizes <file>` prints a line for each struct and `mialuc
// dump-types <file>` a line for each name, see docs/mialu-cli.md. The text of an open document is
// written to a temporary file first, so the answers follow what is typed and not only what was
// saved.
//
// Plain JavaScript that uses only Node.js, so there is nothing to build and nothing to install with
// a package manager, like the other extensions in editors/.
//
// The path of the compiler is `initializationOptions.compilerPath`, otherwise the environment
// variable `MIALUC`, otherwise `mialuc` on the PATH.

"use strict";

const childProcess = require("child_process");
const fs = require("fs");
const os = require("os");
const path = require("path");

// The error codes of JSON-RPC and of the protocol that this server answers with.
const ERROR_METHOD_NOT_FOUND = -32601;
const ERROR_SERVER_NOT_INITIALIZED = -32002;

// How a document is kept in sync: the whole text is sent on every change.
const TEXT_DOCUMENT_SYNC_FULL = 1;

/**
 * The open documents by URI: the text, and the lenses and the names with their types that were made
 * from the last text that the compiler accepted, which are used while the text does not compile.
 */
const documents = new Map();

const state = {
    compilerPath: process.env.MIALUC || "mialuc",
    isInitialized: false,
    isShutdown: false,
};

/**
 * Writes one message, framed with a header that has its length in bytes, as the protocol says.
 */
function send(message) {
    const body = Buffer.from(JSON.stringify({ jsonrpc: "2.0", ...message }), "utf8");
    process.stdout.write(`Content-Length: ${body.length}\r\n\r\n`);
    process.stdout.write(body);
}

function reply(id, result) {
    send({ id, result });
}

function replyError(id, code, message) {
    send({ id, error: { code, message } });
}

/**
 * Turns the output of `mialuc dump-struct-sizes` into code lenses. A line of the output is the line
 * of the definition counted from 1, the name, the size and the alignment, separated by tabs. A
 * line that is not like that is skipped.
 */
function lensesFromOutput(output) {
    const lenses = [];
    for (const row of output.split("\n")) {
        const [lineText, name, sizeText, alignmentText] = row.trim().split("\t");
        const line = Number(lineText);
        if (!Number.isInteger(line) || line < 1 || !name || sizeText === undefined ||
            alignmentText === undefined) {
            continue;
        }
        const position = { line: line - 1, character: 0 };
        lenses.push({
            range: { start: position, end: position },
            // A lens needs a command to be shown. This one has no name, so it does nothing when it
            // is clicked, and is only the text.
            command: {
                title: `Size: ${sizeText} bytes | Alignment: ${alignmentText} bytes`,
                command: "",
            },
        });
    }
    return lenses;
}

/**
 * Turns the output of `mialuc dump-types` into the names with their types. A line of the output is
 * the line and the column of a name, both counted from 1 in bytes, the length of the name in bytes,
 * the name and the type, separated by tabs. A line that is not like that is skipped.
 */
function namesFromOutput(output) {
    const names = [];
    for (const row of output.split("\n")) {
        const [lineText, columnText, lengthText, name, type] = row.replace(/\r$/, "").split("\t");
        const line = Number(lineText);
        const column = Number(columnText);
        const length = Number(lengthText);
        if (!Number.isInteger(line) || line < 1 || !Number.isInteger(column) || column < 1 ||
            !Number.isInteger(length) || length < 1 || !name || !type) {
            continue;
        }
        names.push({ line: line - 1, column: column - 1, length, name, type });
    }
    return names;
}

/**
 * Runs the compiler with `command` on `text`. Resolves to what the compiler printed, or to null
 * when the compiler does not accept the text, such as while it is being typed, or cannot be run.
 */
function compilerRun(command, text) {
    return new Promise((resolve) => {
        let directory;
        try {
            directory = fs.mkdtempSync(path.join(os.tmpdir(), "mialu-lsp-"));
            fs.writeFileSync(path.join(directory, "document.mu"), text, "utf8");
        } catch (error) {
            resolve(null);
            return;
        }
        const file = path.join(directory, "document.mu");
        childProcess.execFile(state.compilerPath, [command, file],
            { timeout: 10000, windowsHide: true }, (error, stdout) => {
                fs.rmSync(directory, { recursive: true, force: true });
                resolve(error ? null : stdout);
            });
    });
}

/**
 * Asks the compiler for the sizes of the structs of `text`. Resolves to the lenses, or to null
 * when the compiler does not accept the text.
 */
async function lensesCompute(text) {
    const output = await compilerRun("dump-struct-sizes", text);
    return output === null ? null : lensesFromOutput(output);
}

/**
 * Asks the compiler for the types of the names of `text`. Resolves to the names, or to null when
 * the compiler does not accept the text.
 */
async function namesCompute(text) {
    const output = await compilerRun("dump-types", text);
    return output === null ? null : namesFromOutput(output);
}

/**
 * Answers a hover: the type of the name that `position` is on, or null when it is on no name that
 * has a type. The columns of the compiler are bytes of UTF-8 and the protocol counts UTF-16 code
 * units, so they are converted. While the text does not compile, the names of the last text that
 * did are used, and a name is only shown where the text still has it.
 */
async function hover(uri, position) {
    const document = documents.get(uri);
    if (document === undefined) {
        return null;
    }
    const text = document.text;
    const names = await namesCompute(text);
    if (names !== null && document.text === text) {
        document.names = names;
    }

    const lineText = document.text.split("\n")[position.line]?.replace(/\r$/, "");
    if (lineText === undefined) {
        return null;
    }
    const offset = Buffer.byteLength(lineText.slice(0, position.character), "utf8");
    const bytes = Buffer.from(lineText, "utf8");
    for (const entry of document.names) {
        if (entry.line !== position.line || offset < entry.column ||
            offset >= entry.column + entry.length) {
            continue;
        }
        if (bytes.toString("utf8", entry.column, entry.column + entry.length) !== entry.name) {
            continue;
        }
        const start = bytes.toString("utf8", 0, entry.column).length;
        return {
            contents: {
                kind: "markdown",
                value: "```mialu\n" + `${entry.name}: ${entry.type}` + "\n```",
            },
            range: {
                start: { line: position.line, character: start },
                end: { line: position.line, character: start + entry.name.length },
            },
        };
    }
    return null;
}

async function codeLens(uri) {
    const document = documents.get(uri);
    if (document === undefined) {
        return [];
    }
    const text = document.text;
    const lenses = await lensesCompute(text);
    // The text can have changed while the compiler ran. Its lenses are then for the old text, and
    // the editor asks again after a change.
    if (lenses !== null && document.text === text) {
        document.lenses = lenses;
    }
    return document.lenses;
}

function documentSet(uri, text) {
    const document = documents.get(uri);
    if (document === undefined) {
        documents.set(uri, { text, lenses: [], names: [] });
    } else {
        document.text = text;
    }
}

/**
 * Answers a request, which has an `id`, or handles a notification, which has none.
 */
async function handle(message) {
    const { id, method, params } = message;
    const isRequest = id !== undefined;

    if (method === "initialize") {
        const compilerPath = params?.initializationOptions?.compilerPath;
        if (typeof compilerPath === "string" && compilerPath !== "") {
            state.compilerPath = compilerPath;
        }
        state.isInitialized = true;
        reply(id, {
            capabilities: {
                textDocumentSync: { openClose: true, change: TEXT_DOCUMENT_SYNC_FULL },
                codeLensProvider: { resolveProvider: false },
                hoverProvider: true,
            },
            serverInfo: { name: "mialu-lsp", version: "0.0.1" },
        });
        return;
    }
    if (method === "exit") {
        process.exit(state.isShutdown ? 0 : 1);
    }
    if (isRequest && !state.isInitialized) {
        replyError(id, ERROR_SERVER_NOT_INITIALIZED, "The server is not initialized.");
        return;
    }

    switch (method) {
    case "initialized":
        break;
    case "shutdown":
        state.isShutdown = true;
        reply(id, null);
        break;
    case "textDocument/didOpen":
        documentSet(params.textDocument.uri, params.textDocument.text);
        break;
    case "textDocument/didChange": {
        // The synchronization is full, so the last change has the whole text.
        const changes = params.contentChanges;
        documentSet(params.textDocument.uri, changes[changes.length - 1].text);
        break;
    }
    case "textDocument/didClose":
        documents.delete(params.textDocument.uri);
        break;
    case "textDocument/codeLens":
        reply(id, await codeLens(params.textDocument.uri));
        break;
    case "textDocument/hover":
        reply(id, await hover(params.textDocument.uri, params.position));
        break;
    default:
        // A request must be answered, and a notification that is not known is ignored.
        if (isRequest) {
            replyError(id, ERROR_METHOD_NOT_FOUND, `${method} is not supported.`);
        }
    }
}

/**
 * Cuts the messages out of the input as they arrive. A message is a header with `Content-Length`,
 * an empty line and a body of that many bytes of JSON, and a chunk can end anywhere in it.
 */
let pending = Buffer.alloc(0);
process.stdin.on("data", (chunk) => {
    pending = Buffer.concat([pending, chunk]);
    for (;;) {
        const headerEnd = pending.indexOf("\r\n\r\n");
        if (headerEnd === -1) {
            return;
        }
        const match = /Content-Length: *(\d+)/i.exec(pending.subarray(0, headerEnd).toString("ascii"));
        if (match === null) {
            // Not a message, so the rest cannot be trusted to be one either.
            pending = Buffer.alloc(0);
            return;
        }
        const bodyStart = headerEnd + 4;
        const bodyEnd = bodyStart + Number(match[1]);
        if (pending.length < bodyEnd) {
            return;
        }
        const body = pending.subarray(bodyStart, bodyEnd).toString("utf8");
        pending = pending.subarray(bodyEnd);
        let message;
        try {
            message = JSON.parse(body);
        } catch (error) {
            continue;
        }
        handle(message).catch((error) => {
            if (message.id !== undefined) {
                replyError(message.id, -32603, String(error?.message ?? error));
            }
        });
    }
});
process.stdin.on("end", () => process.exit(0));
