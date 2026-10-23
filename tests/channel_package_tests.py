# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with python3 tests/channel_package_tests.py from any directory."""
import importlib.util
from pathlib import Path
import struct
import unittest


REPO = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("make_channel", REPO / "tools/make_channel.py")
channel = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(channel)


class WiiStartupOverlapTests(unittest.TestCase):
    PRODUCTION_START_SIZE = 424

    def setUp(self):
        # A synthetic start.S with both cache-clear signatures and a final
        # partial cache line, so its padding also belongs to the stub.
        self.start = b"\0" * 4 + b"RFTW" + b"\0" * 4
        self.start += b"".join(channel.L2_CLEAR)
        self.start += b"\0" * (self.PRODUCTION_START_SIZE - len(self.start))
        self.stub_size = (len(self.start) + 31) & ~31
        self.stub_end = channel.WII_START_AT + self.stub_size
        self.assertEqual(self.stub_size, 448)
        self.assertEqual(self.stub_end, 0x800035C0)

    def dol(self, section_address=None, bss_address=None):
        dol = bytearray(0x140)
        struct.pack_into(">I", dol, 0, 0x100)
        struct.pack_into(">I", dol, 0x48, 0x80004000)
        struct.pack_into(">I", dol, 0x90, 0x20)
        struct.pack_into(">I", dol, 0xE0, 0x80004000)
        if section_address is not None:
            struct.pack_into(">I", dol, 4, 0x120)
            struct.pack_into(">I", dol, 0x4C, section_address)
            struct.pack_into(">I", dol, 0x94, 0x20)
        if bss_address is not None:
            struct.pack_into(">II", dol, 0xD8, bss_address, 0x20)
        return dol

    def test_sections_cannot_overlap_any_stub_byte(self):
        for address in (channel.WII_START_AT, channel.WII_START_AT + 32, self.stub_end - 1):
            with self.subTest(address=hex(address)):
                with self.assertRaisesRegex(SystemExit, "section overlaps"):
                    channel.wii_forwarder(self.dol(section_address=address), self.start)

    def test_bss_cannot_overlap_any_stub_byte(self):
        for address in (channel.WII_START_AT, channel.WII_START_AT + 32, self.stub_end - 1):
            with self.subTest(address=hex(address)):
                with self.assertRaisesRegex(SystemExit, "bss overlaps"):
                    channel.wii_forwarder(self.dol(bss_address=address), self.start)

    def test_adjacent_sections_and_bss_are_kept(self):
        dol = self.dol(section_address=self.stub_end, bss_address=channel.WII_START_AT - 0x20)
        packed = channel.wii_forwarder(dol, self.start)
        starts = struct.unpack_from(">18I", packed, 0x48)
        sizes = struct.unpack_from(">18I", packed, 0x90)
        self.assertEqual(starts[:3], (channel.WII_START_AT, 0x80004000, self.stub_end))
        self.assertEqual(sizes[0], self.stub_size)
        self.assertEqual(struct.unpack_from(">II", packed, 0xD8), (channel.WII_START_AT - 0x20, 0x20))
        self.assertEqual(struct.unpack_from(">I", packed, 0xE0)[0], 0x80004000)


if __name__ == "__main__":
    unittest.main()
