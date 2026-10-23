# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the built Wii Remote logger against strings at the ends of Wii RAM.

Build with Makefile.runtime first, then run with Python's unicorn package:
    py -3.13 tools/wlog_boundary_emutest.py --devkitppc C:/devkitPro/devkitPPC

Only the final page of each RAM region is mapped: reading past it fails.
This exercises the actual PowerPC blob, without Dolphin or hardware.
"""

import argparse
from pathlib import Path
import struct
import subprocess

from unicorn import (Uc, UC_ARCH_PPC, UC_MODE_32, UC_MODE_BIG_ENDIAN,
                     UC_HOOK_CODE, UC_HOOK_MEM_READ_UNMAPPED, UcError)
from unicorn.ppc_const import UC_PPC_REG_0, UC_PPC_REG_LR, UC_PPC_REG_PC


ROOT = Path(__file__).resolve().parents[1]
CODE = 0x81000000
CONTEXT = 0x90000000
STATE = 0x90001000
STACK = 0x90020000
WORDS = 0x90030000
RETURN = 0x80001000


def print_offset(devkitppc):
    executable = devkitppc / "bin/powerpc-eabi-nm"
    elf = ROOT / "build-runtime/wlog/riftwii_wlog.elf"
    result = subprocess.run([str(executable), str(elf)], check=True,
                            capture_output=True, text=True)
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[2] == "wlog_on_print":
            return int(fields[0], 16)
    raise RuntimeError("wlog_on_print is missing from the built blob")


def check_boundary(blob, entry, end, string_argument, terminated):
    machine = Uc(UC_ARCH_PPC, UC_MODE_32 | UC_MODE_BIG_ENDIAN)
    machine.mem_map(CODE, (len(blob) + 4095) & ~4095)
    machine.mem_write(CODE, blob)
    machine.mem_map(CONTEXT, 0x40000)
    machine.mem_map(0x80000000, 0x2000)
    machine.mem_map(end - 4096, 4096)
    machine.mem_write(0x80000000, b"RMCE01\0\0")
    machine.mem_write(CONTEXT + 4, struct.pack(">I", STATE))

    source = end - 4
    machine.mem_write(source, b"abc\0" if terminated else b"abcd")
    if string_argument:
        format_address = WORDS + 64
        machine.mem_write(format_address, b"pad: %s\0")
        arguments = [format_address, source] + [0] * 6
    else:
        arguments = [source] + [0] * 7
    machine.mem_write(WORDS, struct.pack(">8I", *arguments))
    machine.reg_write(UC_PPC_REG_0 + 1, STACK)
    machine.reg_write(UC_PPC_REG_0 + 3, CONTEXT)
    machine.reg_write(UC_PPC_REG_0 + 4, WORDS)
    machine.reg_write(UC_PPC_REG_LR, RETURN)

    def stop_at_return(uc, address, size, data):
        if address == RETURN:
            uc.emu_stop()

    invalid_reads = []

    def fail_outside_ram(uc, access, address, size, value, data):
        invalid_reads.append(address)
        return False

    machine.hook_add(UC_HOOK_MEM_READ_UNMAPPED, fail_outside_ram)
    machine.hook_add(UC_HOOK_CODE, stop_at_return)
    try:
        machine.emu_start(CODE + entry, RETURN, count=100000)
    except UcError as error:
        if invalid_reads:
            raise AssertionError(f"Logger read outside RAM at {invalid_reads[0]:#010x}") from error
        raise
    # The instruction cap also ends emu_start: only a return counts.
    assert machine.reg_read(UC_PPC_REG_PC) == RETURN, "the logger did not return"
    # First entry: 40-byte header, then 40 bytes before its format and
    # another 64 bytes before the copied strings (runtime/rtwlog.h).
    text_at = STATE + 40 + 40 + (64 if string_argument else 0)
    copied = bytes(machine.mem_read(text_at, 5))
    expected = b"abc\0\0" if terminated else b"abcd\0"
    assert copied == expected, (hex(end), string_argument, copied)
    total = struct.unpack(">I", machine.mem_read(STATE + 20, 4))[0]
    assert total == 1, "the logger did not finish its entry"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--devkitppc", type=Path, required=True)
    args = parser.parse_args()
    blob = (ROOT / "build-runtime/riftwii_wlog.bin").read_bytes()
    entry = print_offset(args.devkitppc)
    for end in (0x81800000, 0x94000000):
        for string_argument in (False, True):
            for terminated in (False, True):
                check_boundary(blob, entry, end, string_argument, terminated)
    print("Wii Remote log boundary tests passed (8 cases)")


if __name__ == "__main__":
    main()
