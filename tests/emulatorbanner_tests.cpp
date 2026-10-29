// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/emulatorbanner.hpp"
#include "riftwii/emulatorart.hpp"
#include "riftwii/coverart.hpp"

#include <cmath>
#include <iostream>
#include <vector>

namespace {
int failures = 0;
#define EXPECT(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

void crops() {
    using namespace riftwii;
    const auto title = emulator_banner_crop(640, 480);
    EXPECT(title.x == 0 && title.width == 640);
    EXPECT(title.y == 20 && title.height == 440);
    const auto wide = emulator_banner_crop(920, 430);
    EXPECT(wide.y == 0 && wide.height == 430);
    EXPECT(std::abs(wide.width / wide.height - 16.0 / 11) < 0.00001);
    EXPECT(std::abs(wide.x * 2 + wide.width - 920) < 0.00001);
    const auto wide_title = emulator_banner_crop(640, 480, true);
    EXPECT(std::abs(wide_title.width / wide_title.height - 171.0 / 88) < 0.00001);
    EXPECT(wide_title.y > title.y);
    const auto odd = emulator_banner_crop(257, 193);
    EXPECT(std::abs(odd.width / odd.height - 16.0 / 11) < 0.00001);
    EXPECT(emulator_banner_crop(0, 80).width == 0);
    EXPECT(emulator_banner_crop(1025, 80).width == 0);
    EXPECT(emulator_banner_crop(80, -1).height == 0);
}

void images() {
    using namespace riftwii;
    // Red edges above and below a green middle: title crop removes exactly
    // the ten lines at either end, without squashing them into the output.
    std::vector<std::uint8_t> rgba(320 * 240 * 4, 255);
    for (int y = 0; y < 240; ++y) {
        for (int x = 0; x < 320; ++x) {
            auto* pixel = rgba.data() + (y * 320 + x) * 4;
            pixel[0] = y < 10 || y >= 230 ? 255 : 0;
            pixel[1] = y < 10 || y >= 230 ? 0 : 255;
            pixel[2] = 0;
        }
    }
    auto file = make_emulator_banner(rgba.data(), 320, 240);
    EXPECT(file.size() == kEmulatorBannerFileSize);
    EXPECT(emulator_banner_header_valid(file.data()));
    for (std::size_t i = 8; i < file.size(); i += 2) {
        EXPECT(file[i] == 0x83 && file[i + 1] == 0xE0);
    }
    file[5] = 127;
    EXPECT(!emulator_banner_header_valid(file.data()));
    EXPECT(!emulator_banner_header_valid(nullptr));
    EXPECT(make_emulator_banner(nullptr, 320, 240).empty());
    EXPECT(make_emulator_banner(rgba.data(), 2048, 1).empty());
    std::vector<std::uint8_t> cover(kCoverPixelBytes);
    for (std::size_t i = 0; i < cover.size(); i += 2) {
        cover[i] = 0xFC;
        cover[i + 1] = 0;
    }
    file = emulator_banner_from_cover(cover.data());
    EXPECT(emulator_banner_header_valid(file.data()));
    EXPECT(file[8] == 0xFC && file[9] == 0);
    EXPECT(emulator_banner_from_cover(nullptr).empty());
}

void sources() {
    using namespace riftwii;
    EXPECT(emulator_banner_local_path("Dreamcast", "Power Stone (USA)") ==
           "sd:/riftwii/banners/emulators/Dreamcast/Power Stone (USA).png");
    for (const auto* name : {"../secret", "x/y", "x\\y", ".hidden", "x:", "x\n"}) {
        EXPECT(emulator_banner_local_path(name, "game").empty());
        EXPECT(emulator_banner_local_path("Dreamcast", name).empty());
    }
    EXPECT(libretro_title_url("Sega - Dreamcast", "Power Stone (USA).png") ==
           "https://thumbnails.libretro.com/Sega%20-%20Dreamcast/Named_Titles/Power%20Stone%20%28USA%29.png");
    EXPECT(libretro_title_url("../private").empty());
    EXPECT(libretro_title_url("Dreamcast", "../private.png").empty());
    EXPECT(libretro_title_url("Sega - Dreamcast") ==
           "https://thumbnails.libretro.com/Sega%20-%20Dreamcast/Named_Titles/");
    EXPECT(steamgrid_banner_path(123) == "/api/v2/grids/game/123?dimensions=460x215,920x430&mimes=image/png");
    EXPECT(steamgrid_banner_path(0).empty());
    const std::string id = "emu-" + std::string(64, 'a');
    EXPECT(emulator_banner_key(id) == "channel-" + id);
    EXPECT(emulator_banner_id(emulator_banner_key(id)));
    EXPECT(emulator_banner_id(emulator_banner_key(id, true)));
    EXPECT(emulator_banner_game_id(emulator_banner_key(id, true)) == id);
    EXPECT(emulator_banner_wide(emulator_banner_key(id, true)));
    EXPECT(!emulator_banner_wide(emulator_banner_key(id)));
    EXPECT(!emulator_banner_id("channel-../secret"));
    EXPECT(emulator_banner_key("RMCE01").empty());
    const std::string json = "{\"success\":true,\"data\":["
        "{\"url\":\"https://cdn2.steamgriddb.com/grid/test.png\",\"mime\":\"image/png\",\"width\":460,\"height\":215},"
        "{\"url\":\"https://cdn2.steamgriddb.com/grid/other.png\",\"mime\":\"image/png\",\"width\":600,\"height\":900}]}";
    std::vector<std::string> urls;
    EXPECT(parse_steamgrid_grids(json, urls, true));
    EXPECT(urls.size() == 1 && urls[0].find("test.png") != std::string::npos);
    EXPECT(parse_steamgrid_grids(json, urls));
    EXPECT(urls.size() == 1 && urls[0].find("other.png") != std::string::npos);
}
}  // namespace

int main() {
    crops();
    images();
    sources();
    return failures ? 1 : 0;
}
