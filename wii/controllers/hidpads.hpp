// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include "rthid.h"

namespace riftwii::wii {

// File access runs on the menu/loader thread, never an IPC callback.
void HidPadsRemember(const rthid_device* devices, unsigned count);
const rthid_catalog& HidPadsCatalog();
bool HidPadsWanted();
// Launches need a mapped pad in the latest device list, not just config files.
bool HidPadsConnected();
void HidPadsCopyCatalog(rthid_catalog& out);
// Remembers connected devices first, then fills unused slots for in-game hotplug.
// A launch refreshes the list even when the menu already loaded the configs.
void HidPadsPreload(bool refresh_devices = false);
bool HidPadsConfigured(std::uint32_t vid_pid);

}  // namespace riftwii::wii

// Used by the existing keyboard/tablet device-list filter. No file access.
extern "C" int riftwii_hid_pad_configured(std::uint16_t vid, std::uint16_t pid);
