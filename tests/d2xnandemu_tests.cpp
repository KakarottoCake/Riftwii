// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// d2x's NAND emulation from the SD card (wii/storage/d2xnandemu.cpp)
// against a mocked IOS, libfat and SD drivers: RiftWii lets go of the card
// completely before d2x's FAT module mounts it (#128), tells the module to
// let go again when the emulation does not come on and mounts the card
// again then, stays off the card while the emulation is on, and logs every
// step. The mock proves the order of the calls and the state left behind;
// it cannot show that d2x's -101 goes away on a Wii, which only hardware
// can.
#include "d2xnandemu.hpp"

#include <gccore.h>
#include <fat.h>
#include <sdcard/wiisd_io.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iostream>
#include <string>
#include <vector>

#include "d2xsd.hpp"
#include "log.hpp"
#include "rtable.h"

namespace {

int failures = 0;
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "Failed: " #condition " at " << __LINE__ << '\n'; \
            ++failures; \
        } \
    } while (0)

constexpr s32 kFatFd = 4;
constexpr s32 kFsFd = 9;
constexpr s32 kMountSd = 0xF0;
constexpr s32 kUnmountSd = 0xF1;
constexpr s32 kSetMode = 100;
constexpr s32 kIpcNotFound = -6;

// The console as one launch sees it.
struct Console {
    s32 ios = 251;
    bool has_fat_module = true;
    bool d2x_driver = false;          // a game on the card: d2x's /dev/sdio/sdhc
    bool libogc_slot_open = true;     // the menu left libogc's driver running
    bool libfat_mounted = true;
    bool log_open = true;
    std::deque<s32> mount_answers;    // one per 0xF0, then 0
    s32 set_answer = 0;
    bool remount_works = true;
    bool d2x_device_starts = true;    // d2x's /dev/sdio/sdhc takes the card
    unsigned d2x_device_starts_asked = 0;
    unsigned d2x_forgets = 0;
    bool held = false;                // hold_sd_for_d2x_fat
    s32 unmount_answer = 0;           // every 0xF1 once unmount_answers is used up
    std::deque<s32> unmount_answers;
    u32 set_mode = 0;
    unsigned d2x_shutdowns = 0;
    bool slot_open_at_first_mount = true;
    bool libfat_mounted_at_first_mount = true;
    bool log_open_at_first_mount = true;
    std::string set_path;
    std::vector<std::string> events;  // every call, in order
    std::vector<std::string> lines;   // every line logged
    std::vector<std::string> card_lines;  // the ones written while boot.log was open
    std::vector<std::uint8_t> card = std::vector<std::uint8_t>(2 * 512 * 16, 0);
} console;

void start_over() { console = Console{}; }

// A FAT32 card with an MBR: partition 1 (type 0x0C) from sector 8.
void put_fat32_card() {
    std::uint8_t* mbr = console.card.data();
    mbr[446 + 4] = 0x0C;
    mbr[446 + 8] = 8;
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    std::uint8_t* boot = console.card.data() + 8 * 512;
    boot[0] = 0xEB;
    boot[11] = 0x00;
    boot[12] = 0x02;  // 512 bytes per sector
    boot[13] = 64;
    std::memcpy(boot + 82, "FAT32   ", 8);
    boot[510] = 0x55;
    boot[511] = 0xAA;
}

std::size_t index_of(const std::string& event) {
    for (std::size_t i = 0; i < console.events.size(); ++i)
        if (console.events[i] == event) return i;
    return console.events.size();
}

std::size_t last_index_of(const std::string& event) {
    std::size_t found = console.events.size();
    for (std::size_t i = 0; i < console.events.size(); ++i)
        if (console.events[i] == event) found = i;
    return found;
}

unsigned count_of(const std::string& event) {
    unsigned n = 0;
    for (const std::string& e : console.events) n += e == event ? 1 : 0;
    return n;
}

bool logged(const std::string& text) {
    for (const std::string& line : console.lines)
        if (line.find(text) != std::string::npos) return true;
    return false;
}

bool logged_on_card(const std::string& text) {
    for (const std::string& line : console.card_lines)
        if (line.find(text) != std::string::npos) return true;
    return false;
}

bool card_read(sec_t sector, sec_t count, void* buffer) {
    if ((sector + count) * 512 > console.card.size()) return false;
    std::memcpy(buffer, console.card.data() + sector * 512, count * 512);
    return true;
}

bool libogc_startup() {
    console.events.push_back("libogc startup");
    console.libogc_slot_open = true;
    return true;
}
bool libogc_shutdown() {
    console.events.push_back("libogc shutdown");
    console.libogc_slot_open = false;
    return true;
}
bool always() { return true; }
bool no_write(sec_t, sec_t, const void*) { return false; }

bool d2x_startup() {
    console.events.push_back("d2x device startup");
    ++console.d2x_device_starts_asked;
    return console.d2x_device_starts;
}

bool d2x_shutdown() {
    ++console.d2x_shutdowns;
    return true;
}

const DISC_INTERFACE d2x_io = {
    DEVICE_TYPE_WII_SD, FEATURE_MEDIUM_CANREAD | FEATURE_WII_SD, d2x_startup, always, card_read, no_write, always,
    d2x_shutdown,
};

// The plain launch: a game on the USB drive (or a channel), the menu's
// libogc driver still running, d2x answering as it should. With the
// emulation on, RiftWii stays off the card: no libfat, no boot.log, no
// driver of its own started again, and every way to the card refuses.
void test_card_is_released_before_the_mount() {
    start_over();
    put_fat32_card();
    std::string error;
    bool card_back = true;
    CHECK(riftwii::wii::enable_d2x_saves("sd:/nands/01", error, riftwii::wii::D2xNandScope::GamePaths, &card_back));
    CHECK(error.empty());
    // libogc's driver was gone, libfat unmounted and boot.log closed when
    // the module mounted the card.
    CHECK(!console.slot_open_at_first_mount);
    CHECK(!console.libfat_mounted_at_first_mount);
    CHECK(!console.log_open_at_first_mount);
    CHECK(index_of("log close") < index_of("fat unmount"));
    CHECK(index_of("fat unmount") < index_of("libogc shutdown"));
    CHECK(index_of("libogc shutdown") < index_of("fat 0xF0"));
    CHECK(index_of("fat 0xF0") < index_of("fs set"));
    CHECK(index_of("fs set") < index_of("fat close"));
    CHECK(count_of("fat 0xF0") == 1);
    CHECK(count_of("fat 0xF1") == 0);  // the emulation is on: d2x keeps the card
    CHECK(console.set_path == "/nands/01");
    CHECK(console.set_mode == 1);  // the game's paths only
    // The state: d2x has the card, and RiftWii has nothing on it.
    CHECK(console.held);
    CHECK(!card_back);
    CHECK(count_of("fat mount") == 0 && count_of("log reopen") == 0);
    CHECK(!console.libfat_mounted && !console.log_open && !console.libogc_slot_open);
    CHECK(count_of("libogc startup") == 0 && console.d2x_device_starts_asked == 0);
    CHECK(count_of("use d2x device") == 0);
    CHECK(logged_on_card("FAT32, 512-byte sectors, 64 sectors per cluster, in MBR partition 1 (type 0x0C) from sector 8"));
    // The rest waits in memory and on screen: not on the card.
    CHECK(logged("libogc's SD driver was shut down (/dev/sdio/slot0 closed)"));
    CHECK(logged("d2x's FAT module mounted the SD card (0)"));
    CHECK(logged("d2x turned the NAND emulation of the game's paths on for sd:/nands/01 (0)"));
    CHECK(logged("the SD card is d2x's now"));
    CHECK(logged("FAT module 4, mount 0, set 0, mode 0x1\n"));
    CHECK(!logged_on_card("d2x's FAT module mounted the SD card"));
    std::cout << "d2x's FAT module mounted the card with libogc's slot " << (console.slot_open_at_first_mount ? "open" : "closed")
              << ", libfat " << (console.libfat_mounted_at_first_mount ? "mounted" : "unmounted") << '\n';
}

// A channel of an emulated NAND: the whole NAND, d2x's full flag.
void test_whole_nand_sets_the_full_flag() {
    start_over();
    put_fat32_card();
    std::string error;
    CHECK(riftwii::wii::enable_d2x_saves("sd:/nands/01", error, riftwii::wii::D2xNandScope::WholeNand));
    CHECK(console.set_mode == 0x101);
    CHECK(logged("NAND emulation of the whole NAND (full flag 0x100) on for sd:/nands/01"));
    CHECK(logged("mode 0x101\n"));
}

// A channel launch remembers an IOS whose module could not mount the card,
// even after its retry, and doesn't touch the card on it again. Another IOS
// and a new launch still get their chance.
void test_channel_mount_failures_are_kept_for_one_launch() {
    start_over();
    put_fat32_card();
    riftwii::wii::D2xNandMountFailures mount_failures;
    console.mount_answers = {-101, -101};
    std::string error;
    bool card_back = true;
    CHECK(!riftwii::wii::enable_d2x_saves("sd:/nands/01", error,
          riftwii::wii::D2xNandScope::WholeNand, &card_back, &mount_failures));
    CHECK(mount_failures.ios[251]);
    CHECK(count_of("fat 0xF0") == 2);
    CHECK(count_of("fat 0xF1") == 2);
    CHECK(card_back && !console.held);
    CHECK(count_of("fs set") == 0);

    const std::size_t events_before_retry = console.events.size();
    CHECK(!riftwii::wii::enable_d2x_saves("sd:/nands/01", error,
          riftwii::wii::D2xNandScope::WholeNand, &card_back, &mount_failures));
    CHECK(error.find("already failed") != std::string::npos);
    CHECK(console.events.size() == events_before_retry);
    CHECK(card_back);

    console.ios = 250;
    error.clear();
    CHECK(riftwii::wii::enable_d2x_saves("sd:/nands/01", error,
          riftwii::wii::D2xNandScope::WholeNand, &card_back, &mount_failures));
    CHECK(!mount_failures.ios[250]);
    CHECK(!card_back && console.held);
    CHECK(riftwii::wii::release_d2x_saves());

    start_over();
    riftwii::wii::D2xNandMountFailures next_launch;
    CHECK(riftwii::wii::enable_d2x_saves("sd:/nands/01", error,
          riftwii::wii::D2xNandScope::WholeNand, nullptr, &next_launch));
}

// The retry the first unmount allows still runs for a channel launch: a
// mount that works the second time is not remembered as a failure.
void test_channel_mount_retry_still_runs() {
    start_over();
    put_fat32_card();
    riftwii::wii::D2xNandMountFailures mount_failures;
    console.mount_answers = {-101, 0};
    std::string error;
    bool card_back = true;
    CHECK(riftwii::wii::enable_d2x_saves("sd:/nands/01", error,
          riftwii::wii::D2xNandScope::WholeNand, &card_back, &mount_failures));
    CHECK(!mount_failures.ios[251]);
    CHECK(count_of("fat 0xF0") == 2);
    CHECK(console.held && !card_back);
    CHECK(riftwii::wii::release_d2x_saves());
}

void test_channel_mount_failure_keeps_the_card_when_unmount_fails() {
    start_over();
    console.mount_answers = {-101};
    console.unmount_answer = -6;
    riftwii::wii::D2xNandMountFailures mount_failures;
    std::string error;
    bool card_back = true;
    CHECK(!riftwii::wii::enable_d2x_saves("sd:/nands/01", error,
          riftwii::wii::D2xNandScope::WholeNand, &card_back, &mount_failures));
    CHECK(mount_failures.ios[251]);
    CHECK(count_of("fat 0xF0") == 1 && count_of("fat 0xF1") == 1);
    CHECK(console.held && !card_back);
    CHECK(count_of("fat mount") == 0 && count_of("fs set") == 0);
}

// #128's answer on every try: the module is told to let go after each
// failure, the emulation is never asked for, and RiftWii's own save code
// gets the card back.
void test_refused_mount_lets_go_and_falls_back() {
    start_over();
    put_fat32_card();
    console.mount_answers = {-101, -101};
    std::string error;
    bool card_back = false;
    CHECK(!riftwii::wii::enable_d2x_saves("sd:/nands/01", error, riftwii::wii::D2xNandScope::WholeNand, &card_back));
    CHECK(error == "d2x could not mount the SD card (-101)");
    CHECK(count_of("fat 0xF0") == 2);
    CHECK(count_of("fat 0xF1") == 2);
    CHECK(index_of("fat 0xF1") < last_index_of("fat 0xF0"));  // let go before the second try
    CHECK(last_index_of("fat 0xF1") > last_index_of("fat 0xF0"));  // and again after it
    CHECK(count_of("fs set") == 0);
    CHECK(last_index_of("fat 0xF1") < index_of("fat mount"));
    CHECK(console.libfat_mounted && console.log_open && card_back);
    CHECK(!console.held);
    CHECK(logged_on_card("mounted no file system on the SD card: a read of the card failed, or no partition held a "
                         "FAT volume it could use (-101); it was told to let go"));
    CHECK(logged_on_card("(-101) at the second try"));
    CHECK(logged_on_card("the NAND emulation was not asked for"));
    CHECK(logged_on_card("told to let go of the SD card again (0), so RiftWii's own save code has it alone"));
    CHECK(logged_on_card("FAT module 4, mount -101, set -1, mode 0x101\n"));
    // RiftWii's own save code: libogc's driver again, d2x's device untouched.
    CHECK(!console.d2x_driver && console.d2x_device_starts_asked == 0);
    CHECK(count_of("libogc startup") == 1);
    CHECK(logged_on_card("the SD card was mounted again (libfat, through libogc's SD driver)"));
}

// A game on the card, carried by d2x's own device, and the mount fails:
// asked once, never told to let go (that would stop the card under the
// device that reads the game).
void test_d2x_sd_device_mount_failure_is_left_alone() {
    start_over();
    put_fat32_card();
    console.d2x_driver = true;
    console.libogc_slot_open = false;
    console.mount_answers = {-101};
    std::string error;
    CHECK(!riftwii::wii::enable_d2x_saves("sd:/riftwii/saves/RMCP01/fresh", error));
    CHECK(error == "d2x could not mount the SD card (-101)");
    CHECK(count_of("fat 0xF0") == 1);
    CHECK(count_of("fat 0xF1") == 0);
    CHECK(console.d2x_shutdowns == 0 && console.d2x_forgets == 0);
    CHECK(console.d2x_driver && console.libfat_mounted && !console.held);
    CHECK(!logged("at the second try"));
}

// The same with the first answer -4 (the module's driver did not start the
// card): still no second try and no 0xF1 under d2x's own device.
void test_d2x_sd_device_first_mount_minus_4() {
    start_over();
    put_fat32_card();
    console.d2x_driver = true;
    console.libogc_slot_open = false;
    console.mount_answers = {-4};
    std::string error;
    CHECK(!riftwii::wii::enable_d2x_saves("sd:/riftwii/saves/RMCP01/fresh", error));
    CHECK(error == "d2x could not mount the SD card (-4)");
    CHECK(count_of("fat 0xF0") == 1 && count_of("fat 0xF1") == 0);
    CHECK(console.d2x_shutdowns == 0);
    CHECK(logged_on_card("d2x's FAT module could not start the SD card (-4)\n"));
}

// d2x's own device carries the game, the module mounts, the emulation is
// refused: the module (which serves nothing) is not told to let go, and
// libfat goes back on d2x's device.
void test_d2x_sd_device_refused_emulation() {
    start_over();
    put_fat32_card();
    console.d2x_driver = true;
    console.libogc_slot_open = false;
    console.set_answer = -1;
    std::string error;
    CHECK(!riftwii::wii::enable_d2x_saves("sd:/riftwii/saves/RMCP01/fresh", error));
    CHECK(error == "d2x refused the NAND emulation (-1)");
    CHECK(count_of("fat 0xF1") == 0 && console.d2x_shutdowns == 0);
    CHECK(!console.held && console.libfat_mounted && console.d2x_driver);
    CHECK(console.d2x_device_starts_asked == 1 && count_of("libogc startup") == 0);
    CHECK(logged_on_card("mounted again (libfat, through d2x's SD device)"));
}

// d2x's own device, the mount fails and the device does not take the card
// again either: the mount's failure is the error, the card stays off, and
// libogc's driver is not started under d2x's device.
void test_d2x_sd_device_and_remount_both_fail() {
    start_over();
    put_fat32_card();
    console.d2x_driver = true;
    console.libogc_slot_open = false;
    console.mount_answers = {-101};
    console.d2x_device_starts = false;
    std::string error;
    bool card_back = true;
    CHECK(!riftwii::wii::enable_d2x_saves("sd:/riftwii/saves/RMCP01/fresh", error,
                                          riftwii::wii::D2xNandScope::GamePaths, &card_back));
    CHECK(error == "d2x could not mount the SD card (-101)");
    CHECK(!card_back && !console.libfat_mounted && !console.log_open);
    CHECK(count_of("libogc startup") == 0 && count_of("fat 0xF1") == 0 && !console.held);
}

void test_second_try_mounts() {
    start_over();
    put_fat32_card();
    console.mount_answers = {-4, 0};
    std::string error;
    CHECK(riftwii::wii::enable_d2x_saves("sd:/nands/01", error));
    CHECK(count_of("fat 0xF0") == 2);
    CHECK(count_of("fat 0xF1") == 1);
    CHECK(count_of("fs set") == 1);
    CHECK(console.held);
    CHECK(logged("could not start the SD card (-4)"));
    CHECK(logged("d2x's FAT module mounted the SD card (0) at the second try"));
}

void test_refused_emulation_lets_go() {
    start_over();
    put_fat32_card();
    console.set_answer = -1;
    std::string error;
    CHECK(!riftwii::wii::enable_d2x_saves("sd:/nands/01", error));
    CHECK(error == "d2x refused the NAND emulation (-1)");
    CHECK(count_of("fat 0xF1") == 1);
    CHECK(index_of("fs set") < index_of("fat 0xF1"));
    CHECK(logged_on_card("d2x refused the NAND emulation for sd:/nands/01 (-1)"));
    CHECK(console.libfat_mounted && !console.held);
}

// The game's own IOS, last in a channel's list: no module, and the card
// and boot.log are not touched.
void test_ios_without_the_module_leaves_the_card() {
    start_over();
    console.has_fat_module = false;
    std::string error;
    bool card_back = false;
    CHECK(!riftwii::wii::enable_d2x_saves("sd:/nands/01", error, riftwii::wii::D2xNandScope::WholeNand, &card_back));
    CHECK(error == "IOS56 has no d2x FAT module (-6)");
    CHECK(!card_back);  // left as the caller set it
    CHECK(count_of("log close") == 0 && count_of("fat unmount") == 0 && count_of("libogc shutdown") == 0);
    CHECK(console.libogc_slot_open && console.libfat_mounted && console.log_open);
    CHECK(logged_on_card("IOS56 rev 5918 has no d2x FAT module (opening it answered -6)"));
}

// A game on the card itself (d2x base 56 or 57): d2x's own SD device
// carries it and stays open; with the emulation on RiftWii stays off the
// card, and the module is never told to let go under that device.
void test_d2x_sd_device_stays_open() {
    start_over();
    put_fat32_card();
    console.d2x_driver = true;
    console.libogc_slot_open = false;
    std::string error;
    CHECK(riftwii::wii::enable_d2x_saves("sd:/riftwii/saves/RMCP01/fresh", error));
    CHECK(console.d2x_shutdowns == 0);
    CHECK(count_of("libogc shutdown") == 0 && count_of("libogc startup") == 0);
    CHECK(console.held && count_of("fat mount") == 0);
    CHECK(logged("d2x's own SD device (/dev/sdio/sdhc) stayed open"));
    CHECK(count_of("fat 0xF1") == 0);
    // The launch failed: no game reads through d2x's device any more, so
    // the module lets go and the device goes too (libogc's driver next).
    CHECK(riftwii::wii::release_d2x_saves());
    CHECK(count_of("fat 0xF1") == 1 && !console.held);
    CHECK(console.d2x_forgets == 1 && !console.d2x_driver);
}

// Emulation off and libfat does not take the card again.
void test_remount_failure_after_a_refusal() {
    start_over();
    put_fat32_card();
    console.set_answer = -1;
    console.remount_works = false;
    std::string error;
    bool card_back = true;
    CHECK(!riftwii::wii::enable_d2x_saves("sd:/nands/01", error, riftwii::wii::D2xNandScope::GamePaths, &card_back));
    CHECK(error == "d2x refused the NAND emulation (-1)");
    CHECK(!card_back && !console.held && !console.log_open);
}

// The launch does not go on with the emulation: the module unmounts the
// card (and writes what it holds) and RiftWii may use it again.
// As boot_channel does after ES refused the title on an IOS where the
// emulation came on, and a failed launch's session: emulation off (mode
// 0, no path) first, then the module's unmount, before anything reloads.
// (boot_channel itself is not built for the host; this is the call it makes.)
void test_release_after_the_emulation() {
    start_over();
    put_fat32_card();
    std::string error;
    CHECK(riftwii::wii::enable_d2x_saves("sd:/nands/01", error, riftwii::wii::D2xNandScope::WholeNand));
    CHECK(console.held);
    const std::size_t before = console.events.size();
    CHECK(riftwii::wii::release_d2x_saves());
    CHECK(count_of("fs set") == 2 && last_index_of("fs set") >= before);
    CHECK(console.set_mode == 0 && console.set_path.empty());
    CHECK(last_index_of("fs set") < last_index_of("fat 0xF1"));
    CHECK(count_of("fat 0xF1") == 1 && !console.held);
    CHECK(console.d2x_forgets == 0);  // libogc's driver was the card's: no d2x device to let go
    CHECK(logged("turned off (0) and its FAT module let go of the SD card (0)"));
    // The hand-off's summary again, for a failure's boot.log.
    CHECK(logged("the d2x hand-off earlier in this launch: sd:/nands/01 on IOS251: the card has FAT32"));
    CHECK(logged("mount 0 (first 0), set 0, mode 0x101"));
    // Nothing to do a second time.
    CHECK(riftwii::wii::release_d2x_saves());
    CHECK(count_of("fat 0xF1") == 1);
}

// The module does not let go: the card stays held, and the log says
// RiftWii stays off it until its restart reloads IOS.
void test_release_refused_keeps_the_hold() {
    start_over();
    put_fat32_card();
    std::string error;
    CHECK(riftwii::wii::enable_d2x_saves("sd:/nands/01", error));
    console.unmount_answer = -1;
    CHECK(!riftwii::wii::release_d2x_saves());
    CHECK(console.held);
    CHECK(logged("did not let go of the SD card (-1; emulation off: 0), so RiftWii stays off the card until it "
                 "restarts"));
}

void test_release_keeps_the_backing_card_when_emulation_will_not_stop() {
    start_over();
    std::string error;
    CHECK(riftwii::wii::enable_d2x_saves("sd:/nands/01", error));
    console.set_answer = -101;
    CHECK(!riftwii::wii::release_d2x_saves());
    CHECK(console.held);
    CHECK(count_of("fat 0xF1") == 0);
    CHECK(console.d2x_forgets == 0);
    CHECK(logged("NAND emulation could not be turned off (-101"));
    CHECK(!logged("NAND emulation was turned off"));
}

// The 0xF1 before the second try fails: no second mount over a card the
// module may still hold, the card is treated as d2x's (held, libfat off)
// until a release works.
void test_failed_unmount_before_the_retry_holds_the_card() {
    start_over();
    put_fat32_card();
    console.mount_answers = {-101};
    console.unmount_answers = {-1};
    std::string error;
    bool card_back = true;
    CHECK(!riftwii::wii::enable_d2x_saves("sd:/nands/01", error, riftwii::wii::D2xNandScope::GamePaths, &card_back));
    CHECK(error == "d2x could not mount the SD card (-101), and its FAT module did not let go of the card (-1)");
    CHECK(count_of("fat 0xF0") == 1 && count_of("fat 0xF1") == 1 && count_of("fs set") == 0);
    CHECK(console.held && !card_back);
    CHECK(count_of("fat mount") == 0 && !console.libfat_mounted && !console.log_open);
    CHECK(logged("did not let go of the card when told to (-1), so it was not asked again"));
    CHECK(logged("may still have the SD card, so RiftWii stays off it"));
    // A later release that works gives the card back.
    CHECK(riftwii::wii::release_d2x_saves());
    CHECK(!console.held);
}

// The emulation is refused and the module will not let go afterwards:
// held, libfat off, and a release that fails again keeps it so.
void test_failed_final_unmount_holds_the_card() {
    start_over();
    put_fat32_card();
    console.set_answer = -1;
    console.unmount_answer = -1;
    std::string error;
    CHECK(!riftwii::wii::enable_d2x_saves("sd:/nands/01", error));
    CHECK(error == "d2x could not turn the NAND emulation on (-1), and its FAT module did not let go of the card (-1)");
    CHECK(console.held && count_of("fat mount") == 0 && !console.libfat_mounted);
    CHECK(logged("did not let go of the SD card again (-1)"));
    CHECK(!riftwii::wii::release_d2x_saves());
    CHECK(console.held && count_of("fat mount") == 0);
    CHECK(logged("so RiftWii stays off the card until it restarts"));
}

// Which launches keep d2x's emulation off because RiftWii needs the card.
void test_what_needs_the_card() {
    using riftwii::wii::packs_need_sd;
    using riftwii::wii::entries_need_sd;
    CHECK(!packs_need_sd({}));
    CHECK(!packs_need_sd({"usb:/riivolution/a.xml", "usb:/riivolution/b.xml"}));
    CHECK(!packs_need_sd({"usb2:/riivolution/a.xml", "usb1:/riivolution/b.xml"}));
    CHECK(packs_need_sd({"usb:/riivolution/a.xml", "sd:/riivolution/b.xml"}));
    CHECK(packs_need_sd({"riifs:/server/pack.xml"}));  // anything not on the USB drive counts
    std::vector<rt_entry> entries(2);
    entries[0].kind = RT_KIND_USB;
    entries[1].kind = RT_KIND_DISC;
    CHECK(!entries_need_sd(entries));
    entries[1].kind = RT_KIND_SD;
    CHECK(entries_need_sd(entries));
}

void test_not_a_folder_of_the_card() {
    start_over();
    std::string error;
    bool card_back = false;  // as the caller had it: left so
    CHECK(!riftwii::wii::enable_d2x_saves("usb:/nands/01", error, riftwii::wii::D2xNandScope::WholeNand, &card_back));
    CHECK(error == "RiftWii turns d2x's NAND emulation on only from a folder of the SD card, and usb:/nands/01 is "
                   "not one");
    CHECK(!card_back);
    CHECK(console.events.empty());
}

// d2x takes 255 characters and a terminator: a longer path is refused
// before anything is touched, rather than cut into another folder.
void test_path_over_255_is_refused() {
    start_over();
    put_fat32_card();
    std::string error;
    const std::string longest = "sd:/" + std::string(254, 'a');  // "/" and 254 letters: 255
    CHECK(riftwii::wii::enable_d2x_saves(longest, error));
    CHECK(console.set_path == longest.substr(3));
    start_over();
    put_fat32_card();
    bool card_back = false;
    CHECK(!riftwii::wii::enable_d2x_saves(longest + "b", error, riftwii::wii::D2xNandScope::GamePaths, &card_back));
    CHECK(error == "the folder's path is 256 characters long, and d2x takes at most 255");
    CHECK(!card_back && console.events.empty() && console.libfat_mounted && console.log_open);
}

}  // namespace

const DISC_INTERFACE __io_wiisd = {
    DEVICE_TYPE_WII_SD, FEATURE_MEDIUM_CANREAD | FEATURE_WII_SD, libogc_startup, always, card_read, no_write, always,
    libogc_shutdown,
};

namespace riftwii::wii {
const DISC_INTERFACE* sd_interface() { return console.d2x_driver ? &d2x_io : &__io_wiisd; }
bool using_d2x_sd() { return console.d2x_driver; }
void use_d2x_sd(bool on) {
    console.events.push_back(on ? "use d2x device" : "use libogc driver");
    console.d2x_driver = on;
}
void d2x_sd_forget() {
    ++console.d2x_forgets;
    console.d2x_driver = false;  // the hold stays, as in d2xsd.cpp
}
void hold_sd_for_d2x_fat(bool held) {
    console.events.push_back(held ? "hold" : "unhold");
    console.held = held;
}
bool sd_held_by_d2x_fat() { return console.held; }

void LogClose() {
    console.events.push_back("log close");
    console.log_open = false;
}
void LogReopen() {
    console.events.push_back("log reopen");
    console.log_open = true;
}
void logf(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    // A line logged while boot.log is closed waits in memory on the Wii
    // and reaches the card only if boot.log reopens.
    console.lines.push_back(text);
    if (console.log_open) console.card_lines.push_back(text);
}
}  // namespace riftwii::wii

void fatUnmount(const char*) {
    console.events.push_back("fat unmount");
    console.libfat_mounted = false;
}

bool fatMountSimple(const char*, const DISC_INTERFACE* interface) {
    console.events.push_back("fat mount");
    if (!interface->startup()) return false;
    console.libfat_mounted = console.remount_works;
    return console.remount_works;
}

s32 IOS_GetVersion() { return console.has_fat_module ? console.ios : 56; }
s32 IOS_GetRevision() { return console.has_fat_module ? 21011 : 5918; }

s32 IOS_Open(const char* path, u32) {
    const std::string name = path;
    if (name == "fat") {
        console.events.push_back("fat open");
        return console.has_fat_module ? kFatFd : kIpcNotFound;
    }
    if (name == "/dev/fs") return kFsFd;
    CHECK(false);
    return kIpcNotFound;
}

s32 IOS_Close(s32 fd) {
    if (fd == kFatFd) console.events.push_back("fat close");
    return 0;
}

s32 IOS_Ioctl(s32, s32, void*, u32, void*, u32) {
    CHECK(false);
    return -1;
}

s32 IOS_Ioctlv(s32 fd, s32 command, u32 in, u32, ioctlv* vectors) {
    if (fd == kFatFd && command == kMountSd) {
        if (count_of("fat 0xF0") == 0) {
            console.slot_open_at_first_mount = console.libogc_slot_open;
            console.libfat_mounted_at_first_mount = console.libfat_mounted;
            console.log_open_at_first_mount = console.log_open;
        }
        console.events.push_back("fat 0xF0");
        if (console.mount_answers.empty()) return 0;
        const s32 answer = console.mount_answers.front();
        console.mount_answers.pop_front();
        return answer;
    }
    if (fd == kFatFd && command == kUnmountSd) {
        console.events.push_back("fat 0xF1");
        if (console.unmount_answers.empty()) return console.unmount_answer;
        const s32 answer = console.unmount_answers.front();
        console.unmount_answers.pop_front();
        return answer;
    }
    if (fd == kFsFd && command == kSetMode) {
        CHECK(in == 2);
        console.events.push_back("fs set");
        console.set_path = static_cast<const char*>(vectors[1].data);
        std::memcpy(&console.set_mode, vectors[0].data, sizeof(console.set_mode));
        return console.set_answer;
    }
    CHECK(false);
    return -1;
}

int main() {
    test_channel_mount_failures_are_kept_for_one_launch();
    test_channel_mount_retry_still_runs();
    test_channel_mount_failure_keeps_the_card_when_unmount_fails();
    test_card_is_released_before_the_mount();
    test_whole_nand_sets_the_full_flag();
    test_refused_mount_lets_go_and_falls_back();
    test_d2x_sd_device_mount_failure_is_left_alone();
    test_d2x_sd_device_first_mount_minus_4();
    test_d2x_sd_device_refused_emulation();
    test_d2x_sd_device_and_remount_both_fail();
    test_second_try_mounts();
    test_refused_emulation_lets_go();
    test_ios_without_the_module_leaves_the_card();
    test_d2x_sd_device_stays_open();
    test_remount_failure_after_a_refusal();
    test_release_after_the_emulation();
    test_release_refused_keeps_the_hold();
    test_release_keeps_the_backing_card_when_emulation_will_not_stop();
    test_what_needs_the_card();
    test_failed_unmount_before_the_retry_holds_the_card();
    test_failed_final_unmount_holds_the_card();
    test_not_a_folder_of_the_card();
    test_path_over_255_is_refused();
    if (failures == 0) std::cout << "the SD card is released before d2x's FAT module mounts it, and stays d2x's while the emulation is on\n";
    return failures == 0 ? 0 : 1;
}
