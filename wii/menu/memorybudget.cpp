// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "memorybudget.hpp"

#include <cstddef>

#include "fault_hook.h"
#include "gcadapter.hpp"
#include "i18n.hpp"
#include "loadersettings.hpp"
#include "riftwii/gxpatches.hpp"
#include "riftwii_fault_bin.h"
#include "riftwii_pad_bin.h"
#include "riftwii_rt_rvz_zst.h"
#include "riftwii_rt_zst.h"
#include "riftwii_shot_bin.h"
#include "riftwii_vsd_bin.h"
#include "pad_hook.h"
#include "rt_hook.h"
#include "rtfs.h"
#include "rtgcad.h"
#include "shot_hook.h"
#include "vsd_hook.h"
#include "zstd.h"

namespace riftwii::wii {
namespace {

std::uint32_t align(std::uint64_t n) { return static_cast<std::uint32_t>((n + 31) & ~31ull); }

// A runtime blob's size unpacked, from its zstd frame header.
std::uint32_t frame_bytes(const std::uint8_t* packed, std::size_t size) {
    const unsigned long long bytes = ZSTD_getFrameContentSize(packed, size);
    if (bytes == ZSTD_CONTENTSIZE_UNKNOWN || bytes == ZSTD_CONTENTSIZE_ERROR) return 0;
    return align(bytes);
}

template <typename Header>
std::uint32_t blob_bytes(const std::uint8_t* blob) {
    return align(reinterpret_cast<const Header*>(blob)->size);
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
    const bool saves = state.model.save_mode == "separate" || state.model.save_mode == "fresh";
    const bool rvz = is_rvz(state);
    const bool protected_game = gx_protected_game(id);
    const bool clears_mem1_top = game_clears_mem1_top(id);
    const bool whole_mem1 = code_build || clears_mem1_top;

    // The mods' runtime (wii/launch/resident.cpp): its code below the MEM1 arena's
    // top (in MEM2 when the game needs all of MEM1), its data in MEM2.
    const bool runtime = packs || saves || rvz;
    if (runtime) {
        const std::uint32_t code = rvz ? frame_bytes(riftwii_rt_rvz_zst, riftwii_rt_rvz_zst_size)
                                       : frame_bytes(riftwii_rt_zst, riftwii_rt_zst_size);
        std::uint32_t data = 0;
        if (packs) data += kTypicalTable + RT_MAX_PENDING * RT_BOUNCE_BYTES;
        if (saves) data += align(sizeof(rt_fs_state)) + 32;
        if (rvz) data += align(rt_rvz_state_bytes(kTypicalRvzExtents)) + kTypicalRvzBuffers;
        b.add(rvz ? tr("RVZ and mods") : packs ? tr("mods") : tr("saves on the card"), whole_mem1 ? 0 : code,
              data + (whole_mem1 ? code : 0));
    }
    // The game's own SD card passed through beside the runtime (wii/launch/boot.cpp).
    if (runtime && !protected_game && !whole_mem1)
        b.add(tr("SD card for the game"), blob_bytes<vsd_header>(riftwii_vsd_bin), align(sizeof(vsd_state)));
    if (!protected_game && !whole_mem1) {
        b.add(tr("crash recorder"), blob_bytes<rt_fault_header>(riftwii_fault_bin), align(sizeof(rt_fault_state)));
        if (settings.screenshots != "off")
            b.add(tr("screenshots"), blob_bytes<rt_shot_header>(riftwii_shot_bin), align(sizeof(rt_shot_state)));
        GcAdapterView view;
        const bool plugged = GcAdapterMenuLastView(view) && view.open && view.link != GCAD_LINK_NONE;
        const bool adapter = settings.gc_adapter == "on" || (settings.gc_adapter == "auto" && plugged);
        if (adapter && !rvz)  // RVZ games and the adapter do not work together yet (issue #4)
            b.add(tr("GameCube adapter"), blob_bytes<rt_pad_header>(riftwii_pad_bin), align(sizeof(gcad)));
    }
    return b;
}

}  // namespace riftwii::wii
