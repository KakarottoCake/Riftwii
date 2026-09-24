// SPDX-License-Identifier: GPL-3.0-or-later
#include "menuios.hpp"

#include <fat.h>
#include <gccore.h>
#include <ogc/isfs.h>
#include <sdcard/wiisd_io.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ios_reload.hpp"
#include "log.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kSettingPath = "sd:/riftwii/menu_ios.txt";
constexpr int kFirstSlot = 248;
constexpr int kLastSlot = 251;
int g_menu_cios = 0;

// SYSCONF (wiibrew /shared2/sys/SYSCONF): "SCv0", an item count and a
// table of item offsets; an item starts with a byte holding its type in
// the top three bits and its name length - 1 below, then the name, then
// for a big array a 16-bit length - 1 and the bytes.
constexpr std::size_t kSysconfBytes = 0x4000;
constexpr unsigned kTypeBigArray = 1;
// BT.DINF: a registered count, then ten entries of a 6-byte address and a
// 64-byte name.
constexpr std::size_t kPadEntryBytes = 6 + 0x40;
constexpr int kPadEntries = 10;

std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }

}  // namespace

int LoadMenuIos() {
    FILE* f = std::fopen(kSettingPath, "r");
    if (!f) return 0;
    char text[16] = {};
    const std::size_t n = std::fread(text, 1, sizeof(text) - 1, f);
    std::fclose(f);
    text[n] = 0;
    const int slot = std::atoi(text);
    return slot >= kFirstSlot && slot <= kLastSlot ? slot : 0;
}

bool SaveMenuIos(int slot) {
    if (slot == 0) {
        std::remove(kSettingPath);
        return true;
    }
    FILE* f = std::fopen(kSettingPath, "w");
    if (!f) return false;
    const bool ok = std::fprintf(f, "%d\n", slot) > 0;
    return std::fclose(f) == 0 && ok;
}

std::vector<int> MenuIosChoices() {
    std::vector<int> out{0};
    for (int slot = kFirstSlot; slot <= kLastSlot; ++slot) {
        if (slot_has_ticket(slot)) out.push_back(slot);
    }
    return out;
}

int MenuCiosSlot() { return g_menu_cios; }

bool StartMenuIos(bool sd_mounted, bool fresh) {
    int slot = sd_mounted ? LoadMenuIos() : 0;
    if (slot != 0 && !slot_has_ticket(slot)) {
        logf("Menu IOS: IOS%d is not installed; staying on IOS%d\n", slot, IOS_GetVersion());
        slot = 0;
    }
    if (slot == 0) {
        if (!fresh) return true;
        std::string error;
        if (sd_mounted) {
            LogClose();
            fatUnmount("sd:");
            __io_wiisd.shutdown();
        }
        const ReloadResult r = reload_ios(58, error, true);
        if (r == ReloadResult::Terminal) halt_after_terminal_reload();
        if (sd_mounted && __io_wiisd.startup() && __io_wiisd.isInserted() && fatMountSimple("sd", &__io_wiisd)) {
            LogReopen();
        }
        logf("Menu IOS: fresh IOS%d after a restart%s%s\n", IOS_GetVersion(), error.empty() ? "" : ": ",
             error.c_str());
        return false;
    }
    const PadPairings before = ReadPadPairings();
    logf("Menu IOS: reloading into IOS%d (%s)\n", slot, DescribePadPairings(before).c_str());
    LogClose();
    fatUnmount("sd:");
    __io_wiisd.shutdown();
    std::string error;
    const ReloadResult r = reload_ios(slot, error, fresh);
    if (r == ReloadResult::Terminal) halt_after_terminal_reload();
    const bool mounted = __io_wiisd.startup() && __io_wiisd.isInserted() && fatMountSimple("sd", &__io_wiisd);
    if (mounted) LogReopen();
    if (r == ReloadResult::NotInstalled || r == ReloadResult::Failed) {
        logf("Menu IOS: reload into IOS%d failed (%s); staying on IOS%d\n", slot, error.c_str(), IOS_GetVersion());
        return false;
    }
    g_menu_cios = IOS_GetVersion() == slot ? slot : 0;
    const PadPairings after = ReadPadPairings();
    logf("Menu IOS: running IOS%d rev %d (%s); %s%s\n", IOS_GetVersion(), IOS_GetRevision(),
         last_reload_detail().c_str(), DescribePadPairings(after).c_str(),
         after.fake > before.fake ? ", added by this IOS just now (fakemote is in it)" : "");
    return g_menu_cios != 0;
}

PadPairings ReadPadPairings() {
    PadPairings out;
    static std::uint8_t conf[kSysconfBytes] ATTRIBUTE_ALIGN(32);
    // Opened and closed per read: an IOS reload in between leaves a kept
    // /dev/fs handle dead, and nothing else here uses ISFS.
    if (ISFS_Initialize() < 0) return out;
    const s32 fd = ISFS_Open("/shared2/sys/SYSCONF", ISFS_OPEN_READ);
    s32 got = -1;
    if (fd >= 0) {
        got = ISFS_Read(fd, conf, sizeof(conf));
        ISFS_Close(fd);
    }
    ISFS_Deinitialize();
    if (got != static_cast<s32>(sizeof(conf)) || std::memcmp(conf, "SCv0", 4) != 0) return out;
    const unsigned count = be16(conf + 4);
    static const char kName[] = "BT.DINF";
    const std::size_t name_len = sizeof(kName) - 1;
    for (unsigned i = 0; i < count && 6 + 2 * i + 2 <= sizeof(conf); ++i) {
        const std::size_t at = be16(conf + 6 + 2 * i);
        if (at + 1 + name_len + 2 > sizeof(conf)) continue;
        const std::uint8_t head = conf[at];
        if ((head >> 5) != kTypeBigArray || (head & 0x1F) + 1u != name_len) continue;
        if (std::memcmp(conf + at + 1, kName, name_len) != 0) continue;
        const std::size_t data = at + 1 + name_len + 2;
        const std::size_t length = be16(conf + at + 1 + name_len) + 1u;
        if (length < 1 + kPadEntryBytes * kPadEntries || data + length > sizeof(conf)) return out;
        const int registered = conf[data];
        out.registered = registered > kPadEntries ? kPadEntries : registered;
        for (int e = 0; e < out.registered; ++e) {
            char name[0x41] = {};
            std::memcpy(name, conf + data + 1 + e * kPadEntryBytes + 6, 0x40);
            if (std::strstr(name, "Fake Wiimote")) ++out.fake;
        }
        return out;
    }
    return out;
}

std::string DescribePadPairings(const PadPairings& p) {
    if (p.registered < 0) return "SYSCONF pad list unreadable";
    return std::to_string(p.registered) + " Wii Remote pairing(s), " + std::to_string(p.fake) + " from fakemote";
}

}  // namespace riftwii::wii
