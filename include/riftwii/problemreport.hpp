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
// Every part and the summary go through printable_utf8 first.
std::string assemble_report(const std::string& summary, const std::vector<ReportPart>& parts, std::size_t limit);

// `text` as valid UTF-8 text: each byte that does not start or continue a
// well-formed sequence (overlong forms, surrogates and code points past
// U+10FFFF included), and each control character but tab, CR and LF,
// becomes '?', one byte for one. The report goes up as UTF-8 text, and it
// carries raw files (logs with disc titles in Shift-JIS or Latin-1, bytes
// from a damaged card); paste.rs answered one such report with a 500.
std::string printable_utf8(const std::string& text);

// The game of the launch boot.log holds: the one its first line names
// ("RiftWii 2.4.3-beta: launch SB4E01 with packages"), else, for a boot
// without packs, the one its disc line names ("Disc: RSBE01  "Super Smash
// Bros. Brawl"  disc 0 version 2"). "" when neither is there.
std::string launched_game_id(const std::string& boot_log);

// settings.txt as a report shows it: a secret's value (riitag_key, the
// player's RiiTag key) replaced by "(hidden)", as a report is posted
// where anyone can read it.
std::string hide_settings_secrets(const std::string& settings);

// Every "key=" value in a URL's query ("?key=..." or "&key=...", any case)
// replaced by "(hidden)": a RiiTag key logged with an address. Reports are
// posted publicly, so assemble_report applies this to everything.
std::string hide_url_keys(const std::string& text);

// The packs a choices file (sd:/riftwii/choices/<ID>.txt) has turned on,
// as it names them ("RetroRewind6.xml", "mod.xml @ 192.168.1.20:1137").
std::vector<std::string> enabled_pack_files(const std::string& choices);

// paste.rs's answer to the upload: 201 and the link, or 206 and the link
// to a paste cut short. False with `error` for anything else.
// `site` names the paste site in the error (paste.rs, or dpaste.com, the
// fallback; both answer 201 with the link as the whole body).
bool paste_link(int status, const std::string& answer, std::string& link, bool& partial, std::string& error,
                const std::string& site = "paste.rs");

}  // namespace riftwii
