// SPDX-License-Identifier: GPL-3.0-or-later
#include "channel.hpp"

#include <gccore.h>
#include <fat.h>
#include <malloc.h>
#include <ogc/es.h>

#include <sys/stat.h>

#include "dolboot.h"
#include "ios_reload.hpp"
#include "log.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii {
namespace {

constexpr u64 kChannelTitle = 0x0001000152465457ull;  // 00010001-RFTW, tools/make_channel.py
constexpr const char* kInstaller = "sd:/apps/riftwii_channel/boot.dol";

bool installer_on_card() {
    struct stat st;
    return stat(kInstaller, &st) == 0;
}

}  // namespace

bool ChannelInstalled(unsigned& version) {
    version = 0;
    u32 tickets = 0;
    if (ES_GetNumTicketViews(kChannelTitle, &tickets) < 0 || tickets == 0) return false;
    u32 size = 0;
    if (ES_GetTMDViewSize(kChannelTitle, &size) < 0 || size < sizeof(tmd_view) || size > 0x400) return false;
    static u8 view[0x400] ATTRIBUTE_ALIGN(32);
    if (ES_GetTMDView(kChannelTitle, reinterpret_cast<tmd_view*>(view), size) < 0) return false;
    version = reinterpret_cast<const tmd_view*>(view)->title_version;
    return true;
}

bool ChannelInstallerPresent(std::string& why) {
    if (installer_on_card()) return true;
    why = "Copy apps/riftwii_channel from the RiftWii zip to the SD card first";
    return false;
}

bool ChannelCanInstall(std::string& why) {
    if (!ChannelInstallerPresent(why)) return false;
    if (running_in_dolphin()) {
        why = "Dolphin checks real signatures, so the channel can only be installed on a Wii";
        return false;
    }
    for (int slot : {249, 250, 251}) {
        if (slot_has_ticket(slot)) return true;
    }
    why = "Installing the channel needs a d2x cIOS in slot 249, 250 or 251";
    return false;
}

bool StartChannelInstaller(std::string& error) {
    logf("Channel: starting %s\n", kInstaller);
    u32 size = 0;
    u8* dol = dolboot_read(kInstaller, &size, [](u32 n) -> void* { return memalign(32, n); });
    if (!dol || !dolboot_valid(dol, size)) {
        error = std::string("The channel installer (") + kInstaller + ") is missing or damaged";
        logf("Channel: %s\n", error.c_str());
        std::free(dol);
        return false;
    }
    LogClose();
    fatUnmount("sd:");
    fatUnmount("usb:");
    AUDIO_StopDMA();
    dolboot_run(dol, kInstaller);
}

}  // namespace riftwii::wii
