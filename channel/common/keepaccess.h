// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Keeping the Homebrew Channel's hardware access (AHBPROT off) over an IOS
// reload. RiftWii uses it before its own reloads, and the channel installer
// before the two it makes (into the cIOS and back into IOS 58), so RiftWii
// comes back from the installer able to read the NAND.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// What keepaccess_for_next_ios did.
enum {
    KEEPACCESS_NO_ACCESS = -1,  // AHBPROT is on: the running IOS can't be patched
    KEEPACCESS_NOT_FOUND = 0,   // ES's check was not found in the running IOS
    // > 0: the number of places patched
};

// Patches the running IOS's ES so the next IOS it starts leaves hardware
// access on for this program. Call it right before IOS_ReloadIOS.
int keepaccess_for_next_ios(void);

#ifdef __cplusplus
}
#endif
