// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "channel.hpp"

#include <gccore.h>
#include <fat.h>
#include <malloc.h>
#include <ogc/es.h>

#include <cstdio>
#include <sys/stat.h>

#include <cstring>
#include <utility>
#include <vector>

#include "boot.hpp"
#include "dolboot.h"
#include "i18n.hpp"
#include "ios_reload.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "menumusic.hpp"
#include "netsock.hpp"
#include "restart.hpp"
#include "riftwii/channelupdate.hpp"
#include "riftwii/returnstub.hpp"
#include "riftwii_stub_bin.h"
#include "usbcatalog.hpp"

namespace riftwii::wii {

const char* const kChannelMarkerError = "the SD card would not save a note before the installer (is it locked?)";

namespace {

// 00010001-UFTW (tools/make_channel.py); up to its version 5 it was
// 00010001-RFTW, which counts too until the installer replaces it.
constexpr u64 kChannelTitles[] = {0x0001000155465457ull, 0x0001000152465457ull};
constexpr const char* kInstaller = "sd:/apps/riftwii_channel/boot.dol";
constexpr const char* kInstallerMarker = "sd:/riftwii/channel_installer_opened.txt";

void clear_installer_marker(const char* reason) {
    if (std::remove(kInstallerMarker) == 0) {
        logf("Channel: the installer marker was cleared because %s.\n", reason);
    }
}

bool installer_on_card() {
    struct stat st;
    return stat(kInstaller, &st) == 0;
}

// A ticket and a TMD: a ticket alone is what a failed install leaves, and
// launching that title fails.
bool title_installed(u64 title, unsigned& version) {
    version = 0;
    u32 tickets = 0;
    if (ES_GetNumTicketViews(title, &tickets) < 0 || tickets == 0) return false;
    u32 size = 0;
    if (ES_GetTMDViewSize(title, &size) < 0 || size < sizeof(tmd_view) || size > 0x400) return false;
    static u8 view[0x400] ATTRIBUTE_ALIGN(32);
    if (ES_GetTMDView(title, reinterpret_cast<tmd_view*>(view), size) < 0) return false;
    version = reinterpret_cast<const tmd_view*>(view)->title_version;
    return true;
}

}  // namespace

unsigned long long ChannelTitle() {
    unsigned version = 0;
    for (u64 title : kChannelTitles) {
        if (title_installed(title, version)) return title;
    }
    return 0;
}

// RiftWii's own return stub (plant_return_stub, below) has "STUBHAXX" too:
// a RiftWii started by an app that RiftWii started finds it there, and its
// way back is then the RiftWii channel, not the Homebrew Channel.
bool StartedByHomebrewChannel() {
    return riftwii::return_stub_kind(reinterpret_cast<const std::uint8_t*>(RTSTUB_BASE)) ==
           riftwii::ReturnStubKind::HomebrewChannel;
}

unsigned long long HomebrewChannelTitle() {
    static const u64 kTitles[] = {0x000100014C554C5Aull, 0x000100014F484243ull, 0x0001000148415858ull,
                                  0x00010001AF1BF516ull};
    for (u64 title : kTitles) {
        u32 views = 0;
        if (ES_GetNumTicketViews(title, &views) >= 0 && views != 0) return title;
    }
    return 0;
}

bool ChannelInstalled(unsigned& version) {
    for (u64 title : kChannelTitles) {
        if (title_installed(title, version)) return true;
    }
    return false;
}

unsigned ChannelVersionNeeded() {
    return is_wii_u() ? kChannelVersion : 11;
}

riftwii::ChannelUpdateState ChannelInstallerUpdateState() {
    struct stat st;
    const bool opened = stat(kInstallerMarker, &st) == 0;
    auto attempt = opened ? ChannelInstallerAttempt::Opened : ChannelInstallerAttempt::None;
    if (FILE* marker = std::fopen(kInstallerMarker, "r")) {
        char line[32];
        if (std::fgets(line, sizeof(line), marker)) {
            line[std::strcspn(line, "\r\n")] = '\0';
            if (std::strcmp(line, "returned") == 0) attempt = ChannelInstallerAttempt::Returned;
        }
        std::fclose(marker);
    }
    // A normal installer exit records its return before cold-starting
    // RiftWii. ChannelDone is only the refused hand-off's warm restart.
    const bool refused = CurrentRestartNote().kind == RestartKind::ChannelDone;
    unsigned version = 0;
    const bool installed = ChannelInstalled(version);
    const auto state = channel_update_state(installed, version, ChannelVersionNeeded(), attempt, refused);
    if (state.clear_marker) {
        clear_installer_marker(refused ? "the installer could not be opened" : "the channel needs no update");
    }
    return state;
}

void ChannelUpdateNotNow() {
    // Keep the marker so later starts still offer Not now until a completed
    // update removes it or the channel no longer needs updating.
    logf("Channel: the update was postponed for this start; Home opens and the next start asks again.\n");
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

bool StartChannelInstaller(std::string& error, bool allow_unmarked) {
    logf("Channel: starting %s\n", kInstaller);
    u32 size = 0;
    u8* dol = dolboot_read(kInstaller, &size, [](u32 n) -> void* { return memalign(32, n); },
                            [](void* p) { std::free(p); });
    if (!dol || !dolboot_valid(dol, size)) {
        error = tr("The channel installer ({1}) is missing or damaged", {kInstaller});
        logf("Channel: %s\n", error.c_str());
        std::free(dol);
        return false;
    }
    // Close this before handing off: even a hang before the installer's
    // first line must leave evidence for the next start.
    mkdir("sd:/riftwii", 0777);
    bool marked = false;
    if (FILE* marker = std::fopen(kInstallerMarker, "w")) {
        const bool wrote = std::fputs("The channel installer was opened.\n", marker) >= 0;
        const bool closed = std::fclose(marker) == 0;
        marked = wrote && closed;
    }
    if (!marked && !allow_unmarked) {
        // The refused restart offers an explicit choice to open without
        // interruption detection when the card will not save this note.
        error = kChannelMarkerError;
        logf("Channel: the installer was not opened: %s\n", error.c_str());
        std::free(dol);
        return false;
    }
    if (marked) logf("Channel: the launch marker was saved before opening the installer.\n");
    else logf("Channel: the player chose to open the installer without a saved launch marker.\n");
    NetCancelBackground();  // a theme download: fetched again next start
    NetWaitForBackground();
    LogClose();
    fatUnmount("sd:");
    fatUnmount("usb:");
    AUDIO_StopDMA();
    dolboot_run(dol, kInstaller);
}

namespace {

constexpr u64 kWiiMenuTitle = 0x0000000100000002ull;

// The title's first ticket view, which ES_LaunchTitle takes with it: read
// now, as the stub has no ES of its own to ask.
bool first_ticket_view(u64 title, riftwii::ReturnStubTitle& out) {
    static_assert(sizeof(tikview) == RTSTUB_VIEW_BYTES, "the stub keeps a whole ticket view per title");
    u32 views = 0;
    if (ES_GetNumTicketViews(title, &views) < 0 || views == 0) return false;
    // ES_GetTicketViews fills as many as asked: room for one is enough.
    static tikview found[4] ATTRIBUTE_ALIGN(32);
    if (ES_GetTicketViews(title, found, 1) < 0) return false;
    out.title = title;
    std::memcpy(out.view.data(), &found[0], RTSTUB_VIEW_BYTES);
    return true;
}

// Clears the "STUBHAXX" libogc's exit() looks for at 0x80001804, so the
// app's exit goes to the Wii Menu instead of into a stub that is stale or
// that the app's own sections will overwrite.
void clear_return_stub_magic() {
    volatile u32* const words = reinterpret_cast<volatile u32*>(RTSTUB_BASE);
    words[RTSTUB_OFFSET_HBC_MAGIC / 4] = 0;
    DCFlushRange(reinterpret_cast<void*>(RTSTUB_BASE), 32);
}

// Where the app's exit goes, settled before it starts. libogc's exit()
// runs whatever stub is at 0x80001800, and without one goes to the Wii
// Menu, which nothing but a Wii Remote can leave (a Wii U with only the
// GameCube adapter). So RiftWii's stub (riftwii/returnstub.h) goes there:
// the RiftWii channel, then the Homebrew Channel, then the Wii Menu,
// whichever starts first. Left as it was when the Homebrew Channel started
// RiftWii (its apps go back to it, as they always have), and not put
// there when Settings send the Wii Menu button to the Wii Menu.
void plant_return_stub(const u8* dol, u32 size) {
    const riftwii::ReturnStubKind there =
        riftwii::return_stub_kind(reinterpret_cast<const std::uint8_t*>(RTSTUB_BASE));
    const bool clear = riftwii::dol_clear_of_return_stub(dol, size);
    if (there == riftwii::ReturnStubKind::HomebrewChannel) {
        // The app's sections are copied over the same range: a stub they
        // overwrite only in part would keep its magic and crash the exit.
        if (!clear) {
            clear_return_stub_magic();
            logf("Homebrew app: its exit goes to the Wii Menu (the app loads where the Homebrew Channel's stub is, "
                 "0x80001800-0x80002400)\n");
            return;
        }
        logf("Homebrew app: its exit goes back to the Homebrew Channel, which started RiftWii\n");
        return;
    }
    const bool back_to_riftwii = Settings().return_to == "riftwii";
    if (!back_to_riftwii || !clear) {
        // An earlier RiftWii's stub would still be found: its magic goes.
        if (there == riftwii::ReturnStubKind::RiftWii) clear_return_stub_magic();
        logf("Homebrew app: its exit goes to the Wii Menu (%s)\n",
             !back_to_riftwii ? "the Wii Menu button is set to the Wii Menu"
                              : "the app loads where the return stub would go, 0x80001800-0x80002400");
        return;
    }
    std::vector<riftwii::ReturnStubTitle> titles;
    std::string order;
    const std::pair<u64, const char*> candidates[] = {
        {ChannelTitle(), "the RiftWii channel"},
        {HomebrewChannelTitle(), "the Homebrew Channel"},
        {kWiiMenuTitle, "the Wii Menu"},
    };
    for (const auto& [title, name] : candidates) {
        riftwii::ReturnStubTitle found;
        if (title == 0 || !first_ticket_view(title, found)) continue;
        titles.push_back(found);
        order += order.empty() ? std::string(name) : std::string(", then ") + name;
    }
    std::string error;
    const std::vector<std::uint8_t> image =
        riftwii::fill_return_stub(riftwii_stub_bin, riftwii_stub_bin_size, titles, error);
    if (image.empty()) {
        // Nor an earlier RiftWii's stub, with its old titles.
        if (there == riftwii::ReturnStubKind::RiftWii) clear_return_stub_magic();
        logf("Homebrew app: no return stub (%s), so its exit goes to the Wii Menu\n", error.c_str());
        return;
    }
    void* const base = reinterpret_cast<void*>(RTSTUB_BASE);
    std::memcpy(base, image.data(), image.size());
    DCFlushRange(base, image.size());
    ICInvalidateRange(base, image.size());
    logf("Homebrew app: its exit goes back to %s (the return stub, %u bytes at 0x%08x)\n", order.c_str(),
         static_cast<unsigned>(image.size()), static_cast<unsigned>(RTSTUB_BASE));
}

}  // namespace

HomebrewExit HomebrewExitGoesTo() {
    if (riftwii::return_stub_kind(reinterpret_cast<const std::uint8_t*>(RTSTUB_BASE)) ==
        riftwii::ReturnStubKind::HomebrewChannel)
        return HomebrewExit::HomebrewChannel;
    if (Settings().return_to != "riftwii") return HomebrewExit::WiiMenu;
    // Without the RiftWii channel the stub starts the Homebrew Channel or
    // the Wii Menu, whichever has a ticket: not promised in the popup.
    return ChannelTitle() != 0 ? HomebrewExit::RiftWii : HomebrewExit::NotSaid;
}

bool StartHomebrewApp(const std::string& path, std::string& error) {
    std::string line = path;
    line.push_back('\0');
    line.push_back('\0');
    return StartHomebrewApp(path, line, error);
}

bool StartHomebrewApp(const std::string& path, const std::string& command_line, std::string& error) {
    u32 size = 0;
    u8* dol = dolboot_read(path.c_str(), &size, [](u32 n) -> void* { return memalign(32, n); },
                            [](void* p) { std::free(p); });
    if (!dol || !dolboot_valid(dol, size)) {
        error = path + " is missing, damaged or does not fit next to RiftWii";
        logf("Homebrew app: %s\n", error.c_str());
        std::free(dol);
        return false;
    }
    logf("Homebrew app: starting %s (%u bytes)\n", path.c_str(), static_cast<unsigned>(size));
    plant_return_stub(dol, size);
    LaunchMusicEnd("the app starts next");
    LogClose();
    fatUnmount("sd:");
    fatUnmount("usb:");
    AUDIO_StopDMA();
    dolboot_run_line(dol, command_line.data(), static_cast<u32>(command_line.size()));
}

}  // namespace riftwii::wii
