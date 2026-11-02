// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "homescan.hpp"
#include "memlimits.hpp"
#include "netsock.hpp"
#include "sharednandfind.hpp"

#include <ogc/lwp.h>
#include <ogc/mutex.h>

#include <functional>
#include <iostream>

namespace {

int failures = 0;
bool network_busy = false;
int busy_polls_left = 0;
int network_joins = 0;
int worker_starts = 0;
int worker_joins = 0;
int sd_reads = 0;
int usb_reads = 0;
bool fail_worker = false;
bool worker_finished = false;
void* (*worker_entry)(void*) = nullptr;
void* worker_argument = nullptr;

#define EXPECT(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

void FinishWorker() {
    EXPECT(worker_entry != nullptr);
    if (!worker_entry || worker_finished) return;
    worker_entry(worker_argument);
    worker_finished = true;
}

}  // namespace

s32 LWP_MutexInit(mutex_t* mutex, bool) {
    *mutex = 1;
    return 0;
}

s32 LWP_MutexLock(mutex_t) { return 0; }
s32 LWP_MutexUnlock(mutex_t) { return 0; }

s32 LWP_CreateThread(lwp_t* thread, void* (*entry)(void*), void* argument,
                     void*, u32, u8) {
    ++worker_starts;
    EXPECT(!network_busy);
    EXPECT(network_joins == worker_starts);
    if (fail_worker) return -1;
    *thread = 1;
    worker_entry = entry;
    worker_argument = argument;
    worker_finished = false;
    return 0;
}

s32 LWP_JoinThread(lwp_t, void**) {
    ++worker_joins;
    FinishWorker();
    worker_entry = nullptr;
    return 0;
}

namespace riftwii::wii {

bool NetBackgroundBusy() {
    if (busy_polls_left > 0) {
        --busy_polls_left;
        network_busy = busy_polls_left > 0;
    }
    return network_busy;
}

void NetWaitForBackground() {
    // A blocking network join here would reproduce the frozen Home.
    EXPECT(!network_busy);
    ++network_joins;
}

void logf(const char*, ...) {}

bool scan_sd_games(ImageCatalog& out, std::string&) {
    EXPECT(!network_busy);
    EXPECT(!DriveScanDone());
    ++sd_reads;
    out.status = "new SD result";
    return true;
}

bool scan_usb_games(UsbCatalog& out, std::string&) {
    EXPECT(!DriveScanDone());
    ++usb_reads;
    out.status = "new USB result";
    return true;
}

bool list_nand_channels(std::vector<NandChannel>&, std::string&, bool) { return true; }
bool list_emu_channels(const std::string&, std::vector<NandChannel>&, std::string&, bool) { return true; }
SharedNandLookup look_up_channel_nand() { return {}; }
PackageCatalog ReadPackageCatalog(PackIndex*) { return {}; }
void ScanEmulators(EmulatorCatalog&) {}
unsigned list_wad_channels(bool, std::vector<NandChannel>&) { return 0; }

std::string RefreshNetworkPacks(const std::function<void(const char*)>&) {
    // The SD part is published while the USB part is still being read.
    EXPECT(!DriveScanDone());
    EXPECT(!DriveScanFinished());
    return {};
}

namespace mem {
bool CheckHeap(const char*) { return true; }
}  // namespace mem

}  // namespace riftwii::wii

int main() {
    using namespace riftwii::wii;
    DriveScanPart part;
    EXPECT(DriveScanDone());
    EXPECT(!DriveScanFinished());
    EXPECT(!TakeSdScan(part));
    EXPECT(!TakeUsbScan(part));

    network_busy = true;
    RequestDriveScan();
    EXPECT(!DriveScanDone());
    EXPECT(!DriveScanFinished());
    for (int frame = 0; frame < 3; ++frame) {
        StartDriveScan();
        EXPECT(!TakeSdScan(part));
        EXPECT(!TakeUsbScan(part));
    }
    EXPECT(network_joins == 0);
    EXPECT(worker_starts == 0);
    EXPECT(sd_reads == 0);

    network_busy = false;
    StartDriveScan();
    StartDriveScan();
    EXPECT(worker_starts == 1);
    EXPECT(network_joins == 1);
    EXPECT(!DriveScanDone());
    FinishWorker();
    EXPECT(DriveScanDone());
    EXPECT(DriveScanFinished());
    EXPECT(TakeSdScan(part));
    EXPECT(part.catalog.status == "new SD result");
    EXPECT(!TakeSdScan(part));
    EXPECT(TakeUsbScan(part));
    EXPECT(part.catalog.status == "new USB result");
    EXPECT(!TakeUsbScan(part));
    WaitDriveScan();
    EXPECT(worker_joins == 1);

    RequestDriveScan();
    StartDriveScan();
    FinishWorker();
    // Leave both results untaken, as when Settings is open. A new request
    // must hide them immediately, even while the next network job finishes.
    network_busy = true;
    RequestDriveScan();
    EXPECT(worker_joins == 2);
    EXPECT(!DriveScanDone());
    EXPECT(!DriveScanFinished());
    EXPECT(!TakeSdScan(part));
    EXPECT(!TakeUsbScan(part));
    EXPECT(worker_starts == 2);

    // Leaving Home can wait before its loop ever starts the worker.
    busy_polls_left = 2;
    WaitDriveScan();
    EXPECT(worker_starts == 3);
    EXPECT(worker_joins == 3);
    EXPECT(DriveScanDone());
    EXPECT(TakeSdScan(part));
    EXPECT(TakeUsbScan(part));

    // Preserve the existing synchronous fallback when no worker can be made.
    fail_worker = true;
    RequestDriveScan();
    StartDriveScan();
    EXPECT(DriveScanDone());
    EXPECT(TakeSdScan(part));
    EXPECT(TakeUsbScan(part));
    WaitDriveScan();
    EXPECT(worker_starts == 4);
    EXPECT(worker_joins == 3);
    EXPECT(sd_reads == 4);
    EXPECT(usb_reads == 4);
    return failures == 0 ? 0 : 1;
}
