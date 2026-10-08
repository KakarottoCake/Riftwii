# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Packs riftwii-theme-kit-v<version>.zip, a release's download for themers.

    py -3.13 tools/make_theme_kit.py <theme_kit.exe> <out.zip>

<theme_kit.exe> is the host tool (cmake --build build-host --target
theme_kit). It paints the template pictures with the menu's own painter,
so the kit always matches the release it ships with. The zip holds:

    riftwii-theme-kit/README.txt          themes/kit/README.txt
    riftwii-theme-kit/THEMES.md           docs/THEMES.md
    riftwii-theme-kit/MyTheme/theme.ini   starter theme (theme_kit)
    riftwii-theme-kit/Midnight/           themes/Midnight (colours only)
    riftwii-theme-kit/Bookshelf/          themes/Bookshelf (its own pictures)
    riftwii-theme-kit/guide/              themes/kit/guide: where each
                                          picture and colour shows
    riftwii-theme-kit/templates/Default/  every picture, default look
    riftwii-theme-kit/templates/Midnight/ the same in Midnight's colours
    riftwii-theme-kit/templates/Bookshelf/ the same in Bookshelf's
"""
import pathlib
import re
import subprocess
import sys
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
TOP = "riftwii-theme-kit"


def version():
    # As Makefile.wii: the release's YYMM, then the commits since its tag.
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    base = re.search(r"project\(Riftwii VERSION ([0-9]+)[ )]", text).group(1)
    tag = re.search(r'set\(RIFTWII_BASE_TAG "([^"]*)"\)', text).group(1)
    count = subprocess.run(["git", "-C", str(ROOT), "rev-list", "--count", tag + "..HEAD"], check=True,
                           capture_output=True, text=True).stdout.strip()
    return base if count == "0" else base + "-" + count


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    tool, out = str(pathlib.Path(sys.argv[1]).resolve()), pathlib.Path(sys.argv[2])
    with tempfile.TemporaryDirectory() as tmp:
        gen = pathlib.Path(tmp)
        subprocess.run([tool, str(gen), "Default", "Midnight=" + str(ROOT / "themes/Midnight/theme.ini"),
                        "Bookshelf=" + str(ROOT / "themes/Bookshelf/theme.ini")], check=True)
        count = len(list((gen / "templates/Default").glob("*.png")))
        readme = (ROOT / "themes/kit/README.txt").read_text(encoding="utf-8")
        readme = readme.replace("{version}", version()).replace("{count}", str(count))
        files = {
            "README.txt": readme.replace("\n", "\r\n").encode("utf-8"),
            "THEMES.md": (ROOT / "docs/THEMES.md").read_bytes(),
            "MyTheme/theme.ini": (gen / "MyTheme/theme.ini").read_bytes(),
        }
        # The sample themes whole: Midnight is colours only, Bookshelf has
        # its own pictures (and music, if it ever gets some).
        for theme in ("Midnight", "Bookshelf"):
            for f in sorted((ROOT / "themes" / theme).iterdir()):
                files[theme + "/" + f.name] = f.read_bytes()
        for f in sorted((ROOT / "themes/kit/guide").glob("*.png")):
            files["guide/" + f.name] = f.read_bytes()
        for png in sorted((gen / "templates").rglob("*.png")):
            files["templates/" + png.relative_to(gen / "templates").as_posix()] = png.read_bytes()
        out.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
            for name, data in files.items():
                z.writestr(TOP + "/" + name, data)
    print(f"{out}: {len(files)} files")


if __name__ == "__main__":
    main()
