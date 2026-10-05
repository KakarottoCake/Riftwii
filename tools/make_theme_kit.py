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
    riftwii-theme-kit/Midnight/theme.ini  themes/Midnight
    riftwii-theme-kit/templates/Default/  43 pictures, default look
    riftwii-theme-kit/templates/Midnight/ the same in Midnight's colours
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
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    number = re.search(r"project\(Riftwii VERSION ([0-9.]+)[ )]", text).group(1)
    suffix = re.search(r'set\(RIFTWII_VERSION_SUFFIX "([^"]*)"\)', text)
    return number + (suffix.group(1) if suffix else "")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    tool, out = str(pathlib.Path(sys.argv[1]).resolve()), pathlib.Path(sys.argv[2])
    with tempfile.TemporaryDirectory() as tmp:
        gen = pathlib.Path(tmp)
        subprocess.run([tool, str(gen), "Default", "Midnight=" + str(ROOT / "themes/Midnight/theme.ini")],
                       check=True)
        readme = (ROOT / "themes/kit/README.txt").read_text(encoding="utf-8").replace("{version}", version())
        files = {
            "README.txt": readme.replace("\n", "\r\n").encode("utf-8"),
            "THEMES.md": (ROOT / "docs/THEMES.md").read_bytes(),
            "MyTheme/theme.ini": (gen / "MyTheme/theme.ini").read_bytes(),
            "Midnight/theme.ini": (ROOT / "themes/Midnight/theme.ini").read_bytes(),
        }
        for png in sorted((gen / "templates").rglob("*.png")):
            files["templates/" + png.relative_to(gen / "templates").as_posix()] = png.read_bytes()
        out.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
            for name, data in files.items():
                z.writestr(TOP + "/" + name, data)
    print(f"{out}: {len(files)} files")


if __name__ == "__main__":
    main()
