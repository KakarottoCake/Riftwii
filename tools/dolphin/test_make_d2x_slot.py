# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the synthetic slot's binary records without starting Dolphin.

Usage: python -m unittest discover -s tools/dolphin -p test_make_d2x_slot.py
"""

import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

import make_d2x_slot


class D2xSlotTests(unittest.TestCase):
    def test_installed_records(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            make_d2x_slot.write_slot(root, 249, 56, 11, 21011, "beta3")
            content_dir = root / "Wii/title/00000001/000000f9/content"
            tmd = (content_dir / "title.tmd").read_bytes()
            content = (content_dir / "00000000.app").read_bytes()
            ticket = (root / "Wii/ticket/00000001/000000f9.tik").read_bytes()

            self.assertEqual(len(tmd), 0x1E4 + 0x24)
            self.assertEqual(struct.unpack_from(">I", tmd)[0], 0x10001)
            self.assertTrue(tmd[0x140:0x180].startswith(b"Root-RiftWii-Test-CP\0"))
            self.assertEqual(struct.unpack_from(">Q", tmd, 0x184)[0], 0)
            self.assertEqual(struct.unpack_from(">Q", tmd, 0x18C)[0], 0x1000000F9)
            self.assertEqual(struct.unpack_from(">I", tmd, 0x194)[0], 1)
            self.assertEqual(struct.unpack_from(">HHH", tmd, 0x1DC), (21011, 1, 0))
            self.assertEqual(struct.unpack_from(">IHHQ", tmd, 0x1E4), (0, 0, 1, len(content)))
            self.assertEqual(tmd[0x1F4:0x208], hashlib.sha1(content).digest())

            self.assertEqual(len(content), 0x40)
            self.assertEqual(struct.unpack_from(">IIII", content), (0x1EE7C105, 1, 11, 56))
            self.assertEqual(content[0x10:0x20], b"d2x" + bytes(13))
            self.assertEqual(content[0x20:0x30], b"beta3" + bytes(11))
            self.assertEqual(len(ticket), 0x2A4)
            self.assertEqual(struct.unpack_from(">I", ticket)[0], 0x10001)
            self.assertEqual(struct.unpack_from(">Q", ticket, 0x1DC)[0], 0x1000000F9)
            self.assertEqual(ticket[0x222], 0x80)
            self.assertEqual(tmd[4:0x140], bytes(0x13C))
            self.assertEqual(ticket[4:0x140], bytes(0x13C))

    def test_slot_boundaries_and_custom_version(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for slot, base in ((200, 57), (255, 58)):
                make_d2x_slot.write_slot(root, slot, base, 12, 65535, "123456789abcdef")
                content_dir = root / f"Wii/title/00000001/{slot:08x}/content"
                content = (content_dir / "00000000.app").read_bytes()
                tmd = (content_dir / "title.tmd").read_bytes()
                self.assertEqual(struct.unpack_from(">I", content, 8)[0], 12)
                self.assertEqual(content[0x0F], base)
                self.assertEqual(content[0x20:0x30], b"123456789abcdef\0")
                self.assertEqual(struct.unpack_from(">H", tmd, 0x1DC)[0], 65535)

    def test_repeats_and_conflicts(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            make_d2x_slot.write_slot(root, 250, 57, 11, 21011, "beta3")
            before = {path: path.read_bytes() for path in root.rglob("*") if path.is_file()}
            make_d2x_slot.write_slot(root, 250, 57, 11, 21011, "beta3")
            with self.assertRaises(FileExistsError):
                make_d2x_slot.write_slot(root, 250, 58, 12, 21012, "final")
            after = {path: path.read_bytes() for path in root.rglob("*") if path.is_file()}
            self.assertEqual(before, after)

    def test_cli_rejects_invalid_input_before_writing(self):
        script = Path(make_d2x_slot.__file__)
        invalid_arguments = (
            ["199", "56"], ["256", "56"], ["249", "0"], ["249", "200"],
            ["249", "56", "--major", "1000"],
            ["249", "56", "--revision", "65536"],
            ["249", "56", "--release", "123456789abcdefg"],
            ["249", "56", "--release", "beta\n3"],
            ["../249", "56"],
        )
        with tempfile.TemporaryDirectory() as folder:
            for arguments in invalid_arguments:
                with self.subTest(arguments=arguments):
                    result = subprocess.run(
                        [sys.executable, str(script), folder, *arguments],
                        capture_output=True, text=True, check=False,
                    )
                    self.assertEqual(result.returncode, 2)
                    self.assertFalse((Path(folder) / "Wii").exists())


if __name__ == "__main__":
    unittest.main()
