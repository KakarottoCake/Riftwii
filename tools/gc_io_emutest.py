# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the built GameCube I/O worker with Unicorn's PowerPC core.

Run after Makefile.runtime, with Python's unicorn package installed:
    py -3.13 tools/gc_io_emutest.py --devkitppc C:/devkitPro/devkitPPC

This checks the actual continuation assembly, alarm returns, DMA bytes and
callback ordering against a deliberately slow fake IOS. It is not a Wii
or Dolphin timing measurement. Simulated ticks advance between alarms and
at one tick per twelve executed instructions, solely to exercise deadlines.
"""

import argparse
import heapq
from pathlib import Path
import random
import struct
import subprocess
import tempfile

from unicorn import Uc, UC_ARCH_PPC, UC_MODE_32, UC_MODE_BIG_ENDIAN
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.ppc_const import UC_PPC_REG_0, UC_PPC_REG_PC, UC_PPC_REG_LR, UC_PPC_REG_CR


ROOT = Path(__file__).resolve().parents[1]
BASE = 0x81700000
SET_ALARM = 0x80010000
CALLBACK = 0x80011000
RETURN = 0x80012000
IPC = 0xCD000000
FIELDS = (
    "sdio_fd", "sdhc", "runs", "run_count", "set_alarm", "alarm_entry",
    "bounce", "bounce_bytes", "delay_ticks", "usb_fd", "flags", "io_stack",
    "io_buffer", "io_job", "busy", "alarm", "io_alarm", "read_state",
    "read_length", "read_generation", "io_pending", "io_reply", "sd_requests",
    "stream", "stream_generation", "card", "beat_alarm", "drive_timing", "rvz",
    "card_rca",
)
EXTRA_FIELDS = {
    "stream": ("buf", "buf_bytes", "playing", "start", "length", "pos", "fetched", "consumed", "refills"),
    "card": ("image", "run_count", "runs", "dirty", "dirty_count", "activity", "seen_activity", "flushes"),
    "rvz": ("disc_bytes", "chunk_bytes", "group_count", "stored_buffer", "stored_bytes", "dctx_buffer",
            "dctx_bytes", "extent_count", "table_extent_count", "slots", "entry_sector", "table_extents",
            "extents", "header", "groups_loaded"),
}


def words(data):
    return struct.unpack(f">{len(data) // 4}I", data)


def symbols(path):
    data = path.read_bytes()
    section_offset = struct.unpack_from(">I", data, 32)[0]
    section_size, section_count = struct.unpack_from(">HH", data, 46)
    sections = [struct.unpack_from(">10I", data, section_offset + i * section_size)
                for i in range(section_count)]
    result = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, address = struct.unpack_from(">II", data, offset)
            end = names.find(b"\0", name)
            result[names[name:end].decode()] = BASE + address
    return result


def context_layout(devkitppc):
    """Ask the target compiler, so tests never duplicate struct offsets."""
    compiler = devkitppc / "bin/powerpc-eabi-gcc"
    objcopy = devkitppc / "bin/powerpc-eabi-objcopy"
    with tempfile.TemporaryDirectory(prefix="riftwii-gcio-") as directory:
        temporary = Path(directory)
        source = temporary / "layout.c"
        expressions = {name: f"offsetof(struct gc_rt_context, {name})" for name in FIELDS}
        for area, fields in EXTRA_FIELDS.items():
            type_name = "gc_rt_stream" if area == "stream" else f"gc_{area}"
            for name in fields:
                expressions[f"{area}.{name}"] = f"offsetof(struct {type_name}, {name})"
        entries = ",\n".join(expressions.values())
        source.write_text('#include <stddef.h>\n#include "gc_rt.h"\n'
                          '#include "gc_card.h"\n#include "gc_rvz.h"\n'
                          'const unsigned layout[] __attribute__((section(".layout"))) = {\n'
                          + entries + "\n};\n")
        obj = temporary / "layout.o"
        binary = temporary / "layout.bin"
        subprocess.run([str(compiler), "-mcpu=750", "-I", str(ROOT / "runtime/gc"),
                        "-I", str(ROOT / "runtime"),
                        "-c", str(source), "-o", str(obj)], check=True)
        subprocess.run([str(objcopy), "-O", "binary", "-j", ".layout", str(obj), str(binary)], check=True)
        return dict(zip(expressions, words(binary.read_bytes())))


class Machine:
    def __init__(self, layout, slow=False, usb=False, latency=0, rvz=False):
        self.uc = Uc(UC_ARCH_PPC, UC_MODE_32 | UC_MODE_BIG_ENDIAN)
        self.uc.mem_map(0x80000000, 0x01800000)
        self.uc.mem_map(0x90000000, 0x00400000)
        self.uc.mem_map(0xCC000000, 0x10000)
        self.uc.mem_map(IPC, 0x10000)
        variant = "riftwii_gc_rvz" if rvz else "riftwii_gc"
        binary = bytearray((ROOT / f"build-runtime/{variant}.bin").read_bytes())
        if rvz:
            relocations = (ROOT / "build-runtime/riftwii_gc_rvz.rel").read_bytes()
            for offset, kind, target in struct.iter_unpack(">III", relocations):
                address = BASE + target
                if kind == 1:
                    struct.pack_into(">I", binary, offset, address)
                else:
                    half = address if kind == 4 else (address + (0x8000 if kind == 6 else 0)) >> 16
                    struct.pack_into(">H", binary, offset, half & 0xFFFF)
        self.uc.mem_write(BASE, bytes(binary))
        self.ctx = BASE + words(binary[:16])[3]
        self.layout = layout
        directory = "gcrvz" if rvz else "gc"
        self.functions = symbols(ROOT / f"build-runtime/{directory}/{variant}.elf")
        self.ticks = 0
        self.instructions = 0
        self.alarms = []
        self.callbacks = []
        self.durations = []
        self.pending = None
        self.control = 0
        self.usb = usb
        self.latency = latency
        self.disk = None
        self.writes = []
        self.checkpoints = 0
        self.uc.hook_add(UC_HOOK_CODE, self.code)
        self.uc.hook_add(UC_HOOK_MEM_READ, self.mmio_read, begin=IPC, end=IPC + 8)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.mmio_write, begin=IPC, end=IPC + 8)
        for name, value in {
            "sdio_fd": 1, "sdhc": 1, "runs": 0x90000000, "run_count": 1,
            "set_alarm": SET_ALARM, "alarm_entry": self.functions["gc_alarm"],
            "bounce": 0x90010000, "bounce_bytes": 0x8000, "delay_ticks": 2000,
            "usb_fd": 2 if usb else 0xFFFFFFFF, "flags": 0x20000 if slow else 0,
            "io_stack": 0x90100000, "io_buffer": 0x90200000,
        }.items():
            self.put(name, value)
        # A deliberately unaligned run exercises sector skips and bounce copies.
        self.uc.mem_write(0x90000000, struct.pack(">5I", 0, 0x40000, 10, 37, 4 if usb else 0))

    def reg(self, number):
        return self.uc.reg_read(UC_PPC_REG_0 + number)

    def set_reg(self, number, value):
        self.uc.reg_write(UC_PPC_REG_0 + number, value)

    def get(self, name):
        return words(self.uc.mem_read(self.ctx + self.layout[name], 4))[0]

    def put(self, name, value):
        self.uc.mem_write(self.ctx + self.layout[name], struct.pack(">I", value))

    def put_object(self, address, area, name, value):
        self.uc.mem_write(address + self.layout[f"{area}.{name}"], struct.pack(">I", value))

    def get_object(self, address, area, name):
        return words(self.uc.mem_read(address + self.layout[f"{area}.{name}"], 4))[0]

    def code(self, uc, address, size, unused):
        self.instructions += 1
        if self.instructions % 12 == 0:
            self.ticks += 1
        if address == self.functions.get("gc_io_checkpoint"):
            self.checkpoints += 1
        if address == RETURN:
            uc.emu_stop()
            return
        if address == SET_ALARM:
            alarm = self.reg(3)
            assert not any(queued[1] == alarm for queued in self.alarms), "alarm queued twice"
            heapq.heappush(self.alarms, (self.ticks + self.reg(6), alarm))
            uc.reg_write(UC_PPC_REG_PC, uc.reg_read(UC_PPC_REG_LR))
            return
        if address == CALLBACK:
            self.callbacks.append((self.ticks, self.reg(3)))
            uc.reg_write(UC_PPC_REG_PC, uc.reg_read(UC_PPC_REG_LR))
            return
        instruction = words(uc.mem_read(address, 4))[0]
        # mftb/mftbu: Unicorn supplies no running time base of its own.
        if instruction & 0xFC1FFFFF in (0x7C0C42E6, 0x7C0D42E6):
            destination = (instruction >> 21) & 31
            self.set_reg(destination, self.ticks if instruction & 0xFFFF == 0x42E6 else 0)
            # The TBU form uses SPR 269 instead of 268.
            if instruction & 0xFC1FFFFF == 0x7C0D42E6:
                self.set_reg(destination, 0)
            uc.reg_write(UC_PPC_REG_PC, address + 4)

    def finish_ios(self):
        if self.pending is None:
            return
        physical, started, completed = self.pending
        if self.ticks >= started + 1000:
            self.control |= 2
        if self.ticks < completed:
            return
        request = 0x80000000 | physical
        fields = words(self.uc.mem_read(request, 64))
        if fields[0] == 6:
            # CMD13: ready, in transfer state, after an SD write.
            self.uc.mem_write(fields[6] | 0x80000000, struct.pack(">I", 4 << 9))
        else:
            vectors = words(self.uc.mem_read(fields[6] | 0x80000000, 24))
            command = words(self.uc.mem_read(vectors[0] | 0x80000000, 36))
            if fields[2] == 2:
                sector = command[0]
                count = words(self.uc.mem_read(vectors[2] | 0x80000000, 4))[0]
                target = vectors[4] | 0x80000000
            else:
                sector, count = command[3], command[4]
                target = vectors[2] | 0x80000000
            length = count * 512
            if command[0] == 0x19 and fields[2] == 1:
                self.writes.append((sector, bytes(self.uc.mem_read(target, length))))
            else:
                payload = (self.disk[sector * 512:sector * 512 + length] if self.disk is not None else
                           bytes((sector * 512 + i) % 251 for i in range(length)))
                self.uc.mem_write(target, bytes(payload))
        self.uc.mem_write(request + 4, b"\0" * 4)
        self.uc.mem_write(IPC + 8, struct.pack(">I", physical))
        self.control |= 6
        self.pending = None

    def mmio_read(self, uc, access, address, size, value, unused):
        self.finish_ios()
        if address == IPC + 4:
            uc.mem_write(address, struct.pack(">I", self.control))

    def mmio_write(self, uc, access, address, size, value, unused):
        if address != IPC + 4:
            return
        if value == 1:
            assert self.pending is None, "a second request replaced an in-flight DMA"
            physical = words(uc.mem_read(IPC, 4))[0]
            # sd_slow imposes its deadline independently of this IOS latency.
            self.pending = (physical, self.ticks, self.ticks + self.latency)
        elif value in (2, 4):
            self.control &= ~value

    def call(self, function, *arguments):
        self.set_reg(1, 0x81000000)
        preserved = (2, 13, *range(14, 32))
        for number in preserved:
            self.set_reg(number, 0x80500000 + number * 0x100)
        self.uc.reg_write(UC_PPC_REG_CR, 0x12345678)
        self.uc.reg_write(UC_PPC_REG_LR, RETURN)
        for number, value in enumerate(arguments, 3):
            self.set_reg(number, value)
        started = self.ticks
        self.uc.emu_start(self.functions[function], RETURN + 4, count=8000000)
        assert self.uc.reg_read(UC_PPC_REG_PC) == RETURN, f"{function} did not return"
        assert self.reg(1) == 0x81000000, "the worker retained the game's stack"
        for number in preserved:
            assert self.reg(number) == 0x80500000 + number * 0x100, f"r{number} was corrupted"
        assert self.uc.reg_read(UC_PPC_REG_CR) & 0x00FFF000 == 0x12345678 & 0x00FFF000
        self.durations.append(self.ticks - started)

    def alarm(self):
        ticks, alarm = heapq.heappop(self.alarms)
        self.ticks = max(ticks, self.ticks)
        self.call("gc_on_alarm", self.ctx, alarm, 0)

    def drain(self):
        for turn in range(3000):
            if not self.alarms:
                return
            self.alarm()
        raise AssertionError("I/O did not finish in 3000 alarms")


def test_reads(layout):
    for usb in (False, True):
        machine = Machine(layout, slow=True, usb=usb)
        machine.call("gc_on_read", machine.ctx, 0x80020003, 0x18001, 123, CALLBACK)
        assert not machine.callbacks
        assert machine.get("sd_requests") == 0, "DVDLowRead performed storage I/O before returning"
        machine.drain()
        assert [kind for _, kind in machine.callbacks] == [1]
        actual = bytes(machine.uc.mem_read(0x80020003, 0x18001))
        expected = bytes((10 * 512 + 37 + 123 + i) % 251 for i in range(len(actual)))
        assert actual == expected, "disc bytes or sector-skip copies are wrong"
        if not usb:
            assert machine.ticks >= 2 * 91125 + 192 * 3645
        assert max(machine.durations) < 60750, "an alarm blocked interrupts for over one simulated ms"
        print(f"{'USB' if usb else 'SD'}: bytes, callback, private stack and alarm returns pass; "
              f"{len(machine.durations)} entries, longest {max(machine.durations)} simulated ticks")


def test_replacement(layout):
    machine = Machine(layout, slow=True)
    machine.call("gc_on_read", machine.ctx, 0x80040000, 0x10000, 0, CALLBACK)
    # Start the job, then suspend its DMA before replacing the command.
    for _ in range(2):
        ticks, alarm = heapq.heappop(machine.alarms)
        machine.ticks = max(machine.ticks, ticks)
        machine.call("gc_on_alarm", machine.ctx, alarm, 0)
    machine.call("gc_on_read", machine.ctx, 0x80050000, 512, 4096, CALLBACK)
    machine.drain()
    assert [kind for _, kind in machine.callbacks] == [1]
    assert bytes(machine.uc.mem_read(0x80040000, 0x10000)) == bytes(0x10000)
    expected = bytes((10 * 512 + 37 + 4096 + i) % 251 for i in range(512))
    assert bytes(machine.uc.mem_read(0x80050000, 512)) == expected
    print("Replacement read: old DMA drained without publishing bytes or a stale callback")


def test_drive_timing(layout):
    for slow, latency in ((True, 0), (False, 558000)):
        machine = Machine(layout, slow=slow, latency=latency)
        machine.uc.mem_write(0x90000000, struct.pack(">5I", 0, 0x40000, 10, 0, 0))
        machine.put("drive_timing", 1)
        machine.call("gc_on_read", machine.ctx, 0x80020000, 0x10000, 0, CALLBACK)
        machine.drain()
        assert machine.get("sd_requests") == 1
        assert machine.callbacks[0][0] >= 0x10000 // 2 * 243 // 4
        assert max(machine.durations) < 60750
    print("Aligned 64 KiB: one request; drive timing survives instant and delayed IOS replies")


def test_cancel_before_start(layout):
    machine = Machine(layout, slow=True)
    machine.call("gc_on_read", machine.ctx, 0x80040000, 512, 0, CALLBACK)
    machine.alarm()  # reserve the worker, without starting its DMA yet
    machine.call("gc_on_done", machine.ctx, CALLBACK)
    machine.drain()
    assert machine.get("sd_requests") == 0
    assert [kind for _, kind in machine.callbacks] == [1]
    print("Cancellation before the worker starts: no old request or duplicate callback")


def test_stream(layout):
    stream = 0x90040000
    for cancel in (False, True):
        machine = Machine(layout, slow=True)
        machine.put("stream", stream)
        for name, value in {"buf": 0x90060000, "buf_bytes": 0x20000, "playing": 1,
                            "start": 8192, "length": 0x10000, "pos": 8192}.items():
            machine.put_object(stream, "stream", name, value)
        machine.call("gc_on_pad_read", machine.ctx, 0, 0)
        assert machine.get("sd_requests") == 0, "PADRead waited for a ring refill"
        if cancel:
            machine.alarm()
            machine.call("gc_on_audio_stream", machine.ctx, 0x10000, 0, 0, CALLBACK)
        machine.drain()
        fetched = machine.get_object(stream, "stream", "fetched")
        if cancel:
            assert fetched == 0, "a cancelled stream published old bytes"
        else:
            assert fetched > 0
            expected = bytes((10 * 512 + 37 + 8192 + i) % 251 for i in range(fetched))
            assert bytes(machine.uc.mem_read(0x90060000, fetched)) == expected
    print("Music ring: PADRead returns before I/O, complete bytes publish once, cancellation discards them")


def test_card(layout):
    machine = Machine(layout, slow=True, latency=607500)
    card = 0x90040000
    image = 0x90080000
    machine.put("card", card)
    machine.put("card_rca", 1)
    for name, value in {"image": image, "run_count": 1, "dirty_count": 1,
                        "activity": 1, "seen_activity": 1}.items():
        machine.put_object(card, "card", name, value)
    machine.uc.mem_write(card + layout["card.runs"], struct.pack(">3I", 0, 300, 1))
    machine.uc.mem_write(card + layout["card.dirty"], b"\x01")
    machine.uc.mem_write(image, b"A" * 512)

    def heartbeat():
        alarm = machine.ctx + layout["beat_alarm"]
        machine.call("gc_on_alarm", machine.ctx, alarm, 0)
        machine.alarms = [entry for entry in machine.alarms if entry[1] != alarm]
        heapq.heapify(machine.alarms)

    heartbeat()
    machine.alarm()
    machine.uc.mem_write(image, b"B" * 512)
    machine.put_object(card, "card", "activity", 2)
    machine.drain()
    assert machine.writes == [(300, b"A" * 512)], "IOS did not receive an immutable save snapshot"
    assert machine.get_object(card, "card", "dirty_count") == 1
    heartbeat()  # see the intervening card access
    machine.drain()
    heartbeat()  # quiet now: commit the newer sector
    machine.drain()
    assert machine.writes[-1] == (300, b"B" * 512)
    assert machine.get_object(card, "card", "dirty_count") == 0
    print("Save overlap: the first snapshot stays immutable and newer dirty bytes are written again")


def test_timeout(layout):
    machine = Machine(layout, latency=607500000)
    machine.call("gc_on_read", machine.ctx, 0x80040000, 512, 0, CALLBACK)
    machine.alarm()
    machine.alarm()
    machine.ticks += 303750001
    machine.drain()
    assert [kind for _, kind in machine.callbacks] == [2]
    assert machine.get("io_pending") != 0
    machine.call("gc_on_read", machine.ctx, 0x80050000, 512, 4096, CALLBACK)
    machine.alarm()
    assert machine.get("sd_requests") == 1, "a timeout released a buffer IOS still owned"
    machine.ticks += 303750001
    machine.latency = 0
    machine.drain()
    assert [kind for _, kind in machine.callbacks] == [2, 1]
    assert machine.get("io_pending") == 0
    print("Timeout: buffers stay quarantined until the late DMA reply, then the next read succeeds")


def test_reset_drain(layout):
    for latency in (0, 60750):
        machine = Machine(layout, latency=latency, slow=True)
        machine.call("gc_on_read", machine.ctx, 0x80040000, 512, 0, CALLBACK)
        machine.alarm()
        machine.alarm()
        arguments = 0x80060000
        machine.uc.mem_write(arguments, bytes(12))
        machine.call("gc_on_reset_system", machine.ctx, arguments)
        machine.drain()
        assert not machine.callbacks
        assert machine.get("io_pending") == 0
        assert machine.get("busy") == 0
        assert bytes(machine.uc.mem_read(0x80040000, 512)) == bytes(512)
        machine.latency = 0
        machine.call("gc_on_read", machine.ctx, 0x80050000, 512, 4096, CALLBACK)
        machine.drain()
        assert [kind for _, kind in machine.callbacks] == [1]
    print("Reset/exec drain: pending and already routed DMA replies are safe before dropping the worker")


def test_rvz(layout, devkitppc):
    # A real Zstandard frame exercises the relocated RVZ decoder and
    # continuation through its sequence checkpoints, not a decoder mock.
    generator = random.Random(229)
    pieces = [generator.randbytes(64) for _ in range(256)]
    payload = b"".join(generator.choice(pieces) + generator.randbytes(4) for _ in range(2000))[:0x20000]
    zstd = devkitppc.parent / "msys2/usr/bin/zstd.exe"
    compressed = subprocess.run([str(zstd), "-q", "-c", "-5"], input=payload,
                                capture_output=True, check=True).stdout
    read_rvz_fixture(layout, compressed, 0x80000000, payload, "Zstandard")
    read_rvz_fixture(layout, struct.pack(">I", len(payload)) + payload, 0x40000000, payload, "packed literals")
    padding = struct.pack(">I", 0x80020000) + bytes(68)
    read_rvz_fixture(layout, padding, 0x40000000, bytes(0x20000), "packed padding")


def read_rvz_fixture(layout, stored, flags, payload, label):
    machine = Machine(layout, slow=True, rvz=True)
    disk = bytearray(0x40000)
    disk[512:520] = struct.pack(">2I", 4096 // 4, len(stored) | flags)
    disk[4096:4096 + len(stored)] = stored
    machine.disk = disk
    rvz = 0x90050000
    machine.put("rvz", rvz)
    for name, value in {"disc_bytes": len(payload), "chunk_bytes": len(payload), "group_count": 1,
                        "stored_buffer": 0x90060000, "stored_bytes": 0x30000,
                        "dctx_buffer": 0x90300000, "dctx_bytes": 0x80000,
                        "extent_count": 1, "table_extent_count": 1, "entry_sector": 0xFFFFFFFF}.items():
        machine.put_object(rvz, "rvz", name, value)
    machine.uc.mem_write(rvz + layout["rvz.extents"], struct.pack(">3I", 0, 0, len(disk) // 512))
    machine.uc.mem_write(rvz + layout["rvz.table_extents"], struct.pack(">3I", 0, 1, 1))
    for slot, buffer in enumerate((0x900A0000, 0x900D0000)):
        machine.uc.mem_write(rvz + layout["rvz.slots"] + slot * 24,
                             struct.pack(">6I", buffer, 0x21000, 0xFFFFFFFF, 0, 0, 0))
    machine.uc.mem_write(0x90000000, struct.pack(">5I", 0, len(payload), 0, 0, 3))
    machine.call("gc_on_read", machine.ctx, 0x80020000, len(payload) - 128, 128, CALLBACK)
    machine.drain()
    assert [kind for _, kind in machine.callbacks] == [1]
    assert bytes(machine.uc.mem_read(0x80020000, len(payload) - 128)) == payload[128:]
    assert machine.get_object(rvz, "rvz", "groups_loaded") == 1
    assert max(machine.durations) < 60750, (max(machine.durations), machine.checkpoints)
    print(f"RVZ {label}: 128 KiB group decodes across short alarms, cache reused; "
          f"longest {max(machine.durations)} simulated ticks")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--devkitppc", type=Path, required=True)
    args = parser.parse_args()
    layout = context_layout(args.devkitppc)
    test_reads(layout)
    test_replacement(layout)
    test_drive_timing(layout)
    test_cancel_before_start(layout)
    test_stream(layout)
    test_card(layout)
    test_timeout(layout)
    test_reset_drain(layout)
    test_rvz(layout, args.devkitppc)


if __name__ == "__main__":
    main()
