# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare the GameCube blob's hook trampolines with the old ones, in
Unicorn's PowerPC core.

    python -I tools/gc_hooks_emutest.py --devkitppc /opt/devkitpro/devkitPPC

runtime/gc/gc_entry.S and the copy of it from before the trampolines were
shared (tests/fixtures/gc-legacy/gc_entry_legacy.S) are each assembled and
linked with the same stand-ins for the C functions they call: every stand-in
records which function ran and the registers r3 to r10 it got, and the one
for gc_on_exi also sets the `pass` word to what the test asks. Each hook is
then called with the same made-up game registers in both, with `pass` 0 and
1 for the EXI hooks, whose continue slot jumps to a stop address as the
loader's does to the original function's second word. The two must agree on
where the call ended (back in the game or in the replay slot), on every
record, on the caller's stack and on every register the game can rely on:
for the EXI hooks all but r12 (the continue slot's own scratch register),
for the DVDLow hooks all but the volatile r11, r12 and CTR.

Needs Python's unicorn package and devkitPPC; not part of ctest, like
tools/gc_io_emutest.py.
"""

import argparse
from pathlib import Path
import random
import struct
import subprocess
import tempfile

from unicorn import Uc, UC_ARCH_PPC, UC_MODE_32, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
from unicorn.ppc_const import (UC_PPC_REG_0, UC_PPC_REG_PC, UC_PPC_REG_LR, UC_PPC_REG_CR, UC_PPC_REG_CTR,
                               UC_PPC_REG_XER, UC_PPC_REG_MSR)

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x81700000
RECORDS = 0x80100000      # count, pass, then 10 words a call
RETURN = 0x80200000       # where the game's call returns to
REPLAYED = 0x80300000     # where a continue slot goes (the original's second word)
STACK = 0x80F00000
EXI_HOOKS = 11
DVD_HOOKS = ("read", "disk_id", "inquiry", "audio_status", "audio_stream", "load_queued",
             "load_callbacks_done", "stop_motor", "load_alloc", "load_callbacks", "seek", "cb_r3", "cb_r5")


def run(command):
    subprocess.run([str(c) for c in command], check=True)


def build(devkitppc, source, directory, name):
    """The entry file linked with recording stand-ins for its C callees."""
    gcc = devkitppc / "bin/powerpc-eabi-gcc"
    nm = devkitppc / "bin/powerpc-eabi-nm"
    entry = directory / f"{name}_entry.o"
    run([gcc, "-mcpu=750", "-I", ROOT / "runtime/gc", "-c", "-o", entry, source])
    undefined = subprocess.run([str(nm), "-u", str(entry)], check=True, capture_output=True,
                               text=True).stdout.split()
    callees = sorted(n for n in undefined if n not in ("U",))
    lines = ["    .section .text.stubs, \"ax\", @progbits"]
    for number, callee in enumerate(callees):
        lines += [f"    .globl {callee}", f"{callee}:", f"    li 0, {number}", "    b gc_test_record"]
    exi = callees.index("gc_on_exi") if "gc_on_exi" in callees else -1
    lines += [
        "gc_test_record:",
        f"    lis 11, {RECORDS >> 16}",
        "    lwz 12, 0(11)",
        "    addi 12, 12, 1",
        "    stw 12, 0(11)",
        "    mulli 12, 12, 40",
        "    add 12, 11, 12",
        "    stw 0, 0(12)",
    ] + [f"    stw {r}, {4 * (r - 2)}(12)" for r in range(3, 11)] + [
        f"    cmpwi 0, {exi}",
        "    bne 1f",
        "    lwz 12, 4(11)",
        "    stw 12, 0(10)",
        "1:  oris 3, 0, 0x5A00",
        "    or 3, 3, 0",
        "    li 0, 0x77",
        "    blr",
    ]
    stubs_source = directory / f"{name}_stubs.S"
    stubs_source.write_text("\n".join(lines) + "\n")
    stubs = directory / f"{name}_stubs.o"
    run([gcc, "-mcpu=750", "-c", "-o", stubs, stubs_source])
    elf = directory / f"{name}.elf"
    run([gcc, "-nostdlib", "-nostartfiles", "-static", "-Wl,--build-id=none", "-T", ROOT / "runtime/gc/gc.ld",
         f"-Wl,--defsym,RT_BASE={BASE:#x}", "-o", elf, entry, stubs])
    image = directory / f"{name}.bin"
    run([devkitppc / "bin/powerpc-eabi-objcopy", "-O", "binary", elf, image])
    table = {}
    for line in subprocess.run([str(nm), str(elf)], check=True, capture_output=True, text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3:
            table[parts[2]] = int(parts[0], 16)
    return image.read_bytes(), table, callees


class Blob:
    def __init__(self, image, table, callees):
        self.image, self.table, self.callees = image, table, callees

    def call(self, entry, registers, pass_word=0, exi_index=None):
        uc = Uc(UC_ARCH_PPC, UC_MODE_32 | UC_MODE_BIG_ENDIAN)
        uc.mem_map(0x80000000, 0x01800000)
        uc.mem_write(BASE, self.image)
        if exi_index is not None:
            # As the loader does: the continue slot jumps to the original's second word.
            cont = self.table[f"gc_exi_continue_{exi_index}"]
            words = (0x3D800000 | (REPLAYED >> 16), 0x618C0000 | (REPLAYED & 0xFFFF), 0x7D8903A6, 0x4E800420)
            uc.mem_write(cont, struct.pack(">4I", *words))
        uc.mem_write(RECORDS, struct.pack(">II", 0, pass_word))
        for address in (RETURN, REPLAYED):
            uc.mem_write(address, struct.pack(">I", 0x48000000))  # b .
        uc.mem_write(STACK - 0x100, bytes(range(256)) * 2)
        for number, value in registers.items():
            uc.reg_write(UC_PPC_REG_0 + number, value)
        uc.reg_write(UC_PPC_REG_1, STACK)
        uc.reg_write(UC_PPC_REG_LR, RETURN)
        uc.reg_write(UC_PPC_REG_CTR, 0xC7C7C7C7)
        uc.reg_write(UC_PPC_REG_CR, 0x24682468)
        ended = []

        def stop(unicorn, address, size, unused):
            if address in (RETURN, REPLAYED):
                ended.append(address)
                unicorn.emu_stop()

        uc.hook_add(UC_HOOK_CODE, stop)
        uc.emu_start(entry, 0, count=100000)
        assert ended, "the hook did not come back"
        count = struct.unpack(">I", uc.mem_read(RECORDS, 4))[0]
        records = []
        for i in range(1, count + 1):
            fields = struct.unpack(">10I", uc.mem_read(RECORDS + 40 * i, 40))
            # r3 is the context: compared as its distance from gc_context_data,
            # which moves with the code before it.
            records.append((self.callees[fields[0]], fields[1] - self.table["gc_context_data"]) + fields[2:9])
        state = {f"r{n}": uc.reg_read(UC_PPC_REG_0 + n) for n in range(32)}
        state.update(lr=uc.reg_read(UC_PPC_REG_LR), ctr=uc.reg_read(UC_PPC_REG_CTR),
                     cr=uc.reg_read(UC_PPC_REG_CR), xer=uc.reg_read(UC_PPC_REG_XER))
        caller_stack = bytes(uc.mem_read(STACK, 0x40))
        return ended[0], records, state, caller_stack


UC_PPC_REG_1 = UC_PPC_REG_0 + 1


def game_registers(rng):
    registers = {n: rng.getrandbits(32) for n in range(32) if n != 1}
    registers[2] = 0x80500000
    registers[13] = 0x80600000
    return registers


def compare(what, old, new, volatile):
    old_end, old_records, old_state, old_stack = old
    new_end, new_records, new_state, new_stack = new
    assert old_end == new_end, f"{what}: ended at {old_end:#x} before, {new_end:#x} now"
    assert old_records == new_records, f"{what}: calls {old_records} before, {new_records} now"
    assert old_stack == new_stack, f"{what}: the caller's stack differs"
    for name, value in old_state.items():
        if name in volatile:
            continue
        assert new_state[name] == value, f"{what}: {name} {value:#x} before, {new_state[name]:#x} now"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--devkitppc", type=Path, required=True)
    parser.add_argument("--legacy", type=Path, default=ROOT / "tests/fixtures/gc-legacy/gc_entry_legacy.S")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="riftwii-gchooks-") as directory:
        temporary = Path(directory)
        old = Blob(*build(args.devkitppc, args.legacy, temporary, "legacy"))
        new = Blob(*build(args.devkitppc, ROOT / "runtime/gc/gc_entry.S", temporary, "current"))
    rng = random.Random(504)
    checks = 0
    for _ in range(64):
        registers = game_registers(rng)
        for index in range(EXI_HOOKS):
            for pass_word in (0, 1):
                what = f"EXI hook {index}, pass {pass_word}"
                before = old.call(old.table[f"gc_exi_hook_{index}"], registers, pass_word, index)
                after = new.call(new.table[f"gc_exi_hook_{index}"], registers, pass_word, index)
                assert before[0] == (REPLAYED if pass_word else RETURN), what
                compare(what, before, after, {"r12"})
                checks += 1
        for name in DVD_HOOKS:
            what = f"gc_hook_{name}"
            before = old.call(old.table[what], registers)
            after = new.call(new.table[what], registers)
            assert before[0] == RETURN and before[1], what
            compare(what, before, after, {"r11", "r12", "ctr"})
            checks += 1
    print(f"gc hooks emutest: {checks} calls, old and new trampolines agree")


if __name__ == "__main__":
    main()
