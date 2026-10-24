// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2011 Dimok
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "d2xnandemu.hpp"

#include <fat.h>
#include <gccore.h>
#include <malloc.h>
#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "d2xsd.hpp"
#include "log.hpp"
#include "riftwii/cardvolume.hpp"

namespace riftwii::wii {
namespace {

// d2x's FFS plugin (wiidev/d2x-cios, source/ffs-plugin): /dev/fs ioctlv
// 100 sets the NAND emulation, its mode SD (1), with the full flag (0x100)
// for the whole NAND or without it for the game's paths only. Its FAT
// module (source/fat-module): ioctlv 0xF0 mounts the SD card, 0xF1
// unmounts it and stops the module's own SD driver.
constexpr u32 kSetMode = 100;
constexpr u32 kModeSd = 1;
constexpr u32 kModeFull = 0x100;
constexpr u32 kFatMountSd = 0xF0;
constexpr u32 kFatUnmountSd = 0xF1;
// The path d2x takes, its terminator included.
constexpr std::size_t kPathBytes = 256;

// What the FAT module's mount answers. Its SD driver could not start the
// card: IPC's -4. FS_EFATAL, -101: the module did not mount a file system
// through any of the four partition entries, which covers a card it could
// not read as well as one with no FAT volume it reads.
constexpr s32 kFatNoCard = -4;
constexpr s32 kFatNoVolume = -101;

// Between a failed mount, the module's unmount and the second try: the
// card gets a moment with no driver on it before the module starts it
// again from the beginning.
constexpr useconds_t kRemountPauseUs = 100 * 1000;

// While d2x's FAT module holds the card (sd_held_by_d2x_fat): whether
// d2x's own SD device carried a game on the card when it took it.
bool g_d2x_driver_at_hold = false;

// What the hand-off ended with, in one line: during a launch boot.log
// stays closed from the hand-off on, and its backlog in memory keeps only
// the newest lines, so release_d2x_saves logs this again for a failure's
// boot.log. Fixed size, in the DOL's own data.
char g_summary[320];

// The mode and path ioctlv 100 reads, aligned for IOS.
u32 g_mode ATTRIBUTE_ALIGN(32) = kModeSd;
char g_path[kPathBytes] ATTRIBUTE_ALIGN(32);
ioctlv g_set_vectors[2] ATTRIBUTE_ALIGN(32);

// ioctlv 100 with `mode` and the path in g_path; -1 when /dev/fs did not
// open (`fs_out` says what it answered).
s32 set_mode(u32 mode, s32& fs_out) {
    fs_out = IOS_Open("/dev/fs", 0);
    if (fs_out < 0) return -1;
    g_mode = mode;
    g_set_vectors[0].data = &g_mode;
    g_set_vectors[0].len = sizeof(g_mode);
    g_set_vectors[1].data = g_path;
    g_set_vectors[1].len = sizeof(g_path);
    const s32 answer = IOS_Ioctlv(fs_out, kSetMode, 2, 0, g_set_vectors);
    IOS_Close(fs_out);
    return answer;
}

std::string mount_answer(s32 code) {
    if (code >= 0) return "mounted the SD card (" + std::to_string(code) + ")";
    if (code == kFatNoCard) return "could not start the SD card (" + std::to_string(code) + ")";
    if (code == kFatNoVolume)
        return "mounted no file system on the SD card: a read of the card failed, or no partition held a FAT volume "
               "it could use (" + std::to_string(code) + ")";
    return "could not mount the SD card (" + std::to_string(code) + ")";
}

// The card's partition and file system, read through the driver libfat
// uses while it still has the card. A short-lived buffer from the heap:
// both SD drivers copy through their own aligned buffers when they must.
std::string card_layout() {
    u8* sector = static_cast<u8*>(memalign(32, kCardSectorBytes));
    if (sector == nullptr) return "a layout that could not be read (no memory for one sector)";
    const DISC_INTERFACE* card = sd_interface();
    std::string text;
    if (!card->readSectors(0, 1, sector)) {
        text = "a layout that could not be read (sector 0 did not read)";
    } else {
        const CardVolumeStart start = find_card_volume(sector);
        const bool has_boot_sector = start.scheme == CardVolumeStart::Scheme::Mbr ||
                                     start.scheme == CardVolumeStart::Scheme::NoTable;
        const bool boot_read = has_boot_sector && card->readSectors(start.first_sector, 1, sector);
        text = describe_card_volume(start, boot_read ? sector : nullptr);
    }
    std::free(sector);
    return text;
}

}  // namespace

bool enable_d2x_saves(const std::string& dir, std::string& error, D2xNandScope scope, bool* card_back) {
    // Nothing is touched before the checks: the card, boot.log and
    // *card_back stay as they are.
    if (dir.compare(0, 4, "sd:/") != 0) {
        error = "RiftWii turns d2x's NAND emulation on only from a folder of the SD card, and " + dir + " is not one";
        return false;
    }
    const char* card_path = dir.c_str() + 3;  // "/nands/01"
    if (std::strlen(card_path) >= sizeof(g_path)) {
        // A cut path would be another folder: d2x would make and use it.
        error = "the folder's path is " + std::to_string(dir.size() - 3) + " characters long, and d2x takes at most " +
                std::to_string(kPathBytes - 1);
        return false;
    }
    const u32 wanted_mode = kModeSd | (scope == D2xNandScope::WholeNand ? kModeFull : 0);
    const char* scope_name = scope == D2xNandScope::WholeNand ? "the whole NAND (full flag 0x100)" : "the game's paths";
    const s32 ios = IOS_GetVersion();
    const s32 revision = IOS_GetRevision();

    // The module first, before the card is touched: an IOS that is not d2x
    // (the game's own IOS, last in a channel's list) has none, and the card
    // then stays as it is.
    const s32 fat = IOS_Open("fat", 0);
    if (fat < 0) {
        std::snprintf(g_summary, sizeof(g_summary), "IOS%d has no d2x FAT module (%d)", ios, fat);
        logf("Saves: IOS%d rev %d has no d2x FAT module (opening it answered %d), so it cannot keep the emulated NAND "
             "of %s; the SD card was left mounted\n", ios, revision, fat, dir.c_str());
        error = "IOS" + std::to_string(ios) + " has no d2x FAT module (" + std::to_string(fat) + ")";
        return false;
    }
    const bool d2x_driver = using_d2x_sd();
    const std::string layout = card_layout();
    logf("Saves: d2x NAND emulation of %s from %s on IOS%d rev %d: d2x's FAT module is open (%d); the SD card has "
         "%s; RiftWii lets go of the card for d2x now\n", scope_name, dir.c_str(), ios, revision, fat, layout.c_str());

    // libfat's writes reach the card before d2x reads its FAT, and boot.log
    // goes first, as it is a file on that card.
    LogClose();
    fatUnmount("sd:");
    if (card_back != nullptr) *card_back = false;
    // libfat's unmount leaves the driver under it running (libfat 2.1.0
    // never calls a device's shutdown), and libogc's driver then still has
    // /dev/sdio/slot0 open with the card started, next to the FAT module's
    // own driver on that slot. That is the suspected cause of the "mount
    // -101" testers' logs show on every d2x slot (a Wii U and a Wii); only
    // a Wii can confirm it. d2x's own SD device is the exception: it stays
    // open, as it carries the game on the card.
    if (!d2x_driver) sd_interface()->shutdown();

    const s32 first_mount = IOS_Ioctlv(fat, kFatMountSd, 0, 0, nullptr);
    s32 mounted = first_mount;
    s32 unmount_before_retry = 0;
    // With d2x's own SD device carrying the game, the module is never told
    // to let go: that stops the card under the device too.
    const bool may_release = !d2x_driver;
    const bool retried = first_mount < 0 && may_release;
    if (retried) {
        // Once more from the beginning: the unmount stops the module's own
        // driver too, so the second mount starts the card afresh.
        unmount_before_retry = IOS_Ioctlv(fat, kFatUnmountSd, 0, 0, nullptr);
        usleep(kRemountPauseUs);
        mounted = IOS_Ioctlv(fat, kFatMountSd, 0, 0, nullptr);
    }
    // Only with the card mounted in d2x: emulation turned on over a card
    // d2x could not mount left ES unable to open the game (a tester's
    // Wii Sports Resort from USB on IOS249: "mount -101, set 0", then
    // "open partition failed: DI reply 64 (ES -101)"). RiftWii's own save
    // code keeps the save instead.
    s32 set = -1;
    s32 fs = -1;
    if (mounted >= 0) {
        std::snprintf(g_path, sizeof(g_path), "%s", card_path);
        set = set_mode(wanted_mode, fs);
    }
    const bool emulating = mounted >= 0 && set >= 0;
    // Without the emulation, d2x lets go of the card again, so RiftWii's
    // own save code is the only one writing to it while the game runs.
    // With d2x's own SD device (no release), a module that mounted but
    // serves nothing writes nothing: libfat goes back on that device.
    const bool release = !emulating && may_release;
    const s32 released = release ? IOS_Ioctlv(fat, kFatUnmountSd, 0, 0, nullptr) : 0;
    IOS_Close(fat);

    // With the emulation on, the card is d2x's alone until the next IOS
    // reload: no libfat, no boot.log, no raw reads, no runtime on it. Its
    // lines below wait in memory (and show on screen).
    bool remounted = false;
    if (emulating) {
        g_d2x_driver_at_hold = d2x_driver;
        hold_sd_for_d2x_fat(true);
    } else {
        // libfat reads the card afresh, through the driver it had.
        remounted = fatMountSimple("sd", sd_interface());
        if (remounted) LogReopen();
        if (card_back != nullptr) *card_back = remounted;
    }

    // What each step did, in boot.log when it has the card back.
    logf("Saves: the SD card was unmounted for d2x (libfat wrote its cache and let go; boot.log was closed)\n");
    if (d2x_driver) {
        logf("Saves: d2x's own SD device (/dev/sdio/sdhc) stayed open: the game on the card is read through it\n");
    } else {
        logf("Saves: libogc's SD driver was shut down (/dev/sdio/slot0 closed), so d2x's FAT module had the card to "
             "itself\n");
    }
    if (retried) {
        logf("Saves: d2x's FAT module %s; it was told to let go of the card (%d) and tried once more after %u ms\n",
             mount_answer(first_mount).c_str(), unmount_before_retry,
             static_cast<unsigned>(kRemountPauseUs / 1000));
    }
    logf("Saves: d2x's FAT module %s%s\n", mount_answer(mounted).c_str(), retried ? " at the second try" : "");
    if (fs >= 0) {
        if (set >= 0) logf("Saves: d2x turned the NAND emulation of %s on for %s (%d)\n", scope_name, dir.c_str(), set);
        else logf("Saves: d2x refused the NAND emulation for %s (%d)\n", dir.c_str(), set);
    } else if (mounted >= 0) {
        logf("Saves: the NAND emulation could not be asked for: /dev/fs did not open (%d)\n", fs);
    } else {
        logf("Saves: the NAND emulation was not asked for, as d2x had not mounted the SD card\n");
    }
    if (release) {
        logf("Saves: d2x's FAT module was told to let go of the SD card again (%d), so RiftWii's own save code has "
             "it alone\n", released);
    }
    if (emulating) {
        logf("Saves: the SD card is d2x's now: RiftWii does not mount it again or write to it until the next IOS "
             "reload, and boot.log ends here (the rest is on screen)\n");
    } else if (remounted) {
        logf("Saves: the SD card was mounted again (libfat, %s)\n",
             using_d2x_sd() ? "through d2x's SD device" : "through libogc's SD driver");
    }
    std::snprintf(g_summary, sizeof(g_summary), "%s on IOS%d: the card has %.160s; FAT module %d, mount %d (first %d), "
                  "set %d, mode 0x%x", dir.c_str(), ios, layout.c_str(), fat, mounted, first_mount, set,
                  static_cast<unsigned>(wanted_mode));
    logf("Saves: d2x NAND emulation from %s: FAT module %d, mount %d, set %d, mode 0x%x\n", dir.c_str(), fat, mounted,
         set, static_cast<unsigned>(wanted_mode));

    if (mounted < 0) {
        error = "d2x could not mount the SD card (" + std::to_string(mounted) + ")";
        return false;
    }
    if (set < 0) {
        error = "d2x refused the NAND emulation (" + std::to_string(set) + ")";
        return false;
    }
    return true;
}

bool release_d2x_saves() {
    if (!sd_held_by_d2x_fat()) return true;
    // The emulation off first, so no NAND request goes to a file system
    // that is going away: d2x's ffs-plugin (__FS_SetMode in plugin.c) turns
    // it off for a mode with neither the SD nor the USB bit, while no title
    // runs.
    g_path[0] = '\0';
    s32 fs = -1;
    const s32 off = set_mode(0, fs);
    // Then the module writes what it holds and lets go of the card. With
    // a game on the card through d2x's own device, the launch is over by
    // now (no game reads through it any more), so this is safe as well.
    const s32 fat = IOS_Open("fat", 0);
    const s32 released = fat >= 0 ? IOS_Ioctlv(fat, kFatUnmountSd, 0, 0, nullptr) : fat;
    if (fat >= 0) IOS_Close(fat);
    logf("Saves: d2x's NAND emulation earlier in this launch: %s\n", g_summary);
    if (released < 0) {
        logf("Saves: d2x's FAT module did not let go of the SD card (%d; emulation off: %d), so RiftWii stays off the "
             "card until it restarts (the restart reloads IOS, which ends the module)\n", released, off);
        return false;
    }
    // d2x's own device lost its card with the module's unmount: libogc's
    // driver from here.
    if (g_d2x_driver_at_hold) d2x_sd_forget();
    hold_sd_for_d2x_fat(false);
    logf("Saves: d2x's NAND emulation was turned off (%d) and its FAT module let go of the SD card (%d): RiftWii "
         "may use the card again\n", off, released);
    return true;
}

bool packs_need_sd(const std::vector<std::string>& xml_paths) {
    for (const std::string& xml : xml_paths) {
        if (xml.compare(0, 5, "usb:/") != 0) return true;
    }
    return false;
}

bool entries_need_sd(const std::vector<rt_entry>& entries) {
    for (const rt_entry& entry : entries) {
        if (entry.kind == RT_KIND_SD) return true;
    }
    return false;
}

}  // namespace riftwii::wii
