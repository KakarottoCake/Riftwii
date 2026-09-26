// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// The RiftWii channel (docs/CHANNEL.md): a Wii Menu channel that starts
// RiftWii from apps/riftwii/boot.dol on the SD card or USB drive. The
// channel holds only that small forwarder and its banner
// (tools/make_channel.py, embedded in this program), so RiftWii's own
// updates never need it reinstalled.
namespace riftwii::wii {

// Whether the channel is on this Wii, and its version (0 when it is not).
bool ChannelInstalled(unsigned& version);
// The version this build of RiftWii installs.
unsigned ChannelPackageVersion();

// Whether installing can work here: a d2x cIOS to install under (its
// signature patches take the channel's unsigned TMD and ticket), and not
// Dolphin, whose ES checks real signatures. `why` says what is missing.
bool ChannelCanInstall(std::string& why);

// Install or remove the channel. Runs after the menu has closed: reloads a
// d2x cIOS (the drives are released and the card comes back, as for a
// launch), does the NAND work, and leaves the IOS for the caller's restart.
// Everything is logged; `result` is a line for Home.
bool InstallChannel(std::string& result);
bool RemoveChannel(std::string& result);

}  // namespace riftwii::wii
