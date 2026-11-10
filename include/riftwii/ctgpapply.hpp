// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/ctgpimage.hpp"
#include "riftwii/ctgppackages.hpp"

// The channel's "apply settings to the game" step (chan_internal
// FUN_808973a8, docs/CTGPR.md): turns the option values into words in
// the loader block, bytes in the game's low memory (0x80000a40..) and a few
// code patches in the game. It is a pure function here, so it can be tested
// off the Wii; the launcher runs the resulting operations on the memory image.
namespace riftwii {

// The options the step reads, named as in config.ini where the channel's key
// table names them (variables at r13 = 0x80c02460 + the offset in the
// comment). The ones the key table does not name are called opt_XXX; their
// meaning is not known yet and the defaults are what the channel starts with
// before anything sets them.
struct CtgpSettings {
    bool ctgpr_enabled = true;          // ini "ctgpr" (0x816ff518)
    std::int32_t cup_course_layout_using = 0;     // +0xb4, 0..2
    std::int32_t room_race_count = 0;             // +0xec, byte at 0x80000a46
    std::int32_t anti_cheat = 0;                  // +0x90
    std::int32_t anti_cheat_fr_strength = 0;      // +0x448
    std::int32_t show_race_rules = 0;             // +0x34
    std::int32_t minimap_mii_heads = 0;           // +0x444
    std::int32_t room_host_votes = 0;             // +0x440
    std::int32_t room_engine_class = 0;           // +0x764
    std::int32_t room_vehicle_availability = 0;   // +0x76c
    std::int32_t room_encryption = 0;             // +0x770
    std::int32_t room_tc_effect_mega = 0;         // +0x774
    std::int32_t room_disable_tc_autofire = 0;    // +0x778
    std::int32_t room_track_availability = 0;     // +0x77f (a byte)
    std::int32_t use_country_id_for_region_id = 0;  // +0x30
    std::int32_t knockout_type = 0;               // +0x438
    std::int32_t knockout_bonus_type = 0;         // +0x88
    std::int32_t knockout_bonus_stockpile_max = 0;    // +0x2c
    std::int32_t knockout_bonus_point_threshold = 0;  // +0x28
    std::int32_t knockout_players_eliminated_1 = 0;   // +0x24
    std::int32_t knockout_phase_2_threshold = 0;      // +0x8c
    std::int32_t knockout_players_eliminated_2 = 0;   // +0x20
    std::int32_t my_stuff = 0;                    // ini "my_stuff", 0..3
    bool no_music = false;                        // ini "no_music"
    std::int32_t font = 0;                        // ini "font", 0..3
    std::int32_t ghost_saving_mode = 0;           // +0x43c, 0..2; copied to 0x816ff514 at launch
    // not named by the key table
    std::int32_t opt_754 = 0, opt_494 = 0, opt_760 = 0, opt_7d0 = 0, opt_8bc = 0;
    bool opt_8b1 = false;
    // what the package files say
    std::uint32_t ctgpr_package_version = 0;   // 0 when the package is missing
    std::uint32_t core_package_version = 0;
    // 0 NTSC-U (E), 1 NTSC-J (J), 2 PAL (P): the column of the game's hook table
    std::uint32_t region = 2;
};

enum class CtgpOpKind { Store, RowStore, RowMove };

// One step, in the order the channel does them.
struct CtgpOp {
    CtgpOpKind kind = CtgpOpKind::Store;
    // Store: write `size` (1, 2 or 4) bytes of `value` at `address`.
    // RowStore: write the word `value` at game table row `row`, byte `offset`.
    // RowMove: move `size` bytes inside the row's code from `from` to `offset` (overlap allowed).
    std::uint32_t address = 0;
    std::uint32_t row = 0;
    std::uint32_t offset = 0;
    std::uint32_t from = 0;
    std::uint32_t size = 0;
    std::uint32_t value = 0;
};

// Rows of the game address table (the one the hooks use) that get patched.
constexpr std::uint32_t kCtgpRowStub1 = 28, kCtgpRowStub2 = 29, kCtgpRowPatchA = 1253,
                        kCtgpRowPatchB = 2525;

// The options from config.ini (keys the file lacks keep the struct defaults;
// `ctgpr` defaults to yes like the channel's). Package versions come from
// the package list.
CtgpSettings ctgp_settings_from_ini(const std::string& config_ini, const std::vector<CtgpPackage>& packages,
                                    CtgpRegion region);

// The operations, in order. The course table and the cup layouts are baked
// into the build (ctgpapply.cpp), the same way the row table is. Already-zero
// memory the channel copies in at the end (its three runtime tables for
// 0x816ff9f8, 0x816ff260 and 0x816fe0e0) stays zero: the launcher's image
// starts zeroed.
//
// Online play is always off: the network calls are stubbed as the channel
// stubs them without network settings, and neither LAN mode nor UPnP is
// ever set, whatever config.ini says (the CTGP-R developers' condition for
// this launch).
std::vector<CtgpOp> ctgp_apply_settings(const CtgpSettings& settings);

// The words the settings leave at fixed values (for the tests).
constexpr std::uint32_t kCtgpCourseTable = 0x816ffaf8;   // 32 words
constexpr std::uint32_t kCtgpLayoutWords = 0x816ffb78;   // 216 words
constexpr std::uint32_t kCtgpLayoutCount = 0x816ffed8;

// The package lists the channel merges into the game's file system table,
// in its order (the 10-entry table at chan_internal 0x80bff740; the options'
// callbacks set each entry's flag): CTGPR's list (`ctgpr = no` takes
// no_ct_24p.bin), My Stuff's (`my_stuff` 1..3: all, no tracks, music only),
// NOMUSIC's (`no_music`), one font (`font` 1..3), then CORE's. `hash_list`
// names the package whose signed digest list vouches for the files (empty for
// none). `allow_unsigned` is true only for My Stuff, the one package whose
// files are the player's own and may be served without a signed digest (as the
// channel serves them); every other package is strict.
struct CtgpPackageList {
    std::string path;       // on the package disk (efa:)
    std::string hash_list;  // "CTGPR", "CORE" or ""
    bool allow_unsigned = false;
};
std::vector<CtgpPackageList> ctgp_package_lists(const CtgpSettings& settings);

// The channel's table of the CTGPR tracks' digests (efa:/packages/ctgpr/
// deyazhashes.hsh, 255 SHA-1s): it fills 0x816ff260 (the first 0x280 bytes)
// and 0x816fe0e0 (the next 0x1180) of the loader's block.
constexpr std::uint32_t kCtgpTrackHashCount = 255;
constexpr std::uint32_t kCtgpTrackHashesA = 0x816ff260, kCtgpTrackHashesALength = 0x280;
constexpr std::uint32_t kCtgpTrackHashesB = 0x816fe0e0, kCtgpTrackHashesBLength = 0x1180;

}  // namespace riftwii
