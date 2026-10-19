// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/dol.hpp"
#include "riftwii/patch.hpp"
#include "rtvsd.h"
#include "vsd_hook.h"

// The virtual SD card (runtime/vsd): the game's SD slot answered from a
// card image, so a code build (Project+, REX) whose files are in the
// image reads them through the game's own SD driver wherever the image
// is. The image is riftwii/sd.raw (a FAT32 card image such as Dolphin's)
// on the SD card or, when the SD card has none, on the USB drive
// (wii/hooks/vsdimage.hpp), used when the code builds launched are inside it.
// On the USB drive it is read through d2x's /dev/usb2, which the loader
// opens for a game on the USB drive only.
//
// The blob's code and state go to the top of the MEM2 arena, whose end
// comes down (the start stays: Project M 3.6 reads fixed addresses in
// Brawl's MEM2 heaps);
// the game's functions reach the code through veneers in the code
// handler's list room, which a code build's own list leaves free. Without
// that room the code goes below the MEM1 arena's top instead.
//
// The same blob also passes the real SD card through to a game while the
// resident runtime holds it (wii/launch/boot.cpp): the image is then the whole
// card, read and written on the runtime's open handle, and its block sits
// right above the runtime's MEM2 data.
namespace riftwii::wii {

struct VsdImage {
    bool enabled = false;
    std::string path;                      // "sd:/riftwii/pm.raw" or "usb:/riftwii/pm.raw"
    bool on_usb = false;                   // extents are the USB drive's sectors
    std::uint32_t sectors = 0;             // the image's size
    std::vector<rtvsd_extent> extents;     // its pieces on the card
};

// Where the image `name` ("pm.raw") is, before the card is unmounted:
// false with `why` when there is none or it cannot be used.
bool find_vsd_image(const std::string& name, VsdImage& out, std::string& why);

// The device holding the image, as the loader left it for the game.
struct VsdDevice {
    std::uint32_t backend = VSD_BACKEND_SLOT0;
    std::int32_t fd = -1;
    bool sdhc = false;
    std::uint16_t rca = 0;
};

struct VsdHook {
    bool active = false;
    std::uint32_t functions[VSD_ENTRIES] = {};  // 0: not in the game, or not hooked
    std::uint32_t code_base = 0;                // final addresses
    std::uint32_t code_bytes = 0;
    std::uint32_t state_base = 0;
    std::uint32_t state_bytes = 0;
    bool code_in_mem2 = false;
    std::uint32_t data_base = 0;                // the MEM2 block (code when in MEM2, then the state), at the arena's top
    std::uint32_t data_bytes = 0;
    std::uint32_t stage_base = 0;               // where it is built: data_base itself, or below the runtime's stage
    std::uint32_t veneers = 0;                  // MEM1 veneers' address (code in MEM2)
    std::uint32_t new_arena1_hi = 0;
    std::uint32_t new_arena2_lo = 0;           // unchanged (above the runtime's data: past this block)
    std::uint32_t new_arena2_hi = 0;           // the MEM2 arena's new end (0x80003128): data_base; 0 above the runtime
};

// Before the <memory> patches. `mem1_veneers`: the MEM1 room for the
// veneers, 0 when there is none (the code then goes below `arena1_hi`).
// `after_runtime`: the resident runtime's MEM2 data starts the arena,
// staged at `runtime_stage`: the card's block goes right above that data
// (the arena's start rising, as the runtime's own already does), staged
// below the runtime's, instead of at the arena's top. `known_functions`
// (VSD_ENTRIES, in vsd_hook.h's order): the game's IPC functions as the
// runtime found them, before its hooks changed them.
bool plan_vsd_hook(const DolHeader& dol, const VsdImage& image, const VsdDevice& device, std::uint32_t arena1_hi,
                   std::uint32_t mem1_floor, std::uint32_t arena2_lo, std::uint32_t mem1_veneers,
                   const std::vector<MemoryPatch>& patches, VsdHook& out, std::string& why,
                   bool after_runtime = false, std::uint32_t runtime_stage = 0,
                   const std::uint32_t* known_functions = nullptr);

// The blob passing the SD card through beside the resident runtime: the
// two drive the one card, so each waits while the other's write is still
// being programmed (BUG-05). `runtime_busy` is the runtime's card_busy
// word (0 when it has no savegame state, so never writes); returns the
// address of the blob's own "writing" word, for the runtime.
std::uint32_t link_vsd_with_runtime(const VsdHook& hook, std::uint32_t runtime_busy);

// After the patches and the cheats, before the other blobs' hooks.
bool install_vsd_hook(VsdHook& hook, std::string& why);

// At the jump, after the last use of this loader's memory: the staged
// block to its place.
void place_vsd_hook(const VsdHook& hook);

}  // namespace riftwii::wii
