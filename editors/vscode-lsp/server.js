// A language server for Mialu, which speaks the Language Server Protocol over the standard input
// and output, so any editor that has a client for it can use it. It has two features. The first is
// a code lens above the definition of each struct that says how large the struct is in memory:
//
//     Size: 24 bytes | Alignment: 8 bytes
//
// The second is a hover: resting the pointer on the name of a constant, a global variable, a
// parameter or a variable shows its type, such as `LIMIT: untyped Int`. A constant that is a string
// that `++` joined when the program was compiled shows the text it was joined to as well.
//
// A third feature is the outline of a document, the definitions with the fields and the members
// below them, from `mialuc dump-outline`.
//
// A fifth is completion: after a dot that starts the member of an enum, where the enum can be told
// from the place, as in `if error == .`, the members of the enum are suggested.
//
// A fourth is diagnostics: a moment after a document is opened or changed, the server checks it with
// the compiler and publishes the error, with `textDocument/publishDiagnostics`, so that the editor
// marks it in the code and lists it as a problem while it is typed, without a build.
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
 * Gives the text of `quoted`, which is a string in double quotes as `dump-types` writes it, with a
 * backslash before a backslash, a double quote and a control character, or the empty string when it
 * is not one. Three dots after the closing quote say that the compiler cut the text, and are kept.
 */
function unquote(quoted) {
    const match = /^"((?:[^"\\]|\\.)*)"(\.\.\.)?$/s.exec(quoted);
    if (match === null) {
        return "";
    }
    const text = match[1].replace(/\\(x[0-9A-Fa-f]{2}|.)/g, (all, escape) => {
        switch (escape[0]) {
            case "n": return "\n";
            case "r": return "\r";
            case "t": return "\t";
            case "x": return String.fromCharCode(parseInt(escape.slice(1), 16));
            default: return escape;
        }
    });
    return text + (match[2] ?? "");
}

/**
 * Turns the output of `mialuc dump-types` into the names with their types. A line of the output is
 * the line and the column of a name, both counted from 1 in bytes, the length of the name in bytes,
 * the name and the type, a note that some lines have, and after it the value of a constant that is
 * a string that `++` joined, as a string in double quotes, separated by tabs. A line that is not
 * like that is skipped.
 */
function namesFromOutput(output) {
    const names = [];
    for (const row of output.split("\n")) {
        const [lineText, columnText, lengthText, name, type, note, value, doc] =
            row.replace(/\r$/, "").split("\t");
        const line = Number(lineText);
        const column = Number(columnText);
        const length = Number(lengthText);
        if (!Number.isInteger(line) || line < 1 || !Number.isInteger(column) || column < 1 ||
            !Number.isInteger(length) || length < 1 || !name || !type) {
            continue;
        }
        names.push({
            line: line - 1,
            column: column - 1,
            length,
            name,
            type,
            note: note ?? "",
            value: value ?? "",
            doc: unquote(doc ?? ""),
        });
    }
    return names;
}

// The kinds of symbols of the protocol that the kinds of `mialuc dump-outline` are shown as.
const SYMBOL_KINDS = {
    procedure: 12, // Function
    struct: 23,    // Struct
    enum: 10,      // Enum
    constant: 14,  // Constant
    variable: 13,  // Variable
    field: 8,      // Field
    member: 22,    // EnumMember
};

/**
 * Turns the output of `mialuc dump-outline` into the document symbols of `text`. A line of the
 * output is the depth, 0 for a definition and 1 for a field or a member, the kind, the line and the
 * column of the name, both counted from 1 in bytes, the length of the name in bytes and the name,
 * separated by tabs. A line that is not like that is skipped. The range of a definition goes from
 * its name to the last line that is not empty before the next definition, and the range of what is
 * in it is its name.
 */
function symbolsFromOutput(output, text) {
    const lines = text.split("\n").map((line) => line.replace(/\r$/, ""));
    const rows = [];
    for (const row of output.split("\n")) {
        const [depthText, kind, lineText, columnText, lengthText, name] =
            row.replace(/\r$/, "").split("\t");
        const depth = Number(depthText);
        const line = Number(lineText);
        const column = Number(columnText);
        const length = Number(lengthText);
        if ((depth !== 0 && depth !== 1) || SYMBOL_KINDS[kind] === undefined ||
            !Number.isInteger(line) || line < 1 || line > lines.length ||
            !Number.isInteger(column) || column < 1 || !Number.isInteger(length) || length < 1 ||
            !name) {
            continue;
        }
        // The columns are bytes of UTF-8 and the protocol counts UTF-16 code units.
        const bytes = Buffer.from(lines[line - 1], "utf8");
        const start = bytes.toString("utf8", 0, column - 1).length;
        rows.push({
            depth, kind, name, line: line - 1,
            start, end: start + bytes.toString("utf8", column - 1, column - 1 + length).length,
        });
    }

    const symbols = [];
    for (let i = 0; i < rows.length; i += 1) {
        const row = rows[i];
        const selectionRange = {
            start: { line: row.line, character: row.start },
            end: { line: row.line, character: row.end },
        };
        const symbol = {
            name: row.name, kind: SYMBOL_KINDS[row.kind], range: selectionRange, selectionRange,
        };
        if (row.depth === 0) {
            let next = i + 1;
            while (next < rows.length && rows[next].depth !== 0) {
                next += 1;
            }
            let last = next < rows.length ? rows[next].line - 1 : lines.length - 1;
            while (last > row.line && lines[last].trim() === "") {
                last -= 1;
            }
            symbol.range = {
                start: selectionRange.start,
                end: { line: last, character: lines[last].length },
            };
            symbol.children = [];
            symbols.push(symbol);
        } else if (symbols.length > 0) {
            symbols[symbols.length - 1].children.push(symbol);
        }
    }
    return symbols;
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
 * Turns what the compiler printed to the standard error into diagnostics of `text`. An error has the
 * form `<file>:<line>:<column>: <message>`, with the line and the column counted from 1, the column
 * in bytes, or `<file>: <message>` when it is about the whole program, which is shown on the first
 * line. The compiler stops at the first error, so there is one. The range is the word at the place of
 * the error, or the character there when it is not on a word, and the columns are converted from
 * bytes of UTF-8 to UTF-16 code units.
 */
function diagnosticsFromErrors(errors, file, text) {
    const lines = text.split("\n").map((line) => line.replace(/\r$/, ""));
    const escaped = file.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
    const located = new RegExp(`^${escaped}:(\\d+):(\\d+): (.*)$`);
    const whole = new RegExp(`^${escaped}: (.*)$`);
    const diagnostics = [];
    for (const row of errors.split("\n")) {
        const trimmed = row.replace(/\r$/, "");
        let match = located.exec(trimmed);
        let line = 0;
        let column = 0;
        let message;
        if (match !== null) {
            line = Math.min(Math.max(Number(match[1]), 1), lines.length) - 1;
            column = Number(match[2]) - 1;
            message = match[3];
        } else if ((match = whole.exec(trimmed)) !== null) {
            message = match[1];
        } else {
            continue;
        }
        const bytes = Buffer.from(lines[line], "utf8");
        const start = bytes.toString("utf8", 0, Math.min(column, bytes.length)).length;
        const word = /^\w+/.exec(lines[line].slice(start));
        const end = Math.min(start + (word === null ? 1 : word[0].length), lines[line].length);
        diagnostics.push({
            range: {
                start: { line, character: start },
                end: { line, character: Math.max(end, start) },
            },
            severity: 1, // Error
            source: "mialuc",
            message,
        });
    }
    return diagnostics;
}

/**
 * Checks `text` with the compiler. Resolves to the diagnostics, an empty list when the text is
 * accepted, or to null when the compiler cannot be run or does not answer in time.
 */
function diagnosticsCompute(text) {
    return new Promise((resolve) => {
        let directory;
        let file;
        try {
            directory = fs.mkdtempSync(path.join(os.tmpdir(), "mialu-lsp-"));
            file = path.join(directory, "document.mu");
            fs.writeFileSync(file, text, "utf8");
        } catch (error) {
            resolve(null);
            return;
        }
        childProcess.execFile(state.compilerPath, ["dump-types", file],
            { timeout: 10000, windowsHide: true }, (error, stdout, stderr) => {
                fs.rmSync(directory, { recursive: true, force: true });
                if (!error) {
                    resolve([]);
                } else if (error.code === 1) {
                    resolve(diagnosticsFromErrors(stderr, file, text));
                } else {
                    resolve(null);
                }
            });
    });
}

/** How long after the last change the document is checked, so that typing does not run it often. */
const DIAGNOSTICS_DELAY_MS = 300;

/**
 * Checks a document and publishes what is wrong with it, which replaces what was published before.
 * A check for a text that is not the text any more is dropped, since a newer one follows it.
 */
async function diagnosticsPublish(uri) {
    const document = documents.get(uri);
    if (document === undefined) {
        return;
    }
    const text = document.text;
    const diagnostics = await diagnosticsCompute(text);
    if (diagnostics !== null && documents.get(uri) === document && document.text === text) {
        send({ method: "textDocument/publishDiagnostics", params: { uri, diagnostics } });
        // A text that compiles is where the names and the enums for the completions come from, so
        // that they are ready when a dot is typed, which does not compile.
        if (diagnostics.length === 0) {
            await cachesRefresh(document);
        }
    }
}

/** Checks a document after `delay` milliseconds, or later when it changes again before that. */
function diagnosticsSchedule(uri, delay = DIAGNOSTICS_DELAY_MS) {
    const document = documents.get(uri);
    if (document === undefined) {
        return;
    }
    clearTimeout(document.timer);
    document.timer = setTimeout(() => {
        diagnosticsPublish(uri).catch(() => {});
    }, delay);
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
 * Returns `code` as a fenced block of code that is highlighted as Mialu. The fence is longer than
 * any run of backticks in the code, so that a string of backticks cannot end the block early.
 */
function codeBlock(code) {
    const runs = code.match(/`+/g) ?? [];
    const fence = "`".repeat(Math.max(3, ...runs.map((run) => run.length + 1)));
    return `${fence}mialu\n${code}\n${fence}`;
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
        let value = codeBlock(`${entry.name}: ${entry.type}`);
        if (entry.doc !== "") {
            // The documentation comment of the item is its description, and is markdown.
            value += "\n" + entry.doc + "\n";
        }
        if (entry.note !== "") {
            // The note is the array or slice that the name is an element of, declared like any
            // other name, so it is code too, and is highlighted as such.
            value += "\nelement of\n" + codeBlock(entry.note);
        }
        if (entry.value !== "") {
            // The text that the constant was joined to, as a string literal, so it is code too.
            value += "\nvalue\n" + codeBlock(entry.value);
        }
        return {
            contents: { kind: "markdown", value },
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

/**
 * Answers the outline of a document. While the text does not compile, the symbols of the last text
 * that did are kept.
 */
async function documentSymbol(uri) {
    const document = documents.get(uri);
    if (document === undefined) {
        return [];
    }
    const text = document.text;
    const output = await compilerRun("dump-outline", text);
    if (output !== null && document.text === text) {
        document.symbols = symbolsFromOutput(output, text);
    }
    return document.symbols;
}

/**
 * Turns the output of `mialuc dump-enums` into the members of each enum. A line of the output is the
 * name of the enum as the program writes it, the name of a member, and the documentation comment of
 * the member as a string in double quotes when it has one, separated by tabs. A line that is not
 * like that is skipped.
 */
function enumsFromOutput(output) {
    const enums = new Map();
    for (const row of output.split("\n")) {
        const [enumName, name, doc] = row.replace(/\r$/, "").split("\t");
        if (!enumName || !name) {
            continue;
        }
        if (!enums.has(enumName)) {
            enums.set(enumName, []);
        }
        enums.get(enumName).push({ name, doc: unquote(doc ?? "") });
    }
    return enums;
}

/**
 * Reads the enums that `text` defines, `Mode :: enum Int ->` and the indented lines below it that are
 * its members, with the documentation comment above each. It is for a compiler that does not have
 * `dump-enums`, and it does not know the enums of the files that are imported, which only the
 * compiler does. The names are as the text writes them.
 */
function enumsFromText(text) {
    const enums = new Map();
    const lines = text.split("\n").map((line) => line.replace(/\r$/, ""));
    for (let i = 0; i < lines.length; i += 1) {
        const definition = /^([A-Za-z_]\w*)\s*::\s*enum\b.*->\s*(?:\/\/.*)?$/.exec(lines[i]);
        if (definition === null) {
            continue;
        }
        const members = [];
        let doc = [];
        for (let j = i + 1; j < lines.length; j += 1) {
            const line = lines[j];
            if (line.trim() === "") {
                doc = [];
                continue;
            }
            if (!/^\s/.test(line)) {
                break;
            }
            const comment = /^\s*\/\/\/(?!\/) ?(.*)$/.exec(line);
            if (comment !== null) {
                doc.push(comment[1]);
                continue;
            }
            const member = /^\s+([A-Za-z_]\w*)\b/.exec(line);
            if (member !== null) {
                members.push({ name: member[1], doc: doc.join("\n") });
            }
            doc = [];
        }
        enums.set(definition[1], members);
    }
    return enums;
}

/**
 * Asks the compiler for the enums of `text`. Resolves to them, or to null when the compiler does not
 * accept the text.
 */
async function enumsCompute(text) {
    const output = await compilerRun("dump-enums", text);
    return output === null ? null : enumsFromOutput(output);
}

/**
 * Makes the names and the enums of the document those of its text when the compiler accepts it. A
 * text that is being typed is not accepted, such as one that has a dot that is not finished, and
 * then the names and the enums of the last text that was are kept. When there are none yet, the
 * line that is being typed, which is `line`, is taken out, since the rest of the text may be fine.
 */
async function cachesRefresh(document, line) {
    const text = document.text;
    if (document.cacheText === text) {
        return;
    }
    // A compiler that has no `dump-enums`, such as one that was built before it was added, does not
    // answer it, so the enums that the text itself defines are taken from the text.
    const enumsOf = async (source) => (await enumsCompute(source)) ?? enumsFromText(source);
    let names = await namesCompute(text);
    let enums = names === null ? null : await enumsOf(text);
    if (names === null && document.names.length === 0 && line !== undefined) {
        const lines = text.split("\n");
        lines[line] = "";
        const without = lines.join("\n");
        names = await namesCompute(without);
        enums = names === null ? null : await enumsOf(without);
    }
    if (names !== null && enums !== null && document.text === text) {
        document.names = names;
        document.enums = enums;
        document.cacheText = text;
    }
}

/**
 * Cuts `text` at the commas that are not inside parentheses, brackets or braces.
 */
function splitTopLevel(text) {
    const parts = [];
    let depth = 0;
    let start = 0;
    for (let i = 0; i < text.length; i += 1) {
        const character = text[i];
        if (character === "(" || character === "[" || character === "{") {
            depth += 1;
        } else if (character === ")" || character === "]" || character === "}") {
            depth -= 1;
        } else if (character === "," && depth === 0) {
            parts.push(text.slice(start, i));
            start = i + 1;
        }
    }
    parts.push(text.slice(start));
    return parts;
}

/**
 * Takes apart the type of a procedure as `dump-types` writes it, `proc(a: Int, b: Mode) (Mode, Bool)`,
 * into the types of its parameters and of what it returns. Returns null for a type that is not one.
 */
function procedureSignature(type) {
    if (!type.startsWith("proc(")) {
        return null;
    }
    let depth = 0;
    let close = -1;
    for (let i = 4; i < type.length; i += 1) {
        if (type[i] === "(") {
            depth += 1;
        } else if (type[i] === ")") {
            depth -= 1;
            if (depth === 0) {
                close = i;
                break;
            }
        }
    }
    if (close === -1) {
        return null;
    }
    const inside = type.slice(5, close).trim();
    const parameters = inside === "" ? [] :
        splitTopLevel(inside).map((parameter) => parameter.replace(/^[^:]*:\s*/, "").trim());
    let rest = type.slice(close + 1).trim();
    if (rest.startsWith("(") && rest.endsWith(")")) {
        rest = rest.slice(1, -1);
    }
    const results = rest === "" ? [] : splitTopLevel(rest).map((result) => result.trim());
    return { parameters, results };
}

/**
 * Returns the type of the name `name`, taken from the names that the compiler gave: the one that is
 * the closest above `line`, or else the first, since the same name can be a variable in several
 * procedures. Returns null when no name has it.
 */
function typeOfName(document, name, line) {
    let found = null;
    for (const entry of document.names) {
        if (entry.name !== name) {
            continue;
        }
        if (found === null || (entry.line <= line && entry.line >= found.line) ||
            found.line > line) {
            found = entry;
        }
    }
    return found === null ? null : found.type;
}

/**
 * Tells the enum that a dot that starts a member can only be the member of, from what is before it
 * in the line and above it, or returns null. A member can be written with a dot alone where the type
 * is known from the place: after `==`, `not=` and `=` of a name that has the enum as its type, which
 * includes a field that a struct literal gives a value, as the value after `:` and the type of a
 * name, as an argument of a procedure that takes the enum, as a value that a procedure returns, and
 * as the pattern of a case of a match on a name that has the enum as its type. A name of an enum
 * before the dot, `Mode.`, is the enum itself. The names are the ones of the last text that
 * compiled, so the text with the dot that is being typed does not need to.
 */
function enumDeduce(document, before, line) {
    const dot = before.lastIndexOf(".");
    if (dot === -1) {
        return null;
    }
    const isKnown = (type) => type !== null && document.enums.has(type) ? type : null;

    // The name of an enum and a dot.
    const qualified = /([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*)\.\w*$/.exec(before);
    if (qualified !== null) {
        return isKnown(qualified[1]);
    }
    // A dot after something that is not a name is where a member starts, but not after a dot, which
    // is a range, and not after a caret, a bracket or a parenthesis that ends something.
    if (dot > 0 && /[\w\]^).]/.test(before[dot - 1])) {
        return null;
    }
    const head = before.slice(0, dot);
    const lines = document.text.split("\n").map((text) => text.replace(/\r$/, ""));

    let match = /([A-Za-z_]\w*)\s*(?:==|not=|=)\s*$/.exec(head);
    if (match !== null && isKnown(typeOfName(document, match[1], line)) !== null) {
        return typeOfName(document, match[1], line);
    }
    match = /:\s*([A-Za-z_][\w.]*)\s*=\s*$/.exec(head);
    if (match !== null) {
        return isKnown(match[1]);
    }

    // The case of a match: the match is the nearest line above that is less indented.
    if (/^\s*case\s*$/.test(head)) {
        const indentation = /^\s*/.exec(head)[0].length;
        for (let i = line - 1; i >= 0; i -= 1) {
            const above = /^(\s*)match\s+(?:#\([^)]*\)\s*)*(.+?)\s*->\s*$/.exec(lines[i]);
            if (above !== null && above[1].length < indentation) {
                const subject = /([A-Za-z_]\w*)\s*$/.exec(above[2]);
                return subject === null ? null : isKnown(typeOfName(document, subject[1], i));
            }
        }
        return null;
    }

    // An argument of a call: the parenthesis that is not closed, and the name before it.
    let depth = 0;
    for (let i = head.length - 1; i >= 0; i -= 1) {
        if (head[i] === ")" || head[i] === "]" || head[i] === "}") {
            depth += 1;
        } else if (head[i] === "(" && depth > 0) {
            depth -= 1;
        } else if (head[i] === "(" || head[i] === "[" || head[i] === "{") {
            if (head[i] !== "(") {
                return null;
            }
            const callee = /([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*)\s*$/.exec(head.slice(0, i));
            if (callee === null) {
                return null;
            }
            const signature = procedureSignature(typeOfName(document, callee[1], line) ?? "");
            const index = splitTopLevel(head.slice(i + 1)).length - 1;
            return signature === null ? null : isKnown(signature.parameters[index] ?? null);
        }
    }
    // The values of a return: the procedure is the definition above, and its types are the ones
    // that it returns, of which the one at the place of the value is the one that is wanted.
    match = /^\s*return\s(.*)$/.exec(head);
    if (match !== null || /^\s*return\s*$/.test(head)) {
        const values = splitTopLevel(match === null ? "" : match[1]);
        for (let i = line - 1; i >= 0; i -= 1) {
            const definition = /^([A-Za-z_]\w*)\s*::\s*proc\(/.exec(lines[i]);
            if (definition !== null) {
                const signature = procedureSignature(typeOfName(document, definition[1], i) ?? "");
                return signature === null ? null :
                    isKnown(signature.results[values.length - 1] ?? null);
            }
        }
        return null;
    }

    return null;
}

/**
 * Answers a request for completions: after a dot that starts the member of an enum, where the enum
 * can be told, all the members of it, with their documentation, see `enumDeduce`. Anything else has
 * no completions.
 */
async function completion(uri, position) {
    const document = documents.get(uri);
    if (document === undefined) {
        return null;
    }
    await cachesRefresh(document, position.line);

    const lineText = document.text.split("\n")[position.line]?.replace(/\r$/, "");
    if (lineText === undefined) {
        return null;
    }
    const type = enumDeduce(document, lineText.slice(0, position.character), position.line);
    if (type === null) {
        return null;
    }
    return document.enums.get(type).map((member, index) => {
        const item = {
            label: member.name,
            kind: 20, // EnumMember
            detail: type,
            // The members are suggested in the order that the enum has them.
            sortText: String(index).padStart(4, "0"),
        };
        if (member.doc !== "") {
            item.documentation = { kind: "markdown", value: member.doc };
        }
        return item;
    });
}

function documentSet(uri, text) {
    const document = documents.get(uri);
    if (document === undefined) {
        documents.set(uri, {
            text, lenses: [], names: [], enums: new Map(), cacheText: null, symbols: [],
            timer: undefined,
        });
    } else {
        document.text = text;
    }
    diagnosticsSchedule(uri);
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
                documentSymbolProvider: true,
                // The members of an enum are suggested after a dot.
                completionProvider: { triggerCharacters: ["."] },
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
    case "textDocument/didClose": {
        const uri = params.textDocument.uri;
        clearTimeout(documents.get(uri)?.timer);
        documents.delete(uri);
        // What was published for a document that is closed is taken away.
        send({ method: "textDocument/publishDiagnostics", params: { uri, diagnostics: [] } });
        break;
    }
    case "textDocument/codeLens":
        reply(id, await codeLens(params.textDocument.uri));
        break;
    case "textDocument/hover":
        reply(id, await hover(params.textDocument.uri, params.position));
        break;
    case "textDocument/documentSymbol":
        reply(id, await documentSymbol(params.textDocument.uri));
        break;
    case "textDocument/completion":
        reply(id, await completion(params.textDocument.uri, params.position));
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
