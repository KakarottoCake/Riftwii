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
//  - exclude_game: the games whose code its other extras stay out of;
//  - the per-game preferences: speaker_patch and motor_patch (the Wii
//    Remote's speaker and rumble off), PatchAspectRatio (4:3 or 16:9 kept
//    whatever the console says) and WiiPower's PatchCountryStrings
//    (RiftWii's Region strings fix).
// `loaded` is what the apploader filled; nothing outside it is written.
namespace riftwii {

struct GxReport {
    std::vector<std::string> notes;  // one line each, for boot.log
};

// exclude_game(id, false): Prince of Persia: The Forgotten Sands, Driver:
// San Francisco, The Adventures of Tintin, We Dare. They check their own
// code (MetaFortress), so GX leaves its code-changing extras out of them.
// RiftWii adds The Smurfs: Dance Party (SDU?41), MetaFortress's best known
// victim ("MetaFortress response!" on a tester's Wii).
bool gx_protected_game(const std::string& game_id);

// Kirby's Return to Dream Land checks its code too, but GX patches the
// checks out of it (gx_game_patches), so changing it is safe afterwards.
bool gx_kirby_game(const std::string& game_id);

// Games that clear MEM1 up to their FST, past the arena's end the loader
// set: Resident Evil 4 Wii Edition does at its title screen. Whatever a
// loader puts below the FST is wiped, so RiftWii's blobs stay out.
bool game_clears_mem1_top(const std::string& game_id);
// Of those, the ones whose SDK maps no MEM2 for instructions: the runtime
// (an RVZ's reads, a pack's files) cannot move its code to the top of MEM2
// for them, and stays below the FST, where the game overwrites it.
bool game_runs_no_mem2_code(const std::string& game_id);
// Games that need nearly all of MEM2, so the few hundred KB an RVZ's reader
// takes in game can be too much (the menu says so before an RVZ launch).
bool game_needs_most_of_mem2(const std::string& game_id);

// GameCube games (the native loader, gcnative.cpp) whose ARAM addresses
// must stay as the SDK gives them: raising them to 0x10004000, for the
// sound program to read MEM2, breaks a check of their own. Mario Party 4's
// sound manager (msmSysInit, its ARAM end fixed at 0x808000) tests
// ARGetBaseAddress() plus its sound data's size against that end, sees no
// room and stops in an endless loop before the first frame. Their sound
// program then reads its samples from MEM1 (wrong sound), but they run.
bool gc_keeps_aram_addresses(const std::string& game_id);

// GameCube games whose file loader thread takes a queued load as soon as a
// read finishes and allocates from JSystem's current heap, a single global:
// with reads at the SD card's speed it can wake while another thread has
// made a small, full heap current (Luigi's Mansion: a 512 KB solid heap for
// a room's models) and gets nothing. The native loader makes each load use
// the heap current where it was queued (gcnative.cpp's find_lm_loader).
bool gc_loads_use_queued_heap(const std::string& game_id);

// Games whose main.dol only starts another executable on the disc (EA's
// 2007 games among them): the FST index of the one to start in its place,
// 0 for none. Started through main.dol, Madden NFL 08 from an RVZ stopped
// on a disc error. The indexes are USB Loader GX's (defaultAltDol).
std::uint32_t game_start_file(const std::string& game_id);

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
// speaker_patch: WPADControlSpeaker returns at once, so the Wii Remote's
// speaker never turns on. The first match in each loaded part, as GX
// patches each DOL section. Returns how many functions changed.
unsigned gx_speaker_off(const std::vector<CodeSpan>& loaded, GxReport& report);
// motor_patch: WPADControlMotor returns at once: no rumble.
unsigned gx_rumble_off(const std::vector<CodeSpan>& loaded, GxReport& report);
// PatchAspectRatio: the SDK's read of the console's widescreen setting
// gives 16:9 (`widescreen`) or 4:3 instead.
unsigned gx_force_aspect(bool widescreen, const std::vector<CodeSpan>& loaded, GxReport& report);
// PatchCountryStrings (WiiPower's), the Region strings fix: the game's
// country tables (a region number and two letters, "\1US\0") name the
// console's region; each that names the console's region is changed to
// the game's, so an import finds its own. `console_region` is CONF's:
// 0 Japan, 1 USA, 2 Europe, 4 Korea, 5 China; `game_region` the game ID's
// fourth letter. Returns how many strings changed.
unsigned gx_country_strings(int console_region, char game_region, const std::vector<CodeSpan>& loaded,
                            GxReport& report);

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

// Games that read a USB keyboard through IOS's /dev/usb/kbd: Animal
// Crossing: City Folk (RUU) and Monster Hunter Tri (RMH, its chat).
bool game_uses_usb_keyboard(const std::string& game_id);
// Whether an IOS, or a d2x cIOS on that base, has /dev/usb/kbd: IOS30 and
// up, but not 57-59, whose new USB stack has no keyboard driver (as
// Dolphin's VersionInfo.cpp has it; City Folk under IOS58 in Dolphin:
// "Unknown device: /dev/usb/kbd").
bool ios_has_usb_keyboard(int ios);
// `order` (cIOS slots to try) with the slots whose base has the keyboard
// driver first, then the others, each group as it was. The d2x slots not
// in `order` that have it are tried too. With the SD card in use, a base
// below 56 goes last, after the ones without the keyboard: GX keeps the
// SD card to bases 56-60 (see gx_pick_cios), and a pack that fails in game
// is worse than no keyboard.
std::vector<int> keyboard_slots_first(const std::vector<int>& order, const std::vector<D2xSlot>& slots,
                                      bool sd_card);

}  // namespace riftwii
