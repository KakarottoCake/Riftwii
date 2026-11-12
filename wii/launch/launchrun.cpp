// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "launchrun.hpp"
#include "launchsession.hpp"
#include "d2xsave.hpp"
#include "d2xsd.hpp"
#include "progress.hpp"
#include "riftwii/ctgpconfig.hpp"
#include "riftwii/textlines.hpp"
#include "riftwii/wfstitle.hpp"
#include "riftwii/imagevolume.hpp"

#include <fat.h>
#include <gccore.h>
#include <ogc/machine/processor.h>
#include <ogc/system.h>
#include <strings.h>
#include <sys/stat.h>

#include <cstdio>

#include "boot.hpp"
#include "channel.hpp"
#include "ctgpr.hpp"
#include "di.hpp"
#include "frontend.hpp"
#include "gameextras.hpp"
#include "gclaunch.hpp"
#include "ios_reload.hpp"
#include "log.hpp"
#include "plugins.hpp"
#include "disclight.hpp"
#include "memlimits.hpp"
#include "menuios.hpp"
#include "menumusic.hpp"
#include "modplan.hpp"
#include "netpacks.hpp"
#include "sdfile.hpp"
#include "loadersettings.hpp"
#include "riftwii/sharednand.hpp"
#include "sharednandfind.hpp"
#include "textfile.hpp"
#include "umsdev.hpp"
#include "usbprobe.hpp"
#include "wfsdrive.hpp"

namespace riftwii::wii {
namespace {

std::string basename_of(const std::string& disc_path);

bool g_force_ctgpr_native = false;  // autorun's "ctgpnative" (test builds)

// The CTGP-R pack starts CTGP-R's own channel app, with d2x serving a USB or
// SD image as the disc, as its way of starting. A file ctgpnative.txt in the
// running boot.dol's folder opts out: the native launch is used instead when
// its files are on the card.
std::string switch_file(const char* name) {
    std::string folder = plugin::Directory();
    const std::string plugins = "plugins/";
    if (folder.size() >= plugins.size() &&
        folder.compare(folder.size() - plugins.size(), plugins.size(), plugins) == 0)
        folder.erase(folder.size() - plugins.size());
    if (!folder.empty() && folder.back() != '/') folder.push_back('/');
    return folder + name;
}

std::string ctgpr_native_file() { return switch_file("ctgpnative.txt"); }

bool file_present(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool ctgpr_native_opted() {
    if (g_force_ctgpr_native) return true;
    return file_present(ctgpr_native_file());
}

// The native CTGP-R launch, only when opted into (ctgpnative.txt).
bool ctgpr_native_chosen(const std::string& game_id, const std::string& app) {
    if (!ctgpr_native_opted()) return false;
    return ctgpr_native_wanted() && ctgp_is_native_stand_in(game_id, app);
}

// Whether one of the chosen packs is CTGP-R's own XML (ctgpr.xml): with
// the channel's boot.dol missing, homebrew_app_stand_in does not see the
// swap at all, and the channel launch must stop rather than patch the game.
bool ctgpr_pack_chosen(const std::vector<PackageChoices>& packages) {
    for (const PackageChoices& p : packages) {
        const std::string name = basename_of(p.xml_sd_path);
        if (strcasecmp(name.c_str(), "ctgpr.xml") == 0) return true;
    }
    return false;
}

std::string basename_of(const std::string& disc_path) {
    const auto slash = disc_path.find_last_of('/');
    return slash == std::string::npos ? disc_path : disc_path.substr(slash + 1);
}

// CTGP Revolution's channel, started under a d2x cIOS: its own IOS exploit
// can wait forever there for hardware access, so sd:/ctgpr/config.ini gets
// "disable_ios_exploit = yes" in [exploit] unless the player set that key.
// On d2x's virtual disc (a USB or SD image) that key is set to yes whatever
// it was, and so are "[sd] compatible_mode" and "[exploit] disable_patches"
// (ctgpconfig.hpp says why): the channel then reloads
// into the d2x cIOS before the game, and d2x carries its fragment list
// across that reload (it saves it to the NAND when a disc game's program
// reloads IOS, and the reloaded cIOS reads it back). Before any IOS reload,
// while libfat has the card.
void SetCtgpDefaults(const std::string& app, bool virtual_disc) {
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
    // Each key: [section] key, the value it had ("" = not set), whether
    // RiftWii changed it. On a virtual disc every key is set to yes; on a
    // real disc only disable_ios_exploit is added when missing.
    struct Key {
        const char* section;
        const char* name;
        std::string old_value;
        bool changed;
    };
    Key keys[3] = {{"exploit", "disable_ios_exploit", "", false},
                   {"sd", "compatible_mode", "", false},
                   {"exploit", "disable_patches", "", false}};
    if (virtual_disc) {
        for (Key& k : keys) k.changed = riftwii::ini_set(text, k.section, k.name, "yes", &k.old_value);
    } else {
        std::string probe = text;  // only to read the old value
        riftwii::ini_set(probe, keys[0].section, keys[0].name, "yes", &keys[0].old_value);
        keys[0].changed = riftwii::ctgp_config_defaults(text);
    }
    const int count = virtual_disc ? 3 : 1;
    bool any = false;
    for (int i = 0; i < count; ++i) any = any || keys[i].changed;
    bool written = !any;
    if (any) {
        written = WriteWholeFile(path, text.data(), text.size());
        if (!written) logf("CTGP: could not write %s\n", path);
        else forget_sd_layout();
    }
    for (int i = 0; i < count; ++i) {
        const Key& k = keys[i];
        const char* was = k.old_value.empty() ? "not set" : k.old_value.c_str();
        if (!k.changed)
            logf("CTGP: config.ini [%s] %s = %s, left as it is\n", k.section, k.name, was);
        else
            logf("CTGP: config.ini [%s] %s = yes (was %s)%s\n", k.section, k.name, was,
                 written ? "" : ", NOT written");
    }
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
        if (riftwii::is_usb_path(p.xml_sd_path)) return true;
    }
    return false;
}


}  // namespace

void TakeCardBackFromD2x() {
    if (usb_held_by_d2x_fat()) release_d2x_saves();
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
        // Dolphin's own IOS has no d2x to keep a folder on the USB drive.
        if (own_saves_on_usb && running_in_dolphin()) {
            error = "Saves: the emulated NAND on the USB drive needs d2x's NAND emulation, which Dolphin does not have";
            return false;
        }
        if (!own_saves_dir.empty() && !running_in_dolphin()) {
            std::string why;
            if (source.game.title_id == 0) why = "the game's title ID is not known";
            // On the USB drive (#487) the SD card stays RiftWii's: packs,
            // code builds and an RVZ's tables on it are no trouble.
            else if (own_saves_on_usb && !usb_d2x_nand_partition(own_saves_dir, own_saves_usb_partition, why)) {}
            else if (own_saves_on_usb) d2x_ready = prepare_d2x_saves(source.game, own_saves_dir, own_saves_clone,
                                                                     own_saves_clone_empty, why, &own_saves_whole);
            else if (sd_after_probe)
                why = "RiftWii reads the SD card while the game loads and runs (packs or a code build on it), and "
                      "d2x's FAT module must have the card to itself";
            // An RVZ on any drive: its group table and stub are files on the
            // SD card (sd:/riftwii/rvz), read and written at the launch, and
            // the runtime holds the card for the game's reads.
            else if (source.game.format == UsbImageFormat::Rvz)
                why = "an RVZ game (RiftWii keeps its group table on the SD card and reads the card as it runs)";
            else d2x_ready = prepare_d2x_saves(source.game, own_saves_dir, own_saves_clone,
                                                    own_saves_clone_empty, why, &own_saves_whole);
            // RiftWii's own save code keeps saves on the SD card only: a
            // USB folder d2x cannot keep stops the launch rather than
            // saving somewhere the player did not choose.
            if (!d2x_ready && own_saves_on_usb) {
                error = "Saves: the emulated NAND on the USB drive can't be used: " + why;
                return false;
            }
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
        if (d2x_ready) {
            const int base = d2x_base(used_slot);
            std::string why;
            const D2xNandScope scope = own_saves_whole ? D2xNandScope::WholeNand : D2xNandScope::GamePaths;
            if (!own_saves_on_usb && source.game.device == ImageDevice::Sd && base != 56 && base != 57)
                logf("Saves: RiftWii's own save code, not d2x's NAND emulation (a game on the SD card with d2x "
                     "base %d; d2x keeps both on one card on base 56 or 57)\n", base);
            else if (!(d2x_saves = enable_d2x_saves(own_saves_dir, why, scope, nullptr, nullptr,
                                                    own_saves_usb_partition))) {
                if (own_saves_on_usb) {
                    error = "Saves: " + why;
                    return false;
                }
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
        ProbeOptions virtual_disc;
        virtual_disc.virtual_source = true;
        if (!probe_disc(probe, error, virtual_disc)) return false;
        mem::CheckHeap("after the disc probe");
        if (probe.header.game_id != source.game.id || probe.header.version != source.game.revision ||
            probe.header.disc_number != source.game.disc_number) {
            error="d2x virtual disc identity does not match the selected image";
            return false;
        }
        // Dragon Quest X's disc only installs the game, with IOS59's WFS
        // drivers, which no cIOS has; from an image it cannot.
        if (probe.wfs_install_disc) {
            error = "Dragon Quest X's disc installs the game onto its own USB drive with the Wii's IOS59, which "
                    "cannot read a disc image";
            if (const std::uint64_t installed = WfsTitleForDisc(probe.header.game_id))
                error += "; the installed game (" + wfs_title_text(installed) + ") starts from Wii channels";
            else
                error += "; install it from the disc with the Wii Menu, then start it from Wii channels";
            return false;
        }
    } else {
        // A disc's saves go through RiftWii's own save code, which keeps
        // them on the SD card only (#487).
        if (own_saves_on_usb) {
            error = "Saves: the emulated NAND on the USB drive is for games started from the SD card or the USB drive; "
                    "choose another save location on this disc's page";
            return false;
        }
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
    DiscLightActivity discLight;
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
    for (const MissingModInput& input : mod.missing_inputs) {
        if (input.kind == MissingModInputKind::Folder) {
            logf("Mods: %s is missing the folder %s; the files it would bring will be left out.\n",
                 input.pack.c_str(), input.external.c_str());
            continue;
        }
        logf("Mods: %s is missing %s; its %s changes will be left out.\n", input.pack.c_str(),
             input.external.c_str(), input.kind == MissingModInputKind::Memory ? "memory patch" : "file");
    }
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
    DiscLightActivity discLight;
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
    s.own_saves_on_usb = shared_nand_on_usb(own.dir);
    // Only the shared emulated NAND can be a whole one; RiftWii's own save
    // folders hold one game's save.
    s.own_saves_whole = !shared_nand.empty() && own.dir == shared_nand && Settings().emunand_keeps == "all";
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
    DiscLightActivity discLight;
    Session s(source, "sd:/riftwii/boot.log");
    std::string shared_nand;
    if (!FindSharedSaves(save_mode, game_id, shared_nand, error)) return false;
    SetOwnSaves(s, save_mode, game_id, shared_nand);
    s.sd_in_use = !mod.entries.empty() || !mod.savegame_dir.empty() || (!s.own_saves_dir.empty() && !s.own_saves_on_usb);
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

bool CtgprNativeOptedIn() {
    return ctgpr_native_opted() && ctgpr_native_wanted();
}

bool CtgprPackGoesThroughRunLaunch(const std::vector<PackageChoices>& packages, const std::string& game_id) {
    std::string app, dropped;
    return homebrew_app_stand_in(packages, game_id, app, dropped) && ctgp_is_native_stand_in(game_id, app);
}

#ifdef RIFTWII_TEST_BUILD
void ForceCtgprNativeLaunch(bool on) {
    g_force_ctgpr_native = on;
}
#endif

bool RunLaunch(const std::vector<PackageChoices>& packages, std::string& error, const LaunchSource& source,
               const std::string& save_mode, const std::string& game_id,
               const MissingModConfirmation& confirm_missing) {
    DiscLightActivity discLight;
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
        if (game_id.compare(0, 3, "RMC") == 0 && ctgpr_pack_chosen(packages) && !ctgpr_native_opted()) {
            struct stat st;
            if (stat("sd:/apps/ctgpr/boot.dol", &st) != 0 || !S_ISREG(st.st_mode)) {
                error = "CTGP-R channel: sd:/apps/ctgpr/boot.dol (CTGP-R's channel) is missing, so nothing was "
                        "started. Put CTGP-R's apps/ctgpr folder on the SD card, or add ctgpnative.txt next to boot.dol for the native launch.";
                logf("%s\n", error.c_str());
                return false;
            }
        }
        if (homebrew_app_stand_in(packages, game_id, app, dropped)) {
            logf("Mods: the packs only replace the game's executable with %s, a Homebrew Channel app: started as "
                 "the Homebrew Channel starts it, not loaded as the game%s\n",
                 app.c_str(), dropped.empty() ? "" : ("; " + dropped + " does not apply to it").c_str());
            // CTGP-R's own channel app (not another app that stands in for the game).
            const bool ctgpr_channel = ctgp_is_native_stand_in(game_id, app);
            if (ctgpr_channel && !ctgpr_native_chosen(game_id, app))
                logf("CTGP-R channel: launch chosen: CTGP-R's own channel app %s is started (%s)\n", app.c_str(),
                     ctgpr_native_opted() ? "the native launch is opted into but its files are not set up"
                                          : "the native launch is not opted into");
            if (ctgpr_native_chosen(game_id, app)) {
                // The native launch: RiftWii loads the game itself and places
                // CTGP-R's code in it (docs/CTGPR.md), in place of the
                // channel. Offline only. Narrowed to CTGP-R's own channel swap
                // on Mario Kart Wii: another pack that swaps main.dol for some
                // other homebrew app is started below as an ordinary app, with
                // none of CTGP-R's Mario Kart Wii hooks.
                logf("CTGP-R: native launch (the CTGP-R Channel app is not started)\n");
                Session s(source, "sd:/riftwii/boot.log");
                // CTGP's files are read from the card after the probe, and
                // CTGP's own code reads the card in the game.
                s.sd_in_use = true;
                s.sd_after_probe = true;
                if (!s.ensure_layout(error)) return false;
                BootOptions options;
                options.allow_ios_fallback = true;
                options.preserve_current_ios =
                    source.kind != LaunchSource::Kind::Disc || MenuCiosSlot() != 0 || s.on_cios;
                // The error is already in the player's words and names CTGP-R.
                if (!prepare_ctgpr_launch(s.probe.header.game_id, options.ctgpr, error)) return false;
                return boot_game(s.probe, std::move(options), error);
            }
            SetCtgpDefaults(app, source.kind != LaunchSource::Kind::Disc);
            if (source.kind != LaunchSource::Kind::Disc) {
                // The app reloads IOS itself (CTGP asks for IOS37), which
                // would drop d2x and the game with it: d2x blocks that.
                Session s(source, "sd:/riftwii/boot.log");
                s.block_ios_reload = true;
                const bool probed = s.ensure_probe(error);
                if (ctgpr_channel)
                    logf("CTGP-R channel: d2x virtual disc from %s: %s\n",
                         source.kind == LaunchSource::Kind::Usb ? "USB" : "SD",
                         probed ? ("set up, the disc reads as " + s.probe.header.game_id).c_str()
                                : ("FAILED: " + error).c_str());
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
            if (ctgpr_channel) {
                // The channel turns MEM2 protection on (MEM_PROT 0x0d8b420a = 1)
                // and then clears MEM2 below its start, which IOS leaves at
                // 0x13620000; d2x's plugins live in 0x13400000-0x1361ffff, so the
                // clear wipes them and the channel's first IOS reload loses the
                // virtual disc ("insert disc"). The start moved down to
                // 0x13400000 (above the IPC buffer at 0x133e0000-0x133fffff,
                // which the channel uses too) makes the channel's own switch
                // protect them. Starting the app reloads no IOS under a cIOS, and
                // each IOS reload puts the start back.
                if (source.kind != LaunchSource::Kind::Disc &&
                    *reinterpret_cast<volatile u32*>(0xCD800064) == 0xFFFFFFFFu) {
                    const u16 before = read16(0xCD8B420C);
                    write16(0xCD8B420C, 0x3400);
                    logf("CTGP-R channel: MEM2 protection start moved from 0x%04x to 0x%04x (0x13400000) so the "
                         "channel's clear keeps d2x's plugins\n",
                         before, read16(0xCD8B420C));
                }
            }
            // CTGP-R's channel app is started without the libogc argument
            // block, exactly as the Homebrew Channel and the apploader leave
            // it: its start-up memory check hashes that block (0x80004008 in
            // the channel's first segment), and a filled one made the check
            // fail when d2x served the virtual disc (the block's 12 bytes were
            // the whole difference: crc 0xf07e20fc vs the normal 0xa9399ae0).
            if (ctgpr_channel) logf("CTGP-R channel: starting %s\n", app.c_str());
            if (StartHomebrewApp(app, error, !ctgpr_channel)) return true;
            if (ctgpr_channel) logf("CTGP-R channel: it did not start: %s\n", error.c_str());
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
    s.sd_in_use = !packages.empty() || (!s.own_saves_dir.empty() && !s.own_saves_on_usb);
    s.sd_after_probe = packages_need_sd(packages) || !CurrentLaunchExtras().code_builds.empty();
    if (!s.ensure_layout(error)) return false;
    CompiledMod mod;
    if (!compile_packages(packages, s.probe, s.partition, mod, error)) return false;
    LogCompileNotes(mod);
    if (!mod.missing_inputs.empty()) {
        if (confirm_missing) {
            if (!confirm_missing(mod.missing_inputs)) {
                logf("Mods: the player went back because selected mod files are missing.\n");
                error.clear();  // a cancellation, not a failed launch
                return false;
            }
            logf("Mods: the player chose Play anyway with missing mod files.\n");
        } else {
            logf("Mods: selected files are missing; this unattended launch continues without a question.\n");
        }
    }
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
