# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with python3 tests/app_list_tests.py from any directory."""
import email.message
import importlib.util
import io
import tempfile
import unittest
import urllib.error
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("app_list", REPO / "tools/app_list.py")
app_list = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(app_list)


def _opaque(tag):
    return tag[2:] if tag.startswith("W/") else tag


class FakeServer:
    """Stands in for urlopen: serves `body` with `etag`, and answers an
    If-None-Match that matches weakly (RFC 9110 13.1.2: W/"x" matches "x")
    with a 304 that carries its current ETag, as a real server may."""

    def __init__(self, body, etag=""):
        self.body, self.etag, self.requests = body, etag, []

    def _headers(self):
        h = email.message.Message()
        if self.etag:
            h["ETag"] = self.etag
        return h

    def __call__(self, req, timeout=None):
        sent = req.get_header("If-none-match") or ""
        self.requests.append(sent)
        if sent and self.etag and _opaque(sent) == _opaque(self.etag):
            raise urllib.error.HTTPError(req.full_url, 304, "Not Modified", self._headers(), None)
        server = self

        class Response(io.BytesIO):
            headers = server._headers()

            def __enter__(self):
                return self

            def __exit__(self, *exc):
                return False

        return Response(self.body)


class CacheTests(unittest.TestCase):
    def setUp(self):
        self.cache = tempfile.TemporaryDirectory()
        self.real_urlopen = app_list.urllib.request.urlopen

    def tearDown(self):
        app_list.urllib.request.urlopen = self.real_urlopen
        self.cache.cleanup()

    def download(self, server, stamp=None):
        app_list.urllib.request.urlopen = server
        return app_list.cached_download("https://example.com/app.zip", self.cache.name, stamp)

    def test_strong_etag_confirmed_reuses_the_copy(self):
        server = FakeServer(b"one", '"abc"')
        self.assertEqual(self.download(server), b"one")
        server.body = b"never sent"
        self.assertEqual(self.download(server), b"one")
        self.assertEqual(server.requests[-1], '"abc"')

    def test_changed_file_is_downloaded_again(self):
        server = FakeServer(b"one", '"abc"')
        self.download(server)
        server.body, server.etag = b"two", '"def"'
        self.assertEqual(self.download(server), b"two")

    def test_weak_etag_is_never_trusted(self):
        # A weak ETag may stay the same when the bytes change.
        server = FakeServer(b"one", 'W/"abc"')
        self.download(server)
        server.body = b"two"
        self.assertEqual(self.download(server), b"two")
        self.assertEqual(server.requests[-1], "")

    def test_strong_to_weak_304_is_not_trusted(self):
        # The server's ETag turns weak with the same value: it answers the
        # old strong one with 304 (weak comparison), but the bytes changed.
        server = FakeServer(b"one", '"abc"')
        self.download(server)
        server.body, server.etag = b"two", 'W/"abc"'
        self.assertEqual(self.download(server), b"two")
        self.assertEqual(server.requests[-2:], ['"abc"', ""])

    def test_no_etag_means_a_fresh_download(self):
        # Last-Modified alone misses changes within its second.
        server = FakeServer(b"one")
        self.download(server)
        server.body = b"two"
        self.assertEqual(self.download(server), b"two")

    def test_github_stamp_keys_the_cache(self):
        server = FakeServer(b"one")
        self.assertEqual(self.download(server, "1-2026-11-01"), b"one")
        server.body = b"two"
        self.assertEqual(self.download(server, "1-2026-11-01"), b"one")
        self.assertEqual(self.download(server, "2-2026-11-23"), b"two")


class RuleTests(unittest.TestCase):
    def test_reserved_ids(self):
        self.assertFalse(app_list.id_ok("RiftWii"))
        self.assertTrue(app_list.id_ok("snes9xgx"))

    def test_riftwii_only_as_its_own_manifest(self):
        # RiftWii's own entry may use the id; no app may name it as `newer`.
        self.assertFalse(app_list.id_ok("riftwii"))
        manifest = {"id": "riftwii", "name": "RiftWii", "author": "RiftWii contributors",
                    "description": "A loader.", "category": "loaders", "page": "https://riftwii.com/",
                    "platforms": ["wii"], "source": {"github": "a/b", "asset": "*.zip"},
                    "install": [{"from": "apps/riftwii/", "to": "apps/riftwii/"}]}
        app_list.check_manifest(manifest, "riftwii.json")
        other = dict(manifest, id="snes9xgx", newer="riftwii",
                     install=[{"from": "apps/snes9xgx/", "to": "apps/snes9xgx/"}])
        with self.assertRaises(app_list.AppError):
            app_list.check_manifest(other, "snes9xgx.json")

    def test_trailing_dots_and_spaces(self):
        self.assertFalse(app_list.part_ok("boot.dol."))
        self.assertFalse(app_list.part_ok("boot.dol "))
        self.assertFalse(app_list.part_ok("..."))
        self.assertTrue(app_list.part_ok("boot.dol"))

    def test_hosts(self):
        self.assertFalse(app_list.https_url_ok("https://0x7f.0x0.0x0.1/a.zip", 300))
        self.assertFalse(app_list.https_url_ok("https://2130706433/a.zip", 300))
        self.assertTrue(app_list.https_url_ok("https://github.com/a/b.zip", 300))

    def test_backslashes_separate_folders(self):
        rules = [{"from": "apps/x/", "to": "apps/x/"}]
        self.assertEqual(app_list.install_path(rules, "apps\\x\\boot.dol"), "apps/x/boot.dol")
        self.assertIsNone(app_list.install_path(rules, "apps/x/a\\..\\b"))

    def test_install_path_wildcard(self):
        rules = [{"from": "*/wii/gcmm/", "to": "apps/gcmm/"}]
        self.assertEqual(app_list.install_path(rules, "gcmm_1.5.2/wii/gcmm/boot.dol"), "apps/gcmm/boot.dol")
        self.assertIsNone(app_list.install_path(rules, "wii/gcmm/boot.dol"))
        # What "*" takes is checked as a written part is.
        self.assertIsNone(app_list.install_path(rules, "../wii/gcmm/boot.dol"))
        self.assertIsNone(app_list.install_path(rules, "./wii/gcmm/boot.dol"))
        self.assertIsNone(app_list.install_path(rules, "..\\wii\\gcmm\\boot.dol"))



def _chunk(kind, body):
    return len(body).to_bytes(4, "big") + kind + body + zlib.crc32(kind + body).to_bytes(4, "big")


def _png(width, height):
    """A whole RGBA PNG of that size, every pixel clear."""
    header = width.to_bytes(4, "big") + height.to_bytes(4, "big") + b"\x08\x06\x00\x00\x00"
    rows = (b"\x00" + b"\x00" * width * 4) * height
    return (b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", header) + _chunk(b"IDAT", zlib.compress(rows)) +
            _chunk(b"IEND", b""))


class OwnIconTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.manifest = Path(self.folder.name) / "x.json"
        self.icon = Path(self.folder.name) / "x.png"

    def tearDown(self):
        self.folder.cleanup()

    def test_no_file_means_no_icon(self):
        self.assertIsNone(app_list.own_icon(str(self.manifest), "x.json"))

    def test_a_homebrew_channel_icon_is_taken(self):
        self.icon.write_bytes(_png(128, 48))
        self.assertEqual(app_list.own_icon(str(self.manifest), "x.json"), _png(128, 48))

    def test_other_sizes_and_non_pngs_are_refused(self):
        self.icon.write_bytes(_png(256, 96))
        with self.assertRaises(app_list.AppError):
            app_list.own_icon(str(self.manifest), "x.json")
        self.icon.write_bytes(b"GIF89a" + b"\x00" * 40)
        with self.assertRaises(app_list.AppError):
            app_list.own_icon(str(self.manifest), "x.json")

    def test_a_cut_or_damaged_png_is_refused(self):
        whole = _png(128, 48)
        cut = whole[:-12]  # no IEND
        damaged = bytearray(whole)
        damaged[40] ^= 0xFF  # inside IDAT: its CRC no longer matches
        header_only = whole[:33]
        short_data = (whole[:33] + _chunk(b"IDAT", zlib.compress(b"\x00" * 10)) + _chunk(b"IEND", b""))
        for data in (cut, bytes(damaged), header_only, short_data):
            self.icon.write_bytes(data)
            with self.assertRaises(app_list.AppError):
                app_list.own_icon(str(self.manifest), "x.json")

    def test_a_file_over_the_cap_is_refused(self):
        self.icon.write_bytes(_png(128, 48) + b"\x00" * app_list.MAX_ICON)
        with self.assertRaises(app_list.AppError):
            app_list.own_icon(str(self.manifest), "x.json")


if __name__ == "__main__":
    unittest.main()
