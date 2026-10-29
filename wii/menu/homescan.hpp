// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "nandchannels.hpp"
#include "emulators.hpp"
#include "packcatalog.hpp"
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
    EmulatorCatalog emulators;
    // Every pack and code build found so far: the SD card's after its part,
    // the USB drive's with them after the drive's.
    PackIndex packs;
    PackageCatalog package_catalog;
    // The SD part carries installed channels and SD WADs; USB carries USB
    // WADs to merge by title. The network note is empty without a problem.
    std::vector<NandChannel> channels;
    unsigned unreadable_wads = 0;
    std::string network_note;
};

// Requests a look at both drives and clears the previous scan's parts at
// once. A scan still running is waited for first. Menu thread only.
void RequestDriveScan();
// Starts the requested scan once the network's background job is done;
// otherwise returns at once. Called from Home's loop and scan waits, on
// the menu thread only, so the GUI keeps answering while a job finishes.
void StartDriveScan();
// What the scan is doing now, for Home's status line.
std::string DriveScanStatus();
// The SD card's part, then the USB drive's, each once, when it is done.
bool TakeSdScan(DriveScanPart& out);
bool TakeUsbScan(DriveScanPart& out);
// Both parts done (taken or not): the drives are the menu's again. False
// from the request, including while it waits for a background job.
bool DriveScanDone();
// A scan has run to its end since RiftWii started (DriveScanDone is
// also true before the first one starts).
bool DriveScanFinished();
// Starts a pending request, waiting for the background job if needed,
// then blocks until the scan thread has finished. Menu thread only. A scan
// is never cut short: one stopped half way would leave a drive mounted half way.
void WaitDriveScan();

}  // namespace riftwii::wii
