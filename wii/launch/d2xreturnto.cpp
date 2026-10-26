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

void prepare_d2x_return_to(std::uint64_t title) {
    if (title == g_accepted_title) return;
    const int ios = IOS_GetVersion();
    if (ios < 200 || ios > 255 || d2x_base(ios) == 0) return;

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
}

void forget_d2x_return_to() {
    g_accepted_title = 0;
}

}  // namespace riftwii::wii
