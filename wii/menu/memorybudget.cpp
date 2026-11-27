// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "memorybudget.hpp"

#include <cstddef>

#include "fault_hook.h"
#include "gcadapter.hpp"
#include "i18n.hpp"
#include "loadersettings.hpp"
#include "packedblobs.hpp"
#include "resident.hpp"
#include "riftwii/gxpatches.hpp"
#include "riftwii_hooks_bin.h"
#include "riftwii_pad_sizes.h"
#include "riftwii_rts_bin.h"
#include "pad_hook.h"
#include "rt_hook.h"
#include "rtfs.h"
#include "rtgcad.h"
#include "shot_hook.h"
#include "vsd_hook.h"

namespace riftwii::wii {
namespace {

std::uint32_t align(std::uint64_t n) { return static_cast<std::uint32_t>((n + 31) & ~31ull); }

// A blob's size unpacked, from the big-endian sizes in front of its set's
// zstd frame (wii/core/packedblobs.cpp), without unpacking it.
std::uint32_t listed_bytes(const std::uint8_t* packed, int index) {
    const std::uint8_t* p = packed + 4 * index;
    return align((std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3]);
}

// The runtime's set: the plain build, then the RVZ one (wii/launch/resident.cpp).
std::uint32_t runtime_bytes(bool rvz) { return listed_bytes(riftwii_rts_bin, rvz ? 1 : 0); }

// The pad code's full build (the pads plugin carries the builds; the DOL
// keeps their sizes, Makefile.runtime's riftwii_pad_sizes.h).
std::uint32_t pad_build_bytes() {
    constexpr std::uint32_t kBytes[] = RIFTWII_PAD_BUILD_BYTES;
    return align(kBytes[0]);
}

// A typical pack's redirect table and payload (a few hundred files).
constexpr std::uint32_t kTypicalTable = 16 * 1024;
// A Zstandard RVZ's buffers, as RiftWii sizes them for a 128 KiB-chunk
// disc (wii/launch/resident.cpp: the group as stored, decoded, the decoder).
constexpr std::uint32_t kTypicalRvzBuffers = 127904 + 135872 + RT_RVZ_DCTX_BYTES + 32;
// Pieces of an RVZ copied onto a card (fragments a card's free space leaves).
constexpr std::uint32_t kTypicalRvzExtents = 16;

bool is_rvz(const FrontendState& state) {
    if (state.use_usb && state.usb_index < state.usb_catalog.games.size())
        return state.usb_catalog.games[state.usb_index].format == UsbImageFormat::Rvz;
    if (state.use_sd && state.sd_index < state.sd_catalog.games.size())
        return state.sd_catalog.games[state.sd_index].format == UsbImageFormat::Rvz;
    return false;
}

}  // namespace

ExtrasBudget EstimateExtras(const FrontendState& state) {
    ExtrasBudget b;
    const LoaderSettings& settings = Settings();
    const std::string& id = state.game_id;
    bool packs = false, code_build = false;
    for (const LaunchPackage& p : state.model.packages) {
        if (!p.enabled) continue;
        packs = true;
        code_build = code_build || p.code_build();
    }
    const bool saves = EffectiveSaveMode(state) != "nand";
    const bool rvz = is_rvz(state);
    const bool protected_game = gx_protected_game(id);
    const bool clears_mem1_top = game_clears_mem1_top(id);
    const bool whole_mem1 = code_build || clears_mem1_top;

    // The mods' runtime (wii/launch/resident.cpp): its code below the MEM1 arena's
    // top (in MEM2 when the game needs all of MEM1), its data in MEM2. Brawl's
    // code goes to MEM2 too, its jumps into it staying at the MEM1 arena's top.
    const bool runtime = packs || saves || rvz;
    const bool code_to_mem2 = whole_mem1 || (game_needs_runtime_code_in_mem2(id) && !game_runs_no_mem2_code(id));
    if (runtime) {
        const std::uint32_t code = runtime_bytes(rvz);
        std::uint32_t data = 0;
        if (packs) data += kTypicalTable + RT_MAX_PENDING * RT_BOUNCE_BYTES;
        if (saves) data += align(sizeof(rt_fs_state)) + 32;
        if (rvz) data += align(rt_rvz_state_bytes(kTypicalRvzExtents, 1)) + kTypicalRvzBuffers;
        const std::uint32_t jumps = code_to_mem2 && !whole_mem1 ? kResidentArenaTopVeneerBytes : 0;
        b.add(rvz ? tr("RVZ and mods") : packs ? tr("mods") : tr("saves on the card"), code_to_mem2 ? jumps : code,
              data + (code_to_mem2 ? code : 0));
    }
    // The game's own SD card passed through beside the runtime (wii/launch/boot.cpp).
    // The extent table costs 12 bytes a piece of the image file, and the piece
    // count is only known at the launch (vsdhook.cpp walks the card's cluster
    // chain then). The menu has no cached count, and a scan here would read
    // the card on the menu thread, so this counts one piece: a scattered image
    // (up to 192 KB of table) loses more than shown. The card passed through
    // is always the SD slot's (VSD_BACKEND_SLOT0), so its bounce buffer is the
    // SD card's size; a code build's image (SD or USB) is not counted here.
    if (runtime && !protected_game && !whole_mem1)
        b.add(tr("SD card for the game"), listed_bytes(riftwii_hooks_bin, kHookVsd), align(vsd_state_bytes(false, 1, vsd_bounce_bytes_for(VSD_BACKEND_SLOT0))));
    GcAdapterView view;
    const bool plugged = GcAdapterMenuLastView(view) && view.open && view.link != GCAD_LINK_NONE;
    const bool adapter = settings.gc_adapter == "on" || (settings.gc_adapter == "auto" && plugged) ||
                         (settings.usb_input != "off" && view.hid_pads != 0);
    if (!protected_game && !whole_mem1) {
        b.add(tr("crash recorder"), listed_bytes(riftwii_hooks_bin, kHookFault), align(sizeof(rt_fault_state)));
        if (settings.screenshots != "off")
            b.add(tr("screenshots"), listed_bytes(riftwii_hooks_bin, kHookShot), align(sizeof(rt_shot_state)));
        if (adapter)
            b.add(tr("GameCube adapter"), pad_build_bytes(), align(sizeof(gcad_hub)));
    } else if (!protected_game && code_build && !clears_mem1_top && adapter) {
        // A code build keeps all of MEM1: the adapter's code and state both
        // go to the top of the MEM2 arena, reached through veneers in low
        // memory (wii/hooks/padhook.cpp). A game that clears MEM1's top
        // gets no adapter at all (boot.cpp), code build or not.
        b.add(tr("GameCube adapter"), 0,
              pad_build_bytes() + align(sizeof(gcad_hub)));
    }
    return b;
}

}  // namespace riftwii::wii
