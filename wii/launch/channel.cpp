// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "channel.hpp"

#include <gccore.h>
#include <fat.h>
#include <malloc.h>
#include <ogc/es.h>
#include <ogc/machine/processor.h>

#include <cstdio>
#include <sys/stat.h>

#include <cstring>
#include <utility>
#include <vector>

#include "boot.hpp"
#include "d2xsd.hpp"
#include "dolboot.h"
#include "gcadapter.hpp"
#include "i18n.hpp"
#include "ios_reload.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "menumusic.hpp"
#include "netsock.hpp"
#include "restart.hpp"
#include "riftwii/appmeta.hpp"
#include "riftwii/channelupdate.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/returnstub.hpp"
#include "riftwii/imagevolume.hpp"
#include "riftwii_stub_bin.h"
#include "usbcatalog.hpp"
#include "usbinput.hpp"

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

bool ChannelCanUpdateHere(std::string& why) {
    if (!ChannelCanInstall(why)) return false;
    // The installer starts on RiftWii's IOS with its hardware access. On
    // IOS58 it needs that access to read which cIOS slots hold d2x (IOS58
    // refuses other titles' files), and in update mode it tries no unchecked
    // slot (no one is there to ask). An installer going back to RiftWii
    // takes that access away (its IOS58 reload), so a try from that start
    // could only fail (31 Oct, on a Wii U). On a cIOS (a menu IOS from
    // Settings) d2x leaves that check out, so the installer reads the slots
    // without it; if it still can't, it says so and comes back.
    if (IOS_GetVersion() < 200 && read32(0x0D800064) != 0xFFFFFFFF) {
        why = "this start of RiftWii has no hardware access, which the installer needs. Start RiftWii from the Homebrew Channel or the RiftWii channel (version 9 or newer)";
        return false;
    }
    // With that access the installer tries only slots it identifies as d2x,
    // so a slot RiftWii read as another cIOS would send it back without
    // updating. One RiftWii could not read is left to the installer, which
    // reads them its own way (a Wii U, 31 Oct: RiftWii's reads failed with
    // -102, and the installer's found d2x in 249-251).
    for (int slot : {249, 250, 251}) {
        if (slot_has_ticket(slot) && !slot_known_not_d2x(slot)) return true;
    }
    why = "Installing the channel needs a d2x cIOS in slot 249, 250 or 251";
    return false;
}

bool StartChannelInstaller(std::string& error, bool allow_unmarked, bool update) {
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
    // From the update prompt the installer gets "update": it updates without
    // waiting for a button. It reads only the Wii Remote's own buttons and the
    // Wii's own GameCube ports, so a player with a GameCube controller on a
    // Wii U's adapter couldn't press one there (31 Oct, on a Wii U).
    const std::string line = update ? riftwii::app_command_line(kInstaller, {"update"}) : std::string();
    if (update) logf("Channel: the installer updates at once (its \"update\" argument).\n");
    NetStopForLaunch();
    LogClose();
    fatUnmount("sd:");
    fatUnmount("usb:");
    AUDIO_StopDMA();
    if (!line.empty()) dolboot_run_line(dol, line.data(), static_cast<u32>(line.size()));
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

namespace {

// The app's DOL, whole, in memory of its own (nullptr when it is missing
// or there is no room for it).
u8* read_homebrew_dol(const std::string& path, u32& size) {
    size = 0;
    u8* dol = nullptr;
    if (riftwii::is_usb_path(path)) {
        std::uint64_t bytes = 0;
        if (read_usb_range(path, 0, nullptr, 0, bytes) && bytes > 0x100 && bytes <= (16u << 20) && can_allocate(bytes)) {
            size = static_cast<u32>(bytes);
            dol = static_cast<u8*>(memalign(32, size));
            if (dol && !read_usb_range(path, 0, dol, size, bytes)) {
                std::free(dol);
                dol = nullptr;
            }
        }
    } else {
        dol = dolboot_read(path.c_str(), &size, [](u32 n) -> void* {
            return can_allocate(n) ? memalign(32, n) : nullptr;
        }, [](void* p) { std::free(p); });
    }
    return dol;
}

// The app read once more and compared with `dol`, the copy about to start:
// a copy read on a Wii U around the IOS reload came out damaged once, and
// the app's unpacker stopped on a red screen. Two reads in a row that agree
// are taken as the app, up to three more reads when they don't. Returns the
// copy to start (`dol` or a later read; the others are freed), or nullptr
// when no two reads agreed. With no room for a second copy, `dol` is
// started unchecked, as before; once two reads have differed, a read that
// fails leaves nothing to start.
u8* check_homebrew_dol(const std::string& path, u8* dol, u32& size) {
    constexpr int kExtraReads = 3;
    for (int read = 2; read <= kExtraReads + 1; ++read) {
        u32 again_size = 0;
        u8* again = read_homebrew_dol(path, again_size);
        if (!again && read > 2) {
            logf("Homebrew app: read %d failed after two reads differed; the app is not started\n", read);
            std::free(dol);
            return nullptr;
        }
        if (!again) {
            logf("Homebrew app: not checked (no room for a second copy, or the second read failed)\n");
            return dol;
        }
        u32 differing = 0;
        if (again_size == size) {
            for (u32 at = 0; at < size; ++at) differing += dol[at] != again[at];
        }
        if (again_size == size && differing == 0) {
            std::free(again);
            logf("Homebrew app: read %d matches the one before it\n", read);
            return dol;
        }
        logf("Homebrew app: read %d differs from the one before it (%u of %u bytes); reading it again\n", read,
             static_cast<unsigned>(again_size == size ? differing : again_size), static_cast<unsigned>(size));
        std::free(dol);
        dol = again;
        size = again_size;
    }
    std::free(dol);
    return nullptr;
}

}  // namespace

bool StartHomebrewApp(const std::string& path, const std::string& command_line, std::string& error) {
    if (command_line.empty() || command_line.size() > 1024 || command_line.back() != '\0') {
        error = "The app arguments are empty or exceed the 1024-byte limit.";
        logf("Homebrew app: %s\n", error.c_str());
        return false;
    }
    u32 size = 0;
    u8* dol = read_homebrew_dol(path, size);
    if (!dol || !dolboot_valid(dol, size)) {
        error = path + " is missing, damaged or does not fit next to RiftWii";
        logf("Homebrew app: %s\n", error.c_str());
        std::free(dol);
        return false;
    }
    logf("Homebrew app: starting %s (%u bytes)\n", path.c_str(), static_cast<unsigned>(size));
    NetStopForLaunch();
    // The menu's USB users close first, so their requests don't outlive it
    // (an IOS reload below closes them too; this is for when there's none).
    GcAdapterStop();
    UsbInputStop();
    LaunchMusicEnd("the app starts next");
    // On the menu's IOS58 the app starts on a fresh IOS58, as it would from
    // the Homebrew Channel. Once /dev/usb/hid has been used on an IOS, a new
    // program's device list comes back empty even with every user closed, so
    // the app would see no GameCube adapter, USB pad or keyboard (Dolphin and
    // a Wii U both). Not under a cIOS: there d2x may be serving the app a
    // disc (launchrun.cpp), and a reload would take it away.
    const bool fresh_ios = IOS_GetVersion() == 58;
    if (fresh_ios) {
        const bool on_usb = riftwii::is_usb_path(path);
        // Hardware access, as the Homebrew Channel leaves it for an app that
        // asks for it (<ahb_access/>), carried over to the new IOS.
        if (read32(0x0D800064) == 0xFFFFFFFF) keep_hardware_access("Homebrew app");
        release_wii_remotes();  // Bluetooth stops before its IOS goes
        logf("Homebrew app: on a fresh IOS58\n");
        LogClose();
        fatUnmount("sd:");
        release_usb_driver();
        // libogc's USB opens afresh on the new IOS, as after the USB check's
        // reloads (usbprobe.cpp): its handles belong to the IOS that goes,
        // and a drive is not listed through them again.
        USB_Deinitialize();
        libogc_sd_stop();
        std::string reload_error;
        if (reload_ios(58, reload_error, true) == ReloadResult::Terminal) halt_after_terminal_reload();
        const bool card = libogc_sd_start() && fatMountSimple("sd", sd_interface());
        if (card) LogReopen();
        logf("Homebrew app: IOS%d rev %d now%s\n", IOS_GetVersion(), IOS_GetRevision(),
             reload_error.empty() ? "" : (" (" + reload_error + ")").c_str());
        // The app is read again on the new IOS: on a Wii U the copy read
        // before the reload once came out damaged (a red screen). An app on
        // the USB drive needs the drive back first; the menu's view of it
        // went with the old IOS.
        std::string usb_error;
        const bool drive = on_usb ? mount_usb_files(usb_error) : card;
        if (on_usb && !drive) logf("Homebrew app: the USB drive is not back after the reload: %s\n", usb_error.c_str());
        std::free(dol);
        dol = drive ? read_homebrew_dol(path, size) : nullptr;
        if (dol) dol = check_homebrew_dol(path, dol, size);
        if (!dol || !dolboot_valid(dol, size)) {
            error = tr("{1} could not be read again after the IOS restart", {path});
            logf("Homebrew app: %s\n", error.c_str());
            std::free(dol);
            return false;
        }
    }
    plant_return_stub(dol, size);
    LogClose();
    fatUnmount("sd:");
    // On the fresh IOS the menu's USB driver is closed for the app, which
    // starts its own; under a cIOS the drive is left as it always was.
    if (fresh_ios) release_usb_driver();
    else fatUnmount("usb:");
    AUDIO_StopDMA();
    dolboot_run_line(dol, command_line.data(), static_cast<u32>(command_line.size()));
}

}  // namespace riftwii::wii
