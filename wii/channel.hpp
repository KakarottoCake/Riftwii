// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// The RiftWii channel (docs/CHANNEL.md): a Wii Menu channel that starts
// RiftWii from apps/riftwii/boot.dol on the SD card or USB drive. Its
// installer is an app of its own in the release zip (apps/riftwii_channel,
// channel/installer), so RiftWii carries none of the channel; Settings and
// a one-time offer only start that installer.
namespace riftwii::wii {

// Whether the channel is on this Wii, and its version (0 when it is not).
bool ChannelInstalled(unsigned& version);

// Whether the installer app is on the card (Settings opens it); `why`
// says where to get it when it is not.
bool ChannelInstallerPresent(std::string& why);

// Whether installing can work here: the installer app on the card, a d2x
// cIOS for it (its signature patches take the channel's fakesigned TMD
// and ticket), and not Dolphin, whose ES checks real signatures. `why`
// says what is missing.
bool ChannelCanInstall(std::string& why);

// Starts the installer app. Runs after the menu has closed; returns only
// if it cannot, with `error` saying why.
bool StartChannelInstaller(std::string& error);

}  // namespace riftwii::wii
