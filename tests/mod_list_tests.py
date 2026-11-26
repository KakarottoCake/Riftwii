# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with python3 tests/mod_list_tests.py from any directory. Offline:
no request reaches GameBanana."""
import importlib.util
import io
import sys
import unittest
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))
SPEC = importlib.util.spec_from_file_location("mod_list", REPO / "tools/mod_list.py")
mod_list = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(mod_list)


def make_zip(names, comment=b""):
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w") as z:
        for name in names:
            z.writestr(name, b"x" * 10)
        z.comment = comment
    return out.getvalue()


class ZipTailTests(unittest.TestCase):
    def test_names_from_the_end_of_a_zip(self):
        names = ["Pack v1/riivolution/Pack.xml", "Pack v1/Pack/a.arc", "readme.txt"]
        data = make_zip(names)
        self.assertEqual(mod_list.zip_names_from_tail(data[-65536:], len(data)), names)

    def test_a_tail_without_the_whole_directory(self):
        data = make_zip(["a/riivolution/a.xml"] * 1 + [f"a/f{i}.bin" for i in range(5)])
        # Only the last 30 bytes: the directory is not in them.
        self.assertIsNone(mod_list.zip_names_from_tail(data[-30:], len(data)))

    def test_a_damaged_directory_record(self):
        # A record that claims more than the directory holds.
        record = b"PK\x01\x02" + b"\0" * 24 + b"\xff\x00\0\0\0\0" + b"\0" * 16
        eocd = b"PK\x05\x06" + b"\0" * 4 + (1).to_bytes(2, "little") * 2 + len(record).to_bytes(4, "little") + \
            (0).to_bytes(4, "little") + b"\0\0"
        data = record + eocd
        self.assertIsNone(mod_list.zip_names_from_tail(data, len(data)))
        # And a directory too short for one record's fixed part.
        short = b"PK\x01\x02" + b"PK\x05\x06" + b"\0" * 4 + (1).to_bytes(2, "little") * 2 + \
            (4).to_bytes(4, "little") + (0).to_bytes(4, "little") + b"\0\0"
        self.assertIsNone(mod_list.zip_names_from_tail(short, len(short)))

    def test_not_a_zip(self):
        self.assertIsNone(mod_list.zip_names_from_tail(b"Rar!\x1a\x07\x00" + b"\0" * 100, 107))


class LayoutTests(unittest.TestCase):
    def test_pack_under_a_top_folder(self):
        root, riiv, xmls, folders = mod_list.pack_layout(
            ["Beta v1/", "Beta v1/riivolution/Beta.xml", "Beta v1/Beta/ObjectData/a.arc",
             "Beta v1/Beta/b.arc", "Beta v1/readme.txt", "__MACOSX/Beta v1/._x"])
        self.assertEqual(root, "Beta v1/")
        self.assertEqual(riiv, "riivolution/")
        self.assertEqual(xmls, ["Beta v1/riivolution/Beta.xml"])
        self.assertEqual(folders, ["Beta"])

    def test_pack_at_the_top_keeps_its_folder_case(self):
        root, riiv, xmls, folders = mod_list.pack_layout(["Riivolution/Newer.xml", "NewerRes/x.arc"])
        self.assertEqual(root, "")
        self.assertEqual(riiv, "Riivolution/")
        self.assertEqual(folders, ["NewerRes"])

    def test_no_xml_is_no_pack(self):
        with self.assertRaises(mod_list.Skip):
            mod_list.pack_layout(["Pack/a.arc", "riivolution/readme.txt"])


class TextTests(unittest.TestCase):
    def test_html_to_one_line(self):
        self.assertEqual(mod_list.plain_text("<p>Hello&amp;<b>bye</b></p><br>next", 100), "Hello&bye next")

    def test_cut_at_a_word(self):
        text = mod_list.plain_text("one two three four five six", 15)
        self.assertTrue(text.endswith("..."))
        self.assertLessEqual(len(text.encode()), 15)
        self.assertTrue(text.startswith("one two"))

    def test_cut_keeps_utf8_whole(self):
        text = mod_list.cut("★" * 40, 20)
        text.encode("utf-8")  # no broken character
        self.assertLessEqual(len(text.encode()), 20)

    def test_game_ids(self):
        xml = b'<wiidisc version="1"><id game="RMG"><region type="E"/></id></wiidisc>'
        self.assertEqual(mod_list.game_ids([xml, b"<id game='smn'/>", b'<id game="!!"/>']), ["RMG", "SMN"])


class CappedTests(unittest.TestCase):
    def test_output_over_the_cap_stops_the_command(self):
        writes_a_lot = [sys.executable, "-c", "import sys; sys.stdout.write('x' * 300000)"]
        self.assertIsNone(mod_list.run_capped(writes_a_lot, 100000))
        self.assertEqual(len(mod_list.run_capped(writes_a_lot, 400000)), 300000)

    def test_a_failing_command(self):
        self.assertIsNone(mod_list.run_capped([sys.executable, "-c", "raise SystemExit(3)"], 100))


class XmlTests(unittest.TestCase):
    def test_an_oversized_xml_rejects_the_archive(self):
        texts = {"riivolution/A.xml": b"<id game='RMG'/>", "riivolution/B.xml": None}
        with self.assertRaises(mod_list.Skip):
            mod_list.read_pack_xmls(list(texts), texts.get)
        big = {"riivolution/A.xml": b"<id game='RMG'/>", "riivolution/B.xml": b"x" * (mod_list.MAX_XML + 1)}
        with self.assertRaises(mod_list.Skip):
            mod_list.read_pack_xmls(list(big), big.get)

    def test_all_xmls_read(self):
        texts = {"riivolution/A.xml": b"<id game='RMG'/>", "riivolution/B.xml": b"<id game='SB4'/>"}
        self.assertEqual(mod_list.game_ids(mod_list.read_pack_xmls(list(texts), texts.get).values()), ["RMG", "SB4"])

    def test_verdicts_from_older_rules_are_looked_at_again(self):
        # A pack listed before the version 2 rules: its game IDs may be partial.
        old = {"format": "zip", "program": "Riivolution/Newer.xml", "games": ["SMN"]}
        self.assertFalse(mod_list.cached_verdict_ok(old))
        self.assertTrue(mod_list.cached_verdict_ok(dict(old, v=mod_list.VERDICT_VERSION)))
        self.assertTrue(mod_list.cached_verdict_ok({"skip": "no riivolution/*.xml"}))

    def test_file_verdict_drops_an_old_one(self):
        builder = mod_list.Builder.__new__(mod_list.Builder)
        builder.state = {"mods": {}, "files": {"7": {"format": "zip", "games": ["SMN"]}}}
        looked = []

        def look_inside(f):
            looked.append(f["_idRow"])
            return {"v": mod_list.VERDICT_VERSION, "format": "zip", "games": ["SMN", "SMNP"]}
        builder.look_inside = look_inside
        verdict = builder.file_verdict({"_idRow": 7})
        self.assertEqual(looked, [7])
        self.assertEqual(verdict["games"], ["SMN", "SMNP"])
        # Now current: kept.
        builder.file_verdict({"_idRow": 7})
        self.assertEqual(looked, [7])


class ExcludeTests(unittest.TestCase):
    def test_ids_and_comments(self):
        import tempfile
        with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False, encoding="utf-8") as f:
            f.write("# asked 2026-11-26\n678330\n  12345  # another\nnot-an-id\n\n")
        self.assertEqual(mod_list.read_excluded(f.name), {678330, 12345})
        self.assertEqual(mod_list.read_excluded(None), set())


if __name__ == "__main__":
    unittest.main()
