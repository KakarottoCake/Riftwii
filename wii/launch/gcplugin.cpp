// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The DOL's side of a GameCube launch (docs/PLUGINS.md, "Stage (b)"): what
// the GameCube loader needs from the menu's C++ (the settings, the
// launch's extras, the way back's ticket view, the crash screen), looked
// up here and handed to the gamecube plugin as plain C, and the memory
// card's backup, which the menu's own card tools share. The loader itself
// is plugins/gamecube/gcnative.cpp.
#include "gcplugin.hpp"
#include "i18n.hpp"
#include "gcrealcard.hpp"

#include <sys/stat.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>
#include <utility>

#include "boot.hpp"
#include "channel.hpp"
#include "codehandleronly_bin.h"
#include "gc_crash.h"
#include "ios_reload.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "memlimits.hpp"
#include "netsock.hpp"
#include "plugins.hpp"
#include "riftwii/gcbackup.hpp"
#include "riftwii/gccard.hpp"
#include "riftwii/gcrequest.hpp"
#include "riftwii/pluginfile.hpp"
#include "riftwii/returnto.hpp"
#include "umsdev.hpp"
#include "riftwii/plugin_gamecube.h"
#include "sdfile.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kPluginName = "gamecube";
constexpr u64 kWiiMenuTitle = 0x0000000100000002ull;

// The crash screen's card and strip (SetGcCrashScreen), as gc_crash.h
// lays them out, and whether its message promises that A goes back.
std::vector<std::uint8_t> g_crash_screen;
bool g_crash_screen_way_back = false;

// Where the way back goes, as Settings > Return to says and as a Wii
// game's HOME Menu goes (boot.cpp's return_title): on "riftwii", the
// installed RiftWii channel. If its ticket cannot be used, try the Homebrew
// Channel when it started RiftWii, then the Wii Menu. Only an actual launch
// logs the destination; the crash-screen hint's earlier lookup stays quiet.
u64 way_back_title(GcWayBack& kind, tikview& view, bool log_destination = false) {
    const bool back_to_riftwii = Settings().return_to == "riftwii";
    const bool started_by_hbc = back_to_riftwii && StartedByHomebrewChannel();
    const u64 channel = back_to_riftwii ? ChannelTitle() : 0;
    const u64 hbc = started_by_hbc ? HomebrewChannelTitle() : 0;
    for (const std::uint64_t title : gamecube_return_titles(back_to_riftwii, started_by_hbc, channel, hbc)) {
        u32 views = 0;
        if (ES_GetNumTicketViews(title, &views) < 0 || views == 0) continue;
        // ES_GetTicketViews fills as many as asked: room for one is enough.
        static tikview found[4] ATTRIBUTE_ALIGN(32);
        if (ES_GetTicketViews(title, found, 1) < 0) continue;
        view = found[0];
        if (title == kWiiMenuTitle) {
            kind = GcWayBack::WiiMenu;
        } else if (title == channel) {
            kind = GcWayBack::Channel;
        } else {
            kind = GcWayBack::HomebrewChannel;
        }
        if (log_destination && kind == GcWayBack::Channel) {
            logf("GameCube: holding HOME returns to RiftWii through its installed channel.\n");
        } else if (log_destination && kind == GcWayBack::HomebrewChannel) {
            if (channel == 0) {
                logf("GameCube: holding HOME returns to the Homebrew Channel because the RiftWii channel is not installed.\n");
            } else {
                logf("GameCube: holding HOME returns to the Homebrew Channel because the RiftWii channel could not be opened.\n");
            }
        }
        return title;
    }
    kind = GcWayBack::None;
    return 0;
}

std::uint32_t way_back_kind(GcWayBack kind) {
    switch (kind) {
        case GcWayBack::Channel: return RWP_GC_WAY_BACK_CHANNEL;
        case GcWayBack::WiiMenu: return RWP_GC_WAY_BACK_WII_MENU;
        case GcWayBack::HomebrewChannel: return RWP_GC_WAY_BACK_HBC;
        case GcWayBack::None: break;
    }
    return RWP_GC_WAY_BACK_NONE;
}

// Before the game starts: a copy of the memory card it will use in
// sd:/riftwii/gc/backups/<card>/ (riftwii/gcbackup.hpp), the newest five
// kept, none when the card has not changed since the last one. A shared
// card's copies go in backups/shared, as they hold every game's saves.
// Nothing here stops the start: a copy that fails is only logged.
void back_up_card(const GcImage& image) {
    const std::string path = gc_card_path(image.id, image.shared_card);
    const std::string folder = gc_backup_folder(image.id, image.shared_card);
    // The Wii's clock, as the menu's own clock shows it.
    const time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    const GcBackupStamp stamp = gc_backup_stamp(local);
    const u64 started = gettime();
    const GcBackupResult result = gc_backup_card(path, folder, stamp);
    const unsigned ms = static_cast<unsigned>(ticks_to_millisecs(gettime() - started));
    switch (result.what) {
        case GcBackupResult::What::NoCard:
            logf("GameCube: no memory card to back up yet (%s)\n", path.c_str());
            break;
        case GcBackupResult::What::Same:
            logf("GameCube: memory card %s unchanged since its backup %s/%s, so no new copy (%u ms)\n",
                 path.c_str(), folder.c_str(), result.name.c_str(), ms);
            break;
        case GcBackupResult::What::Made:
            logf("GameCube: memory card backed up to %s/%s (%u KB in %u ms)\n", folder.c_str(), result.name.c_str(),
                 static_cast<unsigned>(result.bytes / 1024), ms);
            for (const std::string& old : result.pruned)
                logf("GameCube: oldest backup %s/%s deleted, to keep the newest %u\n", folder.c_str(), old.c_str(),
                     static_cast<unsigned>(kGcBackupsKept));
            break;
        case GcBackupResult::What::Failed:
            logf("GameCube: memory card %s not backed up: %s (%u ms)\n", path.c_str(), result.error.c_str(), ms);
            break;
    }
    // New files change the SD card's folders, which the raw lookups cache.
    if (result.what == GcBackupResult::What::Made) forget_sd_layout();
}

// A card write cut between its two renames left the card at .old: it is
// put back before the game starts, which would otherwise find no card and
// format a new one (NEW-03). False when it is still only at .old: the game
// is not started then. A card beside a .old is used, with a log line.
bool recover_card(const GcImage& image, std::string& error) {
    const std::string path = gc_card_path(image.id, image.shared_card);
    // A card from before an odd ID got a name of its own (MIN-04): copied
    // to the new name once, the old file left for any other ID that shared
    // it.
    const std::string legacy = gc_card_legacy_path(image.id, image.shared_card);
    struct stat card_stat;
    if (!legacy.empty() && stat(path.c_str(), &card_stat) != 0 && stat((path + ".old").c_str(), &card_stat) != 0 &&
        stat(legacy.c_str(), &card_stat) == 0) {
        std::string copy_error;
        if (gc_copy_file(legacy, path, copy_error)) {
            logf("GameCube: memory card %s copied to %s, this game's card name now\n", legacy.c_str(), path.c_str());
            forget_sd_layout();
        } else {
            // Not started: the game would make a new empty card at the new
            // name, and the old one would never be copied over it.
            logf("GameCube: memory card %s not copied to %s: %s\n", legacy.c_str(), path.c_str(), copy_error.c_str());
            error = "The game's memory card at " + legacy + " could not be copied to " + path + ": " + copy_error +
                    " The game was not started, so it cannot make a new card in its place.";
            return false;
        }
    }
    std::string why;
    switch (gc_recover_card(path, why)) {
        case GcRecovery::Nothing:
            return true;
        case GcRecovery::PutBack:
            logf("GameCube: memory card %s was left at .old by a write cut short; put back\n", path.c_str());
            forget_sd_layout();  // a renamed file changes the folder the raw lookups cache
            return true;
        case GcRecovery::BothKept:
            logf("GameCube: %s\n", why.c_str());
            return true;
        case GcRecovery::Failed:
            break;
    }
    logf("GameCube: %s\n", why.c_str());
    error = why + " The game was not started, so it cannot make a new card in its place.";
    return false;
}

}  // namespace

GcWayBack FindGcWayBack() {
    GcWayBack kind;
    static tikview view ATTRIBUTE_ALIGN(32);
    way_back_title(kind, view);
    return kind;
}

void SetGcCrashScreen(std::vector<std::uint8_t> screen, bool way_back) {
    g_crash_screen.clear();
    g_crash_screen_way_back = way_back;
    if (screen.size() == GC_CRASH_SCREEN_BYTES) g_crash_screen = std::move(screen);
}

bool LaunchGcNative(const GcImage& image, const GcOverlayPlan* pack, std::string& error) {
    NetStopForLaunch();
    // The backup first, as before the loader moved into the plugin: before
    // the game can write to the card.
    std::array<std::uint8_t, 12> physical_id{};
    if (image.real_card) {
        GcRealCard physical;
        std::vector<std::uint8_t> system;
        GcCardSpace space;
        if (physical.Open(0) < 0 || physical.ReadSystem(system) < 0 ||
            !gc_card_space(system, physical.Bytes(), space, error) || !gc_card_flash_id(system, physical_id)) {
            error = tr("The memory card could not be read. It may have changed or been removed. Nothing was copied.");
            return false;
        }
        if ((system[0x25] != 0) != gc_card_shift_jis(image.id)) {
            error = tr("This card uses the other text encoding. Japanese and Western cards cannot be mixed.");
            return false;
        }
        // The game's SDK may check the header against SRAM. An earlier
        // emulated-card launch may have left an SD ID there. libogc's mount
        // only checks the cached ID's byte sum, not the header identity.
        // Pass this physical header's ID, preserving its serial/saves.
        logf("GameCube: the game will use the real memory card in slot A because its Memory card option selects it.\n");
    } else {
        if (!recover_card(image, error)) return false;
        if (Settings().gc_backups != "off") back_up_card(image);
        logf("GameCube: the game will use %s because its Memory card option selects an SD image.\n",
             gc_card_path(image.id, image.shared_card).c_str());
    }

    rwp_gc_launch request{};
    request.size = sizeof(request);
    request.path = image.path.c_str();
    request.id = image.id.c_str();
    request.is_rvz = GcImageIsRvz(image.path) ? 1 : 0;
    request.real_card = image.real_card ? 1 : 0;
    std::copy(physical_id.begin(), physical_id.end(), request.real_card_flash_id);
    request.shared_card = image.shared_card ? 1 : 0;
    request.drive_timing = image.drive_timing ? 1 : 0;
    request.progressive = image.progressive ? 1 : 0;
    request.widescreen = image.widescreen ? 1 : 0;
    request.sharp = image.sharp ? 1 : 0;
    request.sixty_hz = image.sixty_hz ? 1 : 0;
    request.language = image.language;
    request.vi_width = image.vi_width;
    // A game packed in a TGC image, started from the disc's menu last time.
    request.tgc_offset = image.tgc_offset;

    // A game or a pack on the USB drive: d2x's USB device its launch opened
    // (gclaunch.cpp's ready_usb) and, for a game on it, the image's pieces.
    // Kept here until the launch returns: the request points into them.
    std::vector<rwp_fragment> usb_fragments;
    request.usb_fd = -1;
    if (image.usb_in_use) {
        if (ums::Fd() < 0 || (GcImageOnUsb(image.path) && image.usb_fragments.empty())) {
            error = "the USB drive was not made ready for the game";
            return false;
        }
        request.usb_fd = ums::Fd();
        for (const Fragment& f : image.usb_fragments) usb_fragments.push_back(rwp_fragment{f.sector, f.sector_count});
        request.usb_fragments = usb_fragments.empty() ? nullptr : usb_fragments.data();
        request.usb_fragment_count = static_cast<std::uint32_t>(usb_fragments.size());
        request.usb_file_bytes = image.usb_bytes;
    }
    // A two-disc game's second disc, with its own pieces on the USB drive.
    std::vector<rwp_fragment> disc2_fragments;
    if (!image.disc2_path.empty()) {
        request.disc2_path = image.disc2_path.c_str();
        for (const Fragment& f : image.disc2_usb_fragments) disc2_fragments.push_back(rwp_fragment{f.sector, f.sector_count});
        request.disc2_usb_fragments = disc2_fragments.empty() ? nullptr : disc2_fragments.data();
        request.disc2_usb_fragment_count = static_cast<std::uint32_t>(disc2_fragments.size());
        request.disc2_usb_file_bytes = disc2_fragments.empty() ? 0 : image.disc2_usb_bytes;
    }

    // A program switch's arguments, each ending in a NUL; kept here until
    // the launch returns.
    std::string exec_args;
    if (image.exec) {
        request.exec = 1;
        request.exec_dol = image.exec_dol;
        request.exec_restart = image.exec_restart;
        for (const std::string& a : image.exec_args) exec_args.append(a).push_back('\0');
        request.exec_args = exec_args.empty() ? nullptr : exec_args.data();
        request.exec_args_bytes = static_cast<std::uint32_t>(exec_args.size());
        request.exec_arg_count = static_cast<std::uint32_t>(image.exec_args.size());
    }

    // Kept here until the launch returns: the request points into them.
    std::vector<rwp_gc_pack_entry> pack_entries;
    if (pack != nullptr) gc_plan_to_request(*pack, pack_entries, request);

    // The settings and the launch's extras, as they are now.
    const LaunchExtras& extras = CurrentLaunchExtras();
    request.rumble_off = extras.rumble_off ? 1 : 0;
    request.in_dolphin = running_in_dolphin() ? 1 : 0;
    if (!extras.cheat_gct.empty()) {
        request.cheat_gct = extras.cheat_gct.data();
        request.cheat_gct_bytes = static_cast<std::uint32_t>(extras.cheat_gct.size());
    }
    request.cheat_count = static_cast<std::uint32_t>(extras.cheat_count);
    // A code build's gameconfig.txt (Project+) is for Wii games: its list
    // place and hook type mean nothing for a GameCube game, whose loader
    // turns its codes off when this says so.
    request.code_build_codes =
        (!extras.code_builds.empty() || extras.code_list_start != 0 || extras.code_hooktype == 7) ? 1 : 0;
    request.code_handler = codehandleronly_bin;
    request.code_handler_bytes = codehandleronly_bin_size;

    // The GameCube adapter: opened here, on the menu's IOS the game keeps,
    // as a Wii game's launch opens it (gc_gcad.h).
    request.adapter_fd = -1;
    request.adapter_dev = -1;
    {
        std::int32_t fd = -1, dev = -1;
        std::uint32_t version = 0;
        if (TakeGcAdapterForGameCube(image.usb_in_use, fd, version, dev)) {
            request.adapter_fd = fd;
            request.adapter_version = version;
            request.adapter_dev = dev;
            request.adapter_rumble = extras.gc_rumble_off ? 0 : 1;
        }
    }

    if (g_crash_screen.size() == GC_CRASH_SCREEN_BYTES) {
        request.crash_screen = g_crash_screen.data();
        request.crash_screen_bytes = static_cast<std::uint32_t>(g_crash_screen.size());
    }
    request.crash_screen_way_back = g_crash_screen_way_back ? 1 : 0;

    GcWayBack kind = GcWayBack::None;
    static tikview view ATTRIBUTE_ALIGN(32);
    request.way_back_title = way_back_title(kind, view, true);
    request.way_back_kind = way_back_kind(kind);
    if (kind != GcWayBack::None) {
        request.way_back_view = &view;
        request.way_back_view_bytes = sizeof(view);
    }

    char reason[512] = "";
    request.error = reason;
    request.error_bytes = sizeof(reason);
    uint32_t error_code = RWP_GC_ERROR_NONE;
    request.error_code = &error_code;

    std::string message;
    const int32_t result = plugin::Launch(kPluginName, &request, sizeof(request), message);
    if (result == RWP_LAUNCH_NO_MEMORY) {
        // The launch's out-of-memory message, as when the loader was in
        // the DOL.
        logf("GameCube: the gamecube plugin ran out of memory: %s\n", reason);
        error = mem::kLaunchOutOfMemory;
        return false;
    }
    if (result < 0) {
        logf("GameCube games need RiftWii's gamecube plugin in %s (%s), and it could not be used: %s\n",
             plugin::Directory().c_str(), plugin_file_name(kPluginName, RIFTWII_VERSION).c_str(), message.c_str());
        error = message;
        return false;
    }
    if (error_code == RWP_GC_ERROR_REAL_CARD_SRAM) {
        logf("GameCube: the real card could not be prepared because its ID could not be written to SRAM.\n");
        error = tr("The real card could not be prepared. Check the card and try again.");
    } else {
        error = reason[0] != '\0' ? reason : "the gamecube plugin did not start the game";
    }
    return false;
}

}  // namespace riftwii::wii
