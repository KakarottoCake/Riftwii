// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "d2xreturnto.hpp"

#include <gccore.h>

#include "log.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii {
namespace {

std::uint64_t g_accepted_title = 0;

}  // namespace

bool prepare_d2x_return_to(std::uint64_t title) {
    if (title == g_accepted_title) return title != 0;
    const int ios = IOS_GetVersion();
    if (ios < 200 || ios > 255 || d2x_base(ios) == 0) return false;

    alignas(32) static std::uint64_t target;
    alignas(32) static ioctlv vectors[1];
    target = title;
    vectors[0].data = &target;
    vectors[0].len = sizeof(target);

    s32 result = IOS_Open("/dev/es", 0);
    if (result >= 0) {
        const s32 es = result;
        result = IOS_Ioctlv(es, 0xA1, 1, 0, vectors);
        IOS_Close(es);
    }
    if (result >= 0) g_accepted_title = title;
    logf("Return to %08x-%08x: d2x %s the return title on IOS%d (%d).\n",
         static_cast<u32>(title >> 32), static_cast<u32>(title),
         result >= 0 ? "accepted" : "refused", ios, result);
    return result >= 0 && title != 0;
}

bool should_patch_game_return_to(std::uint64_t title, bool nand_emulated, bool d2x_accepted) {
    if (title == 0) return false;
    if (!nand_emulated) return true;

    // Both full and partial emulation redirect /ticket/00010001. Asking
    // for the Wii Menu lets d2x turn emulation off before starting the
    // return title from the real NAND instead of looking for its ticket
    // in the emulated NAND, where the channel may not be installed.
    if (d2x_accepted) {
        logf("Return to: d2x's NAND emulation is on, so the game keeps its Wii Menu exit and d2x starts the selected channel in its place.\n");
    } else {
        logf("Return to: d2x's NAND emulation is on and it did not accept the return title, so HOME > Wii Menu goes to the Wii Menu this time.\n");
    }
    return false;
}

void forget_d2x_return_to() {
    g_accepted_title = 0;
}

}  // namespace riftwii::wii
