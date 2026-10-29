// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2011 Dimok
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/riitag.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>

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

bool valid_riitag_id(const std::string& id) {
    if (id.size() < 15 || id.size() > 20) return false;
    for (unsigned char c : id)
        if (!std::isdigit(c)) return false;
    return true;
}

std::string riitag_image_url(const std::string& id) {
    if (!valid_riitag_id(id)) return "";
    return "https://riitag.t0g3pii.de/" + id + "/tag.max.png";
}

void fit_rgba_into(const std::uint8_t* rgba, int w, int h, std::uint8_t* out, int bw, int bh) {
    if (!out || bw <= 0 || bh <= 0) return;
    std::memset(out, 0, static_cast<std::size_t>(bw) * bh * 4);
    if (!rgba || w <= 0 || h <= 0) return;
    // The largest size that keeps the picture's shape: w/h against bw/bh.
    int dw = bw;
    int dh = static_cast<int>(static_cast<long long>(h) * bw / w);
    if (dh > bh) {
        dh = bh;
        dw = std::max(1, static_cast<int>(static_cast<long long>(w) * bh / h));
    }
    dh = std::max(1, dh);
    const int x0 = (bw - dw) / 2;
    const int y0 = (bh - dh) / 2;
    for (int y = 0; y < dh; ++y) {
        // The source rows this row covers: at least one, so a picture
        // made larger repeats its pixels instead of leaving gaps.
        const int sy0 = static_cast<int>(static_cast<long long>(y) * h / dh);
        const int sy1 = std::max(sy0 + 1, static_cast<int>(static_cast<long long>(y + 1) * h / dh));
        for (int x = 0; x < dw; ++x) {
            const int sx0 = static_cast<int>(static_cast<long long>(x) * w / dw);
            const int sx1 = std::max(sx0 + 1, static_cast<int>(static_cast<long long>(x + 1) * w / dw));
            // Colours weighted by their alpha, so a transparent pixel's
            // colour (often black) does not darken the edges it touches.
            std::uint64_t r = 0, g = 0, b = 0, a = 0;
            unsigned count = 0;
            for (int sy = sy0; sy < sy1; ++sy) {
                const std::uint8_t* px = rgba + (static_cast<std::size_t>(sy) * w + sx0) * 4;
                for (int sx = sx0; sx < sx1; ++sx, px += 4) {
                    r += static_cast<std::uint64_t>(px[0]) * px[3];
                    g += static_cast<std::uint64_t>(px[1]) * px[3];
                    b += static_cast<std::uint64_t>(px[2]) * px[3];
                    a += px[3];
                    ++count;
                }
            }
            std::uint8_t* dst = out + (static_cast<std::size_t>(y0 + y) * bw + x0 + x) * 4;
            if (a == 0) continue;  // left transparent
            dst[0] = static_cast<std::uint8_t>(r / a);
            dst[1] = static_cast<std::uint8_t>(g / a);
            dst[2] = static_cast<std::uint8_t>(b / a);
            dst[3] = static_cast<std::uint8_t>(a / count);
        }
    }
}

}  // namespace riftwii
