# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with python3 tests/release_notes_tests.py from any directory."""
import importlib.util
from pathlib import Path
import unittest


REPO = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("release_notes", REPO / "tools/release_notes.py")
notes = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(notes)


class CommentTests(unittest.TestCase):
    def test_closed_comments_are_dropped(self):
        self.assertEqual(notes.without_comments("<!--\nabout\n-->\nhello\n<!-- b -->bye"), "hello\nbye")

    def test_unclosed_comment_is_refused(self):
        # 2611-162's Discord message went out with its comment in it.
        with self.assertRaises(SystemExit):
            notes.without_comments("<!--\nabout\n\nhello")

    def test_comment_running_into_the_next_is_refused(self):
        with self.assertRaises(SystemExit):
            notes.without_comments("<!-- a\nhello\n<!-- b -->\nbye")

    def test_stray_end_is_refused(self):
        with self.assertRaises(SystemExit):
            notes.without_comments("hello -->")


if __name__ == "__main__":
    unittest.main()
