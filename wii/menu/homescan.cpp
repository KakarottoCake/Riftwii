// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "homescan.hpp"

#include <ogc/lwp.h>
#include <ogc/mutex.h>
#include <sys/stat.h>
#include <unistd.h>

#include <utility>

#include "codebuilds.hpp"
#include "log.hpp"
#include "mem1tail.h"
#include "sdwritecheck.hpp"
#include "memlimits.hpp"
#include "netpacks.hpp"
#include "netsock.hpp"
#include "sharednandfind.hpp"
#include "wadfolder.hpp"

namespace riftwii::wii {
namespace {

mutex_t g_lock = LWP_MUTEX_NULL;
lwp_t g_thread = LWP_THREAD_NULL;
// The scan's stack, riftwii_home_scan_stack, is in wii/core/mem1tail.c:
// linked last, it is what spans 0x81000000 (mem1tail.h says why).

// Written by the scan thread, read by the menu's, under g_lock.
std::string g_status;
DriveScanPart g_sd, g_usb;
bool g_sdReady = false, g_usbReady = false;  // done and not taken yet
bool g_done = true;
// Only the menu thread requests and starts scans. Readers on other threads
// use g_done under the lock, which is false while a request waits too.
bool g_pending = false;

struct Lock {
    Lock() { LWP_MutexLock(g_lock); }
    ~Lock() { LWP_MutexUnlock(g_lock); }
};

void SetStatus(const char* line) {
    Lock lock;
    g_status = line;
}

// The packs and code builds on the drives as they are mounted now (Home's
// Mods view and the tiles' mod badges). `earlier`: the catalog read for the
// SD card's part of this look, so the USB drive's part does not read and
// parse the card's packs a second time.
PackIndex ReadPackIndex(PackageCatalog& catalog, const PackageCatalog* earlier = nullptr) {
    PackIndex packs;
    catalog = ReadPackageCatalog(&packs, earlier);
    logf("Home: %u pack(s) indexed\n", static_cast<unsigned>(packs.size()));
    return packs;
}

void ReadSdCard(DriveScanPart& part) {
    SetStatus("Reading the SD card...");
    part.ok = scan_sd_games(part.catalog, part.error);
    mem::CheckHeap("after the SD scan");
    std::string channelStatus;
    list_nand_channels(part.channels, channelStatus, true);
    // And those in the emulated NAND on the card that WADs go into: USB
    // Loader GX's channel folder when it is on the card, else sd:/nands/01
    // (one on the USB drive can't be played from, and Install WAD files
    // and the wiiware folders put their games in sd:/nands/01 then).
    const SharedNandLookup emu = look_up_channel_nand();
    list_emu_channels(wad_nand_folder(emu), part.channels, channelStatus, true);
    part.unreadable_wads = list_wad_channels(false, part.channels);
    part.packs = ReadPackIndex(part.package_catalog);
    mem::CheckHeap("after the pack index");
}

void ReadUsbDrive(DriveScanPart& part, const PackageCatalog& card_packs) {
    SetStatus("Reading the USB drive... (a big drive takes a moment)");
    part.ok = scan_usb_games(part.catalog, part.error);
    part.unreadable_wads = list_wad_channels(true, part.channels);
    // Both drives are now available, including ROM folders on the other
    // drive from the emulator. Publish this only with the final scan part.
    ScanEmulators(part.emulators);
    mem::CheckHeap("after the USB scan");
    // After the USB scan: packs on the drive count too.
    part.packs = ReadPackIndex(part.package_catalog, &card_packs);
    mem::CheckHeap("after the pack index");
}

void* ScanMain(void*) {
    // The probe finishes before the scan can reload IOS.
    // The menu waits for the scan before unmounting.
    {
        struct stat st;
        ProbeSdWrite(::stat("sd:/", &st) == 0);
    }
    DriveScanPart sd;
    ReadSdCard(sd);
    // The card's pack catalog stays here until the USB drive's part, which
    // lists every pack again and takes the card's from it instead of
    // reading and parsing them twice at every start. Home needs only the
    // index (sd.packs) meanwhile: a game's page, which uses the catalog,
    // opens once both parts are in, and reads the packs itself when it is
    // handed a catalog that is not ready.
    PackageCatalog card_packs = std::move(sd.package_catalog);
    sd.package_catalog = PackageCatalog{};
    {
        Lock lock;
        g_sd = std::move(sd);
        g_sdReady = true;
    }
    // Before the USB drive, as before the scan had a thread: a network
    // pack is listed with the drive's packs.
    std::string networkNote = RefreshNetworkPacks([](const char* line) { SetStatus(line); });
    if (!networkNote.empty()) logf("%s\n", networkNote.c_str());
    DriveScanPart usb;
    ReadUsbDrive(usb, card_packs);
    // Its texts (up to 2 MiB) go before the part is handed over.
    card_packs = PackageCatalog{};
    usb.network_note = std::move(networkNote);
    {
        Lock lock;
        g_usb = std::move(usb);
        g_usbReady = true;
        g_done = true;
    }
    return nullptr;
}

}  // namespace

void RequestDriveScan() {
    WaitDriveScan();
    if (g_lock == LWP_MUTEX_NULL) LWP_MutexInit(&g_lock, false);
    {
        Lock lock;
        g_sdReady = false;
        g_usbReady = false;
        g_done = false;
        g_status = "Reading the SD card...";
    }
    g_pending = true;
    if (NetBackgroundBusy())
        logf("Home: waiting for a background download to finish before reading the drives again\n");
}

void StartDriveScan() {
    if (!g_pending || NetBackgroundBusy()) return;
    // An emulator cover job may read either drive. Join only once it has
    // finished, before the scan can reset the volumes; Home keeps running.
    NetWaitForBackground();
    g_pending = false;
    logf("Home: looking for games on the SD card, then the USB drive, while the menu runs\n");
    // Below the GUI thread (70) and the menu's, as the cover loader is: the
    // scan mostly waits on the drives, and the menu answers meanwhile.
    if (LWP_CreateThread(&g_thread, ScanMain, nullptr, riftwii_home_scan_stack, sizeof(riftwii_home_scan_stack), 40) < 0) {
        g_thread = LWP_THREAD_NULL;
        logf("Home: no thread for the scan; the drives are read before the menu answers\n");
        ScanMain(nullptr);
    }
}

std::string DriveScanStatus() {
    if (g_lock == LWP_MUTEX_NULL) return "";
    Lock lock;
    return g_status;
}

bool TakeSdScan(DriveScanPart& out) {
    if (g_lock == LWP_MUTEX_NULL) return false;
    Lock lock;
    if (!g_sdReady) return false;
    out = std::move(g_sd);
    g_sd = DriveScanPart{};
    g_sdReady = false;
    return true;
}

bool TakeUsbScan(DriveScanPart& out) {
    if (g_lock == LWP_MUTEX_NULL) return false;
    Lock lock;
    if (!g_usbReady) return false;
    out = std::move(g_usb);
    g_usb = DriveScanPart{};
    g_usbReady = false;
    return true;
}

bool DriveScanDone() {
    if (g_lock == LWP_MUTEX_NULL) return true;
    Lock lock;
    return g_done;
}

bool DriveScanFinished() {
    if (g_lock == LWP_MUTEX_NULL) return false;
    Lock lock;
    return g_done;
}

void WaitDriveScan() {
    // Settings or a menu exit may take over before Home starts the worker.
    // A pending request must finish too, before the caller uses the drives.
    while (g_pending) {
        StartDriveScan();
        if (g_pending) usleep(50000);
    }
    if (g_thread == LWP_THREAD_NULL) return;
    LWP_JoinThread(g_thread, nullptr);
    g_thread = LWP_THREAD_NULL;
}

}  // namespace riftwii::wii
