// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/launchargs.hpp"

#include <cstdlib>
#include <utility>

#include "riftwii/gamelang.hpp"
#include "riftwii/settingsfile.hpp"
#include "riftwii/videopatch.hpp"
#include "riftwii/wfcpatch.hpp"

namespace riftwii {
namespace {

// Six characters, or the first four (a loader that knows only those).
bool game_id_ok(const std::string& id) {
    if (id.size() != 6 && id.size() != 4) return false;
    for (char c : id) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

// A 64-bit title ID as 16 hex digits, with or without a dash in the middle.
bool parse_title(std::string s, std::uint64_t& out) {
    if (s.size() == 17 && s[8] == '-') s.erase(8, 1);
    if (s.size() != 16) return false;
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    out = std::strtoull(s.c_str(), nullptr, 16);
    return out != 0;
}

// A pack choices file's name: letters, digits and . _ + - only, ending in
// ".txt", not starting with a dot. It is joined to sd:/riftwii/choices.
bool choices_name_ok(const std::string& name) {
    if (name.size() < 5 || name.size() > 64 || name[0] == '.') return false;
    if (name.compare(name.size() - 4, 4, ".txt") != 0) return false;
    for (char c : name) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
                        c == '_' || c == '+' || c == '-';
        if (!ok) return false;
    }
    return name.find("..") == std::string::npos;
}

// A 32-bit number in decimal, as the blob writes it.
bool parse_decimal32(const std::string& s, std::uint32_t& out) {
    if (s.empty() || s.size() > 10) return false;
    std::uint64_t value = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (value > 0xFFFFFFFFull) return false;
    out = static_cast<std::uint32_t>(value);
    return true;
}

}  // namespace

bool launch_path_stays_put(const std::string& value) {
    std::size_t start = 0;
    while (start <= value.size()) {
        std::size_t end = value.find_first_of("/\\", start);
        if (end == std::string::npos) end = value.size();
        if (value.compare(start, end - start, "..") == 0) return false;
        start = end + 1;
    }
    return true;
}

bool launch_lines_from_text(const std::string& text, std::vector<std::string>& args) {
    args.clear();
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        // A sector's NULs after the text.
        if (const std::size_t nul = line.find('\0'); nul != std::string::npos) line.resize(nul);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        if (line.size() > kMaxLaunchLineBytes || args.size() >= kMaxLaunchLines) {
            args.clear();
            return false;
        }
        args.push_back(line);
    }
    if (!is_headless_launch(args)) {
        args.clear();
        return false;
    }
    return true;
}

bool launch_request_sector(const std::vector<std::string>& args, bool armed, std::size_t sector_bytes,
                           std::vector<std::uint8_t>& sector, std::string& why) {
    std::string text;
    for (std::size_t i = 0; i < args.size(); ++i) text += (i == 0 && !armed ? std::string("--idle--") : args[i]) + "\n";
    if (text.size() >= sector_bytes) {
        why = "the launch is " + std::to_string(text.size()) + " bytes, and a sector holds " +
              std::to_string(sector_bytes - 1);
        return false;
    }
    sector.assign(sector_bytes, 0);
    sector.insert(sector.begin(), text.begin(), text.end());
    sector.resize(sector_bytes);
    return true;
}

bool is_headless_launch(const std::vector<std::string>& args) {
    return !args.empty() && args[0] == "--launch";
}

bool parse_headless_launch(const std::vector<std::string>& args, HeadlessLaunch& out, std::string& error) {
    out = HeadlessLaunch{};
    if (!is_headless_launch(args)) {
        error = "the first argument is not --launch";
        return false;
    }
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const std::size_t eq = arg.find('=');
        if (eq == std::string::npos || eq == 0) {
            error = "argument '" + arg + "' is not key=value";
            return false;
        }
        const std::string key = arg.substr(0, eq);
        const std::string value = arg.substr(eq + 1);
        if (value.size() > kMaxLaunchValueBytes) {
            error = "the value of " + key + " is longer than " + std::to_string(kMaxLaunchValueBytes) + " bytes";
            return false;
        }
        // Names that climb out of their folder are not a launch's to give.
        if ((key == "path" || key == "xml" || key == "code_build" || key == "gct") && !launch_path_stays_put(value)) {
            error = "'" + value + "' climbs out of its folder (\"..\")";
            return false;
        }
        VideoMode mode;
        VideoWidth width;
        Deflicker deflicker;
        WfcServer server;
        int number = 0;
        bool ok = true;
        if (key == "game") {
            ok = game_id_ok(value);
            out.game = value;
        } else if (key == "from") {
            ok = value == "usb" || value == "sd" || value == "disc";
            out.from = value;
        } else if (key == "path") {
            const bool usb = value.compare(0, 5, "usb:/") == 0;
            ok = usb || value.compare(0, 4, "sd:/") == 0;
            out.path = value;
            if (ok && out.from.empty()) out.from = usb ? "usb" : "sd";
        } else if (key == "xml") {
            out.packs_given = true;
            if (value == "all") {
                out.all_packs = true;
            } else if (value != "none") {
                // A path, or a file name in a riivolution folder.
                ok = out.xmls.size() < kMaxLaunchPacks &&
                     (value.compare(0, 4, "sd:/") == 0 || value.compare(0, 5, "usb:/") == 0 ||
                      (!value.empty() && value.find_first_of(":/\\") == std::string::npos));
                out.xmls.push_back(value);
            }
        } else if (key == "code_build") {
            // Code builds are read from the SD card: a path there, a
            // folder name at its top, or a path from the top without the
            // drive ("pm.raw/Project+/RSBE01.gct" inside an SD image).
            out.packs_given = true;
            ok = out.code_builds.size() < kMaxLaunchCodeBuilds &&
                 (value.compare(0, 4, "sd:/") == 0 ||
                  (!value.empty() && value.find_first_of(":\\") == std::string::npos && value[0] != '/'));
            out.code_builds.push_back(value);
        } else if (key == "video_mode") {
            ok = parse_video_mode(value, mode);
            out.settings.video_mode = value;
        } else if (key == "video_width") {
            ok = parse_video_width(value, width);
            out.settings.video_width = value;
        } else if (key == "deflicker") {
            ok = parse_deflicker(value, deflicker);
            out.settings.deflicker = value;
        } else if (key == "borders") {
            ok = value == "keep" || value == "remove" || value == "remove_all";
            out.settings.borders = value;
        } else if (key == "language") {
            ok = parse_game_language(value, number);
            out.settings.language = value;
        } else if (key == "cios") {
            ok = parse_cios_choice(value, number);
            out.settings.cios = value;
        } else if (key == "server") {
            ok = parse_wfc_server(value, server);
            out.settings.server = value;
        } else if (key == "wfc_domain") {
            ok = valid_wfc_domain(value);
            out.wfc_domain = value;
        } else if (key == "gct") {
            ok = value == "none" || value.compare(0, 4, "sd:/") == 0;  // codes are read from the SD card
            out.gct = value;
        } else if (key == "return_to") {
            out.return_to_menu = value == "menu";
            ok = out.return_to_menu || parse_title(value, out.return_to);
        } else if (key == "gc_dol") {
            out.gc_exec = true;
            ok = parse_decimal32(value, out.gc_dol);
        } else if (key == "gc_restart") {
            out.gc_exec = true;
            ok = parse_decimal32(value, out.gc_restart);
        } else if (key == "gc_arg") {
            // As many as the SDK's argument block holds (8 KB, half of it
            // the list): a game's own are a handful of short words.
            out.gc_exec = true;
            ok = out.gc_args.size() < 32 && value.size() < 256;
            out.gc_args.push_back(value);
        } else if (key == "choices") {
            ok = choices_name_ok(value);
            out.choices = value;
        } else if (key == "reset") {
            ok = value == "1";
            out.reset = ok;
        } else {
            error = "unknown argument '" + key + "'";
            return false;
        }
        if (!ok) {
            error = "'" + value + "' is not a value for " + key;
            return false;
        }
    }
    if (!out.path.empty() && out.from != (out.path[0] == 'u' ? "usb" : "sd")) {
        error = "path= is not on the drive from= names";
        return false;
    }
    if (out.game.empty() && out.path.empty() && out.from != "disc") {
        error = "no game= argument";
        return false;
    }
    return true;
}

std::vector<std::string> headless_launch_arguments(const HeadlessLaunch& h) {
    std::vector<std::string> args = {"--launch"};
    if (!h.game.empty()) args.push_back("game=" + h.game);
    if (!h.from.empty()) args.push_back("from=" + h.from);
    if (!h.path.empty()) args.push_back("path=" + h.path);
    if (!h.choices.empty()) args.push_back("choices=" + h.choices);
    if (h.packs_given) {
        if (h.all_packs) args.push_back("xml=all");
        for (const std::string& xml : h.xmls) args.push_back("xml=" + xml);
        // Code builds are a pack choice of their own: any named turns the
        // other packs off, so a launch with none of either says so.
        if (!h.all_packs && h.xmls.empty() && h.code_builds.empty()) args.push_back("xml=none");
        for (const std::string& build : h.code_builds) args.push_back("code_build=" + build);
    }
    const GameSettings& g = h.settings;
    const std::pair<const char*, const std::string*> settings[] = {
        {"video_mode", &g.video_mode}, {"video_width", &g.video_width}, {"deflicker", &g.deflicker},
        {"borders", &g.borders},       {"language", &g.language},       {"cios", &g.cios},
        {"server", &g.server},
    };
    for (const auto& [key, value] : settings) {
        if (*value != "global") args.push_back(std::string(key) + "=" + *value);
    }
    if (!h.wfc_domain.empty()) args.push_back("wfc_domain=" + h.wfc_domain);
    if (!h.gct.empty()) args.push_back("gct=" + h.gct);
    if (h.return_to_menu) {
        args.push_back("return_to=menu");
    } else if (h.return_to != 0) {
        static const char kDigits[] = "0123456789abcdef";
        std::string title;
        for (int shift = 60; shift >= 0; shift -= 4) title += kDigits[(h.return_to >> shift) & 15];
        args.push_back("return_to=" + title);
    }
    if (h.reset) args.push_back("reset=1");
    return args;
}

namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

// A NUL-terminated field of at most `max` bytes (unterminated: all of it).
std::string field(const std::uint8_t* p, std::size_t max) {
    std::size_t n = 0;
    while (n < max && p[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}

}  // namespace

bool friiv_launch_args(const std::uint8_t* data, std::size_t size, std::vector<std::string>& args) {
    // FRIIV_CFG, version 1, big-endian: magic 'FRIV', version, flags, the
    // game ID's first four bytes, GamePath[255], ModXml[64], then padding
    // to 384 bytes.
    constexpr std::size_t kSize = 384, kPath = 16, kPathMax = 255, kXml = kPath + kPathMax, kXmlMax = 64;
    constexpr std::uint32_t kMagic = 0x46524956, kAutoBoot = 1, kGameUsb = 2, kNoPatches = 4;
    args.clear();
    if (data == nullptr || size < kSize || be32(data) != kMagic || be32(data + 4) < 1) return false;
    const std::uint32_t flags = be32(data + 8);
    if (!(flags & kAutoBoot)) return false;  // only a boot request counts
    args.push_back("--launch");
    std::string path = field(data + kPath, kPathMax);
    if (path.empty()) {
        args.push_back("from=disc");
    } else {
        // A path may name its device already ("usb:/wbfs/..."): it is
        // kept, not given a second one ("sd:/usb:/...").
        std::string device = (flags & kGameUsb) ? "usb:" : "sd:";
        std::string lower;
        for (char c : path.substr(0, 4)) lower += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        if (lower.compare(0, 4, "usb:") == 0) {
            device = "usb:";
            path.erase(0, 4);
        } else if (lower.compare(0, 3, "sd:") == 0) {
            device = "sd:";
            path.erase(0, 3);
        }
        if (path.empty() || path[0] != '/') path.insert(0, "/");
        args.push_back("path=" + device + path);
    }
    const std::string id = field(data + 12, 4);
    if (id.size() == 4) args.push_back("game=" + id);
    const std::string xml = field(data + kXml, kXmlMax);
    if (flags & kNoPatches) args.push_back("xml=none");
    else if (!xml.empty()) args.push_back("xml=" + xml);
    else args.push_back("xml=all");  // every pack for the game, as Friivolution loads them
    return true;
}

}  // namespace riftwii
