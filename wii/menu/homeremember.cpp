// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "homeremember.hpp"

#include <sys/stat.h>

#include <cstdio>
#include <string>
#include <vector>

#include "loadersettings.hpp"
#include "log.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/homelist.hpp"
#include "textfile.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kListPath = "sd:/riftwii/homelist.txt";

// The name the game's tile shows: the title list's, else the disc's own,
// else its ID (as Home names it).
std::string ShownName(const ImageGame& game) {
    if (!game.display.empty()) return game.display;
    if (!game.title.empty()) return game.title;
    return game.id;
}

// False without the memory for the list: nothing is written then.
bool AddRemembered(const ImageCatalog& catalog, const PackIndex& packs, std::vector<RememberedGame>& out) {
    for (const ImageGame& game : catalog.games) {
        RememberedGame remembered;
        remembered.usb = game.device == ImageDevice::Usb;
        remembered.id = game.id;
        remembered.disc_number = game.disc_number;
        remembered.gamecube = game.gamecube;
        remembered.has_mods = packs.has_packs(game.id);
        remembered.name = ShownName(game);
        remembered.path = game.path;
        remembered.patch_source = game.gc_patch_source;
        if (!try_push_back(out, std::move(remembered))) return false;
    }
    return true;
}

}  // namespace

bool ShowRememberedGames(ImageCatalog& sd, ImageCatalog& usb, PackIndex& packs) {
    std::string text;
    if (!ReadTextFile(kListPath, text, kHomeListMaxBytes)) return false;
    std::vector<RememberedGame> games;
    if (!parse_home_list(text, games)) {
        logf("Home: %s is not whole, so Home starts empty and fills in as the drives are read\n", kListPath);
        return false;
    }
    // With Settings > Drives > Games on the SD card off, the card's games
    // are not listed, remembered or not.
    const bool sd_listed = Settings().sd_games != "off";
    ImageCatalog remembered_sd;
    remembered_sd.device = ImageDevice::Sd;
    ImageCatalog remembered_usb;
    remembered_usb.device = ImageDevice::Usb;
    PackIndex remembered_packs;
    for (const RememberedGame& remembered : games) {
        if (!remembered.usb && !sd_listed) continue;
        ImageGame game;
        game.device = remembered.usb ? ImageDevice::Usb : ImageDevice::Sd;
        game.id = remembered.id;
        game.disc_number = remembered.disc_number;
        game.gamecube = remembered.gamecube;
        game.display = remembered.name;
        game.path = remembered.path;
        game.gc_patch_source = remembered.patch_source;
        // Not opened, and not to be: the scan's own entry replaces this
        // one before anything is started from it.
        game.checked = false;
        // Up to kHomeListMaxGames, from a file on the card: a heap that
        // cannot hold them starts with Home empty, as without a list.
        if (!try_push_back((remembered.usb ? remembered_usb : remembered_sd).games, std::move(game))) {
            logf("Home: there is not enough memory to show the games found at the last start\n");
            return false;
        }
        if (remembered.has_mods && !remembered_packs.add_game(remembered.id)) {
            logf("Home: there is not enough memory to show the games found at the last start\n");
            return false;
        }
    }
    if (remembered_sd.games.empty() && remembered_usb.games.empty()) return false;
    logf("Home: showing the %u SD and %u USB game(s) found at the last start (%s) while the drives are read\n",
         static_cast<unsigned>(remembered_sd.games.size()), static_cast<unsigned>(remembered_usb.games.size()),
         kListPath);
    sd = std::move(remembered_sd);
    usb = std::move(remembered_usb);
    packs = std::move(remembered_packs);
    return true;
}

void RememberGames(const ImageCatalog& sd, const ImageCatalog& usb, const PackIndex& packs) {
    std::vector<RememberedGame> games;
    if (!AddRemembered(sd, packs, games) || !AddRemembered(usb, packs, games)) {
        logf("Home: there is not enough memory to remember the games for the next start\n");
        return;
    }
    if (games.empty()) {
        // No games on either drive (a disc-only setup): no list to keep,
        // and an old one must not bring games back at the next start.
        struct stat info;
        if (stat(kListPath, &info) == 0) std::remove(kListPath);
        return;
    }
    const std::string text = serialize_home_list(games);
    // The card is written only when the games changed: most starts find
    // the same ones. The file is read again rather than kept from the
    // start, as the card may be another one by now.
    std::string on_card;
    if (ReadTextFile(kListPath, on_card, kHomeListMaxBytes) && on_card == text) return;
    mkdir("sd:/riftwii", 0777);
    if (!WriteWholeFile(kListPath, text.data(), text.size())) {
        logf("Home: %s could not be written, so the next start shows the games as the drives are read\n", kListPath);
        return;
    }
    logf("Home: %u game(s) remembered for the next start (%s)\n", static_cast<unsigned>(games.size()), kListPath);
}

}  // namespace riftwii::wii
