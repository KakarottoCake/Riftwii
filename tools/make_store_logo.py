# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The Rift Store's logo: tools/store_logo.html (SVG, the menu's rounded
font) rendered by a headless Chrome or Edge into docs/images/:

  rift-store.png       1280x640, the banner (README, the Apps repo, posts)
  rift-store-mark.png  512x512, the mark alone (an avatar, an icon)
  rift-store-wide.png  1600x400, a header

    py -3.13 tools/make_store_logo.py [browser.exe]
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAGE = os.path.join(ROOT, "tools", "store_logo.html")
OUT = os.path.join(ROOT, "docs", "images")
SIZES = {"banner": ("rift-store.png", 1280, 640), "mark": ("rift-store-mark.png", 512, 512),
         "wide": ("rift-store-wide.png", 1600, 400)}
BROWSERS = [r"C:\Program Files\Google\Chrome\Application\chrome.exe",
            r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
            "google-chrome", "chromium", "chromium-browser"]


def browser():
    if len(sys.argv) > 1:
        return sys.argv[1]
    for b in BROWSERS:
        if os.path.exists(b) or shutil.which(b):
            return b
    sys.exit("make_store_logo: no Chrome, Edge or Chromium found; name one")


def main():
    exe = browser()
    url = "file:///" + PAGE.replace("\\", "/")
    with tempfile.TemporaryDirectory() as profile:
        for kind, (name, w, h) in SIZES.items():
            out = os.path.join(OUT, name)
            subprocess.run([exe, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--allow-file-access-from-files",
                            f"--user-data-dir={profile}", "--default-background-color=00000000",
                            "--virtual-time-budget=3000", f"--window-size={w},{h}", f"--screenshot={out}",
                            f"{url}#{kind}"], check=True, capture_output=True)
            print("wrote", out)


if __name__ == "__main__":
    main()
