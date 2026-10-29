// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/picturepick.hpp"
#include "riftwii/emulatorbanner.hpp"

#include <iostream>
#include <vector>

namespace {
int failures = 0;
#define EXPECT(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

void candidates() {
    using namespace riftwii;
    const std::string system = "Sega - Dreamcast";
    const std::vector<std::string> names = {"Power Stone (USA).png", "Power Stone (Japan).png",
                                           "Power Stone 2 (USA).png", "Crazy Taxi (USA).png"};
    std::vector<PictureCandidate> list;
    EXPECT(picture_add_libretro(list, PictureSource::Title, system, "Power Stone (USA)", names));
    EXPECT(list.size() == 2);
    EXPECT(list[0].url.find("Power%20Stone%20%28USA%29.png") != std::string::npos);
    EXPECT(list[1].url.find("Japan") != std::string::npos);
    EXPECT(picture_add_libretro(list, PictureSource::Screenshot, system, "Power Stone", names));
    EXPECT(picture_add_libretro(list, PictureSource::Boxart, system, "Power Stone", names));
    EXPECT(list.size() == 6);
    EXPECT(picture_page_count(list.size()) == 1);
    EXPECT(list[2].url.find("/Named_Snaps/") != std::string::npos);
    EXPECT(list[4].url.find("/Named_Boxarts/") != std::string::npos);
    EXPECT(picture_add_libretro(list, PictureSource::Title, system, "Power Stone", names));
    EXPECT(list.size() == 6);
    std::vector<std::string> urls;
    for (int i = 0; i < 20; ++i) urls.push_back("https://cdn2.steamgriddb.com/grid/" + std::to_string(i) + ".png");
    EXPECT(picture_add_steamgrid(list, urls));
    EXPECT(list.size() == 12);
    EXPECT(picture_page_count(list.size()) == 2);
    EXPECT(!picture_add_libretro(list, PictureSource::Title, "../escape", "Power Stone", names));
    EXPECT(!picture_add_steamgrid(list, {"https://evil.example/grid.png"}));
    EXPECT(!picture_candidate_valid({"https://thumbnails.libretro.com/Sega/Named_Titles/../x.png", PictureSource::Title}));
    EXPECT(!picture_candidate_valid({"https://thumbnails.libretro.com/Sega/Named_Titles/x%2fother.png", PictureSource::Title}));
    EXPECT(!picture_candidate_valid({"https://thumbnails.libretro.com/Sega/Named_Snaps/x.png", PictureSource::Title}));
    EXPECT(picture_page_count(0) == 1);
    EXPECT(picture_page_count(7) == 2);
    EXPECT(picture_page_count(24) == 4);
    EXPECT(picture_page_count(25) == 0);

    std::vector<std::string> variants;
    for (int i = 0; i < 12; ++i) variants.push_back("Power Stone (USA) (Rev " + std::to_string(i) + ").png");
    std::vector<PictureCandidate> bounded;
    for (const auto source : {PictureSource::Title, PictureSource::Screenshot, PictureSource::Boxart}) {
        EXPECT(picture_add_libretro(bounded, source, system, "Power Stone", variants));
    }
    EXPECT(bounded.size() == 18);
    EXPECT(picture_add_libretro(bounded, PictureSource::Title, system, "Power Stone", names));
    EXPECT(bounded.size() == 18);
    EXPECT(picture_add_steamgrid(bounded, urls));
    EXPECT(bounded.size() == kPictureCandidatesMax);
    EXPECT(picture_page_count(bounded.size()) == 4);
    EXPECT(picture_add_steamgrid(bounded, urls));
    EXPECT(bounded.size() == kPictureCandidatesMax);
    EXPECT(!picture_candidate_valid({"https://thumbnails.libretro.com/Sega/Named_Titles/%2E%2E.png", PictureSource::Title}));
    EXPECT(!picture_candidate_valid({"https://thumbnails.libretro.com/Sega/Named_Titles/x%00.png", PictureSource::Title}));
    EXPECT(!picture_candidate_valid({"https://thumbnails.libretro.com/Sega/Named_Titles/x%.png", PictureSource::Title}));
    EXPECT(!picture_candidate_valid({"https://thumbnails.libretro.com/Sega/Named_Titles/x.png", static_cast<PictureSource>(99)}));
}

void paths_and_markers() {
    using namespace riftwii;
    EXPECT(picture_libretro_url(PictureSource::Screenshot, "Sega - Dreamcast", "Power Stone (USA).png") ==
           "https://thumbnails.libretro.com/Sega%20-%20Dreamcast/Named_Snaps/Power%20Stone%20%28USA%29.png");
    EXPECT(picture_libretro_url(PictureSource::SteamGrid, "Sega").empty());
    EXPECT(picture_libretro_url(PictureSource::Title, "Sega", "../bad.png").empty());
    EXPECT(picture_save_path_valid(emulator_banner_local_path("Dreamcast", "Power Stone")));
    EXPECT(picture_save_path_valid("sd:/riftwii/banners/ABCD01@tile.png"));
    for (const auto* path : {"usb:/riftwii/banners/game.png", "sd:/riftwii/../game.png", "sd:/riftwii/.hidden/game.png",
                             "sd:/riftwii/banners//game.png", "sd:/riftwii/banners/game.jpg", "sd:/riftwii/banners/game.png/"}) {
        EXPECT(!picture_save_path_valid(path));
    }
    std::vector<std::uint8_t> png = {1, 2, 3};
    const auto marker = picture_pick_marker(png);
    EXPECT(marker.size() < 96);
    EXPECT(picture_pick_marker_matches(png, marker));
    png[0] = 4;
    EXPECT(!picture_pick_marker_matches(png, marker));
    EXPECT(!picture_pick_marker_matches({}, ""));
    EXPECT(!picture_pick_marker_matches(png, "RIFTWII-PICTURE-1\n"));
}
}  // namespace

int main() {
    candidates();
    paths_and_markers();
    return failures ? 1 : 0;
}
