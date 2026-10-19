// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "riftwii/launch.hpp"
#include "usbcatalog.hpp"

// GameCube games on the SD card or a USB drive, started in Wii mode by
// RiftWii's own loader (gcplugin.hpp, the gamecube plugin). One on the
// USB drive runs under a d2x cIOS, whose USB device the game's loader
// reads it through; its memory cards stay on the SD card. Patch-only mods (gcpatch.hpp) are listed as the
// game they make. Packs are applied without touching the image: their
// files are served from a window past its end (riftwii/gcoverlay.hpp).
namespace riftwii::wii {

struct GcImage {
    std::string path;  // sd:/games/.../game.iso, or game.rvz (GcImageIsRvz); usb:/games/... on the USB drive
    std::string id;    // ID6 from its header
    std::string patch_source;  // set: `path` is a patch (gcpatch.hpp) to make the game from this original
    bool shared_card = false;  // the memory card is the shared one, not the game's own (the Memory card option)
    bool drive_timing = true;  // reads take a GameCube drive's time (the game's Loading speed option)
    bool progressive = false;  // 480p (the game's 480p picture option)
    bool widescreen = false;   // 3D drawn for 16:9 (its Widescreen option)
    int language = -1;         // the SRAM language the game reads (its Game language option); -1: the console's
    bool sharp = false;        // every line copied without the game's blur (its Sharper picture option)
    bool sixty_hz = false;     // a European game told the TV takes 60 Hz (its Game speed option)
    std::uint32_t vi_width = 0;  // its Picture width, as the blob takes it (riftwii/gcvideo.hpp); 0: the game's own
    // On the USB drive: the d2x slot picked for the game (0: RiftWii's
    // choice), and, once the launch has found them, the file's pieces in
    // the drive's sectors with its size (the request's usb_fragments).
    int cios_slot = 0;
    std::vector<Fragment> usb_fragments{};
    std::uint64_t usb_bytes = 0;
    // The launch reloaded into d2x and opened its USB device: for an image
    // or a pack on the USB drive (the request's usb_fd).
    bool usb_in_use = false;
};

// Whether the image at `path` is on the USB drive ("usb:/...").
bool GcImageOnUsb(const std::string& path);

// A GameCube image on the SD card in games/<folder>/ (.iso, .gcm or .rvz)
// whose ID starts with `game`, or the image at `path` when given.
bool FindGcImage(const std::string& game, const std::string& path, GcImage& out);

// Every GameCube image in sd:/games/<folder>/ (.iso, .gcm or .rvz), for
// the game list: opened (header read), never needing d2x. An RVZ carries
// check_rvz's verdict (refused ones are listed, and say why at Start).
// The .ciso files beside them, which RiftWii does not play, are noted in
// `ciso` when given (the same walk, so the card is not read twice).
void ScanGcImages(std::vector<ImageGame>& out, CisoFinds* ciso = nullptr);

// The same on the USB drive, in usb:/games/<folder>/, through the menu's
// view of the drive (usbcatalog.hpp): listed beside its Wii games.
void ScanUsbGcImages(std::vector<ImageGame>& out, CisoFinds* ciso = nullptr);
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
