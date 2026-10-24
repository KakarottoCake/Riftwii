// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The actual d2x SD client (wii/storage/d2xsd.cpp) against a mocked IOS
// that reloads: a handle from before a reload must not be used after it
// (#270), so the next open really opens and starts the new IOS's device.
#include "d2xsd.hpp"

#include <gccore.h>
#include <ogc/mutex.h>
#include <sdcard/wiisd_io.h>

#include <iostream>
#include <string>

namespace {

int failures = 0;
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "Failed: " #condition " at " << __LINE__ << '\n'; \
            ++failures; \
        } \
    } while (0)

constexpr s32 kInit = 0x01;
constexpr s32 kRead = 0x02;
constexpr s32 kIpcNotFound = -6;  // what IOS answers for a descriptor it does not know

// One IOS at a time. A reload kills every descriptor the old one gave out;
// descriptor numbers start over, as on a Wii, so a stale number can even
// match a live one: the mock tells them apart by the IOS they came from.
struct MockIos {
    unsigned generation = 1;
    bool has_sdhc = true;  // the IOS has d2x's /dev/sdio/sdhc
    s32 init_result = 0;
    s32 live_fd = -1;
    unsigned live_generation = 0;
    unsigned opens = 0;
    unsigned inits = 0;
    unsigned closes = 0;
    unsigned dead_handle_calls = 0;  // any call on a descriptor of an IOS that is gone
    unsigned reads = 0;
    unsigned libogc_startups = 0;
    int lock_depth = 0;
    int deepest_lock = 0;

    void reload() {
        ++generation;
        live_fd = -1;  // gone with the old IOS, without a close
    }
    bool is_live(s32 fd) const { return fd >= 0 && fd == live_fd && live_generation == generation; }
} ios;

bool libogc_startup() {
    ++ios.libogc_startups;
    return true;
}
bool libogc_none() { return true; }
bool libogc_read(sec_t, sec_t, void*) { return true; }
bool libogc_write(sec_t, sec_t, const void*) { return true; }

void start_over() {
    riftwii::wii::d2x_sd_forget();
    ios = MockIos{};
}

// Opens the device as libfat's mount does, through the driver the loader
// picked: d2x's after a reload into the game's cIOS.
bool mount_through_d2x() {
    riftwii::wii::use_d2x_sd(true);
    return riftwii::wii::sd_interface()->startup();
}

bool read_one_sector() {
    alignas(32) static u8 sector[512];
    return riftwii::wii::d2x_sd_read(0, 1, sector);
}

// The bug the issue's probe showed: open, reload, open again. Before the
// fix the second open returned the first IOS's handle (IOS_Open calls=1)
// and the sector read failed.
void test_reload_between_opens_opens_again() {
    start_over();
    CHECK(mount_through_d2x());
    CHECK(ios.opens == 1 && ios.inits == 1);
    const s32 first_fd = riftwii::wii::d2x_sd_fd();
    CHECK(ios.is_live(first_fd));
    CHECK(read_one_sector());

    // What reload_ios (wii/launch/ios_reload.cpp) does: d2x_sd_forget
    // while the old IOS still answers, then the IOS goes. This test calls
    // the hook itself; that reload_ios calls it is checked by reading.
    riftwii::wii::d2x_sd_forget();
    CHECK(ios.closes == 1);  // closed by the IOS that opened it
    CHECK(riftwii::wii::d2x_sd_fd() == -1);
    CHECK(!riftwii::wii::using_d2x_sd());
    ios.reload();

    CHECK(mount_through_d2x());
    CHECK(ios.opens == 2 && ios.inits == 2);
    CHECK(ios.is_live(riftwii::wii::d2x_sd_fd()));
    CHECK(read_one_sector());
    CHECK(ios.reads == 2);
    CHECK(ios.dead_handle_calls == 0);
    CHECK(riftwii::wii::d2x_sd_last_error().empty());
    std::cout << "second open after a reload: IOS_Open calls=" << ios.opens << " inits=" << ios.inits
              << " fd=" << riftwii::wii::d2x_sd_fd() << " sector read=" << (ios.reads == 2) << '\n';
}

// Back to IOS 58 for the menu: the driver choice goes with the cIOS, so the
// menu mounts the card through libogc's driver, not d2x's.
void test_reload_drops_the_driver_choice() {
    start_over();
    CHECK(mount_through_d2x());
    riftwii::wii::d2x_sd_forget();
    ios.reload();
    ios.has_sdhc = false;  // IOS 58
    CHECK(!riftwii::wii::using_d2x_sd());
    CHECK(riftwii::wii::sd_interface()->startup());
    CHECK(ios.libogc_startups == 1);
    CHECK(ios.opens == 1);  // d2x's device was not asked for again
    CHECK(riftwii::wii::d2x_sd_fd() == -1);
}

// The new IOS's device opens but cannot start the card: the open fails
// with the reason kept for the launch's error, the half-open handle is
// closed, nothing is cached, and the next attempt asks IOS again.
void test_failed_start_after_reload_is_reported_and_retried() {
    start_over();
    CHECK(mount_through_d2x());
    riftwii::wii::d2x_sd_forget();
    ios.reload();
    ios.init_result = -4;
    CHECK(!mount_through_d2x());
    CHECK(ios.opens == 2 && ios.inits == 2 && ios.closes == 2);
    CHECK(riftwii::wii::d2x_sd_fd() == -1);
    CHECK(riftwii::wii::d2x_sd_last_error().find("could not start the card (-4)") != std::string::npos);
    CHECK(!read_one_sector());

    ios.init_result = 0;
    CHECK(mount_through_d2x());
    CHECK(ios.opens == 3 && ios.inits == 3);
    CHECK(riftwii::wii::d2x_sd_last_error().empty());
    CHECK(read_one_sector());
    CHECK(ios.dead_handle_calls == 0);
}

// A cIOS without d2x's SD device (the next slot holds something else).
void test_missing_device_after_reload() {
    start_over();
    CHECK(mount_through_d2x());
    riftwii::wii::d2x_sd_forget();
    ios.reload();
    ios.has_sdhc = false;
    std::string error;
    riftwii::wii::use_d2x_sd(true);
    CHECK(!riftwii::wii::d2x_sd_open(error));
    CHECK(error.find("cannot open d2x's SD device") != std::string::npos);
    CHECK(riftwii::wii::d2x_sd_last_error() == error);
    CHECK(ios.dead_handle_calls == 0);
}

// Forgetting twice (the menu reloads into IOS 58 twice when the first try
// lands elsewhere) or with nothing open closes nothing.
void test_forget_without_a_handle() {
    start_over();
    riftwii::wii::d2x_sd_forget();
    CHECK(ios.closes == 0);
    CHECK(mount_through_d2x());
    riftwii::wii::d2x_sd_forget();
    riftwii::wii::d2x_sd_forget();
    CHECK(ios.closes == 1);
    CHECK(ios.lock_depth == 0 && ios.deepest_lock >= 1);
}

// While d2x's FAT module holds the card for its NAND emulation, nothing of
// RiftWii's reaches it, on either driver, until the hold ends.
void test_held_card_refuses_until_the_reload() {
    for (bool d2x : {false, true}) {
        start_over();
        riftwii::wii::use_d2x_sd(d2x);
        riftwii::wii::hold_sd_for_d2x_fat(true);
        const DISC_INTERFACE* card = riftwii::wii::sd_interface();
        alignas(32) static u8 sector[512];
        CHECK(!card->startup());
        CHECK(!card->isInserted());
        CHECK(!card->readSectors(0, 1, sector));
        CHECK(!card->writeSectors(0, 1, sector));
        CHECK(!riftwii::wii::d2x_sd_read(0, 1, sector));
        CHECK(!riftwii::wii::d2x_sd_write(0, 1, sector));
        CHECK(!riftwii::wii::libogc_sd_start());
        CHECK(!riftwii::wii::libogc_sd_inserted());
        CHECK(ios.opens == 0 && ios.reads == 0 && ios.libogc_startups == 0);
        // d2x_sd_forget (before every reload) keeps it: a reload refused
        // early leaves the old IOS and its module running.
        riftwii::wii::d2x_sd_forget();
        CHECK(riftwii::wii::sd_held_by_d2x_fat());
        CHECK(!riftwii::wii::sd_interface()->startup());
        // reload_ios ends it once the old IOS is gone.
        riftwii::wii::hold_sd_for_d2x_fat(false);
        CHECK(riftwii::wii::sd_interface()->startup());
        CHECK(riftwii::wii::libogc_sd_start() && ios.libogc_startups == 2);
    }
}

}  // namespace

const DISC_INTERFACE __io_wiisd = {
    DEVICE_TYPE_WII_SD, FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_WII_SD,
    libogc_startup,     libogc_none,
    libogc_read,        libogc_write,
    libogc_none,        libogc_none,
};

s32 IOS_Open(const char* path, u32 mode) {
    CHECK(std::string(path) == "/dev/sdio/sdhc" && mode == 1);
    CHECK(ios.live_fd == -1);  // one handle at a time
    ++ios.opens;
    if (!ios.has_sdhc) return kIpcNotFound;
    // Numbers start over in each IOS, so the new handle may well have the
    // old one's number.
    ios.live_fd = 17;
    ios.live_generation = ios.generation;
    return ios.live_fd;
}

s32 IOS_Close(s32 fd) {
    if (!ios.is_live(fd)) {
        ++ios.dead_handle_calls;
        return kIpcNotFound;
    }
    ++ios.closes;
    ios.live_fd = -1;
    return 0;
}

s32 IOS_Ioctl(s32, s32, void*, u32, void*, u32) {
    CHECK(false);
    return -1;
}

s32 IOS_Ioctlv(s32 fd, s32 command, u32, u32, ioctlv*) {
    if (!ios.is_live(fd)) {
        ++ios.dead_handle_calls;
        return kIpcNotFound;
    }
    if (command == kInit) {
        ++ios.inits;
        return ios.init_result;
    }
    if (command == kRead) {
        ++ios.reads;
        return 0;
    }
    return 0;
}

s32 LWP_MutexInit(mutex_t* mutex, bool) {
    *mutex = 1;
    return 0;
}

s32 LWP_MutexLock(mutex_t) {
    ++ios.lock_depth;
    if (ios.lock_depth > ios.deepest_lock) ios.deepest_lock = ios.lock_depth;
    return 0;
}

s32 LWP_MutexUnlock(mutex_t) {
    --ios.lock_depth;
    return 0;
}

int main() {
    test_reload_between_opens_opens_again();
    test_reload_drops_the_driver_choice();
    test_failed_start_after_reload_is_reported_and_retried();
    test_missing_device_after_reload();
    test_forget_without_a_handle();
    test_held_card_refuses_until_the_reload();
    if (failures == 0) std::cout << "d2x SD handle is opened and started again after every IOS reload\n";
    return failures == 0 ? 0 : 1;
}
