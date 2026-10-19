// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "nandchannels.hpp"
#include "riftwii/launch.hpp"
#include "usbcatalog.hpp"

// Home's look for games, on a thread of its own: a big USB drive takes
// seconds to list, and the menu used to answer no press until it had. The
// SD card is read first and handed over as soon as it is done, so Home
// shows its games (and the Wii's channels) while the USB drive is read;
// the drive's games follow.
//
// The scan owns the drives' state in usbcatalog.cpp (the mounted volumes,
// the title list) and the network while it runs: until DriveScanDone, the
// menu reads no game image and starts no download.
namespace riftwii::wii {

struct DriveScanPart {
    bool ok = false;       // the drive was read
    std::string error;     // why not, when it was not
    ImageCatalog catalog;
    // Every pack and code build found so far: the SD card's after its part,
    // the USB drive's with them after the drive's.
    PackIndex packs;
    // The SD card's part only: the Wii's channels (and those in another
    // loader's emulated NAND on the card), and what looking for network
    // packs came to (empty when there was nothing to say).
    std::vector<NandChannel> channels;
    std::string network_note;
};

// Starts a look at both drives. A scan still running is waited for first.
void StartDriveScan();
// What the scan is doing now, for Home's status line.
std::string DriveScanStatus();
// The SD card's part, then the USB drive's, each once, when it is done.
bool TakeSdScan(DriveScanPart& out);
bool TakeUsbScan(DriveScanPart& out);
// Both parts done (taken or not): the drives are the menu's again.
bool DriveScanDone();
// Blocks until the thread has finished. A scan is never cut short: one
// stopped half way would leave a drive mounted half way.
void WaitDriveScan();

}  // namespace riftwii::wii
