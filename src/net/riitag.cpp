// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2011 Dimok
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/riitag.hpp"

#include <pugixml.hpp>

#include <cctype>

namespace riftwii {
namespace {

bool http_url(const std::string& url) {
    return url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0;
}

void replace_all(std::string& s, const std::string& from, const std::string& to) {
    for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
        s.replace(at, from.size(), to);
}

std::string percent_encode(const std::string& s) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 15];
        }
    }
    return out;
}

}  // namespace

std::vector<RiiTagEntry> parse_wiinnertag(const std::string& xml) {
    std::vector<RiiTagEntry> out;
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.data(), xml.size())) return out;
    // GX reads <Tag> elements at the top level, one after another.
    for (pugi::xml_node node = doc.child("Tag"); node; node = node.next_sibling("Tag")) {
        RiiTagEntry e{node.attribute("URL").value(), node.attribute("Key").value()};
        if (e.url.empty() || e.key.empty() || !http_url(e.url) || e.url.size() > 512 || e.key.size() > 128) continue;
        out.push_back(e);
    }
    return out;
}

bool valid_riitag_key(const std::string& key) {
    if (key.empty() || key.size() > 128) return false;
    for (unsigned char c : key)
        if (c <= ' ' || c >= 0x7F) return false;
    return true;
}

std::string riitag_url(const RiiTagEntry& entry, const std::string& game_id) {
    if (game_id.size() != 4 && game_id.size() != 6) return "";
    for (unsigned char c : game_id)
        if (!std::isalnum(c)) return "";
    std::string url = entry.url;
    replace_all(url, "{ID6}", game_id);
    replace_all(url, "{KEY}", percent_encode(entry.key));
    return url;
}

}  // namespace riftwii
