#!/usr/bin/env python3
"""Checks that Mialu source files are accepted by the EBNF grammar in grammar.ebnf.

    python scripts/verify_grammar.py test-code/main-function.mu

The script has no dependencies besides the Python standard library. It reads the grammar, which
is written in ISO 14977 EBNF with the extensions listed at the top of grammar.ebnf, and then
decides for each source file whether the grammar derives it. The grammar works on characters, so
there is no tokenizer step.

How it matches: for a part of the grammar and a start offset in the text, it computes the set of
all offsets where a match can end. Sequences, choices and repetitions are plain set operations on
those, and the result for each rule and offset is remembered. So any grammar without left
recursion is handled exactly, ambiguous ones included, in time that grows polynomially with the
size of the file. There is no backtracking that could be fooled by the order of alternatives.

Exit status: 0 when every file is accepted, 1 when a file is rejected or cannot be read, 2 when
the grammar is wrong or the arguments are.
"""

import argparse
import re
import sys
from pathlib import Path

# The most expected alternatives that an error message lists. More would bury the useful ones.
EXPECTED_COUNT_MAX = 12

ESCAPES = {"n": "\n", "r": "\r", "t": "\t", "\\": "\\", '"': '"', "'": "'"}

# One alternative per kind of token. Comments come before symbols so that "(*" is not read as
# "(", and ".." comes before the single symbols for the same reason.
TOKEN_PATTERN = re.compile(
    r"""
      (?P<space>\s+)
    | (?P<comment>\(\*.*?\*\))
    | (?P<string>"(?:[^"\\\n]|\\.)*"|'(?:[^'\\\n]|\\.)*')
    | (?P<special>\?[^?]*\?)
    | (?P<name>[A-Za-z_][A-Za-z0-9_]*)
    | (?P<symbol>\.\.|[=;|,\[\]{}()\-])
    """,
    re.VERBOSE | re.DOTALL,
)


class GrammarError(Exception):
    """The grammar file is malformed, or cannot be used to match."""


def position_of(text, offset):
    """Returns the 1-based line and column of `offset` in `text`."""
    line = text.count("\n", 0, offset) + 1
    column = offset - (text.rfind("\n", 0, offset) + 1) + 1
    return line, column


def describe_text(text):
    """Shows `text` as a quoted string with the escapes of the grammar notation."""
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    escaped = escaped.replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")
    return '"' + escaped + '"'


def tokenize_grammar(source, path):
    """Splits the grammar into (kind, value, line, column) tuples. Drops spaces and comments."""
    tokens = []
    offset = 0
    while offset < len(source):
        found = TOKEN_PATTERN.match(source, offset)
        line, column = position_of(source, offset)
        if found is None:
            raise GrammarError(f"{path}:{line}:{column}: unexpected character {source[offset]!r}")

        kind = found.lastgroup
        value = found.group()
        if kind == "string":
            value = unescape(value[1:-1], path, line, column)
        if kind in ("string", "special", "name", "symbol"):
            tokens.append((kind, value, line, column))
        offset = found.end()

    return tokens


def unescape(body, path, line, column):
    result = []
    index = 0
    while index < len(body):
        if body[index] != "\\":
            result.append(body[index])
            index += 1
            continue

        # The pattern only lets a backslash through when a character follows it.
        escape = body[index + 1]
        if escape not in ESCAPES:
            raise GrammarError(f"{path}:{line}:{column}: unknown escape '\\{escape}'")
        result.append(ESCAPES[escape])
        index += 2

    return "".join(result)


class GrammarParser:
    """Reads the rules of the grammar into a tree of tuples, one tuple per kind of node.

    ("literal", text)  ("range", low, high)  ("reference", name, line, column)
    ("sequence", [nodes])  ("choice", [nodes])  ("optional", node)  ("repeat", node)
    ("except", node, node)  ("special", name)
    """

    def __init__(self, tokens, path):
        self.tokens = tokens
        self.path = path
        self.index = 0

    def parse(self):
        """Returns a dict from rule name to the tree of its definition."""
        rules = {}
        while self.index < len(self.tokens):
            _, name, line, column = self.expect("name", "a rule name")
            if name in rules:
                raise GrammarError(f"{self.path}:{line}:{column}: rule '{name}' is defined twice")
            self.expect("symbol", "'='", "=")
            rules[name] = self.parse_choice()
            self.expect("symbol", "';'", ";")
        return rules

    def peek_symbol(self, symbol):
        if self.index >= len(self.tokens):
            return False
        kind, value, _, _ = self.tokens[self.index]
        return kind == "symbol" and value == symbol

    def expect(self, kind, description, value=None):
        if self.index >= len(self.tokens):
            raise GrammarError(f"{self.path}: the grammar ends where {description} should be")
        token = self.tokens[self.index]
        if token[0] != kind or (value is not None and token[1] != value):
            raise GrammarError(
                f"{self.path}:{token[2]}:{token[3]}: expected {description}, found {token[1]!r}"
            )
        self.index += 1
        return token

    def parse_choice(self):
        alternatives = [self.parse_sequence()]
        while self.peek_symbol("|"):
            self.index += 1
            alternatives.append(self.parse_sequence())
        if len(alternatives) == 1:
            return alternatives[0]
        return ("choice", alternatives)

    def parse_sequence(self):
        items = [self.parse_except()]
        while self.peek_symbol(","):
            self.index += 1
            items.append(self.parse_except())
        if len(items) == 1:
            return items[0]
        return ("sequence", items)

    def parse_except(self):
        node = self.parse_primary()
        if self.peek_symbol("-"):
            self.index += 1
            return ("except", node, self.parse_primary())
        return node

    def parse_primary(self):
        if self.index >= len(self.tokens):
            raise GrammarError(f"{self.path}: the grammar ends where a term should be")

        kind, value, line, column = self.tokens[self.index]
        self.index += 1
        if kind == "string":
            return self.parse_string(value, line, column)
        if kind == "name":
            return ("reference", value, line, column)
        if kind == "special":
            return ("special", " ".join(value[1:-1].split()).lower())
        if value == "(":
            return self.parse_group(")", lambda node: node)
        if value == "[":
            return self.parse_group("]", lambda node: ("optional", node))
        if value == "{":
            return self.parse_group("}", lambda node: ("repeat", node))
        raise GrammarError(f"{self.path}:{line}:{column}: unexpected {value!r}, expected a term")

    def parse_group(self, closing, wrap):
        node = self.parse_choice()
        self.expect("symbol", f"'{closing}'", closing)
        return wrap(node)

    def parse_string(self, text, line, column):
        if not self.peek_symbol(".."):
            return ("literal", text)

        self.index += 1
        _, high, high_line, high_column = self.expect("string", "the end of a range")
        if len(text) != 1 or len(high) != 1:
            raise GrammarError(f"{self.path}:{line}:{column}: a range needs single characters")
        if text > high:
            raise GrammarError(f"{self.path}:{line}:{column}: the range is empty")
        return ("range", text, high)


def check_references(rules, path):
    """Fails when a rule uses a rule that is not defined."""

    def visit(node):
        kind = node[0]
        if kind == "reference":
            if node[1] not in rules:
                raise GrammarError(f"{path}:{node[2]}:{node[3]}: unknown rule '{node[1]}'")
        elif kind in ("sequence", "choice"):
            for child in node[1]:
                visit(child)
        elif kind in ("optional", "repeat"):
            visit(node[1])
        elif kind == "except":
            visit(node[1])
            visit(node[2])

    # The trees are as deep as the grammar is nested, which is shallow, so recursion is fine in
    # this script.
    for tree in rules.values():
        visit(tree)


class Matcher:
    """Computes where matches of the grammar can end, and remembers why matching failed."""

    def __init__(self, rules, text):
        self.rules = rules
        self.text = text
        self.memo = {}
        self.in_progress = set()
        # What the grammar would have accepted at each offset where matching failed.
        self.expected_by_offset = {}
        # Switched off while matching the right side of an exception, because what it fails on
        # is not what the user should have written.
        self.is_recording = True

    def fail(self, offset, description):
        if self.is_recording:
            self.expected_by_offset.setdefault(offset, set()).add(description)
        return frozenset()

    def ends(self, node, offset):
        """Returns the set of offsets where a match of `node` that starts at `offset` can end."""
        kind = node[0]
        if kind == "literal":
            if self.text.startswith(node[1], offset):
                return frozenset([offset + len(node[1])])
            return self.fail(offset, describe_text(node[1]))
        if kind == "range":
            if offset < len(self.text) and node[1] <= self.text[offset] <= node[2]:
                return frozenset([offset + 1])
            return self.fail(offset, f"{describe_text(node[1])} .. {describe_text(node[2])}")
        if kind == "reference":
            return self.ends_of_rule(node[1], offset)
        if kind == "sequence":
            current = frozenset([offset])
            for item in node[1]:
                current = frozenset(end for start in current for end in self.ends(item, start))
            return current
        if kind == "choice":
            return frozenset(end for item in node[1] for end in self.ends(item, offset))
        if kind == "optional":
            return self.ends(node[1], offset) | {offset}
        if kind == "repeat":
            return self.ends_of_repeat(node[1], offset)
        if kind == "except":
            return self.ends_of_except(node[1], node[2], offset)
        if kind == "special":
            return self.ends_of_special(node[1], offset)
        raise AssertionError(f"unknown node kind {kind}")

    def ends_of_rule(self, name, offset):
        key = (name, offset, self.is_recording)
        if key in self.memo:
            return self.memo[key]
        if key in self.in_progress:
            raise GrammarError(f"rule '{name}' is left-recursive, which this script cannot match")

        self.in_progress.add(key)
        result = self.ends(self.rules[name], offset)
        self.in_progress.discard(key)
        self.memo[key] = result
        return result

    def ends_of_repeat(self, node, offset):
        # Each round only follows the offsets that are new, so the loop ends after at most
        # one round per offset in the text, also when `node` can match nothing.
        reached = {offset}
        frontier = {offset}
        while frontier:
            found = set()
            for start in frontier:
                found |= self.ends(node, start)
            frontier = found - reached
            reached |= frontier
        return frozenset(reached)

    def ends_of_except(self, node, excluded, offset):
        matches = self.ends(node, offset)
        if not matches:
            return matches

        was_recording = self.is_recording
        self.is_recording = False
        excluded_matches = self.ends(excluded, offset)
        self.is_recording = was_recording
        return matches - excluded_matches

    def ends_of_special(self, name, offset):
        if name != "end of input":
            raise GrammarError(f"unknown special sequence '? {name} ?'")
        if offset == len(self.text):
            return frozenset([offset])
        return self.fail(offset, "the end of the file")


def verify(rules, start, text, path):
    """Returns None when `text` is accepted, and otherwise an error message."""
    matcher = Matcher(rules, text)
    ends = matcher.ends(("reference", start, 0, 0), 0)
    if len(text) in ends:
        return None

    # The offset that matching reached furthest is where the text stops making sense.
    offset = max(matcher.expected_by_offset, default=0)
    line, column = position_of(text, offset)
    if offset < len(text):
        found = describe_text(text[offset])
    else:
        found = "the end of the file"

    expected = sorted(matcher.expected_by_offset.get(offset, []))
    if len(expected) > EXPECTED_COUNT_MAX:
        expected = expected[:EXPECTED_COUNT_MAX] + ["..."]
    return f"{path}:{line}:{column}: error: found {found}, expected {', '.join(expected)}"


def main():
    default_grammar = Path(__file__).resolve().parent.parent / "grammar.ebnf"
    parser = argparse.ArgumentParser(description="Check Mialu source files against the grammar.")
    parser.add_argument("files", nargs="+", help="Mialu source files (.mu) to check")
    parser.add_argument("-g", "--grammar", default=default_grammar, type=Path,
                        help="the EBNF grammar file (default: grammar.ebnf in the project root)")
    parser.add_argument("-s", "--start", default="module", help="the start rule (default: module)")
    arguments = parser.parse_args()

    try:
        grammar_source = arguments.grammar.read_text(encoding="utf-8")
        rules = GrammarParser(
            tokenize_grammar(grammar_source, arguments.grammar), arguments.grammar
        ).parse()
        check_references(rules, arguments.grammar)
        if arguments.start not in rules:
            raise GrammarError(f"the grammar has no start rule '{arguments.start}'")
    except (OSError, GrammarError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    exit_status = 0
    for file in arguments.files:
        try:
            # Decoded from bytes so that \r\n stays as it is, which the grammar expects.
            text = Path(file).read_bytes().decode("utf-8")
            message = verify(rules, arguments.start, text, file)
        except (OSError, UnicodeDecodeError) as error:
            message = f"{file}: error: cannot read the file: {error}"
        except GrammarError as error:
            print(f"error: {error}", file=sys.stderr)
            return 2

        if message is None:
            print(f"{file}: OK")
        else:
            print(message)
            exit_status = 1

    return exit_status


if __name__ == "__main__":
    sys.exit(main())
