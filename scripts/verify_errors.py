#!/usr/bin/env python3
"""Checks the errors of the compiler against the programs in test-code/errors.

    python scripts/verify_errors.py
    python scripts/verify_errors.py --compiler build/mialuc.exe
    python scripts/verify_errors.py --bless

Every program in test-code/errors is one that the compiler must reject, and the file next to it with
the same name and the suffix `.expected` is exactly what the compiler must print to the standard
error. The name of a program starts with the code of the error that it has, such as
`E0112-integer-literal-too-large.mu`. The directory that a program is in says how it is compiled:

    front   mialuc dump-ast, which finds the errors of the tokens, the syntax and the names
    x64     mialuc build --target x64-win32, which finds what the x64 target cannot build
    wasm    mialuc build --target wasm, which finds what the wasm target cannot build

The paths in the errors are relative to the root of the project, which the compiler is run in, and
are written with slashes, so that the files are the same on every system.

The script also checks that the name of every program is in the catalog, that its output has the
code in its name, and that every code of src/report.h has a program, except the codes that no
program can have, which are listed in CODES_WITHOUT_PROGRAM with the reason.

`--bless` writes the output of the compiler to the `.expected` files, which is for when an error is
changed on purpose: look at the changes with git before committing them.

Exit status: 0 when everything is as expected, 1 when something is not, 2 when the arguments are
wrong or the compiler is not there.
"""

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ERRORS_DIRECTORY = ROOT / "test-code" / "errors"

# How each directory is compiled. `{output}` is a file that the build may write, and `{source}` is
# the program.
COMMANDS = {
    "front": ["dump-ast", "{source}"],
    "wasm": ["build", "--target", "wasm", "-o", "{output}", "{source}"],
    "x64": ["build", "--target", "x64-win32", "-o", "{output}", "{source}"],
}

# The codes that no program in test-code/errors can have, and why. A program cannot make the system
# fail, and an internal error is a bug of the compiler, which a program that is rejected for it
# would be the proof of.
CODES_WITHOUT_PROGRAM = {
    "E0001": "a file that cannot be read cannot be a program in a directory",
    "E0002": "the system cannot write the output file, which a program cannot make happen",
    "E0003": "the system cannot find the absolute path of a file that it has just read",
    "E0004": "the system is out of memory",
    "E0005": "the standard output cannot be written",
    "E0006": "the directory for temporary files cannot be written",
    "E0007": "the system refuses to start a program that the compiler has built",
    "E0008": "the arguments of the program are on the command line, not in a program",
    "E0009": "run is only refused on a system other than Windows",
    "E0107": "a float literal has at most 128 characters, so it is never above 1.8e308",
    "E0122": "needs a source of 65536 tokens, which is too large to keep as a program",
    "E0201": "needs a source of 65536 syntax tree nodes, which is too large to keep as a program",
}


def codes_of_catalog():
    """Returns the numbers in `ERROR_CODE_NAME = number` of src/report.h, as E and four digits."""
    text = (ROOT / "src" / "report.h").read_text(encoding="utf8")
    return {f"E{int(number):04d}" for number in re.findall(r"ERROR_CODE_\w+ = (\d+),", text)}


def run(compiler, directory, program):
    """Compiles `program` and returns what the compiler printed to the standard error."""
    with tempfile.TemporaryDirectory() as temporary:
        output = Path(temporary) / "output"
        arguments = [
            part.format(source=program.relative_to(ROOT).as_posix(), output=output)
            for part in COMMANDS[directory]
        ]
        completed = subprocess.run([str(compiler)] + arguments, cwd=ROOT, capture_output=True)
    text = completed.stderr.decode("utf8", "replace").replace("\r\n", "\n")
    return completed.returncode, text


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--compiler", default=str(ROOT / "build" / "mialuc.exe"))
    parser.add_argument("--bless", action="store_true", help="write the expected output")
    arguments = parser.parse_args()

    compiler = Path(arguments.compiler)
    if not compiler.exists() and not compiler.with_suffix(".exe").exists():
        print(f"{compiler}: the compiler is not there, build it first", file=sys.stderr)
        return 2
    if not compiler.exists():
        compiler = compiler.with_suffix(".exe")

    failures = []
    codes_seen = set()
    for directory in sorted(COMMANDS):
        for program in sorted((ERRORS_DIRECTORY / directory).glob("*.mu")):
            name = program.relative_to(ROOT).as_posix()
            match = re.match(r"(E\d{4})-", program.name)
            if match is None:
                failures.append(f"{name}: the name must start with the code, such as E0101-")
                continue
            code = match.group(1)
            codes_seen.add(code)
            status, text = run(compiler, directory, program)
            expected_path = program.with_suffix(".expected")
            if arguments.bless:
                expected_path.write_text(text, encoding="utf8", newline="\n")
            if status != 1:
                failures.append(f"{name}: the compiler ended with status {status}, not 1")
            if f"error[{code}]" not in text:
                failures.append(f"{name}: the output has no error[{code}]:\n{text}")
            if not expected_path.exists():
                failures.append(f"{name}: there is no {expected_path.name}, run with --bless")
            elif expected_path.read_text(encoding="utf8").replace("\r\n", "\n") != text:
                failures.append(f"{name}: the output is not what {expected_path.name} has:\n{text}")

    known = codes_of_catalog()
    for code in sorted(codes_seen - known):
        failures.append(f"{code}: a program has this code, which is not in src/report.h")
    for code in sorted(known - codes_seen - set(CODES_WITHOUT_PROGRAM)):
        failures.append(f"{code}: no program in test-code/errors has this error")
    for code in sorted(set(CODES_WITHOUT_PROGRAM) & codes_seen):
        failures.append(f"{code}: has a program, so it is not one that has none")

    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        return 1
    print(f"{len(codes_seen)} error codes checked.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
