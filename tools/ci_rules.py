#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The project rules from AGENTS.md that a machine can check.

Run by the "Project rules" job in .github/workflows/ci.yml, and by hand:

    python3 tools/ci_rules.py --base origin/main      # what a PR would check
    python3 tools/ci_rules.py --before <sha>          # what a push would check
    python3 tools/ci_rules.py                         # the whole tree

Every failure prints a GitHub "::error" annotation and the exit status is 1.
The checks that look at changes (SPDX lines, NOTICE.md, secrets, console
files) only see the files the change adds or modifies, so old files are never
held to a rule they were written before. The build-output and base-name
checks always look at the whole tree, because one bad file breaks everyone.
"""

import argparse
import fnmatch
import os
import re
import subprocess
import sys

# This script holds the secret patterns below, so it must not scan itself.
SELF = "tools/ci_rules.py"

# AGENTS.md: every new source file starts with SPDX lines. These are the
# file types the project writes them in (all existing ones carry both).
SPDX_SUFFIXES = (".c", ".cpp", ".h", ".hpp", ".S", ".py", ".cmake")
SPDX_NAMES = ("CMakeLists.txt",)
SPDX_PREFIXES = ("Makefile",)
SPDX_HEAD_LINES = 10
SPDX_MARKS = ("SPDX-License-Identifier:", "SPDX-FileCopyrightText:")

# Tracked files that match a build-output pattern on purpose. Empty today:
# the one tracked binary, vendor-gecko/codehandleronly.bin, is a .bin and not
# a top-level .dol/.elf, so it is not caught. Add a path here, with the
# reason, if a real exception ever turns up.
BUILD_OUTPUT_ALLOWED = set()

# Console files and problem reports from a tester's Wii never belong in the
# repository. src/data/problemreport.cpp and its header are code, not data,
# so the match is on the whole file name with a data suffix.
CONSOLE_FILE_PATTERNS = (
    "problem*report*.txt", "problem*report*.json", "problem*report*.bin",
    "problem*report*.zip", "problem*report*.png",
    "nand*.bin", "keys.bin", "otp.bin", "seeprom.bin", "*.nand",
)

SECRET_PATTERNS = (
    ("a Discord webhook URL", re.compile(r"discord(app)?\.com/api/webhooks/")),
    ("a GitHub token", re.compile(r"\bgh[pousr]_[A-Za-z0-9]{36,}")),
    ("a GitHub token", re.compile(r"\bgithub_pat_[A-Za-z0-9_]{20,}")),
    ("an AWS access key", re.compile(r"\bAKIA[0-9A-Z]{16}\b")),
    ("a private key", re.compile(r"-----BEGIN ([A-Z0-9]+ )*PRIVATE KEY-----")),
)

failed = False


def error(message, path=None, line=None):
    """Print a GitHub annotation (it also reads fine in a terminal)."""
    global failed
    failed = True
    where = ""
    if path:
        where = f" file={path}"
        if line:
            where += f",line={line}"
    print(f"::error{where}::{message}")


def git(*args):
    result = subprocess.run(["git", *args], capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    if result.returncode != 0:
        print(f"git {' '.join(args)} failed: {result.stderr.strip()}", file=sys.stderr)
        sys.exit(2)
    return result.stdout


def tracked_files():
    return [f for f in git("ls-files", "-z").split("\0") if f]


def names(diff_range, filter_letters):
    out = git("diff", "--name-only", "-z", f"--diff-filter={filter_letters}", diff_range)
    return [f for f in out.split("\0") if f]


def needs_spdx(path):
    if path.startswith("vendor-"):
        return False
    base = os.path.basename(path)
    return (path.endswith(SPDX_SUFFIXES) or base in SPDX_NAMES
            or base.startswith(SPDX_PREFIXES))


def check_spdx(added):
    for path in added:
        if not needs_spdx(path) or not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8", errors="replace") as handle:
            head = [handle.readline() for _ in range(SPDX_HEAD_LINES)]
        text = "".join(head)
        for mark in SPDX_MARKS:
            if mark not in text:
                error(f"{path} is new and has no '{mark}' line in its first "
                      f"{SPDX_HEAD_LINES} lines (AGENTS.md: every new source file "
                      "starts with the SPDX lines).", path, 1)


def is_build_output(path):
    parts = path.split("/")
    base = parts[-1]
    if base in ("riftwii.dol", "riftwii.elf"):
        return True
    if len(parts) == 1 and base.endswith((".dol", ".elf")):
        return True
    # build-host, build-dolphin, build-runtime ... and the Wii makefile's
    # build_wii. Any depth: a nested build folder is still build output.
    if any(p.startswith("build-") or p == "build_wii" for p in parts[:-1]):
        return True
    return "__pycache__" in parts[:-1] or base.endswith(".pyc")


def check_build_output(files):
    for path in files:
        if is_build_output(path) and path not in BUILD_OUTPUT_ALLOWED:
            error(f"{path} is build output and must not be tracked. Remove it "
                  "with 'git rm --cached' (build output stays out of the repository).", path)


def make_variable(text, name):
    """The words of a makefile variable assigned with ':=' (continuations joined)."""
    joined = text.replace("\\\n", " ")
    match = re.search(rf"^{name}\s*:=\s*(.*)$", joined, re.M)
    return match.group(1).split("#")[0].split() if match else []


def wii_build_sources(files):
    """The sources Makefile.wii compiles, read from the Makefile itself so
    this check follows it when a directory is added."""
    with open("Makefile.wii", encoding="utf-8") as handle:
        text = handle.read()
    wii_dirs = make_variable(text, "WII_DIRS")
    src_dirs = make_variable(text, "SRC_DIRS")
    # SOURCES refers to the two variables above; take its literal entries.
    other_dirs = [d for d in make_variable(text, "SOURCES") if not d.startswith("$(")]
    excludes = {os.path.basename(p) for p in make_variable(text, "EXCLUDES")}
    if not wii_dirs or not src_dirs or not other_dirs:
        error("tools/ci_rules.py could not read WII_DIRS, SRC_DIRS and SOURCES from "
              "Makefile.wii. Update the reader in tools/ci_rules.py.", "Makefile.wii")
        return []
    build_dirs = set(wii_dirs + src_dirs + other_dirs)
    sources = []
    for path in files:
        directory, base = os.path.split(path)
        if directory not in build_dirs or base in excludes:
            continue
        # The same extensions Makefile.wii globs (.c .cpp .s .S).
        if base.endswith((".c", ".cpp", ".s", ".S")):
            sources.append(path)
    return sources


def check_base_names(files):
    # Every source is compiled into one folder as <stem>.o, so foo.c and
    # foo.cpp, or two foo.cpp in different directories, overwrite each other.
    by_object = {}
    for path in wii_build_sources(files):
        stem = os.path.splitext(os.path.basename(path))[0]
        by_object.setdefault(stem + ".o", []).append(path)
    for obj, paths in sorted(by_object.items()):
        if len(paths) > 1:
            error(f"{' and '.join(paths)} would both build {obj}. Makefile.wii compiles "
                  "every source into one flat folder, so base names must be unique.", paths[-1])


def check_vendor_notice(changed):
    vendor = [p for p in changed if p.startswith("vendor-")]
    if vendor and "NOTICE.md" not in changed:
        error(f"{vendor[0]} (and {len(vendor) - 1} more) changed under vendor-*/ but NOTICE.md "
              "did not. AGENTS.md: a change to third-party code is recorded in NOTICE.md."
              if len(vendor) > 1 else
              f"{vendor[0]} changed under vendor-*/ but NOTICE.md did not. AGENTS.md: "
              "a change to third-party code is recorded in NOTICE.md.", vendor[0])


def check_console_files(changed):
    for path in changed:
        base = os.path.basename(path)
        for pattern in CONSOLE_FILE_PATTERNS:
            if fnmatch.fnmatch(base.lower(), pattern):
                error(f"{path} looks like a file from a console or a problem report "
                      f"(matches {pattern}). These stay out of the repository.", path)
                break


def added_lines(diff_range):
    """(path, line number, text) for each line the range adds, no context."""
    out = git("diff", "-U0", "--no-color", "--diff-filter=AM", diff_range)
    path = None
    number = 0
    for raw in out.split("\n"):
        if raw.startswith("+++ "):
            path = raw[6:] if raw.startswith("+++ b/") else None
        elif raw.startswith("@@"):
            match = re.search(r"\+(\d+)", raw)
            number = int(match.group(1)) if match else 0
        elif raw.startswith("+") and path:
            yield path, number, raw[1:]
            number += 1


def scan_line(path, number, text):
    for label, pattern in SECRET_PATTERNS:
        if pattern.search(text):
            # The match itself is left out of the message: the log of a
            # public repository would otherwise carry the secret.
            error(f"This line adds {label}. Remove it and, if it was real, revoke it.",
                  path, number)


def check_secrets(diff_range):
    for path, number, text in added_lines(diff_range):
        if path != SELF:
            scan_line(path, number, text)


def check_secrets_tree(files):
    """Whole-tree scan, for running by hand to see what main holds."""
    for path in files:
        if path == SELF or not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8", errors="replace") as handle:
            for number, text in enumerate(handle, 1):
                scan_line(path, number, text)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--base", help="pull request: the base branch ref, e.g. origin/main")
    parser.add_argument("--before", help="push: the commit the branch was at before the push")
    args = parser.parse_args()

    files = tracked_files()
    check_build_output(files)
    check_base_names(files)

    diff_range = None
    if args.base:
        diff_range = f"{args.base}...HEAD"
    elif (args.before and not re.fullmatch(r"0+", args.before)
          and subprocess.run(["git", "cat-file", "-e", args.before + "^{commit}"],
                             capture_output=True).returncode == 0):
        diff_range = f"{args.before}...HEAD"

    if diff_range:
        added = names(diff_range, "A")
        changed = names(diff_range, "AMR")
        check_spdx(added)
        check_console_files(changed)
        check_secrets(diff_range)
        if args.base:
            check_vendor_notice(changed)
    else:
        # No range (a manual run, or the first push of a branch): the
        # change-based checks that make sense on a whole tree.
        check_console_files(files)
        check_secrets_tree(files)

    if failed:
        return 1
    print("Project rules: all passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
