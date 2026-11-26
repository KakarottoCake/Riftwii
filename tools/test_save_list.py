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
import save_gamebanana


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

    def test_named_gci_member(self):
        self.manifest["member"] = "Shared save/progress.gci"
        data = self.zip_bytes([(self.manifest["member"], self.gci), ("README.txt", b"Instructions")])
        self.assertEqual(save_list.inspect_payload(self.manifest, data), {})
        for extra in ("other.gci", "Shared save/PROGRESS.GCI", "../escape", "Shared save/progress.gci/child"):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                save_list.inspect_payload(self.manifest, self.zip_bytes([(self.manifest["member"], self.gci), (extra, self.gci)]))
        with self.assertRaises(ValueError):
            save_list.inspect_payload(self.manifest, self.zip_bytes([("missing.gci", self.gci)]))

    def test_named_wii_root_and_archive_root(self):
        manifest = {"platform": "wii", "game_id": "RAAE01", "format": "wii-zip", "root": "private/wii/title/RAAE/"}
        files = [(manifest["root"] + "banner.bin", b"WIBN"), (manifest["root"] + "save.dat", b"save"), ("README.txt", b"Read me")]
        self.assertEqual(save_list.inspect_payload(manifest, self.zip_bytes(files)), {"files": 2, "unpacked_size": 8})
        for bad in ("../folder/", "/folder/", "folder//sub/", "folder/./", "folder", "folder/\t/"):
            with self.subTest(root=bad), self.assertRaises(ValueError):
                save_list.inspect_payload({**manifest, "root": bad}, self.zip_bytes(files))
        manifest["root"] = ""
        self.assertEqual(save_list.inspect_payload(manifest, self.zip_bytes([("banner.bin", b"WIBN"), ("save.dat", b"save")])),
                         {"files": 2, "unpacked_size": 8})

    def test_bounded_region_ids(self):
        manifest = {"platform": "wii", "game_id": "RAAE01", "format": "data.bin", "game_ids": ["RAAE01", "RAAP01"]}
        self.assertEqual(save_list.inspect_payload(manifest, b"encrypted"), {})
        for ids in ([], ["RAAP01"], ["RAAE01"] * 9, ["RAAE01", "RAAE01"], ["RAAE01", "RBAP01"], ["RAAE01", "RAAP02"], "RAAE01"):
            with self.subTest(ids=ids), self.assertRaises(ValueError):
                save_list.inspect_payload({**manifest, "game_ids": ids}, b"encrypted")

    def test_gamebanana_uses_gci_header(self):
        gci = bytearray(self.gci)
        gci[:6] = b"GMSE01"
        record = {"_sFile": "shared.zip"}
        data = self.zip_bytes([("progress.gci", gci)])
        manifest, evidence = save_gamebanana.classify("Super Mario Sunshine", record, data, "100% save")
        self.assertEqual(manifest["game_id"], "GMSE01")
        self.assertEqual(manifest["member"], "progress.gci")
        self.assertEqual(evidence, "GCI header")
        with self.assertRaises(ValueError):
            save_gamebanana.classify("Super Smash Bros. Melee", record, data, "100% save")
        # A disc hack's own ID is authoritative too. It must never be
        # silently changed to a stock disc's maker code.
        gci[:6] = b"GLME05"
        manifest, _ = save_gamebanana.classify("Luigi's Mansion", record, self.zip_bytes([("save.gci", gci)]), "Save")
        self.assertEqual(manifest["game_id"], "GLME05")

    def test_gamebanana_wii_shapes_and_evidence(self):
        extracted = self.zip_bytes([("private/wii/title/RMGE/banner.bin", b"WIBN"),
                                    ("private/wii/title/RMGE/save.dat", b"save")])
        manifest, _ = save_gamebanana.classify("Super Mario Galaxy", {"_sFile": "save.zip"}, extracted, "100% save")
        self.assertEqual(manifest["root"], "private/wii/title/RMGE/")
        self.assertEqual(manifest["game_id"], "RMGE01")
        export = bytearray(0xF140)
        export[0xF0C0:0xF0C8] = b"\x00\x00\x00\x70Bk\x00\x01"
        export[0xF120:0xF128] = b"\x00\x01\x00\x00RMGP"
        encrypted = self.zip_bytes([("private/wii/title/RMGP/data.bin", export)])
        manifest, _ = save_gamebanana.classify("Super Mario Galaxy", {"_sFile": "save.zip"}, encrypted, "100% save")
        self.assertEqual(manifest["format"], "data.bin")
        self.assertEqual(manifest["game_id"], "RMGP01")
        with self.assertRaisesRegex(ValueError, "no Wii SD export backup header"):
            save_gamebanana.classify("Super Mario Galaxy", {"_sFile": "save.zip"},
                                    self.zip_bytes([("data.bin", b"encrypted")]), "100% save")
        with self.assertRaisesRegex(ValueError, "several Wii saves"):
            save_gamebanana.classify("Super Mario Galaxy", {"_sFile": "save.zip"},
                                    self.zip_bytes([("RMGE/data.bin", b"a"), ("RMGP/data.bin", b"b")]), "100% save")

    def test_gamebanana_never_guesses_portability(self):
        game = "Super Mario Galaxy"
        ids, _ = save_gamebanana.establish_ids(game, ["data/banner.bin"], "A USA save.")
        self.assertEqual(ids, ["RMGE01"])
        ids, _ = save_gamebanana.establish_ids(game, ["data/banner.bin"], "Save works in all regions.")
        self.assertEqual(ids, list(save_gamebanana.GAMES[game]))
        for text in ("100% save", "USA and PAL instructions", "All regions on Switch 2"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                save_gamebanana.establish_ids(game, ["data/banner.bin"], text)

    def test_export_id_without_decryption(self):
        export = bytearray(0xF140)
        export[0xF0C0:0xF0C8] = b"\x00\x00\x00\x70Bk\x00\x01"
        export[0xF120:0xF128] = b"\x00\x01\x00\x00RMGP"
        game = "Super Mario Galaxy"
        manifest, evidence = save_gamebanana.classify(game, {"_sFile": "save.zip"},
                                                     self.zip_bytes([("data.bin", export)]), "100% save")
        self.assertEqual(manifest["game_id"], "RMGP01")
        self.assertEqual(evidence, "unencrypted data.bin backup header")
        with self.assertRaisesRegex(ValueError, "does not identify this game"):
            save_gamebanana.export_game_id("Super Mario Galaxy 2", export)
        self.assertIsNone(save_gamebanana.export_game_id(game, export[:100]))
        export[0xF0C4] ^= 1
        self.assertIsNone(save_gamebanana.export_game_id(game, export))

    def test_extracted_save_with_uninstalled_companions(self):
        root = "524d4750/data/"
        files = [(root + "banner.bin", b"WIBN"), (root + "data.bin", b"game-data"),
                 ("524d4750/content/title.tmd", b"metadata")]
        manifest, _ = save_gamebanana.classify("Super Mario Galaxy", {"_sFile": "save.zip"}, self.zip_bytes(files), "100% save")
        self.assertEqual(manifest["root"], root)
        self.assertEqual(manifest["game_id"], "RMGP01")
        self.assertEqual(manifest["files"], 2)
        export = bytearray(0xF140)
        export[0xF0C0:0xF0C8] = b"\x00\x00\x00\x70Bk\x00\x01"
        export[0xF120:0xF128] = b"\x00\x01\x00\x00RMGP"
        files.append(("data.bin", export))
        manifest, _ = save_gamebanana.classify("Super Mario Galaxy", {"_sFile": "save.zip"}, self.zip_bytes(files), "100% save")
        self.assertEqual(manifest["format"], "wii-zip")
        self.assertEqual(manifest["files"], 2)
        export[0xF127] = ord("E")
        files[-1] = ("data.bin", export)
        with self.assertRaisesRegex(ValueError, "companion export"):
            save_gamebanana.classify("Super Mario Galaxy", {"_sFile": "save.zip"}, self.zip_bytes(files), "100% save")

    def test_gamebanana_tail_and_md5(self):
        data = self.zip_bytes([("save.gci", self.gci), ("README.txt", b"Read me")])
        self.assertEqual(save_gamebanana.zip_names_from_tail(data, len(data)), ["save.gci", "README.txt"])
        self.assertIsNone(save_gamebanana.zip_names_from_tail(data[:30], len(data)))
        record = {"_nFilesize": len(data), "_sMd5Checksum": hashlib.md5(data).hexdigest()}
        save_gamebanana.verify_file(record, data)
        with self.assertRaisesRegex(ValueError, "MD5"):
            save_gamebanana.verify_file(record, bytes([data[0] ^ 1]) + data[1:])
        text = save_gamebanana.plain_text("<p>Player &amp; author</p>" + "界" * 100, 80)
        self.assertLess(len(text.encode()), 80)

    def test_gamebanana_cached_files_are_rechecked(self):
        web = save_gamebanana.Web(self.root / "cache")
        record = {"_idRow": 123, "_nFilesize": len(self.gci), "_sMd5Checksum": hashlib.md5(self.gci).hexdigest(),
                  "_sDownloadUrl": "https://gamebanana.com/dl/123"}
        path = web.cache / "file-123.bin"
        path.write_bytes(self.gci)
        self.assertEqual(web.file(record), self.gci)
        self.assertEqual(web.requests, 0)
        changed = bytearray(self.gci)
        changed[-1] ^= 1
        path.write_bytes(changed)
        with self.assertRaisesRegex(ValueError, "MD5"):
            web.file(record)
        record["_sDownloadUrl"] = "https://example.org/unexpected"
        with self.assertRaisesRegex(ValueError, "unexpected download URL"):
            web.names(record)

    def test_zip_rejects_split_and_null_names(self):
        data = bytearray(self.zip_bytes([("data/banner.bin", b"WIBN"), ("data/save.dat", b"save")]))
        end = data.rfind(b"PK\x05\x06")
        data[end + 4] = 1
        with self.assertRaisesRegex(ValueError, "split ZIP"):
            save_list.checked_zip(data)
        data[end + 4] = 0
        at = data.find(b"data/save.dat", data.find(b"PK\x01\x02"))
        data[at + 5] = 0
        with self.assertRaisesRegex(ValueError, "null character"):
            save_list.checked_zip(data)


if __name__ == "__main__":
    unittest.main()
