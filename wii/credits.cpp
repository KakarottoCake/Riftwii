// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "credits.hpp"


#include <cstdint>

#include "licence_zst.h"
#include "zstd.h"

namespace riftwii::wii {
namespace {

// The notice and the credits, one paragraph per entry ("" for a gap).
const char* const kCredits[] = {
    "RiftWii " RIFTWII_VERSION,
    "Copyright (C) 2026 the RiftWii contributors",
    "",
    "This program is free software: you can redistribute it and/or modify it under the terms of the GNU "
    "General Public License as published by the Free Software Foundation, either version 3 of the License, "
    "or (at your option) any later version.",
    "",
    "This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even "
    "the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General "
    "Public License, in full below, for more details.",
    "",
    "Source code: https://github.com/KakarottoCake/Riftwii, the tag v" RIFTWII_VERSION " for this version. "
    "Each release's zip also carries LICENSE.txt, NOTICE.md and SOURCE.txt.",
    "",
    "CREDITS",
    "",
    "RiftWii contains or follows the work below. NOTICE.md, in the source and in each release, lists "
    "every piece with its origin and license, and each file that follows someone's work names them in "
    "its header.",
    "",
    "USB Loader GX (github.com/wiidev/usbloadergx, GPL-3.0): the online server patches, the Wiimmfi "
    "patches, the Mario Kart Wii security fix, the return-to-channel patch, the language patch, the video "
    "mode tables, its fixes for particular games (Kirby's Return to Dream Land, New Super Mario Bros. Wii, "
    "Prince of Persia, Resident Evil 4, Excite Truck, the error #002 and 480p fixes), its automatic cIOS "
    "choice, the IOS reload block, the region video fix (WiiPower's VIDTV patch), the region strings fix, the "
    "aspect ratio, rumble and speaker options, the Wiinnertag file RiftWii's RiiTag support reads, how to find "
    "the Wii Menu's fonts (its TrueType and bitmap ones) for the Menu font setting, how to use d2x's USB device on either USB port, the "
    "idea of hooking a game's second "
    "executable and RiftWii's copy of the Gecko code handler. With thanks to its developers and to those whose "
    "work it carries: ToadKing (wiilauncher-nossl), Leseratte and the Wiimmfi team, giantpune, Nuke, "
    "crediar (Kirby's MetaFortress patch), dcx2, container12345, blackb0x and WiiPower.",
    "",
    "Gecko OS by Nuke, brkirch, Link and the Gecko authors: the cheat code handler (GPL-2.0-or-later).",
    "",
    "Brainslug by Alex Chadwick and Florian Bach (MIT): the boot sequence.",
    "",
    "libruntimeiospatch by Joseph Jordan, damysteryman, Christopher Bratusek, DarkMatterCore, megazig "
    "and FIX94: how a reloaded IOS keeps hardware access (followed, not copied).",
    "",
    "wup-028-bslug by Alex Chadwick (MIT): the GameCube controller adapter.",
    "",
    "Nintendont by FIX94 and contributors: the USB HID driver, used with its developers' permission.",
    "",
    "WiiDRC by FIX94 (MIT): the Wii U GamePad.",
    "",
    "libwiigui by Tantric (GPL), FreeTypeGX by Armin Tamzarian (GPL-3.0-or-later), oggplayer by "
    "Francisco Munoz \"Hermes\" (BSD-3-Clause): the menu's toolkit, text and music player.",
    "",
    "WiiLink WFC, wfc-patcher-wii (GPL-2.0-or-later): WiiLink online play. Online communications credit "
    "to WiiLink WFC, wfc.wiilink.ca.",
    "",
    "d2x cIOS: the USB and SD game loading interface.",
    "",
    "devkitPro, libogc and libfat (Michael Wiedenbauer, Dave Murphy, Hector Martin, Sven Peter and "
    "others): the toolchain and the Wii library.",
    "",
    "Dolphin, wiibrew and the Riivolution patch format wiki: how the Wii and the patch format behave, "
    "read as documentation.",
    "",
    "pugixml by Arseny Kapoulkine (MIT), Zstandard by Meta (BSD-3-Clause), BearSSL by Thomas Pornin "
    "(MIT), FreeType (FreeType License), zlib, Tremor and libogg (BSD-3-Clause).",
    "",
    "Menu font: M PLUS Rounded 1c by the M+ Fonts Project (SIL Open Font License 1.1).",
    "",
    "Korean translation: DDinghoya. French translation: MidyGamy.",
    "",
    "Menu music: \"Insect Factory (Wii-style music)\" by Zane Little (CC0).",
    "",
    "Game names and covers from GameTDB (gametdb.com). Cheat codes from the GeckoCodes archive kept by "
    "RiiConnect24 (codes.rc24.xyz).",
    "",
    "RiftWii contains no Riivolution code and no Nintendo keys or assets.",
    "",
    "THE GNU GENERAL PUBLIC LICENSE, VERSION 3",
    "",
};

// Word-wrapped to `width`; a word longer than that is cut.
void Wrap(const std::string& text, std::size_t width, std::vector<std::string>& out) {
    if (text.empty()) {
        out.emplace_back();
        return;
    }
    std::size_t at = 0;
    while (at < text.size()) {
        while (at < text.size() && text[at] == ' ') ++at;
        if (at >= text.size()) break;
        std::size_t end = at + width;
        if (end >= text.size()) {
            out.push_back(text.substr(at));
            break;
        }
        std::size_t cut = text.rfind(' ', end);
        if (cut == std::string::npos || cut <= at) cut = end;
        out.push_back(text.substr(at, cut - at));
        at = cut;
    }
}

// The license's paragraphs, rewrapped: a paragraph starts after a blank
// line or at an indented line (the license indents each paragraph's
// first line, and every line of its title block and sample notice).
void AddLicence(std::size_t width, std::vector<std::string>& out) {
    // LICENSE, zstd-compressed (Makefile.wii).
    const unsigned long long size = ZSTD_getFrameContentSize(licence_zst, licence_zst_size);
    std::string text;
    if (size != ZSTD_CONTENTSIZE_UNKNOWN && size != ZSTD_CONTENTSIZE_ERROR && size > 0 && size < (1u << 20)) {
        text.resize(static_cast<std::size_t>(size));
        if (ZSTD_decompress(&text[0], text.size(), licence_zst, licence_zst_size) != text.size()) text.clear();
    }
    if (text.empty()) {
        out.push_back("The license could not be unpacked. It is at https://www.gnu.org/licenses/gpl-3.0.txt");
        return;
    }
    std::string paragraph;
    const auto flush = [&]() {
        if (!paragraph.empty()) Wrap(paragraph, width, out);
        paragraph.clear();
    };
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        if (line.find_first_not_of(' ') == std::string::npos) {
            flush();
            out.emplace_back();
        } else if (line[0] == ' ') {
            flush();  // an indented line starts a paragraph
            paragraph = line.substr(line.find_first_not_of(' '));
        } else {
            if (!paragraph.empty()) paragraph += ' ';
            paragraph += line.substr(line.find_first_not_of(' '));
        }
    }
    flush();
}

}  // namespace

std::vector<std::string> CreditsLines(std::size_t width) {
    std::vector<std::string> out;
    for (const char* paragraph : kCredits) Wrap(paragraph, width, out);
    AddLicence(width, out);
    // No run of blank rows.
    std::vector<std::string> lines;
    for (const std::string& line : out) {
        if (line.empty() && (lines.empty() || lines.back().empty())) continue;
        lines.push_back(line);
    }
    return lines;
}

}  // namespace riftwii::wii
