// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/emulatorbanner.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "riftwii/checkedalloc.hpp"
#include "riftwii/coverart.hpp"
#include "riftwii/emulatorart.hpp"

namespace riftwii {

EmulatorBannerCrop emulator_banner_crop(int width, int height, bool wide) {
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024) return {};
    // Widescreen's menu units are square on the TV, but compressed into
    // frame pixels. Crop for its wider tile before that compression.
    const int tile_width = wide ? 171 : kEmulatorBannerWidth;
    const double scale = std::min(static_cast<double>(width) / tile_width,
                                  static_cast<double>(height) / kEmulatorBannerHeight);
    const double crop_width = tile_width * scale;
    const double crop_height = kEmulatorBannerHeight * scale;
    return {(width - crop_width) / 2, (height - crop_height) / 2, crop_width, crop_height};
}

std::vector<std::uint8_t> make_emulator_banner(const std::uint8_t* rgba, int width, int height, bool wide) {
    const auto crop = emulator_banner_crop(width, height, wide);
    if (!rgba || crop.width <= 0) return {};
    std::vector<std::uint8_t> file;
    if (!try_resize(file, kEmulatorBannerFileSize)) return {};
    const std::uint8_t header[] = {'R', 'W', 'E', '1', 0, 128, 0, 88};
    std::copy(std::begin(header), std::end(header), file.begin());
    const double step_x = crop.width / kEmulatorBannerWidth;
    const double step_y = crop.height / kEmulatorBannerHeight;
    for (int y = 0; y < kEmulatorBannerHeight; ++y) {
        const double y0 = crop.y + y * step_y;
        const double y1 = y0 + step_y;
        for (int x = 0; x < kEmulatorBannerWidth; ++x) {
            const double x0 = crop.x + x * step_x;
            const double x1 = x0 + step_x;
            double channels[3] = {}, area = 0;
            for (int sy = static_cast<int>(y0); sy < height && sy < y1; ++sy) {
                const double vertical = std::min(y1, sy + 1.0) - std::max(y0, static_cast<double>(sy));
                for (int sx = static_cast<int>(x0); sx < width && sx < x1; ++sx) {
                    const double weight = vertical * (std::min(x1, sx + 1.0) - std::max(x0, static_cast<double>(sx)));
                    const auto* pixel = rgba + (static_cast<std::size_t>(sy) * width + sx) * 4;
                    // Transparent art is composed over a dark background,
                    // so it cannot turn the channel back into a white card.
                    for (int c = 0; c < 3; ++c) channels[c] += pixel[c] * pixel[3] / 255.0 * weight;
                    area += weight;
                }
            }
            const unsigned red = static_cast<unsigned>(channels[0] / area + 0.5);
            const unsigned green = static_cast<unsigned>(channels[1] / area + 0.5);
            const unsigned blue = static_cast<unsigned>(channels[2] / area + 0.5);
            const unsigned value = 0x8000 | ((red >> 3) << 10) | ((green >> 3) << 5) | (blue >> 3);
            const std::size_t tile = static_cast<std::size_t>(y / 4) * (kEmulatorBannerWidth / 4) + x / 4;
            const std::size_t offset = 8 + tile * 32 + ((y % 4) * 4 + x % 4) * 2;
            file[offset] = static_cast<std::uint8_t>(value >> 8);
            file[offset + 1] = static_cast<std::uint8_t>(value);
        }
    }
    return file;
}

std::vector<std::uint8_t> emulator_banner_from_cover(const std::uint8_t* cover, bool wide) {
    if (!cover) return {};
    std::vector<std::uint8_t> rgba;
    if (!try_resize(rgba, static_cast<std::size_t>(kCoverWidth) * kCoverHeight * 4)) return {};
    for (int y = 0; y < kCoverHeight; ++y) {
        for (int x = 0; x < kCoverWidth; ++x) {
            const std::size_t tile = static_cast<std::size_t>(y / 4) * (kCoverWidth / 4) + x / 4;
            const std::size_t offset = tile * 32 + ((y % 4) * 4 + x % 4) * 2;
            const unsigned value = (cover[offset] << 8) | cover[offset + 1];
            auto* pixel = rgba.data() + (static_cast<std::size_t>(y) * kCoverWidth + x) * 4;
            if (value & 0x8000) {
                pixel[0] = ((value >> 10) & 31) * 255 / 31;
                pixel[1] = ((value >> 5) & 31) * 255 / 31;
                pixel[2] = (value & 31) * 255 / 31;
            } else {
                pixel[0] = ((value >> 8) & 15) * 17;
                pixel[1] = ((value >> 4) & 15) * 17;
                pixel[2] = (value & 15) * 17;
            }
            pixel[3] = 255;
        }
    }
    return make_emulator_banner(rgba.data(), kCoverWidth, kCoverHeight, wide);
}

bool emulator_banner_header_valid(const std::uint8_t* header) {
    return header && std::memcmp(header, "RWE1\0\x80\0\x58", 8) == 0;
}

std::string emulator_banner_local_path(const std::string& system, const std::string& game) {
    if (!emulator_art_component(system) || !emulator_art_component(game + ".png")) return "";
    return "sd:/riftwii/banners/emulators/" + system + "/" + game + ".png";
}

std::string emulator_banner_key(const std::string& id, bool wide) {
    if (!emulator_cover_id(id)) return "";
    return std::string(wide ? "channel-wide-" : "channel-") + id;
}

std::string emulator_banner_game_id(const std::string& key) {
    if (key.rfind("channel-", 0) != 0) return "";
    const std::string id = key.substr(key.rfind("channel-wide-", 0) == 0 ? 13 : 8);
    return emulator_cover_id(id) ? id : "";
}

bool emulator_banner_id(const std::string& key) {
    return !emulator_banner_game_id(key).empty();
}

bool emulator_banner_wide(const std::string& key) {
    return key.rfind("channel-wide-", 0) == 0 && emulator_banner_id(key);
}

std::string libretro_title_url(const std::string& system, const std::string& name) {
    std::string url = libretro_cover_url(system, name);
    const auto folder = url.find("/Named_Boxarts/");
    if (folder != std::string::npos) url.replace(folder, 15, "/Named_Titles/");
    return url;
}

std::string steamgrid_banner_path(std::uint32_t game) {
    if (!game) return "";
    return "/api/v2/grids/game/" + std::to_string(game) + "?dimensions=460x215,920x430&mimes=image/png";
}

}  // namespace riftwii
