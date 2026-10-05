#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compares RiftWii's own source with another project's, to find code that
was copied or closely followed and so must carry that project's notices.

    py -3 tools/attribution_audit.py <other project's tree> [--out report.md]

RiftWii's own code is everything tracked under src/, include/, wii/,
runtime/ and channel/; vendor-* holds third-party code kept whole with its
own headers and is left out. Three checks:

1. Copied code: each file is cut into C tokens (comments, whitespace and
   #include lines dropped) and fingerprinted MOSS-style (winnowing over
   k-token runs). A run that also occurs in the other project is a match.
   Done twice: with the tokens as they are (copied text), and with every
   name replaced by one placeholder (copied structure with names changed),
   which needs a longer run before it counts.
2. Function names defined in both projects.
3. Constants: hex numbers of 5 digits or more that both projects contain
   (magic words, addresses, patch values).

Every finding needs a person's look: short runs of boilerplate, a
library's API and hardware facts are shared by all Wii homebrew.
"""
import argparse
import collections
import pathlib
import re
import subprocess
import sys

EXTS = {".c", ".cpp", ".h", ".hpp", ".s", ".S", ".inc"}
OWN_DIRS = ("src/", "include/", "wii/", "runtime/", "channel/")
K_EXACT, K_RENAMED, WINDOW = 25, 50, 8

KEYWORDS = set("""auto break case char const continue default do double else enum extern float for goto if inline int
long register restrict return short signed sizeof static struct switch typedef union unsigned void volatile while
bool class namespace template typename using public private protected virtual override new delete this true false
nullptr operator try catch throw constexpr static_cast reinterpret_cast const_cast dynamic_cast""".split())

TOKEN = re.compile(r"""0[xX][0-9a-fA-F]+[uUlL]*|\d+\.?\d*(?:[eE][+-]?\d+)?[uUlLfF]*|[A-Za-z_]\w*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|->|\+\+|--|<<=?|>>=?|[<>=!&|+\-*/%^]=|&&|\|\||::|\S""")
COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
FUNC_DEF = re.compile(r"^[\w:<>,\s\*&]*?\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:const\s*)?(?:override\s*)?\{", re.M)
HEX = re.compile(r"\b0[xX]([0-9a-fA-F]{5,})\b")


def tokens(text):
    text = COMMENT.sub(" ", text)
    out = []  # (token, line)
    for line_no, line in enumerate(text.split("\n"), 1):
        if line.lstrip().startswith("#include") or line.lstrip().startswith("#pragma"):
            continue
        for t in TOKEN.findall(line):
            out.append((t, line_no))
    return out


def renamed(tok):
    if re.match(r"[A-Za-z_]\w*$", tok) and tok not in KEYWORDS:
        return "N"
    if re.match(r"\d|0[xX]", tok):
        return "0"
    if tok[0] in "\"'":
        return "S"
    return tok


def fingerprints(toks, k):
    """Winnowed hashes of k-token runs: {hash: first line}."""
    hashes = []
    for i in range(len(toks) - k + 1):
        hashes.append((hash(tuple(t for t, _ in toks[i:i + k])), toks[i][1], toks[i + k - 1][1]))
    picked = {}
    for i in range(max(0, len(hashes) - WINDOW + 1)):
        h = min(hashes[i:i + WINDOW], key=lambda x: x[0])
        picked.setdefault(h[0], (h[1], h[2]))
    return picked


def source_files(root, own):
    root = pathlib.Path(root)
    if own:
        listed = subprocess.run(["git", "-C", str(root), "ls-files"], capture_output=True, text=True).stdout.split("\n")
        return [root / p for p in listed if p.startswith(OWN_DIRS) and pathlib.Path(p).suffix in EXTS]
    return [p for p in root.rglob("*") if p.suffix in EXTS and p.is_file()]


def read(p):
    return p.read_text("utf-8", errors="replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("other")
    ap.add_argument("--out")
    args = ap.parse_args()
    rift_root = pathlib.Path(__file__).resolve().parent.parent
    other_root = pathlib.Path(args.other)

    # The other project's fingerprints, both kinds, with where they are.
    index = {"exact": {}, "renamed": {}}
    other_funcs = collections.defaultdict(set)
    other_hex = collections.defaultdict(set)
    for p in source_files(other_root, False):
        text = read(p)
        rel = str(p.relative_to(other_root)).replace("\\", "/")
        toks = tokens(text)
        for h, (a, b) in fingerprints(toks, K_EXACT).items():
            index["exact"].setdefault(h, (rel, a, b))
        rtoks = [(renamed(t), l) for t, l in toks]
        for h, (a, b) in fingerprints(rtoks, K_RENAMED).items():
            index["renamed"].setdefault(h, (rel, a, b))
        for m in FUNC_DEF.finditer(COMMENT.sub(" ", text)):
            if m.group(1) not in KEYWORDS:
                other_funcs[m.group(1)].add(rel)
        for m in HEX.finditer(COMMENT.sub(" ", text)):
            other_hex[m.group(1).lower().lstrip("0")].add(rel)

    lines = ["# Attribution audit", "", f"RiftWii's own code compared with `{other_root.name}`.", ""]
    lines.append("## 1. Code runs found in both")
    lines.append("")
    lines.append(f"Runs of {K_EXACT} tokens as written (exact), or {K_RENAMED} tokens with every name replaced (renamed).")
    lines.append("")
    lines.append("| RiftWii file | exact | renamed | where in the other project (lines here -> there) |")
    lines.append("| --- | --- | --- | --- |")
    own_funcs = collections.defaultdict(set)
    own_hex = collections.defaultdict(set)
    any_hit = False
    for p in sorted(source_files(rift_root, True)):
        text = read(p)
        rel = str(p.relative_to(rift_root)).replace("\\", "/")
        toks = tokens(text)
        for m in FUNC_DEF.finditer(COMMENT.sub(" ", text)):
            if m.group(1) not in KEYWORDS:
                own_funcs[m.group(1)].add(rel)
        for m in HEX.finditer(COMMENT.sub(" ", text)):
            own_hex[m.group(1).lower().lstrip("0")].add(rel)
        hits = {}
        for kind, k, toks_k in (("exact", K_EXACT, toks), ("renamed", K_RENAMED, [(renamed(t), l) for t, l in toks])):
            fp = fingerprints(toks_k, k)
            hits[kind] = [(a, b) + index[kind][h] for h, (a, b) in fp.items() if h in index[kind]]
        if not hits["exact"] and not hits["renamed"]:
            continue
        any_hit = True
        where = collections.Counter()
        spans = collections.defaultdict(list)
        for kind in hits:
            for a, b, orel, oa, ob in hits[kind]:
                where[orel] += 1
                spans[orel].append(f"{a}-{b}->{oa}-{ob}")
        desc = "; ".join(f"`{o}` ({', '.join(sorted(set(spans[o]))[:6])}{'...' if len(set(spans[o])) > 6 else ''})"
                         for o, _ in where.most_common(3))
        lines.append(f"| `{rel}` | {len(hits['exact'])} | {len(hits['renamed'])} | {desc} |")
    if not any_hit:
        lines.append("| (none) | | | |")

    lines += ["", "## 2. Function names defined in both", ""]
    shared = sorted(n for n in own_funcs if n in other_funcs and len(n) > 3)
    for n in shared:
        lines.append(f"- `{n}`: here {', '.join(sorted(own_funcs[n]))}; there {', '.join(sorted(other_funcs[n])[:3])}")
    if not shared:
        lines.append("(none)")

    lines += ["", "## 3. Constants found in both", ""]
    consts = sorted(h for h in own_hex if h in other_hex and len(h) >= 5)
    for h in consts:
        lines.append(f"- `0x{h}`: here {', '.join(sorted(own_hex[h]))}; there {', '.join(sorted(other_hex[h])[:3])}")
    if not consts:
        lines.append("(none)")

    report = "\n".join(lines) + "\n"
    if args.out:
        pathlib.Path(args.out).write_text(report, encoding="utf-8")
    else:
        sys.stdout.write(report)
    print(f"{sum(1 for l in lines if l.startswith('| `'))} files with code runs, {len(shared)} shared function names, "
          f"{len(consts)} shared constants", file=sys.stderr)


if __name__ == "__main__":
    main()
