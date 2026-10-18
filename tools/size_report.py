#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Size report for CI: how big the DOL and the runtime blobs are, and how
that changed against a baseline from the base branch.

    python3 tools/size_report.py --dol riftwii.dol \\
        --blob build-runtime/riftwii_rt.bin ... \\
        --plugin build-plugins/selftest.rwp ... \\
        [--baseline base.json] [--json-out size-report.json]

Prints a Markdown table on stdout (CI appends it to the job summary) and
writes the sizes as JSON, which a later run reads back with --baseline.

The runtime blobs are copied into the running game, and the DOL's highest
address (BSS included) decides how much memory is left below the game's
own area, so a layout problem shows here before it shows on a Wii.

The baseline file comes from another run's artifact and is untrusted: it
is parsed strictly and anything unexpected is ignored, never trusted.
"""

import argparse
import json
import os
import struct
import sys

TEXT_SECTIONS = 7
DATA_SECTIONS = 11
SECTION_COUNT = TEXT_SECTIONS + DATA_SECTIONS
ADDRESS_TABLE = 0x48
SIZE_TABLE = 0x90
BSS_ADDRESS = 0xD8
BSS_SIZE = 0xDC
HEADER_SIZE = 0x100

JSON_VERSION = 1
MAX_BASELINE_BYTES = 1 << 20
# Sizes in a baseline can't plausibly pass this; refuse rather than print.
MAX_SIZE = 1 << 32


def read_dol_layout(path):
    """Section totals and the end address of the highest section."""
    with open(path, "rb") as dol:
        header = dol.read(HEADER_SIZE)
    if len(header) < HEADER_SIZE:
        raise ValueError(f"{path}: shorter than a DOL header")

    addresses = struct.unpack_from(f">{SECTION_COUNT}I", header, ADDRESS_TABLE)
    sizes = struct.unpack_from(f">{SECTION_COUNT}I", header, SIZE_TABLE)
    bss_address, bss_size = struct.unpack_from(">II", header, BSS_ADDRESS)

    text_bytes = sum(sizes[:TEXT_SECTIONS])
    data_bytes = sum(sizes[TEXT_SECTIONS:])

    # Empty sections have address 0 and must not count as the highest.
    end_addresses = [addresses[i] + sizes[i] for i in range(SECTION_COUNT) if sizes[i]]
    if bss_size:
        end_addresses.append(bss_address + bss_size)
    return {
        "text": text_bytes,
        "data": data_bytes,
        "bss": bss_size,
        "end": max(end_addresses) if end_addresses else 0,
        # Where the heap starts: the start-up stash (tools/dol_stash.py)
        # sits above the BSS only until crt0 has copied it to MEM2.
        "program_end": bss_address + bss_size if bss_size else 0,
    }


def valid_size(value):
    return isinstance(value, int) and not isinstance(value, bool) and 0 <= value < MAX_SIZE


def load_baseline(path):
    """The baseline's numbers, or None when the file is missing or wrong."""
    try:
        if os.path.getsize(path) > MAX_BASELINE_BYTES:
            return None
        with open(path, encoding="utf-8") as handle:
            raw = json.load(handle)
    except (OSError, ValueError):
        return None
    if not isinstance(raw, dict) or raw.get("version") != JSON_VERSION:
        return None

    files = {}
    raw_files = raw.get("files")
    if isinstance(raw_files, dict):
        for name, size in raw_files.items():
            if isinstance(name, str) and len(name) <= 100 and valid_size(size):
                files[name] = size
    dol = {}
    raw_dol = raw.get("dol")
    if isinstance(raw_dol, dict):
        for key in ("text", "data", "bss", "end", "program_end"):
            if valid_size(raw_dol.get(key)):
                dol[key] = raw_dol[key]
    if not files:
        return None
    return {"files": files, "dol": dol}


def kib(size):
    return f"{size / 1024:.1f}"


def change(now, before):
    """'+1,234 (+0.5%)' for a size against its baseline, '-' without one."""
    if before is None:
        return "-"
    delta = now - before
    if delta == 0:
        return "0"
    percent = f" ({delta / before * 100:+.1f}%)" if before else ""
    return f"{delta:+,}{percent}"


def read_plugin(path):
    """A plugin's header (tools/make_plugin.py): what it takes of the
    plugin arena in MEM2 when it is loaded."""
    with open(path, "rb") as plugin:
        header = plugin.read(128)
    if len(header) < 128 or header[:4] != b"RWPL":
        raise ValueError(f"{path}: not a plugin file")
    image, bss, relocs, state = struct.unpack_from(">IIII", header, 76)
    name = header[8:24].rstrip(b"\0").decode("ascii", "replace")
    memory = sum((part + 31) & ~31 for part in (image, bss, state))
    return name, {"image": image, "bss": bss, "relocations": relocs, "state": state, "memory": memory}


def build_report(dol_path, blob_paths, plugin_paths=()):
    files = {}
    for path in [dol_path] + blob_paths + list(plugin_paths):
        files[os.path.basename(path)] = os.path.getsize(path)
    plugins = dict(read_plugin(path) for path in plugin_paths)
    return {"version": JSON_VERSION, "files": files, "dol": read_dol_layout(dol_path), "plugins": plugins}


def markdown(report, baseline):
    lines = ["## Size report", ""]
    if baseline is None:
        lines += ["No baseline from the base branch was found, so there is no "
                  "change column. Sizes only.", ""]
        lines += ["| File | Bytes | KiB |", "| --- | ---: | ---: |"]
    else:
        lines += ["| File | Bytes | KiB | Change (bytes) |", "| --- | ---: | ---: | ---: |"]

    for name, size in report["files"].items():
        row = f"| `{name}` | {size:,} | {kib(size)} |"
        if baseline is not None:
            before = baseline["files"].get(name)
            row += f" {change(size, before) if before is not None else 'new'} |"
        lines.append(row)

    layout = report["dol"]
    lines += ["", "### DOL layout", ""]
    if baseline is None:
        lines += ["| Part | Value |", "| --- | ---: |"]
    else:
        lines += ["| Part | Value | Change (bytes) |", "| --- | ---: | ---: |"]
    parts = [
        ("Text sections (bytes)", "text", f"{layout['text']:,}"),
        ("Data sections (bytes)", "data", f"{layout['data']:,}"),
        ("BSS (bytes)", "bss", f"{layout['bss']:,}"),
        ("Highest end address (the start-up stash's end)", "end", f"`0x{layout['end']:08X}`"),
        ("Program end (the MEM1 heap starts here)", "program_end", f"`0x{layout['program_end']:08X}`"),
    ]
    for label, key, shown in parts:
        row = f"| {label} | {shown} |"
        if baseline is not None:
            row += f" {change(layout[key], baseline['dol'].get(key))} |"
        lines.append(row)

    # Plugins load into the plugin arena in MEM2 (docs/PLUGINS.md): what
    # each takes of it, so a plugin that grows toward the budget shows.
    if report.get("plugins"):
        lines += ["", "### Plugins (MEM2 arena)", "",
                  "| Plugin | Image | .bss | State | Relocations | Arena bytes |",
                  "| --- | ---: | ---: | ---: | ---: | ---: |"]
        for name, sizes in sorted(report["plugins"].items()):
            lines.append(f"| `{name}` | {sizes['image']:,} | {sizes['bss']:,} | {sizes['state']:,} | "
                         f"{sizes['relocations']:,} | {sizes['memory']:,} |")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--dol", required=True)
    parser.add_argument("--blob", action="append", default=[])
    parser.add_argument("--plugin", action="append", default=[])
    parser.add_argument("--baseline")
    parser.add_argument("--json-out")
    args = parser.parse_args()

    # A blob or plugin that did not build is a CI failure, not a missing row.
    for path in [args.dol] + args.blob + args.plugin:
        if not os.path.isfile(path):
            print(f"size_report: {path} not found", file=sys.stderr)
            return 1

    baseline = load_baseline(args.baseline) if args.baseline else None
    report = build_report(args.dol, args.blob, args.plugin)
    sys.stdout.write(markdown(report, baseline))
    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=2)
            handle.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
