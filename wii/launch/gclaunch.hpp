// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "riftwii/launch.hpp"
#include "usbcatalog.hpp"

// GameCube games on the SD card, started in Wii mode by RiftWii's own
// loader (gcnative.hpp). Patch-only mods (gcpatch.hpp) are listed as the
// game they make. Packs are applied without touching the image: their
// files are served from a window past its end (riftwii/gcoverlay.hpp).
namespace riftwii::wii {

struct GcImage {
    std::string path;  // sd:/games/.../game.iso, or game.rvz (GcImageIsRvz)
    std::string id;    // ID6 from its header
    std::string patch_source;  // set: `path` is a patch (gcpatch.hpp) to make the game from this original
    bool shared_card = false;  // the memory card is the shared one, not the game's own (the Memory card option)
    bool drive_timing = true;  // reads take a GameCube drive's time (the game's Loading speed option)
    bool progressive = false;  // 480p (the game's 480p picture option)
    bool widescreen = false;   // 3D drawn for 16:9 (its Widescreen option)
    int language = -1;         // the SRAM language the game reads (its Game language option); -1: the console's
};

// A GameCube image on the SD card in games/<folder>/ (.iso, .gcm or .rvz)
// whose ID starts with `game`, or the image at `path` when given.
bool FindGcImage(const std::string& game, const std::string& path, GcImage& out);

// Every GameCube image in sd:/games/<folder>/ (.iso, .gcm or .rvz), for
// the game list: opened (header read), never needing d2x. An RVZ carries
// check_rvz's verdict (refused ones are listed, and say why at Start).
void ScanGcImages(std::vector<ImageGame>& out);

// Whether the image at `path` is an RVZ (by its name, as the scan lists it).
bool GcImageIsRvz(const std::string& path);

// The disc of the GameCube image at `path` as its bytes: the file itself,
// or for an RVZ the disc it holds (riftwii/rvz.hpp's RvzDiscSource).
bool OpenGcDisc(const std::string& path, std::unique_ptr<ByteSource>& out, std::string& error);

// What a GameCube game's "global" options follow now: Settings' aspect
// and game language, and the Wii's own TV, 480p and language settings.
GcConsoleSettings GcConsoleNow();

// Starts the game (prepared from its patch first, when it is one) with
// the packs, in order, each with the choices picked over its defaults
// (none: the game as it is). Packs with memory, savegame or shift patches
// stop the launch. Returns only on failure.
bool LaunchGcGame(const GcImage& image, const std::vector<PackageChoices>& packages, std::string& error);

}  // namespace riftwii::wii
