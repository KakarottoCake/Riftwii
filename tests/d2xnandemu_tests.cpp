// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// d2x's NAND emulation from the SD card (wii/storage/d2xnandemu.cpp)
// against a mocked IOS, libfat and SD drivers: RiftWii lets go of the card
// completely before d2x's FAT module mounts it (#128), tells the module to
// let go again when the emulation does not come on, mounts the card again
// afterwards, and logs every step.
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
    unsigned d2x_shutdowns = 0;
    bool slot_open_at_first_mount = true;
    bool libfat_mounted_at_first_mount = true;
    bool log_open_at_first_mount = true;
    std::string set_path;
    std::vector<std::string> events;  // every call, in order
    std::vector<std::string> lines;   // boot.log
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
// libogc driver still running, d2x answering as it should.
void test_card_is_released_before_the_mount() {
    start_over();
    put_fat32_card();
    std::string error;
    CHECK(riftwii::wii::enable_d2x_saves("sd:/nands/01", error));
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
    CHECK(index_of("fat close") < index_of("fat mount"));
    CHECK(index_of("fat mount") < index_of("log reopen"));
    CHECK(count_of("fat 0xF0") == 1);
    CHECK(count_of("fat 0xF1") == 0);  // the emulation is on: d2x keeps the card
    CHECK(console.set_path == "/nands/01");
    CHECK(console.libfat_mounted && console.log_open);
    CHECK(logged("FAT32, 512-byte sectors, 64 sectors per cluster, in MBR partition 1 (type 0x0C) from sector 8"));
    CHECK(logged("libogc's SD driver was shut down (/dev/sdio/slot0 closed)"));
    CHECK(logged("the SD card was unmounted for d2x"));
    CHECK(logged("d2x's FAT module mounted the SD card (0)"));
    CHECK(logged("d2x turned the NAND emulation on for sd:/nands/01 (0)"));
    // Mounted again through d2x's own device: libogc's driver is not
    // started again under d2x's FAT module.
    CHECK(count_of("libogc startup") == 0);
    CHECK(console.d2x_driver && console.d2x_device_starts_asked == 1);
    CHECK(logged("the SD card was mounted again (libfat, through d2x's SD device; the rest of the launch reads the "
                 "card through it too)"));
    CHECK(logged("d2x NAND emulation from sd:/nands/01: FAT module 4, mount 0, set 0\n"));
    std::cout << "d2x's FAT module mounted the card with libogc's slot " << (console.slot_open_at_first_mount ? "open" : "closed")
              << ", libfat " << (console.libfat_mounted_at_first_mount ? "mounted" : "unmounted") << '\n';
}

// #128's answer on every try: the module is told to let go after each
// failure, the emulation is never asked for, and RiftWii's own save code
// gets the card back.
void test_refused_mount_lets_go_and_falls_back() {
    start_over();
    put_fat32_card();
    console.mount_answers = {-101, -101};
    std::string error;
    CHECK(!riftwii::wii::enable_d2x_saves("sd:/nands/01", error));
    CHECK(error == "d2x could not mount the SD card (FAT module 4, mount -101)");
    CHECK(count_of("fat 0xF0") == 2);
    CHECK(count_of("fat 0xF1") == 2);
    CHECK(index_of("fat 0xF1") < last_index_of("fat 0xF0"));  // let go before the second try
    CHECK(last_index_of("fat 0xF1") > last_index_of("fat 0xF0"));  // and again after it
    CHECK(count_of("fs set") == 0);
    CHECK(last_index_of("fat 0xF1") < index_of("fat mount"));
    CHECK(console.libfat_mounted && console.log_open);
    CHECK(logged("started the SD card but found no file system it could read on it (-101); it was told to let go"));
    CHECK(logged("d2x's FAT module started the SD card but found no file system it could read on it (-101) at the "
                 "second try"));
    CHECK(logged("the NAND emulation was not asked for"));
    CHECK(logged("told to let go of the SD card again (0), so RiftWii's own save code has it alone"));
    CHECK(logged("FAT module 4, mount -101, set -1\n"));
    // RiftWii's own save code: libogc's driver again, d2x's device untouched.
    CHECK(!console.d2x_driver && console.d2x_device_starts_asked == 0);
    CHECK(count_of("libogc startup") == 1);
    CHECK(logged("the SD card was mounted again (libfat, through libogc's SD driver)"));
}

// The emulation is on but d2x's own SD device does not start: libogc's
// driver mounts the card as before, and the log says so.
void test_d2x_device_refuses_after_the_emulation() {
    start_over();
    put_fat32_card();
    console.d2x_device_starts = false;
    std::string error;
    CHECK(riftwii::wii::enable_d2x_saves("sd:/nands/01", error));
    CHECK(console.d2x_device_starts_asked == 1 && console.d2x_forgets == 1);
    CHECK(!console.d2x_driver);
    CHECK(count_of("libogc startup") == 1);
    CHECK(count_of("fat 0xF1") == 0);  // the emulation stays on
    CHECK(logged("d2x's own SD device did not take the card (d2x's SD device could not start the card (-4))"));
    CHECK(logged("the SD card was mounted again (libfat, through libogc's SD driver)"));
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
    CHECK(count_of("fat 0xF0") == 1);
    CHECK(count_of("fat 0xF1") == 0);
    CHECK(console.d2x_shutdowns == 0 && console.d2x_forgets == 0);
    CHECK(console.d2x_driver && console.libfat_mounted);
    CHECK(!logged("at the second try"));
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
    CHECK(logged("could not start the SD card (-4)"));
    CHECK(logged("d2x's FAT module mounted the SD card (0) at the second try"));
}

void test_refused_emulation_lets_go() {
    start_over();
    put_fat32_card();
    console.set_answer = -1;
    std::string error;
    CHECK(!riftwii::wii::enable_d2x_saves("sd:/nands/01", error));
    CHECK(error == "d2x refused the NAND emulation (set -1)");
    CHECK(count_of("fat 0xF1") == 1);
    CHECK(index_of("fs set") < index_of("fat 0xF1"));
    CHECK(logged("d2x refused the NAND emulation for sd:/nands/01 (-1)"));
    CHECK(console.libfat_mounted);
}

// The game's own IOS, last in a channel's list: no module, and the card
// and boot.log are not touched.
void test_ios_without_the_module_leaves_the_card() {
    start_over();
    console.has_fat_module = false;
    std::string error;
    CHECK(!riftwii::wii::enable_d2x_saves("sd:/nands/01", error));
    CHECK(error == "IOS56 has no d2x FAT module (FAT module -6)");
    CHECK(count_of("log close") == 0 && count_of("fat unmount") == 0 && count_of("libogc shutdown") == 0);
    CHECK(console.libogc_slot_open && console.libfat_mounted && console.log_open);
    CHECK(logged("IOS56 rev 5918 has no d2x FAT module (opening it answered -6)"));
}

// A game on the card itself (d2x base 56 or 57): d2x's own SD device
// carries it and stays open.
void test_d2x_sd_device_stays_open() {
    start_over();
    put_fat32_card();
    console.d2x_driver = true;
    console.libogc_slot_open = false;
    std::string error;
    CHECK(riftwii::wii::enable_d2x_saves("sd:/riftwii/saves/RMCP01/fresh", error));
    CHECK(console.d2x_shutdowns == 0);
    CHECK(count_of("libogc shutdown") == 0 && count_of("libogc startup") == 0);
    CHECK(logged("d2x's own SD device (/dev/sdio/sdhc) stayed open"));
    CHECK(logged("mounted again (libfat, through d2x's SD device)"));
}

void test_remount_failure_is_the_error() {
    start_over();
    put_fat32_card();
    console.remount_works = false;
    std::string error;
    CHECK(!riftwii::wii::enable_d2x_saves("sd:/nands/01", error));
    CHECK(error == "the SD card did not mount again after d2x took it");
}

void test_not_a_folder_of_the_card() {
    start_over();
    std::string error;
    CHECK(!riftwii::wii::enable_d2x_saves("usb:/nands/01", error));
    CHECK(error == "not a folder of the SD card");
    CHECK(console.events.empty());
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
    console.d2x_driver = false;
}
const std::string& d2x_sd_last_error() {
    static const std::string refused = "d2x's SD device could not start the card (-4)";
    static const std::string none;
    return console.d2x_device_starts ? none : refused;
}

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
    // A line logged while boot.log is closed is lost on the Wii.
    if (console.log_open) console.lines.push_back(text);
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

s32 IOS_GetVersion() { return console.has_fat_module ? 251 : 56; }
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
        return 0;
    }
    if (fd == kFsFd && command == kSetMode) {
        CHECK(in == 2);
        console.events.push_back("fs set");
        console.set_path = static_cast<const char*>(vectors[1].data);
        return console.set_answer;
    }
    CHECK(false);
    return -1;
}

int main() {
    test_card_is_released_before_the_mount();
    test_refused_mount_lets_go_and_falls_back();
    test_d2x_device_refuses_after_the_emulation();
    test_d2x_sd_device_mount_failure_is_left_alone();
    test_second_try_mounts();
    test_refused_emulation_lets_go();
    test_ios_without_the_module_leaves_the_card();
    test_d2x_sd_device_stays_open();
    test_remount_failure_is_the_error();
    test_not_a_folder_of_the_card();
    if (failures == 0) std::cout << "the SD card is released before d2x's FAT module mounts it, and back afterwards\n";
    return failures == 0 ? 0 : 1;
}
