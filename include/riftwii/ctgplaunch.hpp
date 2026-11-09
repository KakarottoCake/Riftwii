// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/ctgpfiles.hpp"
#include "riftwii/ctgpimage.hpp"
#include "riftwii/ctgpmmu.hpp"
#include "riftwii/ctgppackages.hpp"
#include "riftwii/ctgptables.hpp"
#include "riftwii/mempatch.hpp"

// The native CTGP-R launch (docs/CTGPR.md): everything
// the launcher puts in memory before it jumps to the game, as a plan built
// from the package files, and the placement that carries it out. Pure code
// over a MemoryAccess, so the host tests and a host simulation cover it.
namespace riftwii {

constexpr std::uint32_t kCtgpImageStart = 0x81600000;  // the arena end the game gets (PAL: below it is the game's)
constexpr std::uint32_t kCtgpSdaBase = 0x816b5740;     // partition objects
constexpr std::uint32_t kCtgpEfaBase = 0x816cd9a0;
constexpr std::uint32_t kCtgpSdaDisc = 0x816b12c0;     // their DISC_INTERFACE structs (inside BOOT.BIN)
constexpr std::uint32_t kCtgpEfaDisc = 0x816b12e0;
// The DOL CTGP loads and runs when it leaves for the channel (BOAT 0x817944bc:
// the Mario Kart Channel and Rankings menus, and its crash handler): the
// string "efa:/packages/CHAN/stub.dol" in BOOT.BIN's data. That stub starts
// the channel, which does not run after a RiftWii launch.
constexpr std::uint32_t kCtgpChannelStub = 0x81706e10;
constexpr std::uint32_t kCtgpChannelStubLength = 28;

// The files whose presence in sd:/ctgpr/My Stuff the channel checks before
// it lets ghosts be saved (chan FUN_80954cec).
constexpr const char* kCtgpMyStuffCommons[] = {"Common.szs",   "Common_E.szs", "Common_F.szs", "Common_G.szs",
                                                "Common_I.szs", "Common_J.szs", "Common_M.szs", "Common_Q.szs",
                                                "Common_S.szs", "Common_U.szs"};

// Everything read from the card and the package disk.
struct CtgpInputs {
    std::vector<std::uint8_t> boot_bin, boat_bin, beat_bin;  // /PACKAGES/CORE/ on efa:
    std::vector<std::uint8_t> packages_bin;                  // /PACKAGES.BIN
    std::vector<std::uint8_t> rows_bin;                      // the row table file
    std::vector<std::uint8_t> efa_boot_sector;               // block 0 of efa:
    std::vector<std::uint8_t> sda_boot_sector;               // the SD volume's boot sector
    std::uint32_t sda_volume_lba = 0;                        // where that sector is on the card
    std::string config_ini;                                  // sd:/ctgpr/config.ini, may be empty
    std::vector<std::uint8_t> track_hashes;                  // efa:/packages/ctgpr/deyazhashes.hsh, may be empty
    bool my_stuff_common = false;  // sd:/ctgpr/My Stuff holds a Common*.szs (kCtgpMyStuffCommons)
    CtgpTables tables;                       // the package-disk keys and IV (ctgptables.hpp)
    // What CTGP starts when it leaves for the channel (kCtgpChannelStub), in
    // CTGP's device names ("sda:/apps/riftwii/boot.dol"); empty keeps the
    // channel's stub. At most kCtgpChannelStubLength - 1 characters.
    std::string channel_stub;
    CtgpRegion region = CtgpRegion::P;
};

// One step on the game's own memory (the game is loaded when they run).
struct CtgpGameOp {
    enum Kind { Write, Move } kind = Write;
    std::uint32_t address = 0;        // Write: where; Move: destination
    std::vector<std::uint8_t> bytes;  // Write
    std::uint32_t from = 0;           // Move: source (overlap allowed)
    std::uint32_t length = 0;         // Move
    // What the operation is (the start-up stub finds the first hook by it).
    enum Group { LowMemory, Settings, Hook, Environment, Startup, Mmu } group = LowMemory;
    int hook_index = -1;              // Hook: 0..36, the channel's order
    int env_index = -1;               // Environment: 0 blr, 1 zero 808, 2 zero 1376, 3 and 4 nops
};

struct CtgpLaunchPlan {
    std::vector<std::uint8_t> boot;   // goes to kCtgpBootBase: BOOT.BIN linked, partitions and fields in
    std::vector<std::uint8_t> boat;   // goes to kCtgpBoatBase
    std::vector<CtgpGameOp> game_ops; // low memory, settings patches, then the hooks
    std::uint32_t arena1_hi = kCtgpImageStart;  // for 0x80000034 and 0x80003110
    std::uint32_t image_end = 0;      // end of BOAT
    std::uint32_t hook0_original = 0;  // the word the first hook was before the startup stub took its place
    std::vector<CtgpMergedFile> files;  // the packages' files for the game's file system table (ctgp_attach_files)
    std::vector<std::string> missing;   // list entries with no file behind them: "<source> -> <game path>" (a leading '*' if required)
    std::vector<std::string> notes;
    // The channel's memory map: the page table is a game operation (group
    // Mmu); the launcher loads these registers right before the jump.
    bool mmu = false;
    CtgpMmu mmu_regs;  // htab left empty here
};

// The one CTGP-R version this launch was made and tested for (PACKAGES.BIN
// on the package disk). The launch writes into CTGP's code and data at fixed
// places, and the cup layouts are baked for this exact CTGPR version
// (ctgpapply.cpp), so both the core and the CTGPR versions are pinned: any
// other version is refused rather than started (ctgp_check_versions). A later
// step that derives the cups from the player's own package disk will let the
// CTGPR version vary again.
constexpr std::uint32_t kCtgpTestedCoreVersion = 0x01030412;
constexpr std::uint32_t kCtgpTestedCtgprVersion = 0x01031196;

// True when PACKAGES.BIN names the tested versions; otherwise `error` says
// which version was found.
bool ctgp_check_versions(const std::vector<CtgpPackage>& packages, std::string& error);

// True when a homebrew-app stand-in launch is CTGP-R's own channel swap, and
// only then: the game is Mario Kart Wii (its ID starts "RMC") and the pack's
// only change is to start CTGP-R's channel app at exactly
// "sd:/apps/ctgpr/boot.dol" (the path homebrew_app_stand_in returns; matched
// without regard to letter case, as the card's file system is). This narrows
// the native CTGP-R launch, and the cheat/other-pack lockout that goes with it,
// to that one case: a pack that swaps main.dol for some other /apps/*/*.dol,
// even on an RMC-id disc, is an ordinary pack and must not get CTGP-R's Mario
// Kart Wii hooks written into it.
bool ctgp_is_native_stand_in(const std::string& game_id, const std::string& app);

// Builds the plan. Refuses an untested CTGP-R version (ctgp_check_versions).
// Online play stays off whatever config.ini says: the network calls are
// stubbed, LAN mode and UPnP are off (ctgpapply.hpp).
bool ctgp_assemble(const CtgpInputs& inputs, CtgpLaunchPlan& plan, std::string& error);

// The packages' side of the launch: gives every file of the packages' lists
// (`package_fsts`, in merge order, each a package's FST.BIN, with `hash_lists` the
// package's verified list of digests or null) its open-file record,
// writes the record table and the partitions' lists of open files into the plan's
// BOOT image, and keeps the files for the game's file system table in plan.files
// (ctgp_merge_files puts them there at launch). A file the resolver does not
// find is left out unless its list says it is needed.
bool ctgp_attach_files(CtgpLaunchPlan& plan, const std::vector<std::vector<std::uint8_t>>& package_fsts,
                       const std::vector<const CtgpHashList*>& hash_lists, const CtgpSourceResolver& resolver,
                       std::string& error);

// What the plan must not collide with, as the loader sees it right before
// the jump.
struct CtgpMemoryState {
    std::uint32_t loader_end = 0;     // SYS_GetArena1Hi(): the loader's own memory ends here
    std::uint32_t apploader_end = 0;  // the apploader image's end
    std::uint32_t fst_address = 0;    // 0x80000038
    std::uint32_t bi2_address = 0;    // 0x800000F4
    std::vector<MemoryRegion> loaded; // what the apploader loaded (the game's sections)
};
bool ctgp_check_layout(const CtgpLaunchPlan& plan, const CtgpMemoryState& state, std::string& error);

// Writes the plan: BOOT and BOAT, then the game operations, in order.
bool ctgp_place(const CtgpLaunchPlan& plan, MemoryAccess& memory, std::string& error);

}  // namespace riftwii
