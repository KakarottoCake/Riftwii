#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline catalogue-builder checks using only synthetic saves."""

import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

import save_list


class SaveListTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="riftwii-save-list-")
        self.root = Path(self.temporary.name)
        (self.root / "files").mkdir()
        self.gci = bytearray(8256)
        self.gci[:6] = b"GAAE01"
        struct.pack_into(">H", self.gci, 0x38, 1)
        self.manifest = {
            "id": "synthetic",
            "game_id": "GAAE01",
            "platform": "gamecube",
            "title": "Synthetic test",
            "description": "Generated test bytes, not a game save",
            "author": "Test",
            "page": "https://example.org/save",
            "format": "gci",
            "file": "files/synthetic.gci",
            "size": len(self.gci),
            "sha256": hashlib.sha256(self.gci).hexdigest(),
            "approval": {"approved": True, "evidence": "https://example.org/permission", "reviewer": "Test"},
        }
        (self.root / "files/synthetic.gci").write_bytes(self.gci)

    def tearDown(self):
        self.temporary.cleanup()

    def entry(self, allow_pending=False):
        return save_list.catalogue_entry(self.manifest, self.root, "https://riftwii.com/saves/", allow_pending)

    def test_approved_local_build(self):
        (self.root / "synthetic.json").write_text(json.dumps(self.manifest), encoding="utf-8")
        output = self.root / "site"
        args = argparse.Namespace(command="build", manifests=self.root, output=output,
                                  check_tool=None, base_url="https://riftwii.com/saves/")
        save_list.run(args)
        catalogue = json.loads((output / "saves.json").read_text(encoding="utf-8"))
        self.assertEqual(catalogue["schema"], 1)
        self.assertEqual(catalogue["saves"][0]["url"], "https://riftwii.com/saves/files/synthetic.gci")
        self.assertNotIn("approval", catalogue["saves"][0])
        self.assertEqual((output / "files/synthetic.gci").read_bytes(), self.gci)

    def test_pending_never_publishes(self):
        self.manifest["approval"]["approved"] = False
        with self.assertRaisesRegex(ValueError, "human review"):
            self.entry()
        self.assertEqual(self.entry(allow_pending=True)["id"], "synthetic")

    def test_changed_payload_does_not_advance_pin(self):
        self.gci[64] = 1
        (self.root / "files/synthetic.gci").write_bytes(self.gci)
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            self.entry()

    def test_wrong_game_and_block_count(self):
        for game_id, blocks in (("GAAJ01", 1), ("GAAE01", 2)):
            data = bytearray(self.gci)
            data[:6] = game_id.encode("ascii")
            struct.pack_into(">H", data, 0x38, blocks)
            with self.assertRaises(ValueError):
                save_list.inspect_payload(self.manifest, data)

    def test_asset_cannot_escape(self):
        self.manifest["file"] = "../outside.gci"
        with self.assertRaises(ValueError):
            self.entry()

    def test_no_duplicate_json_keys(self):
        path = self.root / "synthetic.json"
        path.write_text('{"id":"synthetic","id":"other"}', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "duplicate JSON key"):
            save_list.read_manifest(path)

    def test_urls(self):
        for bad in ("http://example.org/a", "https://127.0.0.1/a", "https://user@example.org/a",
                    "https://0x7f.0.0.1/a", "https://example..org/a", "https://-example.org/a",
                    "https://example.org:443/a", "https://example.org/a\n", "https://example.org/é"):
            self.assertFalse(save_list.web_url(bad), bad)
        self.assertTrue(save_list.web_url("https://example.org/save.gci"))

    @staticmethod
    def zip_bytes(files):
        output = io.BytesIO()
        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name, contents in files:
                archive.writestr(name, contents)
        return output.getvalue()

    def test_zip_counts_and_deflate(self):
        manifest = {"platform": "wii", "game_id": "RAAE01", "format": "wii-zip"}
        data = self.zip_bytes([("data/banner.bin", b"WIBN"), ("data/sub/save.dat", b"save")])
        self.assertEqual(save_list.inspect_payload(manifest, data), {"files": 2, "unpacked_size": 8})

    def test_zip_rejects_unsafe_paths_and_conflicts(self):
        manifest = {"platform": "wii", "game_id": "RAAE01", "format": "wii-zip"}
        for bad in ("data/../escape", "data//file", "data/hidden.", "data/sub/a:b", "other/file",
                    "data/banner.bin/sub", "data/BANNER.BIN", "data/1234567890123"):
            data = self.zip_bytes([("data/banner.bin", b"WIBN"), (bad, b"save")])
            with self.assertRaises(ValueError, msg=bad):
                save_list.inspect_payload(manifest, data)

    def test_pin_preserves_pending_status(self):
        self.manifest["approval"]["approved"] = False
        self.manifest["sha256"] = "0" * 64
        path = self.root / "synthetic.json"
        path.write_text(json.dumps(self.manifest), encoding="utf-8")
        save_list.run(argparse.Namespace(command="pin", manifest=path))
        pinned = json.loads(path.read_text(encoding="utf-8"))
        self.assertFalse(pinned["approval"]["approved"])
        self.assertEqual(pinned["sha256"], hashlib.sha256(self.gci).hexdigest())


if __name__ == "__main__":
    unittest.main()
