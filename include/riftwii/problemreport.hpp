// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <vector>

// A problem report: one text holding what it takes to see what went wrong
// on someone's Wii (the logs, the crash, the settings, the game's choices
// and its packs' XMLs, the console and its controllers), sent to paste.rs
// so the player only has to pass a link on (wii/reportsend.hpp).
namespace riftwii {

// paste.rs keeps at most 384 KiB; the report stays under it.
constexpr std::size_t kReportLimit = 376u * 1024u;

struct ReportPart {
    std::string name;  // "boot.log", "sd:/riivolution/RetroRewind6.xml"
    std::string text;
    bool found = true;  // false: listed as not there
};

// `summary` first, then a table of the parts and each part under its own
// header line. When it all comes to more than `limit` bytes, the biggest
// parts lose the middle (on line boundaries, the start and more of the
// end kept, since a log's last lines say where it stopped) until it fits.
std::string assemble_report(const std::string& summary, const std::vector<ReportPart>& parts, std::size_t limit);

// The game boot.log's first line names ("RiftWii 2.4.3-beta: launch SB4E01
// with packages"), or "" for a boot without packs or a log it cannot read.
std::string launched_game_id(const std::string& boot_log);

// The packs a choices file (sd:/riftwii/choices/<ID>.txt) has turned on,
// as it names them ("RetroRewind6.xml", "mod.xml @ 192.168.1.20:1137").
std::vector<std::string> enabled_pack_files(const std::string& choices);

// paste.rs's answer to the upload: 201 and the link, or 206 and the link
// to a paste cut short. False with `error` for anything else.
bool paste_link(int status, const std::string& answer, std::string& link, bool& partial, std::string& error);

}  // namespace riftwii
