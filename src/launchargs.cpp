// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/launchargs.hpp"

#include <cstdlib>

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

}  // namespace

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
                ok = value.compare(0, 4, "sd:/") == 0 || value.compare(0, 5, "usb:/") == 0 ||
                     (!value.empty() && value.find_first_of(":/\\") == std::string::npos);
                out.xmls.push_back(value);
            }
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
        if (path[0] != '/') path.insert(0, "/");
        args.push_back(std::string("path=") + ((flags & kGameUsb) ? "usb:" : "sd:") + path);
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
