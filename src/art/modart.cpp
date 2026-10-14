// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/modart.hpp"

#include <algorithm>

#include "riftwii/coverart.hpp"

namespace riftwii {
namespace {

// "sd:/a/b.xml" -> "sd:/a/b" (the part before the last dot of the name).
std::string stem(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    const std::size_t dot = path.rfind('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return path;
    return path.substr(0, dot);
}

std::string dir_of(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

}  // namespace

std::vector<std::string> mod_art_paths(const std::string& pack_path, const std::string& gct_path,
                                       const std::string& image_location) {
    std::vector<std::string> out;
    if (!image_location.empty()) {
        out.push_back(stem(image_location) + ".png");
        return out;
    }
    if (!gct_path.empty()) {
        if (gct_path.compare(0, 4, "sd:/") != 0) return out;
        // The code file's folder, then each folder above it to the top of the card.
        for (std::string dir = dir_of(gct_path); dir.size() > 4; dir = dir_of(dir)) out.push_back(dir + "/cover.png");
        return out;
    }
    if (!pack_path.empty()) out.push_back(stem(pack_path) + ".png");
    return out;
}

std::vector<std::uint8_t> fit_art(const std::uint8_t* rgba, int w, int h, int bw, int bh) {
    std::vector<std::uint8_t> box(static_cast<std::size_t>(bw) * bh * 4, 0);
    if (!rgba || w <= 0 || h <= 0 || bw <= 0 || bh <= 0) return box;
    // The larger side that fits: w/h against bw/bh.
    int dw = bw, dh = static_cast<int>(static_cast<long long>(h) * bw / w);
    if (dh > bh) {
        dh = bh;
        dw = std::max(1, static_cast<int>(static_cast<long long>(w) * bh / h));
    }
    dh = std::max(1, dh);
    const std::vector<std::uint8_t> scaled = scale_rgba(rgba, w, h, dw, dh);
    const int x0 = (bw - dw) / 2, y0 = (bh - dh) / 2;
    for (int y = 0; y < dh; ++y)
        std::copy(scaled.begin() + static_cast<std::ptrdiff_t>(y) * dw * 4,
                  scaled.begin() + static_cast<std::ptrdiff_t>(y + 1) * dw * 4,
                  box.begin() + (static_cast<std::ptrdiff_t>(y0 + y) * bw + x0) * 4);
    return box;
}

}  // namespace riftwii
