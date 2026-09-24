// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/settingsfile.hpp"

#include <sstream>

namespace riftwii {
namespace {

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
            if (value == "keep" || value == "remove") borders = value;
        } else if (key == "online") {
            online = value != "off";
        } else if (key == "gc_adapter") {
            if (value == "off" || value == "on" || value == "demo") gc_adapter = value;
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
    s += std::string("online = ") + (online ? "on" : "off") + "\n";
    s += "gc_adapter = " + gc_adapter + "\n";
    for (const auto& kv : other) s += kv.first + " = " + kv.second + "\n";
    return s;
}

VideoSettings effective_video(const GameSettings& game, const LoaderSettings& global) {
    VideoSettings v;
    const std::string& width = game.video_width == "global" ? global.video_width : game.video_width;
    const std::string& filter = game.deflicker == "global" ? global.deflicker : game.deflicker;
    const std::string& borders = game.borders == "global" ? global.borders : game.borders;
    if (!parse_video_width(width, v.width)) v.width = VideoWidth::Game;
    if (!parse_deflicker(filter, v.deflicker)) v.deflicker = Deflicker::Game;
    v.remove_borders = borders == "remove";
    return v;
}

}  // namespace riftwii
