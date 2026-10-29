// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace riftwii {

// Channels uses these physical pixels in both TV modes. Widescreen has
// 171 menu units across, mapped to 128 pixels by the 0.75 display scale.
constexpr int kEmulatorBannerWidth = 128;
constexpr int kEmulatorBannerHeight = 88;
constexpr std::size_t kEmulatorBannerPixelBytes = 128 * 88 * 2;
constexpr std::size_t kEmulatorBannerFileSize = 8 + kEmulatorBannerPixelBytes;

struct EmulatorBannerCrop {
    double x = 0, y = 0, width = 0, height = 0;
};

// Centre crop without stretching. Fractional edges preserve the exact
// tile aspect even when the source's sides are odd numbers.
EmulatorBannerCrop emulator_banner_crop(int width, int height, bool wide = false);
std::vector<std::uint8_t> make_emulator_banner(const std::uint8_t* rgba, int width, int height, bool wide = false);
std::vector<std::uint8_t> emulator_banner_from_cover(const std::uint8_t* cover, bool wide = false);
bool emulator_banner_header_valid(const std::uint8_t* header);
std::string emulator_banner_local_path(const std::string& system, const std::string& game);
std::string emulator_banner_key(const std::string& id, bool wide = false);
std::string emulator_banner_game_id(const std::string& key);
bool emulator_banner_wide(const std::string& key);
bool emulator_banner_id(const std::string& key);
std::string libretro_title_url(const std::string& system, const std::string& name = "");
std::string steamgrid_banner_path(std::uint32_t game);

}  // namespace riftwii
