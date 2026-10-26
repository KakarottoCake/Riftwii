// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "launchrun.hpp"
#include "launchsession.hpp"
#include "d2xsave.hpp"
#include "d2xsd.hpp"
#include "progress.hpp"
#include "riftwii/ctgpconfig.hpp"
#include "riftwii/textlines.hpp"

#include <fat.h>
#include <gccore.h>
#include <ogc/system.h>
#include <strings.h>
#include <sys/stat.h>

#include <cstdio>

#include "boot.hpp"
#include "channel.hpp"
#include "di.hpp"
#include "frontend.hpp"
#include "gameextras.hpp"
#include "gclaunch.hpp"
#include "ios_reload.hpp"
#include "log.hpp"
#include "memlimits.hpp"
#include "menuios.hpp"
#include "menumusic.hpp"
#include "modplan.hpp"
#include "netpacks.hpp"
#include "sdfile.hpp"
#include "sharednandfind.hpp"
#include "textfile.hpp"
#include "umsdev.hpp"
#include "usbprobe.hpp"

namespace riftwii::wii {
namespace {

std::string basename_of(const std::string& disc_path) {
    const auto slash = disc_path.find_last_of('/');
    return slash == std::string::npos ? disc_path : disc_path.substr(slash + 1);
}

// CTGP Revolution's channel, started under a d2x cIOS: its own IOS exploit
// can wait forever there for hardware access, so sd:/ctgpr/config.ini gets
// "disable_ios_exploit = yes" in [exploit] unless the player set that key.
// Before any IOS reload, while libfat has the card.
void SetCtgpDefaults(const std::string& app) {
    const std::string folder = "sd:/apps/ctgpr/";
    if (app.size() <= folder.size() || strncasecmp(app.c_str(), folder.c_str(), folder.size()) != 0 ||
        app.find('/', folder.size()) != std::string::npos)
        return;
    struct stat st;
    if (stat("sd:/ctgpr", &st) != 0 || !S_ISDIR(st.st_mode)) return;
    const char* path = "sd:/ctgpr/config.ini";
    // No file yet: the defaults go into an empty one.
    std::string text;
    ReadWholeFile(path, text);
    if (!riftwii::ctgp_config_defaults(text)) {
        logf("CTGP: config.ini already has disable_ios_exploit, left as it is\n");
        return;
    }
    if (!WriteWholeFile(path, text.data(), text.size())) {
        logf("CTGP: could not write %s\n", path);
        return;
    }
    forget_sd_layout();
    logf("CTGP: config.ini set to skip CTGP's own IOS exploit ([exploit] disable_ios_exploit = yes)\n");
}

// Before the compile, from the packs' own places (d2xnandemu.hpp): the
// compile's table can only be checked after the probe that turns d2x's
// emulation on.
bool packages_need_sd(const std::vector<PackageChoices>& packages) {
    std::vector<std::string> xml_paths;
    for (const PackageChoices& p : packages) xml_paths.push_back(p.xml_sd_path);
    return packs_need_sd(xml_paths);
}

bool any_on_usb(const std::vector<PackageChoices>& packages) {
    for (const PackageChoices& p : packages) {
        if (p.xml_sd_path.compare(0, 5, "usb:/") == 0) return true;
    }
    return false;
}


}  // namespace

void TakeCardBackFromD2x() {
    if (!sd_held_by_d2x_fat()) return;
    if (!release_d2x_saves()) return;
    if (fatMountSimple("sd", sd_interface())) LogReopen();
}

// every later IOS reload (the menu's own) would land in the cIOS instead.
void ClearIosReloadBlock(std::string& error) {
    if (!di::ios_reload_block_active()) return;
    std::string why;
    if (di::set_ios_reload_block(false, 0, why)) {
        logf("d2x: IOS reload block cleared\n");
        return;
    }
    logf("d2x: IOS reload block could not be cleared (%s)\n", why.c_str());
    error += "; restart the console before starting another game";
}

bool Session::ensure_probe(std::string& error) {
    if (probed) return true;
    ProgressStage(source.kind == LaunchSource::Kind::Disc ? "Reading the disc" : "Opening the game image", 3);
    if (source.kind != LaunchSource::Kind::Disc) {
        bool active = false;
        // Saves on the SD card kept by d2x (wii/storage/d2xsave.hpp): the
        // folders and the Wii's save first, under the menu's IOS. Not
        // for an RVZ on the card (RiftWii reads it from the card while
        // the game runs) or a game whose title ID is not known.
        bool d2x_ready = false;
        if (!own_saves_dir.empty() && cios_paths_enabled()) {
            std::string why;
            if (source.game.title_id == 0) why = "the game's title ID is not known";
            else if (sd_after_probe)
                why = "RiftWii reads the SD card while the game loads and runs (packs or a code build on it), and "
                      "d2x's FAT module must have the card to itself";
            // An RVZ on any drive: its group table and stub are files on the
            // SD card (sd:/riftwii/rvz), read and written at the launch, and
            // the runtime holds the card for the game's reads.
            else if (source.game.format == UsbImageFormat::Rvz)
                why = "an RVZ game (RiftWii keeps its group table on the SD card and reads the card as it runs)";
            else d2x_ready = prepare_d2x_saves(source.game, own_saves_dir, own_saves_clone,
                                                    own_saves_clone_empty, why);
            if (!d2x_ready) logf("Saves: RiftWii's own save code, not d2x's NAND emulation (%s)\n", why.c_str());
        }
        // When every slot fails, the first one's reason is the one to
        // show (249: "not a d2x cIOS", not "251 is not installed").
        // A drive d2x cannot read is said after every slot, each reason
        // once with the slots that gave it: another slot's d2x base may
        // reach the port (a Wii U's d2x often sees one rear port only).
        std::string first_error;
        std::vector<D2xSlotFailure> usb_failures;
        int used_slot = 0;
        const std::vector<int> cios_order = image_cios_order(source.game, source.cios_slot, sd_in_use);
        std::string slots;
        for (int slot : cios_order) slots += (slots.empty() ? "IOS" : ", IOS") + std::to_string(slot);
        logf("%s: %s will try cIOS slots in this order: %s\n",
             source.kind == LaunchSource::Kind::Usb ? "USB" : "SD", source.game.id.c_str(), slots.c_str());
        for (int slot : cios_order) {
            if (activate_image_game(source.game, slot, frag_storage, frag_storage_bytes, log_path, error,
                                    block_ios_reload)) { active=true; used_slot=slot; break; }
            if (reload_terminal_failure()) return false;
            if (d2x_usb_drive_failed()) {
                usb_failures.push_back({slot, error});
            } else if (first_error.empty()) {
                first_error = error;
            }
            // A call d2x never answered can't be cancelled, and another IOS
            // reload with it outstanding may hang: no further slot.
            if (ums::Stuck()) break;
        }
        if (!active) {
            if (!usb_failures.empty()) error = d2x_usb_slots_summary(usb_failures, first_error);
            else if (!first_error.empty()) error = first_error;
            return false;
        }
        // Before the partition is opened: d2x turns its NAND
        // emulation on only while no title runs. A game on the SD card
        // itself only with d2x on base 56 or 57 (USB Loader GX's rule:
        // the others cannot read the game and keep the save on one card).
        ProbeOptions virtual_disc;
        virtual_disc.virtual_source = true;
        if (d2x_ready) {
            // Partial emulation redirects /ticket/00010001 too. Resolve
            // the real NAND's return channel before that lookup changes.
            virtual_disc.return_to = debug_off("returnto") ? 0 : return_title();
            const int base = d2x_base(used_slot);
            std::string why;
            if (source.game.device == ImageDevice::Sd && base != 56 && base != 57)
                logf("Saves: RiftWii's own save code, not d2x's NAND emulation (a game on the SD card with d2x "
                     "base %d; d2x keeps both on one card on base 56 or 57)\n", base);
            else if (!(d2x_saves = enable_d2x_saves(own_saves_dir, why))) {
                // d2x's FAT module would not let go of the card: RiftWii's
                // own save code could not reach it, so the launch stops
                // here (the session gives the card back if it can).
                if (sd_held_by_d2x_fat()) {
                    error = "Saves: " + why;
                    return false;
                }
                logf("Saves: RiftWii's own save code, not d2x's NAND emulation (%s)\n", why.c_str());
            }
        }
        open_usb_for_packs();
        logf("%s: virtual-disc probe\n", source.kind == LaunchSource::Kind::Usb ? "USB" : "SD");
        if (!probe_disc(probe, error, virtual_disc)) return false;
        mem::CheckHeap("after the disc probe");
        if (probe.header.game_id != source.game.id || probe.header.version != source.game.revision ||
            probe.header.disc_number != source.game.disc_number) {
            error="d2x virtual disc identity does not match the selected image";
            return false;
        }
    } else {
        // Cover status cannot identify a swap that happened between menu
        // polls. Keep the launch's full probe independent of the page cache.
        di::disc_cache().begin_launch();
        if (usb_packs && MenuCiosSlot() == 0) {
            if (!activate_disc_cios(source.cios_slot, log_path, error)) return false;
            on_cios = true;
            // A heap damaged across the reload (a tester's Wii U) would
            // only fail later, somewhere else: stop here, with the reason.
            if (!mem::CheckHeap("after the cIOS reload")) {
                error = "RiftWii's memory was damaged during the reload into IOS" +
                        std::to_string(IOS_GetVersion()) + " (the details are in boot.log)";
                return false;
            }
            // The menu's music again (debug_off = musicresume keeps it quiet).
            if (!debug_off("musicresume")) LaunchMusicResumeAfterReload();
        }
        open_usb_for_packs();
        if (!probe_disc(probe, error)) return false;
        mem::CheckHeap("after the disc probe");
    }
    probed = true;
    return true;
}

// A failure is only logged here: the pack that needs the drive says
// why when it is read.
void Session::open_usb_for_packs() {
    if (!usb_packs) return;
    std::string why;
    if (ums::OpenWaiting(why, 20)) logf("USB: d2x's /dev/usb2 opened for the packs, before the game partition\n");
    else logf("USB: d2x's /dev/usb2 for the packs: %s\n", why.c_str());
    mem::CheckHeap("after d2x's USB device");
}

bool Session::ensure_layout(std::string& error) {
    if (!ensure_probe(error)) return false;
    if (laid_out) return true;
    if (!read_partition_layout(partition, error)) return false;
    mem::CheckHeap("after the file table");
    laid_out = true;
    return true;
}

// A GameCube game as its launch takes it: on the card or the USB drive,
// with the d2x slot picked for it (used on the USB drive).
static GcImage gc_image_of(const LaunchSource& source) {
    GcImage image;
    image.path = source.game.path;
    image.id = source.game.id;
    image.patch_source = source.game.gc_patch_source;
    image.cios_slot = source.cios_slot;
    return image;
}

bool RunBoot(bool allow_ios_fallback, std::string& error, const LaunchSource& source) {
    if (source.game.gamecube) return LaunchGcGame(gc_image_of(source), {}, error);
    Session s(source, "sd:/riftwii/boot.log");
    if (!s.ensure_probe(error)) return false;
    BootOptions options;
    options.allow_ios_fallback = allow_ios_fallback;
    options.preserve_current_ios = source.kind != LaunchSource::Kind::Disc || MenuCiosSlot() != 0;
    return boot_game(s.probe, std::move(options), error);
}

bool ProbeInserted(std::string& game_id, std::string& title, std::string& error,
                   std::uint8_t* revision, std::uint8_t* disc_number) {
    if (!di::open(error)) return false;
    bool inserted = false;
    if (!di::cover_status(inserted, error)) return false;
    if (!inserted) {
        error = "no disc in the drive";
        return false;
    }
    // The menu only needs the header; an opened partition would leave a
    // d2x Menu IOS refusing USB for the rest of the session.
    DiscProbe probe;
    ProbeOptions options;
    options.header_only = true;
    if (!probe_disc(probe, error, options)) return false;
    game_id = probe.header.game_id;
    title = probe.header.title;
    if (revision) *revision = probe.header.version;
    if (disc_number) *disc_number = probe.header.disc_number;
    return true;
}

// A compile's warnings and notes for the log. A big mod has a note per
// file and every log line is a card write, so past kMaxNoteLines only the
// skipped files are listed and the rest counted.
void LogCompileNotes(const CompiledMod& mod) {
    constexpr std::size_t kMaxNoteLines = 60;
    LogBatch batch;
    for (const std::string& w : mod.warnings) logf("  warning: %s\n", w.c_str());
    if (mod.notes.size() <= kMaxNoteLines) {
        for (const std::string& n : mod.notes) logf("  %s\n", n.c_str());
        return;
    }
    std::size_t shown = 0, hidden = 0;
    for (const std::string& n : mod.notes) {
        if (shown < kMaxNoteLines && n.find("skipped") != std::string::npos) {
            logf("  %s\n", n.c_str());
            ++shown;
        } else {
            ++hidden;
        }
    }
    logf("  (%u more note(s): files placed, relocated or created)\n", static_cast<unsigned>(hidden));
}

bool CompileSelection(const std::vector<PackageChoices>& packages, CompiledMod& out, std::string& error, const LaunchSource& source) {
    Session s(source, "sd:/riftwii/boot.log");
    s.usb_packs = any_on_usb(packages);
    if (!s.ensure_layout(error)) return false;
    CompiledMod mod;
    if (!compile_packages(packages, s.probe, s.partition, mod, error)) return false;
    mod.source_identity = s.probe.header.identity();
    mod.has_source_identity = true;
    LogCompileNotes(mod);
    logf("%u package(s): %u table entries, %u relocation(s), %u memory patch(es)\n",
         static_cast<unsigned>(packages.size()), static_cast<unsigned>(mod.entries.size()),
         static_cast<unsigned>(mod.relocations.size()), static_cast<unsigned>(mod.memory.size()));
    out = std::move(mod);
    return true;
}

// The folder of the Saves choice "SD, emulated NAND" (empty for the other
// choices). False when it cannot be used: the launch stops with the reason
// rather than keep the save somewhere the player did not choose.
bool FindSharedSaves(const std::string& save_mode, const std::string& game_id, std::string& shared,
                     std::string& error) {
    shared.clear();
    if (save_mode != "emunand" || game_id.empty() || LaunchPackKeepsSaves()) return true;
    std::string why;
    if (find_shared_nand(game_id, shared, why)) return true;
    error = "Saves: " + why;
    return false;
}

// The game page's Saves for a launch (a pack's own <savegame> wins: then
// none). Headless runs pass "nand".
void SetOwnSaves(Session& s, const std::string& save_mode, const std::string& game_id,
                 const std::string& shared_nand) {
    if (LaunchPackKeepsSaves()) return;
    const SaveOverride own = resolve_save_override(save_mode, "", game_id, shared_nand);
    s.own_saves_dir = own.dir;
    s.own_saves_clone = own.clone;
    s.own_saves_clone_empty = own.clone_into_empty;
}

// d2x keeps the save (the runtime leaves it alone), else the runtime
// serves the folder's title/<type>/<id>/data. A pack's <savegame> as
// before.
void KeepOwnSaves(const Session& s, bool xml_saves, BootOptions& options) {
    if (xml_saves || options.savegame_dir.empty()) return;
    if (s.d2x_saves) {
        logf("Saves: kept by d2x's NAND emulation in %s\n", options.savegame_dir.c_str());
        options.savegame_dir.clear();
        options.savegame_clone = false;
        options.savegame_clone_empty = false;
        options.install_resident = !options.table_entries.empty() || !options.replacements.empty() ||
                                   !options.relocations.empty();
        return;
    }
    options.savegame_own = true;
}

bool BootCompiled(CompiledMod&& mod, std::string& error, const LaunchSource& source,
                    const std::string& save_mode, const std::string& game_id) {
    Session s(source, "sd:/riftwii/boot.log");
    std::string shared_nand;
    if (!FindSharedSaves(save_mode, game_id, shared_nand, error)) return false;
    SetOwnSaves(s, save_mode, game_id, shared_nand);
    s.sd_in_use = !mod.entries.empty() || !mod.savegame_dir.empty() || !s.own_saves_dir.empty();
    s.sd_after_probe = packs_need_sd(mod.xml_paths) || entries_need_sd(mod.entries) ||
                       !CurrentLaunchExtras().code_builds.empty();
    if (!s.ensure_probe(error)) return false;
    if (mod.has_source_identity && !same_disc_identity(mod.source_identity, s.probe.header.identity())) {
        error = "disc changed since preflight; recompile the selected packages";
        return false;
    }
    // The UI's save separation applies only when no <savegame external>
    // patch claimed a folder (XML wins); headless runs pass "nand".
    const SaveOverride saves = resolve_save_override(save_mode, mod.savegame_dir, game_id, shared_nand);
    const bool xml_saves = !mod.savegame_dir.empty();
    const std::string dir = xml_saves ? mod.savegame_dir : saves.dir;
    BootOptions options;
    options.allow_ios_fallback = true;
    options.preserve_current_ios = source.kind != LaunchSource::Kind::Disc || MenuCiosSlot() != 0;
    options.install_resident = !mod.entries.empty() || !mod.mem.empty() || !mod.relocations.empty() || !dir.empty();
    options.resident_gecko = false;
    // Moved, not copied: the menu's compiled mod is not needed again.
    options.table_entries = std::move(mod.entries);
    options.replacements = std::move(mod.mem);
    options.relocations = std::move(mod.relocations);
    options.memory_patches = std::move(mod.memory);
    options.main_dol = std::move(mod.main_dol);
    options.banner_names = mod.banner_names;
    options.savegame_dir = dir;
    options.savegame_clone = xml_saves ? mod.savegame_clone : saves.clone;
    options.savegame_clone_empty = !xml_saves && saves.clone_into_empty;
    if (!xml_saves && !saves.note.empty()) logf("Saves: %s\n", saves.note.c_str());
    KeepOwnSaves(s, xml_saves, options);
    return boot_game(s.probe, std::move(options), error);
}

bool RunLaunch(const std::vector<PackageChoices>& packages, std::string& error, const LaunchSource& source,
               const std::string& save_mode, const std::string& game_id) {
    // A GameCube game: RiftWii's own loader, its packs served past the image.
    if (source.game.gamecube) return LaunchGcGame(gc_image_of(source), packages, error);
    {
        // A pack that only swaps the game's executable for a Homebrew
        // Channel app (CTGP Revolution 1.03's channel): the app is started
        // the way the Homebrew Channel starts it, not loaded as the game. It
        // reads the game from the disc drive itself. For a disc, before a
        // probe or an IOS reload would take its IOS and hardware access
        // away; for a game on the USB drive or the SD card, after d2x was
        // set up to serve that game as the disc (the reload into the cIOS
        // ends the hardware access; the app runs under the cIOS).
        std::string app, dropped;
        if (homebrew_app_stand_in(packages, game_id, app, dropped)) {
            logf("Mods: the packs only replace the game's executable with %s, a Homebrew Channel app: started as "
                 "the Homebrew Channel starts it, not loaded as the game%s\n",
                 app.c_str(), dropped.empty() ? "" : ("; " + dropped + " does not apply to it").c_str());
            SetCtgpDefaults(app);
            if (source.kind != LaunchSource::Kind::Disc) {
                // The app reloads IOS itself (CTGP asks for IOS37), which
                // would drop d2x and the game with it: d2x blocks that.
                Session s(source, "sd:/riftwii/boot.log");
                s.block_ios_reload = true;
                const bool probed = s.ensure_probe(error);
                std::string ignored;
                // closepart: the partition stays open, so ES still names the
                // disc's title when the app reloads IOS (d2x keeps its
                // virtual disc across a reload only for a disc game).
                if (debug_off("closepart")) logf("Homebrew app: debug_off: the disc's partition stays open\n");
                else di::close_partition(ignored);
                if (!probed) {
                    ClearIosReloadBlock(error);
                    return false;
                }
                di::close();
                s.keep_reload_block = true;
                logf("Homebrew app: d2x serves %s as the disc for it\n", source.game.id.c_str());
            }
            logf("Homebrew app: IOS%d, hardware access %s\n", IOS_GetVersion(),
                 *reinterpret_cast<volatile u32*>(0xCD800064) == 0xFFFFFFFFu ? "on" : "off");
            if (StartHomebrewApp(app, error)) return true;
            ClearIosReloadBlock(error);
            return false;
        }
    }
    // Keep one session across activation, DI probing, package compilation and
    // boot. In particular, a USB fragment list must survive the cIOS reload.
    Session s(source, "sd:/riftwii/boot.log");
    s.usb_packs = any_on_usb(packages);
    std::string shared_nand;
    if (!FindSharedSaves(save_mode, game_id, shared_nand, error)) return false;
    SetOwnSaves(s, save_mode, game_id, shared_nand);
    s.sd_in_use = !packages.empty() || !s.own_saves_dir.empty();
    s.sd_after_probe = packages_need_sd(packages) || !CurrentLaunchExtras().code_builds.empty();
    if (!s.ensure_layout(error)) return false;
    CompiledMod mod; if (!compile_packages(packages, s.probe, s.partition, mod, error)) return false;
    // A pack on the USB drive whose files turned out to be on the SD card:
    // the runtime would read the card that d2x's emulation holds, and the
    // emulation cannot be turned off once the game's partition is open.
    // The launch stops (the session gives d2x's card back) rather than put
    // two drivers on the card.
    if (s.d2x_saves && entries_need_sd(mod.entries)) {
        error = "a pack on the USB drive has files on the SD card, which d2x's NAND emulation keeps to itself while "
                "the game runs: move the pack's files to the USB drive, or pick another Saves choice for the game";
        return false;
    }
    LogCompileNotes(mod);
    logf("%u package(s): %u table entries, %u relocation(s), %u memory patch(es)\n",
         static_cast<unsigned>(packages.size()), static_cast<unsigned>(mod.entries.size()),
         static_cast<unsigned>(mod.relocations.size()), static_cast<unsigned>(mod.memory.size()));
    const SaveOverride saves = resolve_save_override(save_mode, mod.savegame_dir, game_id, shared_nand);
    const bool xml_saves = !mod.savegame_dir.empty();
    const std::string dir = xml_saves ? mod.savegame_dir : saves.dir;
    BootOptions options; options.allow_ios_fallback=true;
    options.preserve_current_ios = source.kind != LaunchSource::Kind::Disc || MenuCiosSlot() != 0 || s.on_cios;
    options.install_resident=!mod.entries.empty() || !mod.mem.empty() || !mod.relocations.empty() || !dir.empty();
    // Moved, not copied: the compiled mod is not needed again, and a pack's
    // main.dol and replacements can be megabytes.
    options.table_entries=std::move(mod.entries); options.replacements=std::move(mod.mem);
    options.relocations=std::move(mod.relocations); options.memory_patches=std::move(mod.memory);
    options.main_dol=std::move(mod.main_dol);
    options.banner_names=mod.banner_names;
    options.savegame_dir=dir; options.savegame_clone=xml_saves ? mod.savegame_clone : saves.clone;
    options.savegame_clone_empty = !xml_saves && saves.clone_into_empty;
    if (!xml_saves && !saves.note.empty()) logf("Saves: %s\n", saves.note.c_str());
    KeepOwnSaves(s, xml_saves, options);
    return boot_game(s.probe, std::move(options), error);
}

bool RunDump(const std::vector<std::string>& disc_paths, const std::string& sd_dir, std::string& error) {
    LaunchSource source;
    Session s(source, nullptr);
    if (!s.ensure_layout(error)) return false;
    if (!dump_metadata(s.probe, s.partition, sd_dir, error)) return false;
    for (const std::string& p : disc_paths) {
        if (!dump_file(s.partition, p, sd_dir + "/" + basename_of(p), error)) return false;
    }
    return true;
}

}  // namespace riftwii::wii
