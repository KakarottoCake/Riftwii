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
    "io_buffer", "io_job", "storage_busy", "alarm", "io_alarm", "read_state",
    "read_length", "read_generation", "io_pending", "io_reply", "sd_requests",
    "stream", "stream_generation", "card", "beat_alarm", "drive_timing", "rvz",
    "card_rca", "ipc_sending", "bt", "gcad", "disc_id_offset", "read_drive_ticks",
    "last_read_end", "reads", "read_tgc_watch", "ipc", "tgc_count", "tgc_offset", "tgc_dol_start",
    "tgc_dol_end", "disc_change", "di_regs", "vec", "request", "response", "leave_buf", "leave_fd",
)
EXTRA_FIELDS = {
    "stream": ("buf", "buf_bytes", "playing", "start", "length", "pos", "fetched", "consumed", "refills"),
    "card": ("image", "run_count", "runs", "dirty", "dirty_count", "activity", "seen_activity", "flushes"),
    "rvz": ("disc_bytes", "chunk_bytes", "group_count", "stored_buffer", "stored_bytes", "dctx_buffer",
            "dctx_bytes", "extent_count", "table_extent_count", "slots", "entry_sector", "table_extents",
            "extents", "header", "groups_loaded"),
    "bt": ("fd", "stage", "polls", "events"),
    "bt_rq": ("state", "result"),
    "gcad": ("fd", "version", "known_dev", "ticks_per_ms", "polls", "sends", "replies"),
    "disc_change": ("magic", "discs", "current", "state", "due", "alarm", "cover_ticks", "look_ticks"),
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
                          '#include "gc_bt.h"\n#include "gc_gcad.h"\n#include "gc_disc.h"\n'
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
        self.pending = []
        self.handshake = None
        self.abandoned_acks = []
        self.ack_latency = 1000
        self.send_timeout_at = None
        self.controller_latency = 1000
        self.sends = []
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
        if (address == self.functions.get("gc_ipc_wait")
                and self.send_timeout_at is not None and self.ticks >= self.send_timeout_at):
            # Exercise the real poll-limit branch without executing fifty
            # million iterations in Unicorn. Worker deadlines stay real.
            self.set_reg(5, 50000000)
            self.send_timeout_at = None
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
        if self.handshake is not None and self.ticks >= self.handshake:
            self.control |= 2
            self.handshake = None
        while self.abandoned_acks and self.ticks >= self.abandoned_acks[0]:
            heapq.heappop(self.abandoned_acks)
            self.control |= 2
        if self.control & 4:
            return
        ready = next((request for request in self.pending if self.ticks >= request[1]), None)
        if ready is None:
            return
        physical, completed = ready
        self.pending.remove(ready)
        request = 0x80000000 | physical
        fields = words(self.uc.mem_read(request, 64))
        if fields[2] in (3, 4, 5):
            # Controller requests use their own buffers. An empty successful
            # reply suffices to exercise each actual driver's routing path.
            pass
        elif fields[0] == 6:
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
        result = -1017 if fields[2] == 5 else 0  # ES refuses the test's title launch.
        self.uc.mem_write(request + 4, struct.pack(">I", result & 0xFFFFFFFF))
        self.uc.mem_write(IPC + 8, struct.pack(">I", physical))
        self.control |= 4

    def mmio_read(self, uc, access, address, size, value, unused):
        self.finish_ios()
        if address == IPC + 4:
            uc.mem_write(address, struct.pack(">I", self.control))

    def mmio_write(self, uc, access, address, size, value, unused):
        if address != IPC + 4:
            return
        if value == 1:
            assert self.handshake is None and not self.control & 2, "overlapping IPC send handshakes"
            physical = words(uc.mem_read(IPC, 4))[0]
            assert all(request[0] != physical for request in self.pending), "reused an in-flight IPC buffer"
            fields = words(uc.mem_read(physical | 0x80000000, 64))
            latency = self.controller_latency if fields[2] in (3, 4) else self.latency
            # sd_slow imposes its deadline independently of this IOS latency.
            self.handshake = self.ticks + self.ack_latency
            self.pending.append((physical, self.ticks + max(latency, self.ack_latency)))
            self.sends.append((physical, fields[2]))
        elif value in (2, 4):
            if value == 2 and self.handshake is not None:
                # Writing Y2 also writes X1=0. A controller timeout
                # withdraws that doorbell; IOS can still acknowledge late.
                heapq.heappush(self.abandoned_acks, self.handshake)
                self.handshake = None
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


def controller_poll(kind):
    address = 0x90040000 if kind == "bt" else 0x90050000
    return ("gc_bt_poll" if kind == "bt" else "gc_gcad_poll", address)


def setup_controllers(machine):
    machine.put("bt", 0x90040000)
    machine.put("gcad", 0x90050000)
    for name, value in {"fd": 3, "stage": 4}.items():
        machine.put_object(0x90040000, "bt", name, value)
    event = 0x90040000 + machine.layout["bt.events"]
    machine.put_object(event, "bt_rq", "state", 2)
    for name, value in {"fd": 4, "version": 4, "known_dev": 0xFFFFFFFF,
                        "ticks_per_ms": 60750}.items():
        machine.put_object(0x90050000, "gcad", name, value)


def test_controller_overlap(layout):
    for kind in ("bt", "gcad"):
        machine = Machine(layout, latency=6075000)
        setup_controllers(machine)
        function, address = controller_poll(kind)
        machine.uc.mem_write(0x90000000, struct.pack(">5I", 0, 0x40000, 10, 0, 0))
        machine.call("gc_on_read", machine.ctx, 0x80020000, 0x10000, 0, CALLBACK)
        machine.alarm()  # reserve the worker
        machine.alarm()  # X1 is sent; the worker yields before Y2
        assert machine.get("ipc_sending") == 1
        assert machine.get("storage_busy") == 1
        machine.call(function, machine.ctx, address)
        assert len(machine.sends) == 1, "a controller overwrote the read's unacknowledged X1"
        assert machine.get_object(address, kind, "polls") == 0
        while machine.get("ipc_sending"):
            machine.alarm()

        pending = machine.get("io_pending")
        job = machine.get("io_job")
        shared = bytes(machine.uc.mem_read(machine.ctx + layout["ipc"], 160))
        machine.call(function, machine.ctx, address)
        assert len(machine.sends) > 1, "a controller lost its poll while storage waited for a reply"
        assert machine.get("storage_busy") == 1 and machine.get("io_job") == job
        assert machine.get("io_pending") == pending
        assert bytes(machine.uc.mem_read(machine.ctx + layout["ipc"], 160)) == shared
        machine.call("gc_sd_write", machine.ctx, 300, 1, 0x90070000)
        assert machine.reg(3) == (-3000 & 0xFFFFFFFF), "a card write stole the read's buffers"
        assert machine.get("storage_busy") == 1 and machine.get("io_pending") == pending

        # Deliver the controller reply first, then have its next poll take
        # the storage reply before the worker gets another alarm turn.
        machine.ticks += 2000
        machine.call(function, machine.ctx, address)
        if kind == "gcad":
            assert machine.get_object(address, kind, "replies") > 0
        else:
            assert len([fd for _, fd in machine.sends if fd == 3]) >= 2
        machine.ticks += machine.latency
        machine.call(function, machine.ctx, address)
        assert machine.get("io_reply") == 1, "the poll lost the storage reply"
        assert machine.get("storage_busy") == 1 and machine.get("io_pending") == pending
        machine.drain()
        assert [result for _, result in machine.callbacks] == [1]
        expected = bytes((10 * 512 + i) % 251 for i in range(0x10000))
        assert bytes(machine.uc.mem_read(0x80020000, 0x10000)) == expected
        assert machine.get("storage_busy") == 0 and machine.get("ipc_sending") == 0

        blocked = Machine(layout)
        setup_controllers(blocked)
        blocked.put("storage_busy", 1)
        blocked.call(function, blocked.ctx, address)
        assert not blocked.sends, "a controller ran during shared-buffer preparation"
        assert blocked.get("storage_busy") == 1
    print("Adapter and Wii Remote polls: deferred during X1/Y2 and buffer preparation; "
          "send and route replies during storage waits without releasing its buffers")


def test_disk_id(layout):
    # A loader-provided header needs no device wait. Keep the old immediate
    # data copy, with only its completion callback deferred to the alarm.
    ready = Machine(layout)
    header = bytes(range(32))
    ready.uc.mem_write(0x90030000, header)
    ready.uc.mem_write(0x90000000, struct.pack(">5I", 0, 32, 0x90030000, 0, 2))
    ready.call("gc_on_disk_id", ready.ctx, 0x80020000, CALLBACK)
    assert bytes(ready.uc.mem_read(0x80020000, 32)) == header
    assert not ready.callbacks
    ready.drain()
    assert [result for _, result in ready.callbacks] == [1]

    for offset in (0, 4096):
        machine = Machine(layout, latency=60750)
        machine.put("drive_timing", 1)
        machine.put("last_read_end", 0x3F000)
        machine.put("disc_id_offset", offset)
        machine.put("tgc_count", 1)
        for name, value in {"tgc_offset": offset, "tgc_dol_start": offset,
                            "tgc_dol_end": offset + 0x100}.items():
            machine.put(name, value)
        machine.call("gc_on_disk_id", machine.ctx, 0x80020000, CALLBACK)
        assert not machine.callbacks and machine.get("sd_requests") == 1
        machine.drain()
        assert [result for _, result in machine.callbacks] == [1]
        assert machine.callbacks[0][0] < 100000, "ReadDiskID inherited a regular read's seek delay"
        assert machine.get("read_drive_ticks") == 0 and machine.get("last_read_end") == 0x3F000
        assert machine.get("reads") == 0 and machine.get("tgc_count") == 1
        assert machine.get("read_tgc_watch") == 0
        expected = bytes((10 * 512 + 37 + offset + i) % 251 for i in range(32))
        assert bytes(machine.uc.mem_read(0x80020000, 32)) == expected

    # A reset-style command replacement must drain the old read before
    # delivering the ID, and may never publish the superseded DMA.
    machine = Machine(layout, latency=60750)
    machine.call("gc_on_read", machine.ctx, 0x80030000, 512, 8192, CALLBACK)
    machine.alarm()
    machine.alarm()
    machine.call("gc_on_disk_id", machine.ctx, 0x80020000, CALLBACK)
    machine.drain()
    assert [result for _, result in machine.callbacks] == [1]
    assert bytes(machine.uc.mem_read(0x80030000, 512)) == bytes(512)
    expected = bytes((10 * 512 + 37 + i) % 251 for i in range(32))
    assert bytes(machine.uc.mem_read(0x80020000, 32)) == expected

    # Exercise the actual cover alarm's disc insertion, then identify the
    # new image with the same command the SDK issues after a disc swap.
    machine = Machine(layout, latency=60750)
    change = 0x90040000
    di = 0x90050000
    machine.put("disc_change", change)
    machine.put("di_regs", di)
    for name, value in {"magic": 0x52574443, "cover_ticks": 10000, "look_ticks": 1000}.items():
        machine.put_object(change, "disc_change", name, value)
    machine.uc.mem_write(change + layout["disc_change.discs"],
                         struct.pack(">6I", 0x90000000, 1, 0, 0x90001000, 1, 0))
    machine.uc.mem_write(0x90001000, struct.pack(">5I", 0, 0x40000, 20, 0, 0))
    machine.uc.mem_write(di + 4, struct.pack(">I", 2))
    machine.call("gc_on_stop_motor", machine.ctx, CALLBACK)
    machine.drain()
    assert machine.get_object(change, "disc_change", "current") == 1
    machine.callbacks.clear()
    machine.call("gc_on_disk_id", machine.ctx, 0x80020000, CALLBACK)
    machine.drain()
    assert [result for _, result in machine.callbacks] == [1]
    expected = bytes((20 * 512 + i) % 251 for i in range(32))
    assert bytes(machine.uc.mem_read(0x80020000, 32)) == expected
    print("ReadDiskID: ready bytes copied immediately, SD waits yield without emulated seek delay, "
          "correct TGC header without launching its watch, "
          "safe reset-style replacement and correct ID after the cover alarm inserts disc 2")


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


def test_controller_timeout(layout):
    for kind in ("bt", "gcad"):
        for late_ack in (False, True):
            machine = Machine(layout)
            setup_controllers(machine)
            function, address = controller_poll(kind)
            # Infinity represents an IOS request/acknowledgment that never
            # arrives. The finite case acknowledges after the caller exits.
            machine.ack_latency = 10000 if late_ack else float("inf")
            machine.send_timeout_at = machine.ticks + 1000
            machine.call(function, machine.ctx, address)
            assert len(machine.sends) == 1, "the controller did not attempt its timed-out send"
            assert machine.get("io_pending") == 0 and machine.get("storage_busy") == 0
            assert machine.get("ipc_sending") == 0, "a controller timeout kept the doorbell forever"
            assert machine.handshake is None, "the timed-out X1 was not withdrawn"
            if late_ack:
                machine.ticks += 10000
                machine.finish_ios()
                assert machine.control & 2, "the test did not deliver a stale Y2"

            machine.ack_latency = 1000
            machine.call("gc_on_read", machine.ctx, 0x80050000, 512, 4096, CALLBACK)
            machine.drain()
            assert [result for _, result in machine.callbacks] == [1]
            expected = bytes((10 * 512 + 37 + 4096 + i) % 251 for i in range(512))
            assert bytes(machine.uc.mem_read(0x80050000, 512)) == expected
            assert machine.get("io_pending") == 0 and machine.get("ipc_sending") == 0

            polls = machine.get_object(address, kind, "polls")
            machine.call(function, machine.ctx, address)
            assert machine.get_object(address, kind, "polls") == polls + 1
            other_kind = "gcad" if kind == "bt" else "bt"
            other_function, other_address = controller_poll(other_kind)
            sends = len(machine.sends)
            machine.call(other_function, machine.ctx, other_address)
            assert len(machine.sends) > sends, "controller IPC did not recover after the timeout"
            machine.ticks += 2000
            machine.call(other_function, machine.ctx, other_address)
            assert machine.get("ipc_sending") == 0
    print("Controller send timeouts: absent and late Y2 release X1; disc bytes and both controller polls recover")


def test_timeout(layout):
    for late_ack in (False, True):
        machine = Machine(layout, latency=607500000)
        if late_ack:
            machine.ack_latency = machine.latency
        machine.call("gc_on_read", machine.ctx, 0x80040000, 512, 0, CALLBACK)
        machine.alarm()
        machine.alarm()
        machine.ticks += 303750001
        machine.drain()
        assert [kind for _, kind in machine.callbacks] == [2]
        assert machine.get("io_pending") != 0
        assert machine.get("ipc_sending") == int(late_ack)
        assert machine.get("storage_busy") == 0 and machine.get("io_job") == 0
        machine.call("gc_on_read", machine.ctx, 0x80050000, 512, 4096, CALLBACK)
        machine.alarm()
        assert machine.get("sd_requests") == 1, "a timeout released a buffer IOS still owned"
        machine.ticks += 303750001
        machine.latency = 0
        machine.ack_latency = 1000
        machine.drain()
        assert [kind for _, kind in machine.callbacks] == [2, 1]
        assert machine.get("io_pending") == 0 and machine.get("ipc_sending") == 0
    print("Timeout: buffers and send handshake stay quarantined until the late acknowledgment/reply; "
          "the next read then succeeds")


def test_reply_before_ack_timeout(layout):
    machine = Machine(layout, latency=607500000)
    machine.ack_latency = machine.latency
    machine.call("gc_on_read", machine.ctx, 0x80040000, 512, 0, CALLBACK)
    machine.alarm()
    machine.alarm()
    physical, _ = machine.pending[0]
    machine.pending[0] = (physical, machine.ticks)
    machine.alarm()  # route the reply while Y2 is still missing
    assert machine.get("io_reply") == 1 and machine.get("ipc_sending") == 1
    machine.ticks += 303750001
    machine.drain()
    assert [result for _, result in machine.callbacks] == [2]
    assert machine.get("storage_busy") == 0 and machine.get("io_job") == 0
    machine.call("gc_on_read", machine.ctx, 0x80050000, 512, 4096, CALLBACK)
    machine.alarm()
    assert machine.get("io_pending") == 0, "an already routed reply kept the DMA quarantined"
    assert machine.get("ipc_sending") == 1, "the missing acknowledgment lost its quarantine"
    assert machine.get("sd_requests") == 1
    machine.ticks += 303750001
    machine.latency = 0
    machine.ack_latency = 1000
    machine.drain()
    assert [result for _, result in machine.callbacks] == [2, 1]
    expected = bytes((10 * 512 + 37 + 4096 + i) % 251 for i in range(512))
    assert bytes(machine.uc.mem_read(0x80050000, 512)) == expected
    assert machine.get("io_pending") == 0 and machine.get("ipc_sending") == 0
    print("Reply before a send timeout: DMA and late Y2 recover independently before the next read")


def test_reset_drain(layout):
    for function in ("gc_on_reset_system", "gc_on_boot_dol"):
        for latency in (0, 60750):
            machine = Machine(layout, latency=latency, slow=True)
            machine.call("gc_on_read", machine.ctx, 0x80040000, 512, 0, CALLBACK)
            machine.alarm()
            machine.alarm()
            arguments = 0x80060000
            machine.uc.mem_write(arguments, bytes(28))
            machine.call(function, machine.ctx, arguments)
            machine.drain()
            assert not machine.callbacks
            assert machine.get("io_pending") == 0
            assert machine.get("storage_busy") == 0
            assert bytes(machine.uc.mem_read(0x80040000, 512)) == bytes(512)
            machine.latency = 0
            machine.call("gc_on_read", machine.ctx, 0x80050000, 512, 4096, CALLBACK)
            machine.drain()
            assert [kind for _, kind in machine.callbacks] == [1]
    print("Reset and unredirected exec: pending and already routed DMA replies are safe "
          "before dropping the worker")


def test_leave_timeout(layout):
    machine = Machine(layout)
    machine.put("leave_buf", 0x90060000)
    machine.put("leave_fd", 5)
    snapshot = b"T" * 512
    machine.uc.mem_write(0x90070000, snapshot)
    traces = []

    def failed_trace(uc, address, size, unused):
        if address != machine.functions["gc_trace"]:
            return
        traces.append(machine.reg(4))
        if len(traces) == 1:
            # Inject the state a timed-out trace leaves behind, including
            # a late acknowledgment. The real exit must drain its DMA
            # before overwriting this request and its vectors for ES.
            request = machine.ctx + layout["ipc"]
            vectors = machine.ctx + layout["vec"]
            command = machine.ctx + layout["request"]
            fields = [0] * 16
            fields[0], fields[2], fields[3], fields[6] = 7, 1, 7, vectors & 0x3FFFFFFF
            uc.mem_write(request, struct.pack(">16I", *fields))
            uc.mem_write(command, struct.pack(">9I", 0x19, 3, 1, 300, 1, 512, 0x90070000, 1, 0))
            uc.mem_write(vectors, struct.pack(">6I", command & 0x3FFFFFFF, 36, 0x10070000,
                                             512, (machine.ctx + layout["response"]) & 0x3FFFFFFF, 16))
            physical = request & 0x3FFFFFFF
            machine.put("io_pending", physical)
            machine.put("ipc_sending", 1)
            machine.handshake = machine.ticks + 1000
            machine.pending.append((physical, machine.ticks + 10000))
        uc.reg_write(UC_PPC_REG_PC, uc.reg_read(UC_PPC_REG_LR))

    machine.uc.hook_add(UC_HOOK_CODE, failed_trace)
    machine.call("gc_leave", machine.ctx)
    assert machine.writes == [(300, snapshot)], "ES replaced the timed-out trace before IOS finished it"
    assert [fd for _, fd in machine.sends] == [5]
    assert machine.get("io_pending") == 0 and machine.get("ipc_sending") == 0
    print("Exit after a trace timeout: late acknowledgment and unchanged DMA bytes drain before ES reuses IPC")


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
    test_controller_overlap(layout)
    test_disk_id(layout)
    test_cancel_before_start(layout)
    test_stream(layout)
    test_card(layout)
    test_controller_timeout(layout)
    test_timeout(layout)
    test_reply_before_ack_timeout(layout)
    test_reset_drain(layout)
    test_leave_timeout(layout)
    test_rvz(layout, args.devkitppc)


if __name__ == "__main__":
    main()
