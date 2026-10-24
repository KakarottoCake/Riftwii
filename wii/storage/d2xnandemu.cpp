// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2011 Dimok
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "d2xnandemu.hpp"

#include <fat.h>
#include <gccore.h>
#include <malloc.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

#include "d2xsd.hpp"
#include "log.hpp"
#include "riftwii/cardvolume.hpp"

namespace riftwii::wii {
namespace {

// d2x's FFS plugin (wiidev/d2x-cios, source/ffs-plugin): /dev/fs ioctlv
// 100 sets the NAND emulation, its mode SD (1) without the full flag
// (0x100: partial, the game's paths only). Its FAT module
// (source/fat-module): ioctlv 0xF0 mounts the SD card, 0xF1 unmounts it
// and stops the module's own SD driver.
constexpr u32 kSetMode = 100;
constexpr u32 kModeSd = 1;
constexpr u32 kFatMountSd = 0xF0;
constexpr u32 kFatUnmountSd = 0xF1;

// What the FAT module's mount answers. Its SD driver could not start the
// card: IPC's -4. It started the card but found no file system it could
// read, through any of the four partition entries: FS_EFATAL, -101.
constexpr s32 kFatNoCard = -4;
constexpr s32 kFatNoFileSystem = -101;

// Between a failed mount, the module's unmount and the second try: the
// card gets a moment with no driver on it before the module starts it
// again from the beginning.
constexpr useconds_t kRemountPauseUs = 100 * 1000;

std::string mount_answer(s32 code) {
    if (code >= 0) return "mounted the SD card (" + std::to_string(code) + ")";
    if (code == kFatNoCard) return "could not start the SD card (" + std::to_string(code) + ")";
    if (code == kFatNoFileSystem)
        return "started the SD card but found no file system it could read on it (" + std::to_string(code) + ")";
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

bool enable_d2x_saves(const std::string& dir, std::string& error) {
    if (dir.compare(0, 4, "sd:/") != 0) {
        error = "not a folder of the SD card";
        return false;
    }
    static char path[256] ATTRIBUTE_ALIGN(32);
    std::snprintf(path, sizeof(path), "%s", dir.c_str() + 3);
    static u32 mode ATTRIBUTE_ALIGN(32) = kModeSd;
    const s32 ios = IOS_GetVersion();
    const s32 revision = IOS_GetRevision();

    // The module first, before the card is touched: an IOS that is not d2x
    // (the game's own IOS, last in a channel's list) has none, and the card
    // then stays as it is.
    const s32 fat = IOS_Open("fat", 0);
    if (fat < 0) {
        logf("Saves: IOS%d rev %d has no d2x FAT module (opening it answered %d), so it cannot keep the emulated NAND "
             "of %s; the SD card was left mounted\n", ios, revision, fat, dir.c_str());
        error = "IOS" + std::to_string(ios) + " has no d2x FAT module (FAT module " + std::to_string(fat) + ")";
        return false;
    }
    const bool d2x_driver = using_d2x_sd();
    logf("Saves: d2x NAND emulation from %s on IOS%d rev %d: d2x's FAT module is open (%d); the SD card has %s; "
         "RiftWii lets go of the card for d2x now\n", dir.c_str(), ios, revision, fat, card_layout().c_str());

    // libfat's writes reach the card before d2x reads its FAT, and boot.log
    // goes first, as it is a file on that card.
    LogClose();
    fatUnmount("sd:");
    // libfat's unmount leaves the driver under it running (libfat 2.1.0
    // never calls a device's shutdown), and libogc's driver then still has
    // /dev/sdio/slot0 open with the card started. The FAT module starts
    // its own driver on that same slot: with RiftWii's still there it
    // answered -101 on every d2x slot (#128, on a Wii U and on a Wii),
    // while loaders that shut their SD driver down first get the emulated
    // NAND on the same consoles. d2x's own SD device is the exception: it
    // stays open, as it carries the game on the card.
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
    const s32 fs = mounted >= 0 ? IOS_Open("/dev/fs", 0) : -1;
    if (fs >= 0) {
        static ioctlv vectors[2] ATTRIBUTE_ALIGN(32);
        mode = kModeSd;
        vectors[0].data = &mode;
        vectors[0].len = sizeof(mode);
        vectors[1].data = path;
        vectors[1].len = sizeof(path);
        set = IOS_Ioctlv(fs, kSetMode, 2, 0, vectors);
        IOS_Close(fs);
    }
    const bool emulating = mounted >= 0 && set >= 0;
    // Without the emulation, d2x lets go of the card again, so RiftWii's
    // own save code is the only one writing to it while the game runs.
    const bool release = !emulating && may_release;
    const s32 released = release ? IOS_Ioctlv(fat, kFatUnmountSd, 0, 0, nullptr) : 0;
    IOS_Close(fat);

    // And libfat reads the card afresh after d2x made its folders. With the
    // emulation on, through d2x's own SD device: d2x then drives the card
    // with its own modules only, as for a game on the card (d2xsd.hpp),
    // where starting libogc's driver again would reset the card under the
    // FAT module. The loader's raw reads and the runtime follow the same
    // choice. libogc's driver only when d2x's device does not start.
    bool switched_to_d2x = false;
    bool remounted = false;
    std::string d2x_device_error;
    if (emulating && !d2x_driver) {
        use_d2x_sd(true);
        remounted = fatMountSimple("sd", sd_interface());
        switched_to_d2x = remounted;
        if (!remounted) {
            d2x_device_error = d2x_sd_last_error();
            if (d2x_device_error.empty()) d2x_device_error = "libfat found no file system through it";
            d2x_sd_forget();  // back to libogc's driver, d2x's handle closed
        }
    }
    if (!remounted) remounted = fatMountSimple("sd", sd_interface());
    LogReopen();

    // What each step did, now that boot.log can take it.
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
    logf("Saves: d2x's FAT module %s%s\n", mount_answer(mounted).c_str(),
         retried ? " at the second try" : "");
    if (fs >= 0) {
        if (set >= 0) logf("Saves: d2x turned the NAND emulation on for %s (%d)\n", dir.c_str(), set);
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
    if (!d2x_device_error.empty()) {
        logf("Saves: d2x's own SD device did not take the card (%s), so libogc's SD driver mounts it again, next to "
             "d2x's FAT module\n", d2x_device_error.c_str());
    }
    const char* driver = using_d2x_sd() ? "through d2x's SD device" : "through libogc's SD driver";
    if (remounted) {
        logf("Saves: the SD card was mounted again (libfat, %s%s)\n", driver,
             switched_to_d2x ? "; the rest of the launch reads the card through it too" : "");
    } else {
        logf("Saves: the SD card did not mount again (libfat, %s)\n", driver);
    }
    logf("Saves: d2x NAND emulation from %s: FAT module %d, mount %d, set %d\n", dir.c_str(), fat, mounted, set);

    if (mounted < 0) {
        error = "d2x could not mount the SD card (FAT module " + std::to_string(fat) + ", mount " +
                std::to_string(mounted) + ")";
        return false;
    }
    if (set < 0) {
        error = "d2x refused the NAND emulation (set " + std::to_string(set) + ")";
        return false;
    }
    if (!remounted) {
        error = "the SD card did not mount again after d2x took it";
        return false;
    }
    return true;
}

}  // namespace riftwii::wii
