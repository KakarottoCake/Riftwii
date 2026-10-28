// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gccore.h>

#include "riftwii/d2xversion.hpp"

namespace installer {

// IOS58 refuses other titles' files. Follow RiftWii's own cIOS search:
// open its NAND permission check only after -102, with AHBPROT access.
// The access is shared by the whole search. The caller restores it once
// after the search, before any IOS reload.
class NandReadAccess {
public:
    bool Open();
    bool Restore();
    ~NandReadAccess();

private:
    void RestoreProtection();

    u32 word_ = 0;
    u32 before_ = 0;
    u32 after_ = 0;
    u16 protection_ = 0;
    bool protection_saved_ = false;
    bool word_restored_ = true;
    bool protection_restored_ = true;
    bool finished_ = false;
    bool attempted_ = false;
    bool opened_ = false;
};

// Read the same first-content information as RiftWii's cIOS search.
// `read` says whether the slot's first content was read at all, so a
// slot that is not d2x can be told from one that could not be checked.
// `access` is shared by the whole search and restored once by the caller.
// Restore's false forbids any IOS reload, even the ticket fallback or the
// return to RiftWii.
riftwii::D2xInfo ReadCios(int slot, NandReadAccess& access, bool& read);

}  // namespace installer
