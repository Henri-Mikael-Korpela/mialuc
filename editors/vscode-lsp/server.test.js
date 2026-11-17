// Starts the server like an editor does and checks the code lenses that it answers with. Needs the
// compiler, which is `build/mialuc.exe` of this project unless the variable MIALUC says another.
//
//     node --test editors/vscode-lsp/server.test.js

"use strict";

const assert = require("node:assert");
const childProcess = require("node:child_process");
const path = require("node:path");
const { test } = require("node:test");

const COMPILER = process.env.MIALUC ||
    path.join(__dirname, "..", "..", "build", "mialuc.exe");

const SOURCE = [
    "package sizes",
    "",
    "Point :: struct ->",
    "    x: Int",
    "    y: Int",
    "",
    "Person :: struct ->",
    "    age: Int",
    "    name: Str",
    "    home: Point",
    "",
    "main :: proc() Int ->",
    "    return 0",
    "",
].join("\n");

/**
 * Runs the server for the length of `script`, which is given a function that sends a request and
 * resolves to its result, and a function that sends a notification.
 */
async function withServer(script) {
    const server = childProcess.spawn(process.execPath, [path.join(__dirname, "server.js")], {
        stdio: ["pipe", "pipe", "inherit"],
    });
    const waiting = new Map();
    let pending = Buffer.alloc(0);
    server.stdout.on("data", (chunk) => {
        pending = Buffer.concat([pending, chunk]);
        for (;;) {
            const headerEnd = pending.indexOf("\r\n\r\n");
            if (headerEnd === -1) {
                return;
            }
            const length = Number(/Content-Length: (\d+)/.exec(pending.toString("ascii", 0, headerEnd))[1]);
            if (pending.length < headerEnd + 4 + length) {
                return;
            }
            const message = JSON.parse(pending.toString("utf8", headerEnd + 4, headerEnd + 4 + length));
            pending = pending.subarray(headerEnd + 4 + length);
            waiting.get(message.id)?.(message);
        }
    });

    let nextId = 1;
    const write = (message) => {
        const body = JSON.stringify({ jsonrpc: "2.0", ...message });
        server.stdin.write(`Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`);
    };
    const request = (method, params) => new Promise((resolve) => {
        const id = nextId++;
        waiting.set(id, resolve);
        write({ id, method, params });
    });
    const notify = (method, params) => write({ method, params });

    try {
        await script(request, notify);
    } finally {
        server.kill();
    }
}

const TYPES_SOURCE = [
    "package types",
    "",
    "LIMIT :: 100",
    "NAME : Str : \"é\"",
    "",
    "add :: proc(a: Int, b: ^Int) Int ->",
    "    total := a + LIMIT",
    "    return total",
    "",
    "Color :: enum Int ->",
    "    RED",
    "    GREEN",
    "",
    "paint :: proc(color: Color) Color ->",
    "    chosen := Color.RED",
    "    if color == .GREEN ->",
    "        return chosen",
    "    return .RED",
    "",
].join("\n");

test("a name shows its type when the pointer rests on it", async () => {
    await withServer(async (request, notify) => {
        const initialized = await request("initialize", {
            processId: null,
            rootUri: null,
            capabilities: {},
            initializationOptions: { compilerPath: COMPILER },
        });
        assert.strictEqual(initialized.result.capabilities.hoverProvider, true);
        notify("initialized", {});

        const uri = "file:///types.mu";
        notify("textDocument/didOpen", {
            textDocument: { uri, languageId: "mialu", version: 1, text: TYPES_SOURCE },
        });
        const hover = async (line, character) =>
            (await request("textDocument/hover", {
                textDocument: { uri },
                position: { line, character },
            })).result;

        // The lines and the characters are counted from 0.
        const limit = await hover(2, 2);
        assert.strictEqual(limit.contents.value, "```mialu\nLIMIT: untyped Int\n```");
        assert.deepStrictEqual(limit.range, {
            start: { line: 2, character: 0 },
            end: { line: 2, character: 5 },
        });
        // A use of a constant shows the type of its definition, and so does a variable.
        assert.match((await hover(6, 19)).contents.value, /LIMIT: untyped Int/);
        assert.match((await hover(6, 4)).contents.value, /total: Int/);
        assert.match((await hover(5, 0)).contents.value, /add: proc\(a: Int, b: \^Int\) Int/);
        assert.match((await hover(5, 20)).contents.value, /b: \^Int/);
        assert.match((await hover(3, 1)).contents.value, /NAME: Str/);

        // A member of an enum has the enum as its type, written with the enum or without.
        assert.match((await hover(10, 5)).contents.value, /RED: Color/);
        assert.match((await hover(14, 21)).contents.value, /RED: Color/);
        assert.match((await hover(15, 18)).contents.value, /GREEN: Color/);

        // Nothing is shown on a keyword or on a space.
        assert.strictEqual(await hover(6, 12), null);
        assert.strictEqual(await hover(40, 0), null);

        // A text that does not compile keeps the types of the last text that did, where the name
        // is still in place.
        notify("textDocument/didChange", {
            textDocument: { uri, version: 2 },
            contentChanges: [{ text: TYPES_SOURCE + "broken :: proc(" }],
        });
        assert.match((await hover(2, 1)).contents.value, /LIMIT: untyped Int/);

        notify("textDocument/didClose", { textDocument: { uri } });
        assert.strictEqual(await hover(2, 1), null);
        assert.strictEqual((await request("shutdown", null)).result, null);
    });
});

test("a struct has a lens with its size above its definition", async () => {
    await withServer(async (request, notify) => {
        const initialized = await request("initialize", {
            processId: null,
            rootUri: null,
            capabilities: {},
            initializationOptions: { compilerPath: COMPILER },
        });
        assert.deepStrictEqual(initialized.result.capabilities.codeLensProvider, { resolveProvider: false });
        notify("initialized", {});

        const uri = "file:///sizes.mu";
        notify("textDocument/didOpen", {
            textDocument: { uri, languageId: "mialu", version: 1, text: SOURCE },
        });
        const first = await request("textDocument/codeLens", { textDocument: { uri } });
        // The lines are counted from 0, and a lens is on the line of the name of the struct.
        assert.deepStrictEqual(first.result.map((lens) => [lens.range.start.line, lens.command.title]), [
            [2, "Size: 16 bytes | Alignment: 8 bytes"],
            [6, "Size: 40 bytes | Alignment: 8 bytes"],
        ]);

        // A text that does not compile keeps the lenses of the last text that did, so that they do
        // not blink away while a line is typed.
        notify("textDocument/didChange", {
            textDocument: { uri, version: 2 },
            contentChanges: [{ text: SOURCE + "Broken :: struct ->\n    x: " }],
        });
        const broken = await request("textDocument/codeLens", { textDocument: { uri } });
        assert.strictEqual(broken.result.length, 2);

        // A field that is added to a struct that compiles changes the size.
        notify("textDocument/didChange", {
            textDocument: { uri, version: 3 },
            contentChanges: [{ text: SOURCE.replace("    y: Int\n", "    y: Int\n    z: Int\n") }],
        });
        const grown = await request("textDocument/codeLens", { textDocument: { uri } });
        assert.strictEqual(grown.result[0].command.title, "Size: 24 bytes | Alignment: 8 bytes");

        notify("textDocument/didClose", { textDocument: { uri } });
        const closed = await request("textDocument/codeLens", { textDocument: { uri } });
        assert.deepStrictEqual(closed.result, []);

        const unknown = await request("textDocument/definition", {});
        assert.strictEqual(unknown.error.code, -32601);
        assert.strictEqual((await request("shutdown", null)).result, null);
    });
});
