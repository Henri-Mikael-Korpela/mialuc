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
    const notifications = [];
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
            if (message.method !== undefined) {
                notifications.push(message);
            }
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
        await script(request, notify, notifications);
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
    "Point :: struct ->",
    "    x: Int",
    "    next: ^Point",
    "",
    "length :: proc(p: ^Point) Int ->",
    "    q := Point { x = 1, next = p }",
    "    return p.next.x + q.x",
    "",
    "sum :: proc(items: []Int) Int ->",
    "    return items[0]",
    "",
    "table :: proc() Int ->",
    "    a := [const]Int { 1, 2, 3 }",
    "    return sum(a) + a[1]",
    "",
    "middle :: proc() Int ->",
    "    a := [const]Int { 1, 2, 3, 4 }",
    "    part := a[1:3]",
    "    return part[0]",
    "",
    "Person :: struct ->",
    "    age: Int",
    "",
    "oldest :: proc(people: []Person) Int ->",
    "    best := 0",
    "    for person in people ->",
    "        best = person.age",
    "    return best",
    "",
    "SEPARATOR :: ','",
    "GREETING :: \"Hello\"",
    "TITLE :: GREETING ++ SEPARATOR ++ \" \" ++ \"é\"",
    "FRAMED : Str : '«' ++ TITLE ++ '»'",
    "LINES :: \"a\" ++ '\\n' ++ \"b\"",
    "",
    "measure :: proc(text: Str) Int ->",
    "    number, is_number := try_str_to_int(text)",
    "    return length_of(text) + number",
    "",
    "/// The size of a block, in bytes.",
    "/// It is the length of the buffers.",
    "SIZE :: 64 * 64",
    "MASK : U32 : 0x80000000",
    "ENABLED :: true",
    "RATIO :: 0.1",
    "",
    "buffer_size :: proc() Int ->",
    "    buffer := [SIZE]Byte { .. = 0 }",
    "    return length_of(buffer)",
    "",
    "// Not documentation: it has two slashes.",
    "/// Splits the `text`.",
    "split :: proc(text: Str) (Str, Int, Bool) ->",
    "    return text, 1, true",
    "",
    "/// A packet of a network.",
    "Packet :: struct ->",
    "    /// The bytes that follow.",
    "    size: Int",
    "",
    "/// How a packet is sent.",
    "Mode :: enum Int ->",
    "    /// The packet is sent at once.",
    "    NOW",
    "    LATER",
    "",
    "use_split :: proc() Int ->",
    "    word, size, is_ok := split(\"a\")",
    "    packet := Packet { size = 1 }",
    "    mode := Mode.NOW",
    "    return size + packet.size",
    "",
    "grow :: proc() Int ->",
    "    numbers := [dynamic]Int",
    "    names : [dynamic]Str",
    "    append(%numbers, 1, 2)",
    "    first := numbers[0]",
    "    part := numbers[1:]",
    "    for number in numbers ->",
    "        first += number",
    "    return length_of(numbers) + first",
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
        // The value that the compiler worked out is shown with the type.
        assert.strictEqual(limit.contents.value,
                           "```mialu\nLIMIT: untyped Int\n```\nvalue\n```mialu\n100\n```");
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

        // A field of a struct has the type that the struct gives it: where it is defined, where a
        // literal gives it a value, and where it is read, also through a pointer.
        assert.match((await hover(21, 5)).contents.value, /next: \^Point/);
        assert.match((await hover(24, 25)).contents.value, /next: \^Point/);
        assert.match((await hover(25, 14)).contents.value, /next: \^Point/);
        assert.match((await hover(25, 18)).contents.value, /x: Int/);
        assert.match((await hover(25, 24)).contents.value, /x: Int/);

        // An array whose length is left to its elements, `[const]Int`, shows the length that the
        // compiler counted, where it is defined and where it is used.
        assert.match((await hover(31, 4)).contents.value, /a: \[3\]Int/);
        assert.match((await hover(32, 15)).contents.value, /a: \[3\]Int/);
        assert.match((await hover(27, 13)).contents.value, /items: \[\]Int/);

        // A range of an array is a slice, which has no length that is known when the program is
        // compiled, where it is defined and where it is used.
        assert.match((await hover(36, 5)).contents.value, /part: \[\]Int/);
        assert.match((await hover(37, 12)).contents.value, /part: \[\]Int/);

        // The iterator of a for statement over an array or a slice has the type of its elements,
        // where the statement defines it and where it is used, and so do the fields of it.
        // It also says what the iterator is an element of, as code that is highlighted.
        const iterator =
            "```mialu\nperson: Person\n```\nelement of\n```mialu\npeople: []Person\n```";
        assert.strictEqual((await hover(44, 9)).contents.value, iterator);
        assert.strictEqual((await hover(45, 16)).contents.value, iterator);
        assert.match((await hover(45, 23)).contents.value, /age: Int/);

        // A constant that is a string that `++` joined shows the text that it was joined to, as a
        // string literal, where it is defined and where it is used, also when it has a type, and
        // a line break is written out. A string that is written out shows no value.
        const sourceLines = TYPES_SOURCE.split("\n");
        const lineOf = (text) => sourceLines.findIndex((line) => line.startsWith(text));
        const title = "```mialu\nTITLE: untyped Str\n```\nvalue\n```mialu\n\"Hello, é\"\n```";
        assert.strictEqual((await hover(lineOf("TITLE"), 1)).contents.value, title);
        const framedLine = lineOf("FRAMED");
        assert.strictEqual(
            (await hover(framedLine, sourceLines[framedLine].indexOf("TITLE") + 1)).contents.value,
            title);
        assert.strictEqual(
            (await hover(framedLine, 1)).contents.value,
            "```mialu\nFRAMED: Str\n```\nvalue\n```mialu\n\"«Hello, é»\"\n```");
        assert.strictEqual(
            (await hover(lineOf("LINES"), 1)).contents.value,
            "```mialu\nLINES: untyped Str\n```\nvalue\n```mialu\n\"a\\nb\"\n```");
        assert.strictEqual((await hover(lineOf("GREETING"), 1)).contents.value,
                           "```mialu\nGREETING: untyped Str\n```");

        // A call of a built-in procedure shows its signature, though the program defines none.
        const measured = lineOf("measure");
        assert.match((await hover(measured + 1, 25)).contents.value,
                     /try_str_to_int: proc\(text: Str\) \(Int, Bool\)/);
        // The names that are given its values have the types that it gives, `Int` and `Bool`.
        assert.match((await hover(measured + 1, 6)).contents.value, /number: Int/);
        assert.match((await hover(measured + 1, 14)).contents.value, /is_number: Bool/);
        assert.match((await hover(measured + 2, 12)).contents.value,
                     /length_of: proc\(elements: \[N\]T \| \[dynamic\]T \| \[\]T \| Str\) Int/);

        // A constant shows the value that was worked out, at its definition and where it is used,
        // such as the length of an array. A number is also in hexadecimal when it is 256 or more.
        const sized = lineOf("SIZE ::");
        assert.match((await hover(sized, 1)).contents.value, /SIZE: untyped Int[^]*4096 \(0x1000\)/);
        const buffered = lineOf("buffer_size");
        assert.match((await hover(buffered + 1, 16)).contents.value,
                     /SIZE: untyped Int[^]*4096 \(0x1000\)/);
        assert.match((await hover(lineOf("MASK"), 1)).contents.value, /2147483648 \(0x80000000\)/);
        assert.match((await hover(lineOf("ENABLED"), 1)).contents.value, /value[^]*true/);
        assert.match((await hover(lineOf("RATIO"), 1)).contents.value, /value[^]*0\.1/);

        // The documentation comment above a definition is its description, where the item is defined
        // and where it is used, and the lines of it are the lines of the text.
        const documented = (await hover(sized, 1)).contents.value;
        assert.match(documented,
                     /The size of a block, in bytes\.\nIt is the length of the buffers\./);
        assert.match((await hover(buffered + 1, 16)).contents.value, /The size of a block/);
        const split = lineOf("split ::");
        assert.match((await hover(split, 1)).contents.value, /\nSplits the `text`\.\n/);
        assert.doesNotMatch((await hover(split, 1)).contents.value, /Not documentation/);
        const usedAt = lineOf("use_split");
        assert.match((await hover(usedAt + 1, 25)).contents.value, /Splits the `text`\./);
        // A struct and an enum say what they are, and a field and a member are described too.
        assert.match((await hover(lineOf("Packet ::"), 1)).contents.value,
                     /Packet: struct[^]*A packet of a network\./);
        assert.match((await hover(sourceLines.indexOf("    size: Int"), 5)).contents.value, /bytes that follow/);
        assert.match((await hover(lineOf("Mode ::"), 1)).contents.value, /Mode: enum Int/);
        assert.match((await hover(lineOf("    NOW"), 5)).contents.value, /sent at once/);
        assert.match((await hover(usedAt + 3, 18)).contents.value, /sent at once/);
        // A name that has no documentation comment has no description.
        assert.doesNotMatch((await hover(lineOf("MASK"), 1)).contents.value, /\n\n/);

        // The names that are given the values of a procedure of the program have the type that it
        // returns at their place, the first of them as well, and each use of one has it too.
        const used = lineOf("use_split");
        assert.match((await hover(used + 1, 4)).contents.value, /word: Str/);
        assert.match((await hover(used + 1, 10)).contents.value, /size: Int/);
        assert.match((await hover(used + 1, 16)).contents.value, /is_ok: Bool/);
        assert.match((await hover(used + 4, 11)).contents.value, /size: Int/);

        // A dynamic array has the type that is written, or that its value gives, where it is defined
        // and where it is used, and what is made of it has the type of its elements, or of a slice.
        // The iterator says that it is an element of it.
        const grown = lineOf("grow ::");
        assert.match((await hover(grown + 1, 5)).contents.value, /numbers: \[dynamic\]Int/);
        assert.match((await hover(grown + 2, 5)).contents.value, /names: \[dynamic\]Str/);
        assert.match((await hover(grown + 3, 13)).contents.value, /numbers: \[dynamic\]Int/);
        assert.match((await hover(grown + 4, 5)).contents.value, /first: Int/);
        assert.match((await hover(grown + 5, 5)).contents.value, /part: \[\]Int/);
        assert.strictEqual(
            (await hover(grown + 6, 9)).contents.value,
            "```mialu\nnumber: Int\n```\nelement of\n```mialu\nnumbers: [dynamic]Int\n```");
        // The built-in procedures that take one say so in their signatures.
        assert.match((await hover(grown + 3, 5)).contents.value, /append: proc\(array: \^\[dynamic\]T/);
        assert.match((await hover(grown + 8, 12)).contents.value, /length_of: proc\([^)]*\[dynamic\]T/);
        // The word `dynamic` is a keyword, so nothing is shown on it.
        assert.strictEqual(await hover(grown + 1, 17), null);

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

test("a document has an outline of its definitions", async () => {
    await withServer(async (request, notify) => {
        await request("initialize", {
            processId: null,
            rootUri: null,
            capabilities: {},
            initializationOptions: { compilerPath: COMPILER },
        });
        notify("initialized", {});

        const uri = "file:///outline.mu";
        notify("textDocument/didOpen", {
            textDocument: { uri, languageId: "mialu", version: 1, text: SOURCE },
        });
        const outline = await request("textDocument/documentSymbol", { textDocument: { uri } });
        // Function is 12 and Struct is 23, and a field is 8. The lines are counted from 0.
        assert.deepStrictEqual(
            outline.result.map((symbol) => [symbol.name, symbol.kind, symbol.range.start.line, symbol.range.end.line]),
            [["Point", 23, 2, 4], ["Person", 23, 6, 9], ["main", 12, 11, 12]]);
        assert.deepStrictEqual(
            outline.result[1].children.map((symbol) => [symbol.name, symbol.kind, symbol.selectionRange.start.line]),
            [["age", 8, 7], ["name", 8, 8], ["home", 8, 9]]);

        // A text that does not compile keeps the outline of the last text that did.
        notify("textDocument/didChange", {
            textDocument: { uri, version: 2 },
            contentChanges: [{ text: SOURCE + "Broken :: struct ->\n    x: " }],
        });
        const broken = await request("textDocument/documentSymbol", { textDocument: { uri } });
        assert.strictEqual(broken.result.length, 3);
        assert.strictEqual((await request("shutdown", null)).result, null);
    });
});

test("a name that stores the result of a cast shows the type that the cast names", async () => {
    await withServer(async (request, notify) => {
        await request("initialize", {
            processId: null,
            rootUri: null,
            capabilities: {},
            initializationOptions: { compilerPath: COMPILER },
        });
        notify("initialized", {});

        const uri = "file:///casts.mu";
        const text = [
            "package casts",
            "",
            "main :: proc() Int ->",
            "    a := 5",
            "    b := cast(Byte)a",
            "    c := b + 1",
            "    q := cast([4]Byte)'x'",
            "    return cast(Int)b",
            "",
        ].join("\n");
        notify("textDocument/didOpen", {
            textDocument: { uri, languageId: "mialu", version: 1, text },
        });
        const hover = (line, character) =>
            request("textDocument/hover", { textDocument: { uri }, position: { line, character } })
                .then((message) => message.result);
        assert.match((await hover(4, 4)).contents.value, /b: Byte/);
        assert.match((await hover(5, 9)).contents.value, /b: Byte/);
        assert.match((await hover(5, 4)).contents.value, /c: Byte/);
        assert.match((await hover(6, 4)).contents.value, /q: \[4\]Byte/);
        assert.strictEqual((await request("shutdown", null)).result, null);
    });
});

test("publishes the error of a document that does not compile and clears it when it is fixed", async () => {
    await withServer(async (request, notify, notifications) => {
        await request("initialize", {
            processId: null, rootUri: null, capabilities: {},
            initializationOptions: { compilerPath: COMPILER },
        });
        notify("initialized", {});

        const uri = "file:///broken.mu";
        const good = ["package broken", "", "main :: proc() Int ->", "    return 0", ""].join("\n");
        const bad = ["package broken", "", "main :: proc() Int ->", "    return missing", ""]
            .join("\n");
        const published = async (count) => {
            for (let i = 0; i < 100 && notifications.length < count; i += 1) {
                await new Promise((resolve) => setTimeout(resolve, 50));
            }
            return notifications[count - 1].params;
        };

        notify("textDocument/didOpen", {
            textDocument: { uri, languageId: "mialu", version: 1, text: bad },
        });
        const first = await published(1);
        assert.strictEqual(first.uri, uri);
        assert.strictEqual(first.diagnostics.length, 1);
        assert.strictEqual(first.diagnostics[0].code, "E0402");
        assert.strictEqual(first.diagnostics[0].range.start.line, 3);
        assert.strictEqual(first.diagnostics[0].range.start.character, 11);
        assert.strictEqual(first.diagnostics[0].range.end.character, 18);

        notify("textDocument/didChange", {
            textDocument: { uri, version: 2 }, contentChanges: [{ text: good }],
        });
        assert.deepStrictEqual((await published(2)).diagnostics, []);
        assert.strictEqual((await request("shutdown", null)).result, null);
    });
});

test("the value of a constant is shown inline after its operator", async () => {
    await withServer(async (request, notify) => {
        const initialized = await request("initialize", {
            processId: null, rootUri: null, capabilities: {},
            initializationOptions: { compilerPath: COMPILER },
        });
        assert.strictEqual(initialized.result.capabilities.inlayHintProvider, true);
        notify("initialized", {});

        const source = [
            "package values",
            "",
            "VERSION_MAJOR :: 0",
            "VERSION_MINOR :: 3",
            "VERSION_STRING :: int_to_str(VERSION_MAJOR) ++ '.' ++ int_to_str(VERSION_MINOR)",
            "SIZE :: 64 * 64",
            "MASK : U32 : 0x80000000",
            "LIMIT :: 100",
            "ENABLED :: SIZE > 1",
            "GREETING :: \"hi\"",
            "",
            "main :: proc() Int ->",
            "    return SIZE + VERSION_MAJOR",
            "",
        ].join("\n");
        const uri = "file:///values.mu";
        notify("textDocument/didOpen", {
            textDocument: { uri, languageId: "mialu", version: 1, text: source },
        });
        const hints = (await request("textDocument/inlayHint", {
            textDocument: { uri },
            range: { start: { line: 0, character: 0 }, end: { line: 20, character: 0 } },
        })).result;
        const lines = source.split("\n");
        // Each hint is where the operator ends, and says the value that was worked out.
        const shown = hints.map((hint) => [
            lines[hint.position.line].slice(0, hint.position.character), hint.label]);
        assert.deepStrictEqual(shown, [
            ["VERSION_STRING ::", "⇒ \"0.3\""],
            ["SIZE ::", "⇒ 4096 (0x1000)"],
            ["MASK : U32 :", "⇒ 2147483648 (0x80000000)"],
            ["ENABLED ::", "⇒ true"],
        ]);
        assert.strictEqual(hints[0].paddingRight, true);

        // Only the lines of the range have hints.
        const part = (await request("textDocument/inlayHint", {
            textDocument: { uri },
            range: { start: { line: 5, character: 0 }, end: { line: 5, character: 20 } },
        })).result;
        assert.deepStrictEqual(part.map((hint) => hint.label), ["⇒ 4096 (0x1000)"]);
        assert.strictEqual((await request("shutdown", null)).result, null);
    });
});
