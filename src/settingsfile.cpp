// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/settingsfile.hpp"
#include "riftwii/riitag.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "riftwii/gamelang.hpp"

namespace riftwii {
namespace {

constexpr std::size_t kMaxGameFolders = 8;

std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

}  // namespace

void LoaderSettings::parse(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        VideoWidth w;
        Deflicker d;
        if (key == "language") {
            if (value == "auto" || value == "en" || value == "es" || value == "ja" || value == "pt" || value == "it") {
                language = value;
            }
        } else if (key == "video_width") {
            if (parse_video_width(value, w)) video_width = value;
        } else if (key == "deflicker") {
            if (parse_deflicker(value, d)) deflicker = value;
        } else if (key == "borders") {
            if (value == "keep" || value == "remove" || value == "remove_all") borders = value;
        } else if (key == "video_mode") {
            VideoMode m;
            if (parse_video_mode(value, m)) video_mode = value;
        } else if (key == "game_language") {
            int code;
            if (parse_game_language(value, code)) game_language = value;
        } else if (key == "wfc_server") {
            WfcServer server;
            if (parse_wfc_server(value, server)) wfc_server = value;
        } else if (key == "riitag_key") {
            if (value.empty() || valid_riitag_key(value)) riitag_key = value;
        } else if (key == "wfc_domain") {
            if (value.empty() || valid_wfc_domain(value)) wfc_domain = value;
        } else if (key == "game_cios") {
            int slot;
            if (parse_cios_choice(value, slot)) game_cios = value;
        } else if (key == "home_tiles") {
            if (value == "covers" || value == "names" || value == "shelf" || value == "channels") home_tiles = value;
        } else if (key == "menu_widescreen") {
            if (value == "auto" || value == "on" || value == "off") menu_widescreen = value;
        } else if (key == "screen_size") {
            int n = 0;
            bool digits = !value.empty() && value.size() <= 3;
            for (char c : value) {
                if (c < '0' || c > '9') digits = false;
                else n = n * 10 + (c - '0');
            }
            if (digits && n >= 80 && n <= 100) screen_size = n;
        } else if (key == "return_to") {
            if (value == "riftwii" || value == "menu") return_to = value;
        } else if (key == "menu_music") {
            if (value == "on" || value == "off") menu_music = value;
        } else if (key == "home_source") {
            if (value == "all" || value == "sd" || value == "usb") home_source = value;
        } else if (key == "home_sort") {
            if (value == "az" || value == "recent" || value == "most") home_sort = value;
        } else if (key == "home_disc") {
            if (value == "on" || value == "off") home_disc = value;
        } else if (key == "theme") {
            // A folder in sd:/riftwii/themes: one name, no path.
            if (!value.empty() && value.size() <= 64 && value.find_first_of("/\\:") == std::string::npos &&
                value != "." && value != "..")
                theme = value;
        } else if (key == "menu_font") {
            if (value == "riftwii" || value == "wii") menu_font = value;
        } else if (key == "screenshots") {
            if (value == "on" || value == "off" || value == "demo") screenshots = value;
        } else if (key == "menu_sounds") {
            if (value == "normal" || value == "quiet" || value == "off") menu_sounds = value;
        } else if (key == "online") {
            online = value != "off";
        } else if (key == "favorites") {
            favorites.clear();
            std::size_t at = 0;
            while (at <= value.size()) {
                const std::size_t comma = std::min(value.find(',', at), value.size());
                const std::string id = trim(value.substr(at, comma - at));
                // Game IDs are letters and digits; anything else is noise.
                bool plain = !id.empty() && id.size() <= 6;
                for (char c : id) plain = plain && ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
                if (plain) favorites.insert(id);
                at = comma + 1;
            }
        } else if (key == "game_folders") {
            game_folders.clear();
            std::size_t at = 0;
            while (at <= value.size() && game_folders.size() < kMaxGameFolders) {
                const std::size_t semi = std::min(value.find(';', at), value.size());
                std::string folder = trim(value.substr(at, semi - at));
                std::replace(folder.begin(), folder.end(), '\\', '/');
                // A path, not a way out of the drive; short enough to log.
                if (!folder.empty() && folder.size() <= 200 && folder.find("..") == std::string::npos) {
                    game_folders.push_back(folder);
                }
                at = semi + 1;
            }
        } else if (key == "update_channel") {
            if (value == "auto" || value == "stable" || value == "beta") update_channel = value;
        } else if (key == "gc_rumble") {
            if (value == "on" || value == "off") gc_rumble = value;
        } else if (key == "gc_adapter") {
            if (value == "auto" || value == "off" || value == "on" || value == "demo") gc_adapter = value;
        } else {
            other[key] = value;
        }
    }
}

std::string LoaderSettings::serialize() const {
    std::string s = "# RiftWii settings\n";
    s += "language = " + language + "\n";
    s += "video_width = " + video_width + "\n";
    s += "deflicker = " + deflicker + "\n";
    s += "borders = " + borders + "\n";
    s += "video_mode = " + video_mode + "\n";
    s += "game_language = " + game_language + "\n";
    s += "game_cios = " + game_cios + "\n";
    s += "wfc_server = " + wfc_server + "\n";
    s += "wfc_domain = " + wfc_domain + "\n";
    s += "home_tiles = " + home_tiles + "\n";
    s += "menu_widescreen = " + menu_widescreen + "\n";
    s += "screen_size = " + std::to_string(screen_size) + "\n";
    s += "menu_sounds = " + menu_sounds + "\n";
    s += "menu_music = " + menu_music + "\n";
    s += "home_source = " + home_source + "\n";
    s += "home_sort = " + home_sort + "\n";
    s += "home_disc = " + home_disc + "\n";
    s += "theme = " + theme + "\n";
    s += "menu_font = " + menu_font + "\n";
    s += "return_to = " + return_to + "\n";
    s += "screenshots = " + screenshots + "\n";
    s += std::string("online = ") + (online ? "on" : "off") + "\n";
    if (!riitag_key.empty()) s += "riitag_key = " + riitag_key + "\n";
    s += "update_channel = " + update_channel + "\n";
    s += "gc_rumble = " + gc_rumble + "\n";
    s += "gc_adapter = " + gc_adapter + "\n";
    if (!favorites.empty()) {
        std::string list;
        for (const std::string& id : favorites) list += (list.empty() ? "" : ",") + id;
        s += "favorites = " + list + "\n";
    }
    if (!game_folders.empty()) {
        std::string list;
        for (const std::string& f : game_folders) list += (list.empty() ? "" : "; ") + f;
        s += "game_folders = " + list + "\n";
    }
    for (const auto& kv : other) s += kv.first + " = " + kv.second + "\n";
    return s;
}

std::vector<std::string> game_folders_on(const LoaderSettings& settings, const std::string& device) {
    std::vector<std::string> out;
    for (std::string f : settings.game_folders) {
        const std::size_t colon = f.find(':');
        if (colon != std::string::npos) {
            std::string d = f.substr(0, colon);
            for (char& c : d) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (d != device) continue;
            f.erase(0, colon + 1);
        }
        while (!f.empty() && f.back() == '/') f.pop_back();
        if (f.empty()) continue;
        if (f[0] != '/') f.insert(f.begin(), '/');
        std::string lower = f;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == "/wbfs" || lower == "/games") continue;
        bool seen = false;
        for (const std::string& o : out) seen = seen || o == f;
        if (!seen) out.push_back(f);
    }
    return out;
}

VideoSettings effective_video(const GameSettings& game, const LoaderSettings& global) {
    VideoSettings v;
    const std::string& width = game.video_width == "global" ? global.video_width : game.video_width;
    const std::string& filter = game.deflicker == "global" ? global.deflicker : game.deflicker;
    const std::string& borders = game.borders == "global" ? global.borders : game.borders;
    if (!parse_video_width(width, v.width)) v.width = VideoWidth::Game;
    if (!parse_deflicker(filter, v.deflicker)) v.deflicker = Deflicker::Game;
    v.remove_borders = borders == "remove" || borders == "remove_all";
    v.remove_top_bottom = borders == "remove_all";
    const std::string& mode = game.video_mode == "global" ? global.video_mode : game.video_mode;
    if (!parse_video_mode(mode, v.mode)) v.mode = VideoMode::Game;
    return v;
}

int effective_game_language(const GameSettings& game, const LoaderSettings& global) {
    int code = -1;
    if (game.language == "global" || !parse_game_language(game.language, code)) {
        if (!parse_game_language(global.game_language, code)) code = -1;
    }
    return code;
}

int effective_game_cios(const GameSettings& game, const LoaderSettings& global) {
    int slot = 0;
    if (game.cios == "global" || !parse_cios_choice(game.cios, slot)) {
        if (!parse_cios_choice(global.game_cios, slot)) slot = 0;
    }
    return slot;
}

WfcServer effective_wfc_server(const GameSettings& game, const LoaderSettings& global) {
    WfcServer server = WfcServer::Off;
    if (game.server == "global" || !parse_wfc_server(game.server, server)) {
        if (!parse_wfc_server(global.wfc_server, server)) server = WfcServer::Off;
    }
    if (server == WfcServer::Custom && !valid_wfc_domain(global.wfc_domain)) server = WfcServer::Off;
    return server;
}

bool parse_cios_choice(const std::string& s, int& slot) {
    if (s == "auto") {
        slot = 0;
        return true;
    }
    if (s.size() != 3) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    const int n = (s[0] - '0') * 100 + (s[1] - '0') * 10 + (s[2] - '0');
    if (n < kFirstGameCios || n > kLastGameCios) return false;
    slot = n;
    return true;
}

}  // namespace riftwii
