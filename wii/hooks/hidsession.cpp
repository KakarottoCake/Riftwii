// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "hidsession.hpp"

#include <gccore.h>

namespace riftwii::wii {

void OwnHidHandle::close(bool stuck) {
    // No Shutdown is sent on the way out (see padhook.cpp: a Wii U's IOS
    // refuses every later handle after one).
    if (fd >= 0 && !stuck) IOS_Close(fd);
    *this = OwnHidHandle{};
}

}  // namespace riftwii::wii
