// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/shotfile.hpp"

#include <cstdio>
#include <vector>

namespace riftwii {
namespace {

std::uint32_t Be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

std::uint8_t Clamp(int v) { return static_cast<std::uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }

}  // namespace

bool parse_shot_header(const std::uint8_t* h, std::size_t file_size, ShotInfo& out, std::string& error) {
    out = ShotInfo{};
    if (file_size < kShotHeaderBytes || Be32(h) != kShotMagic) {
        error = "not a RiftWii screenshot";
        return false;
    }
    if (Be32(h + 4) != kShotVersion) {
        error = "screenshot version " + std::to_string(Be32(h + 4));
        return false;
    }
    out.width = Be32(h + 8);
    out.lines = Be32(h + 12);
    out.flags = Be32(h + 16);
    out.index = Be32(h + 28);
    if (out.width == 0 || out.width % 2 || out.width > kShotMaxWidth || out.lines == 0 || out.lines > kShotMaxLines) {
        error = "screenshot size " + std::to_string(out.width) + "x" + std::to_string(out.lines);
        return false;
    }
    if (file_size < kShotHeaderBytes + std::size_t(out.width) * 2 * out.lines) {
        error = "screenshot cut short";
        return false;
    }
    for (int i = 0; i < 6; ++i) {
        const char c = static_cast<char>(h[20 + i]);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) out.game_id += c;
        else break;
    }
    return true;
}

// BT.601 studio range to full-range RGB, in 8.8 fixed point.
void yuyv_row_to_rgb(const std::uint8_t* p, std::uint32_t width, std::uint8_t* rgb) {
    for (std::uint32_t x = 0; x < width; x += 2, p += 4) {
        const int u = p[1] - 128, v = p[3] - 128;
        const int r = 409 * v + 128, g = -100 * u - 208 * v + 128, b = 516 * u + 128;
        for (int k = 0; k < 2 && x + k < width; ++k) {
            const int y = 298 * (p[k * 2] - 16);
            *rgb++ = Clamp((y + r) >> 8);
            *rgb++ = Clamp((y + g) >> 8);
            *rgb++ = Clamp((y + b) >> 8);
        }
    }
}

bool write_yuyv_png(const std::uint8_t* yuyv, std::uint32_t width, std::uint32_t lines, std::size_t stride,
                    bool double_lines, PngWriter::Sink sink, void* user, void* work) {
    std::vector<std::uint8_t> row(std::size_t(width) * 3);
    PngWriter png(width, double_lines ? lines * 2 : lines, sink, user, work);
    for (std::uint32_t y = 0; y < lines; ++y) {
        yuyv_row_to_rgb(yuyv + y * stride, width, row.data());
        if (!png.add_row(row.data())) return false;
        if (double_lines && !png.add_row(row.data())) return false;
    }
    return png.finish();
}

std::string shot_file_name(const std::string& prefix, unsigned number) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "-%04u.png", number);
    return prefix + buf;
}

}  // namespace riftwii
