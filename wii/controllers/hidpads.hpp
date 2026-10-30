// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include "rthid.h"

namespace riftwii::wii {

// File access runs on the menu/loader thread, never an IPC callback.
void HidPadsRemember(const rthid_device* devices, unsigned count);
const rthid_catalog& HidPadsCatalog();
void HidPadsCopyCatalog(rthid_catalog& out);
// Remembers connected devices first, then fills unused slots for in-game hotplug.
void HidPadsPreload();
bool HidPadsConfigured(std::uint32_t vid_pid);

}  // namespace riftwii::wii

// Used by the existing keyboard/tablet device-list filter. No file access.
extern "C" int riftwii_hid_pad_configured(std::uint16_t vid, std::uint16_t pid);
