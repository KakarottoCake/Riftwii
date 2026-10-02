// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/dol.hpp"
#include "riftwii/returnto.hpp"

// USB Loader GX's fixes for particular games, applied the way its
// gamepatches() and GameBooter.cpp apply them (https://github.com/wiidev/usbloadergx
// at commit e25c4f3501ed957b7db73f79c51fdf00715ab2e2; NOTICE.md):
//  - the per-game patches it makes at every launch: Kirby's Return to Dream
//    Land's MetaFortress (crediar's patch), New Super Mario Bros. Wii's BCA
//    check and Prince of Persia: The Forgotten Sands (as WIP codes), Resident
//    Evil 4's GameCube controllers;
//  - anti_002_fix, the older error #002 check in a game's code;
//  - patch_sdcard, for games read from an SD card;
//  - PatchFix480p, a game's wrong 480p setting for the video encoder;
//  - vidolpatcher (WiiPower's "VIDTV patch"), RiftWii's per-game Region
//    video fix;
//  - exclude_game: the games whose code its other extras stay out of.
// `loaded` is what the apploader filled; nothing outside it is written.
namespace riftwii {

struct GxReport {
    std::vector<std::string> notes;  // one line each, for boot.log
};

// exclude_game(id, false): Prince of Persia: The Forgotten Sands, Driver:
// San Francisco, The Adventures of Tintin, We Dare. They check their own
// code (MetaFortress), so GX leaves its code-changing extras out of them.
bool gx_protected_game(const std::string& game_id);

// Kirby's Return to Dream Land checks its code too, but GX patches the
// checks out of it (gx_game_patches), so changing it is safe afterwards.
bool gx_kirby_game(const std::string& game_id);

// patch_nsmb, patch_pop, patch_kirby, patch_re4, then anti_002_fix over
// each loaded part. `dol` is the game's executable as the apploader saw it
// (for the WIP codes' file offsets). With `own_executable` (a pack's
// main.dol in place of the game's) the patches GX writes to fixed
// addresses (Kirby's, Resident Evil 4's) are left out: that code is not
// where they point; the WIP codes check each byte first. Returns how many
// words changed.
unsigned gx_game_patches(const std::string& game_id, const DolHeader& dol, const std::vector<CodeSpan>& loaded,
                         GxReport& report, bool own_executable = false);

// patch_sdcard: Excite Truck and Kirby's Return to Dream Land read from an
// SD card (the game image itself on the card).
unsigned gx_sd_card_patches(const std::string& game_id, const std::vector<CodeSpan>& loaded, GxReport& report);

// PatchFix480p: the __VISendI2CData call that sends the video encoder 1
// where 480p needs 3, sent through two instructions placed in the game's
// spare room (find_safe_space). False when the game has neither.
bool gx_fix_480p(const std::vector<CodeSpan>& loaded, GxReport& report);

// The Region video fix, after GX's vidolpatcher (its "VIDTV Patch", off
// unless chosen): where the game's
// video setup reads the NTSC-J bit of the VI's DTV status register (0x6E,
// bit 1; "rlwinm r0, r0, 31, 31, 31" after a "beq; blt; b" switch), it
// gets the value that matches the game's region instead: 0 for a US game
// (region letter E), 1 for a Japanese one (J). Other regions are left
// alone, as GX leaves them. Returns how many reads changed.
unsigned gx_region_video_fix(char region, const std::vector<CodeSpan>& loaded, GxReport& report);

// GameBooter.cpp's cIOS choice for a game whose cIOS is automatic: the d2x
// slot whose base is the IOS the game asks for, else the next base up,
// else the highest. Brawl (RSB) asks for 56 (its mods need it), SpongeBob's
// Boating Bash (SBV) gets 58 or 38 when there is no base 53, and a game
// whose saves or image are on the SD card only takes bases 56-60.
struct D2xSlot {
    int slot = 0;
    int base = 0;
};
int gx_pick_cios(const std::string& game_id, int requested_ios, std::vector<D2xSlot> slots, bool sd_card,
                 std::string& why);

}  // namespace riftwii
