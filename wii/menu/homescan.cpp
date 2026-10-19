// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "homescan.hpp"

#include <ogc/lwp.h>
#include <ogc/mutex.h>

#include <utility>

#include "codebuilds.hpp"
#include "log.hpp"
#include "memlimits.hpp"
#include "netpacks.hpp"
#include "sharednandfind.hpp"

namespace riftwii::wii {
namespace {

mutex_t g_lock = LWP_MUTEX_NULL;
lwp_t g_thread = LWP_THREAD_NULL;
// The scan reads FAT and NTFS volumes, parses pack XMLs (pugixml) and
// looks for network packs: the menu thread's own depth, so a stack as big.
alignas(32) u8 g_stack[128 * 1024];

// Written by the scan thread, read by the menu's, under g_lock.
std::string g_status;
DriveScanPart g_sd, g_usb;
bool g_sdReady = false, g_usbReady = false;  // done and not taken yet
bool g_done = true;

struct Lock {
    Lock() { LWP_MutexLock(g_lock); }
    ~Lock() { LWP_MutexUnlock(g_lock); }
};

void SetStatus(const char* line) {
    Lock lock;
    g_status = line;
}

// The packs and code builds on the drives as they are mounted now (Home's
// "Games with mods" view and the tiles' mod badges).
PackIndex ReadPackIndex() {
    PackIndex packs;
    bool limited = false;
    for (const PackFile& pack : ListPackFiles(256, limited)) {
        const std::string text = ReadPackText(pack.path);
        if (!text.empty()) packs.add(text);
    }
    for (const CodeBuildFile& build : ListCodeBuilds()) packs.add_game(build.game_id);
    logf("Home: %u pack(s) indexed\n", static_cast<unsigned>(packs.size()));
    return packs;
}

void ReadSdCard(DriveScanPart& part) {
    SetStatus("Reading the SD card...");
    part.ok = scan_sd_games(part.catalog, part.error);
    mem::CheckHeap("after the SD scan");
    std::string channelStatus;
    list_nand_channels(part.channels, channelStatus);
    // And those USB Loader GX keeps in an emulated NAND on the card.
    const SharedNandLookup emu = look_up_channel_nand();
    if (emu.usable) list_emu_channels(emu.folder, part.channels, channelStatus);
    part.packs = ReadPackIndex();
    mem::CheckHeap("after the pack index");
}

void ReadUsbDrive(DriveScanPart& part) {
    SetStatus("Reading the USB drive... (a big drive takes a moment)");
    part.ok = scan_usb_games(part.catalog, part.error);
    mem::CheckHeap("after the USB scan");
    // After the USB scan: packs on the drive count too.
    part.packs = ReadPackIndex();
    mem::CheckHeap("after the pack index");
}

void* ScanMain(void*) {
    DriveScanPart sd;
    ReadSdCard(sd);
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
    ReadUsbDrive(usb);
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

void StartDriveScan() {
    WaitDriveScan();
    if (g_lock == LWP_MUTEX_NULL) LWP_MutexInit(&g_lock, false);
    {
        Lock lock;
        g_sdReady = false;
        g_usbReady = false;
        g_done = false;
        g_status = "Reading the SD card...";
    }
    logf("Home: looking for games on the SD card, then the USB drive, while the menu runs\n");
    // Below the GUI thread (70) and the menu's, as the cover loader is: the
    // scan mostly waits on the drives, and the menu answers meanwhile.
    if (LWP_CreateThread(&g_thread, ScanMain, nullptr, g_stack, sizeof(g_stack), 40) < 0) {
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

void WaitDriveScan() {
    if (g_thread == LWP_THREAD_NULL) return;
    LWP_JoinThread(g_thread, nullptr);
    g_thread = LWP_THREAD_NULL;
}

}  // namespace riftwii::wii
