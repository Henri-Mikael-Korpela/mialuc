#!/usr/bin/env python3
"""Updates the table of contents of the language specification.

    python scripts/update_language_spec_table_of_contents.py
    python scripts/update_language_spec_table_of_contents.py --check

The script reads docs/mialu-programming-language.md, collects the titles of all the sections that
come after the "Table of contents" section, and rewrites that section as a list of links to them.
A `##` title is a top-level entry and a `###` title is nested below it, and so on. Lines inside
code blocks are not titles. The link of a title is made the way GitHub makes it: the title in
lower case, without punctuation, with a hyphen for each space, and with `-1`, `-2`, ... added to a
title that repeats.

The script has no dependencies besides the Python standard library. Running it twice changes
nothing the second time, and it keeps the line endings of the file.

Exit status: 0 when the file is up to date or was updated, 1 when `--check` finds it out of date,
2 when the file cannot be read or has no "Table of contents" section.
"""

import argparse
import re
import sys
from pathlib import Path

DEFAULT_SPEC_PATH = Path(__file__).resolve().parent.parent / "docs" / "mialu-programming-language.md"

TABLE_OF_CONTENTS_TITLE = "Table of contents"

# The title of a section: two or more number signs, a space and the text.
TITLE_PATTERN = re.compile(r"^(#{2,6}) +(.+?) *#*$")

# The start and the end of a block of code, which is three backticks or three tildes.
FENCE_PATTERN = re.compile(r"^ {0,3}(```|~~~)")


def link_target(title, seen):
    """Returns the anchor that GitHub gives to a section called `title`.

    `seen` counts the anchors made so far, so that a title that repeats gets a number added.
    """
    # Inline code and links are shown as their text in the anchor.
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", title).replace("`", "")
    anchor = re.sub(r"[^\w\- ]", "", text.lower()).replace(" ", "-")

    count = seen.get(anchor, 0)
    seen[anchor] = count + 1
    if count == 0:
        return anchor
    return f"{anchor}-{count}"


def find_titles(lines):
    """Returns (index, level, title) for every title in `lines`, leaving out code blocks."""
    titles = []
    fence = None
    for index, line in enumerate(lines):
        match = FENCE_PATTERN.match(line)
        if match:
            if fence is None:
                fence = match.group(1)
            elif match.group(1) == fence:
                fence = None
            continue
        if fence is not None:
            continue

        title_match = TITLE_PATTERN.match(line)
        if title_match:
            titles.append((index, len(title_match.group(1)), title_match.group(2)))
    return titles


def build_table(titles, seen):
    """Returns the lines of the list of links for `titles`, a list of (level, title)."""
    # The first title decides the level that is not indented, so a document that starts with a
    # `###` title still gets a list that begins at the left edge.
    top_level = min(level for level, _ in titles)
    lines = []
    for level, title in titles:
        indentation = "  " * (level - top_level)
        lines.append(f"{indentation}- [{title}](#{link_target(title, seen)})")
    return lines


def update(text):
    """Returns `text` with its table of contents rewritten.

    Raises ValueError when the text has no table of contents section.
    """
    # Lines are split on "\n" only, and the "\r" of a "\r\n" ending stays on the line, so the
    # endings of the file are kept as they are.
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = text.split("\n")
    stripped = [line.rstrip("\r") for line in lines]

    titles = find_titles(stripped)
    toc_position = next(
        (position for position, (_, level, title) in enumerate(titles)
         if level == 2 and title == TABLE_OF_CONTENTS_TITLE),
        None,
    )
    if toc_position is None:
        raise ValueError(f'there is no "## {TABLE_OF_CONTENTS_TITLE}" section')

    toc_index = titles[toc_position][0]
    following = titles[toc_position + 1:]
    # The section ends at the next title, or at the end of the file.
    end_index = following[0][0] if following else len(lines)
    if not following:
        raise ValueError("there are no sections after the table of contents")

    # The sections are numbered from the top of the file, so that a title that is also above the
    # table of contents is counted, as GitHub counts it.
    seen = {}
    for _, _, title in titles[:toc_position + 1]:
        link_target(title, seen)

    table = build_table([(level, title) for _, level, title in following], seen)
    section = [lines[toc_index], "\r" if newline == "\r\n" else ""]
    suffix = "\r" if newline == "\r\n" else ""
    section += [line + suffix for line in table]
    section.append(suffix)

    return "\n".join(lines[:toc_index] + section + lines[end_index:])


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("path", nargs="?", type=Path, default=DEFAULT_SPEC_PATH,
                        help="the specification to update (default: %(default)s)")
    parser.add_argument("--check", action="store_true",
                        help="do not write the file, only fail when it is out of date")
    arguments = parser.parse_args()

    try:
        text = arguments.path.read_bytes().decode("utf-8")
    except OSError as error:
        print(f"{arguments.path}: {error.strerror}", file=sys.stderr)
        return 2

    try:
        updated = update(text)
    except ValueError as error:
        print(f"{arguments.path}: {error}", file=sys.stderr)
        return 2

    if updated == text:
        print(f"{arguments.path}: the table of contents is up to date")
        return 0
    if arguments.check:
        print(f"{arguments.path}: the table of contents is out of date", file=sys.stderr)
        return 1

    arguments.path.write_bytes(updated.encode("utf-8"))
    print(f"{arguments.path}: the table of contents was updated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
