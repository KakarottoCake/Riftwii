# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bound every compiled fault-recorder call path, including LTO frames."""
import argparse
from pathlib import Path
import re
import subprocess


def check_stack(elf, objdump, header):
    frames = {}
    for report in elf.parent.glob(elf.name + "*.su"):
        for line in report.read_text().splitlines():
            location, size, kind = line.split("\t")
            if kind != "static":
                raise ValueError(f"Unbounded stack frame: {line}")
            name = location.rsplit(":", 1)[1]
            if name in frames and frames[name] != int(size):
                raise ValueError(f"Conflicting stack reports for {name}; rebuild the fault objects")
            frames[name] = int(size)
    if not frames:
        raise ValueError("No linked stack-usage reports were found")

    assembly = subprocess.check_output([str(objdump), "-d", str(elf)], text=True)
    calls = {}
    instructions = {}
    name = None
    for line in assembly.splitlines():
        symbol = re.match(r"^[0-9a-f]+ <([^>]+)>:", line)
        if symbol:
            name = symbol[1]
            calls[name] = set()
            instructions[name] = []
        elif name:
            instructions[name].append(line)
            branch = re.search(r"\s(?:b|bl|beq|bne|blt|bgt|ble|bge)\s+[0-9a-f]+ <([^>]+)>", line)
            if branch:
                target = branch[1].split("+")[0]
                if target != name:
                    calls[name].add(target)
            if name in frames and re.search(r"\s(?:bctrl|bctr)\s*$", line):
                raise ValueError(f"Indirect call needs a stack bound: {name}")

    def maximum(name, ancestors=()):
        if name in ancestors:
            raise ValueError(f"Recursive call path: {ancestors + (name,)}")
        if name not in frames:
            # libgcc's restore suffixes only unwind their caller's frame.
            if not name.startswith("_restgpr_"):
                raise ValueError(f"Missing stack-usage entry: {name}")
            if any(re.search(r"stwu\s+r1,", line) for line in instructions[name]):
                raise ValueError(f"Restore helper allocates a frame: {name}")
            return 0, (name,)
        children = [maximum(child, ancestors + (name,)) for child in calls[name]]
        child_bytes, child_path = max(children, default=(0, ()))
        return frames[name] + child_bytes, (name,) + child_path

    stack_bytes = int(re.search(r"#define RT_FAULT_STACK_BYTES (\d+)u", header.read_text())[1])
    used, path = maximum("fault_on_exception")
    linkage = 16  # faulthook.cpp leaves this above the C stack's initial r1.
    margin = stack_bytes - used - linkage
    if margin < 128:
        raise ValueError(f"Crash stack margin is only {margin} bytes; at least 128 are required")
    bca_used, _ = maximum("fault_on_ioctl")
    print(f"Crash stack: {used} C bytes + {linkage} linkage + {margin} margin = {stack_bytes}")
    print("Longest path: " + " -> ".join(path))
    print(f"BCA hook: {bca_used + 64} bytes on the game's stack, including its trampoline")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("objdump", type=Path)
    parser.add_argument("header", type=Path)
    args = parser.parse_args()
    check_stack(args.elf, args.objdump, args.header)


if __name__ == "__main__":
    main()
