# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with python3 tests/app_list_tests.py from any directory."""
import importlib.util
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("app_list", REPO / "tools/app_list.py")
app_list = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(app_list)


class FakeServer:
    """Stands in for fetch(): serves `body` with `etag`, and answers a
    matching If-None-Match with 304 (None), as a server would."""

    def __init__(self, body, etag=""):
        self.body, self.etag, self.requests = body, etag, []

    def __call__(self, url, token=None, accept=None, validators=None, answer=None):
        self.requests.append(dict(validators or {}))
        if validators and validators.get("If-None-Match") == self.etag:
            return None
        if answer is not None:
            answer["If-None-Match"] = app_list.strong_etag(self.etag)
        return self.body


class CacheTests(unittest.TestCase):
    def setUp(self):
        self.cache = tempfile.TemporaryDirectory()
        self.real_fetch = app_list.fetch

    def tearDown(self):
        app_list.fetch = self.real_fetch
        self.cache.cleanup()

    def download(self, server, stamp=None):
        app_list.fetch = server
        return app_list.cached_download("https://example.com/app.zip", self.cache.name, stamp)

    def test_strong_etag_confirmed_reuses_the_copy(self):
        server = FakeServer(b"one", '"abc"')
        self.assertEqual(self.download(server), b"one")
        server.body = b"never sent"
        self.assertEqual(self.download(server), b"one")
        self.assertEqual(server.requests[-1], {"If-None-Match": '"abc"'})

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
        self.assertEqual(server.requests[-1], {})

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

    def test_trailing_dots_and_spaces(self):
        self.assertFalse(app_list.part_ok("boot.dol."))
        self.assertFalse(app_list.part_ok("boot.dol "))
        self.assertFalse(app_list.part_ok("..."))
        self.assertTrue(app_list.part_ok("boot.dol"))

    def test_hosts(self):
        self.assertFalse(app_list.https_url_ok("https://0x7f.0x0.0x0.1/a.zip", 300))
        self.assertFalse(app_list.https_url_ok("https://2130706433/a.zip", 300))
        self.assertTrue(app_list.https_url_ok("https://github.com/a/b.zip", 300))

    def test_install_path_wildcard(self):
        rules = [{"from": "*/wii/gcmm/", "to": "apps/gcmm/"}]
        self.assertEqual(app_list.install_path(rules, "gcmm_1.5.2/wii/gcmm/boot.dol"), "apps/gcmm/boot.dol")
        self.assertIsNone(app_list.install_path(rules, "wii/gcmm/boot.dol"))


if __name__ == "__main__":
    unittest.main()
