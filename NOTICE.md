# Third-party notices

Riftwii is licensed under the GNU General Public License v3.0 or later
(`LICENSE`). It links the components below; their licences are compatible
with GPL-3.0-or-later and their notices are reproduced in the vendored files.

| Component | Location | Version / origin | Licence |
| --- | --- | --- | --- |
| pugixml | `vendor-pugixml/` | 1.15, https://github.com/zeux/pugixml | MIT (`vendor-pugixml/LICENSE.md`) |
| libwiigui | `vendor-libgui/source/libwiigui/`, `video.cpp`, `input.cpp`, `audio.cpp`, `gettext.cpp`, `filebrowser.cpp`, `menu.cpp` (template) | 1.07 snapshot by Tantric, https://github.com/dborth/libwiigui | GPL (version unspecified upstream; used under GPL-3.0-or-later) |
| FreeTypeGX | `vendor-libgui/source/FreeTypeGX.*` | Armin Tamzarian, bundled with libwiigui | GPL-3.0-or-later (header in file) |
| oggplayer | `vendor-libgui/source/oggplayer.*` | Francisco Munoz "Hermes", 2008 | BSD-3-Clause (header in file) |
| PNGU | `vendor-libgui/source/pngu.*` | frontier (frontier-dev.net), modified by Tantric | No licence text in the vendored files; distributed inside the libwiigui package. Confirm or replace before a public release. |
| libwiigui template assets | `vendor-libgui/source/images/`, `fonts/font.ttf`, `sounds/` | Artwork by mvit and music by Peter de Man per the libwiigui README | Part of the libwiigui template; replace with Riftwii-owned assets before a public release. |

Build-time dependencies that are not vendored: devkitPPC/libogc, libfat,
libpng, zlib, FreeType, libogg/libvorbisidec, brotli, bzip2 (devkitPro
portlibs), each under its own licence.

## Design provenance

Riftwii is a clean-room implementation. No Riivolution source code was
consulted or copied. Behavioural references used, all publicly available:

- The Riivolution patch-format documentation:
  https://riivolution.github.io/wiki/Patch_Format/
- Dolphin's independent parser/patcher (GPL-2.0-or-later), read for
  behaviour only, at commit `f6a67aa6e722350db3fc778e4f881cb60090faa1`:
  `Source/Core/DiscIO/RiivolutionParser.cpp`, `RiivolutionPatcher.cpp`
- WiiBrew hardware/format pages (`/dev/di`, `/dev/sdio/slot0`, Wii Disc,
  Apploader, Memory map).
