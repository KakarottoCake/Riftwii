// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "autorun.hpp"
#include "launchrun.hpp"
#include "launchsession.hpp"
#include "d2xsave.hpp"
#include "progress.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/ctgpconfig.hpp"
#include "riftwii/textlines.hpp"

#include <fat.h>
#include <gccore.h>
#include <ogc/system.h>
#include <strings.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>

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
#include "movedchannels.hpp"
#include "nandchannels.hpp"
#include "modplan.hpp"
#include "netpacks.hpp"
#include "sdfile.hpp"
#include "sharednandfind.hpp"
#include "textfile.hpp"
#include "umsdev.hpp"
#include "usbprobe.hpp"

namespace riftwii::wii {
namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string basename_of(const std::string& disc_path) {
    const auto slash = disc_path.find_last_of('/');
    return slash == std::string::npos ? disc_path : disc_path.substr(slash + 1);
}

// Reads an SD file as the replacement for an FST entry of the same size.
bool LoadReplacement(const OpenedPartition& partition, const std::string& disc_path, const std::string& sd_path,
                     MemReplacement& out, std::string& error) {
    std::uint32_t index = partition.fst.find(disc_path, false);
    if (index == Fst::npos) index = partition.fst.find(disc_path, true);
    if (index == Fst::npos) {
        error = "no such disc file '" + disc_path + "'";
        return false;
    }
    const FstEntry& entry = partition.fst.entries()[index];
    if (entry.is_directory) {
        error = "'" + disc_path + "' is a directory";
        return false;
    }
    if (!ReadWholeFile(sd_path, out.bytes)) {
        error = "cannot open " + sd_path;
        return false;
    }
    if (out.bytes.size() != entry.size) {
        error = sd_path + " is " + std::to_string(out.bytes.size()) + " bytes but '" + disc_path + "' is " +
                std::to_string(entry.size) + " (only same-size replacements yet)";
        return false;
    }
    out.virtual_offset = entry.offset;
    logf("Replace %s (%u bytes at 0x%llx) with %s\n", disc_path.c_str(), entry.size,
         static_cast<unsigned long long>(entry.offset), sd_path.c_str());
    error.clear();
    return true;
}

// Reads an SD file as the new content of an FST entry, whatever its size.
bool LoadVirtualFile(const OpenedPartition& partition, const std::string& disc_path, const std::string& sd_path,
                     VirtualFile& out, std::string& error) {
    std::uint32_t index = partition.fst.find(disc_path, false);
    if (index == Fst::npos) index = partition.fst.find(disc_path, true);
    if (index == Fst::npos) {
        error = "no such disc file '" + disc_path + "'";
        return false;
    }
    const FstEntry& entry = partition.fst.entries()[index];
    if (entry.is_directory) {
        error = "'" + disc_path + "' is a directory";
        return false;
    }
    if (!partition.fst.path_of(index, out.disc_path)) {
        error = "cannot name '" + disc_path + "'";
        return false;
    }
    if (!ReadWholeFile(sd_path, out.bytes)) {
        error = "cannot open " + sd_path;
        return false;
    }
    logf("Grow %s (%u bytes at 0x%llx) to %u bytes from %s\n", out.disc_path.c_str(), entry.size,
         static_cast<unsigned long long>(entry.offset), static_cast<unsigned>(out.bytes.size()), sd_path.c_str());
    error.clear();
    return true;
}

// Resolves an SD file of the same size as an FST entry to the sectors the
// runtime will read it from.
bool LoadSdReplacement(const OpenedPartition& partition, const std::string& disc_path, const std::string& sd_path,
                       SdReplacement& out, std::string& error) {
    std::uint32_t index = partition.fst.find(disc_path, false);
    if (index == Fst::npos) index = partition.fst.find(disc_path, true);
    if (index == Fst::npos) {
        error = "no such disc file '" + disc_path + "'";
        return false;
    }
    const FstEntry& entry = partition.fst.entries()[index];
    if (entry.is_directory) {
        error = "'" + disc_path + "' is a directory";
        return false;
    }
    Fat32File file;
    if (!resolve_sd_file(sd_path, file, error)) return false;
    if (file.entry.size != entry.size) {
        error = sd_path + " is " + std::to_string(file.entry.size) + " bytes but '" + disc_path + "' is " +
                std::to_string(entry.size) + " (only same-size replacements yet)";
        return false;
    }
    if (!place_on_fragments(file.fragments, 0, file.entry.size, out.runs, error)) return false;
    out.virtual_offset = entry.offset;
    logf("SD replace %s (%u bytes at 0x%llx) with %s: %u fragment(s), first sector %llu\n", disc_path.c_str(),
         entry.size, static_cast<unsigned long long>(entry.offset), sd_path.c_str(),
         static_cast<unsigned>(file.fragments.size()),
         static_cast<unsigned long long>(file.fragments.empty() ? 0 : file.fragments[0].sector));
    error.clear();
    return true;
}

// Resolves an SD file of any size as the new content of an FST entry.
bool LoadSdVirtualFile(const OpenedPartition& partition, const std::string& disc_path, const std::string& sd_path,
                       VirtualFile& out, std::string& error) {
    std::uint32_t index = partition.fst.find(disc_path, false);
    if (index == Fst::npos) index = partition.fst.find(disc_path, true);
    if (index == Fst::npos) {
        error = "no such disc file '" + disc_path + "'";
        return false;
    }
    const FstEntry& entry = partition.fst.entries()[index];
    if (entry.is_directory) {
        error = "'" + disc_path + "' is a directory";
        return false;
    }
    if (!partition.fst.path_of(index, out.disc_path)) {
        error = "cannot name '" + disc_path + "'";
        return false;
    }
    Fat32File file;
    if (!resolve_sd_file(sd_path, file, error)) return false;
    if (file.entry.size == 0) {
        error = sd_path + " is empty";
        return false;
    }
    if (!place_on_fragments(file.fragments, 0, file.entry.size, out.sd_runs, error)) return false;
    logf("SD grow %s (%u bytes at 0x%llx) to %u bytes from %s: %u fragment(s)\n", out.disc_path.c_str(), entry.size,
         static_cast<unsigned long long>(entry.offset), file.entry.size, sd_path.c_str(),
         static_cast<unsigned>(file.fragments.size()));
    error.clear();
    return true;
}


}  // namespace

bool AutorunPresent() {
    struct stat st;
    return stat(kAutorunPath, &st) == 0 && S_ISREG(st.st_mode);
}

void RunAutorun() {
    LogOpen(kAutorunLogPath);
    logf("RiftWii autorun: %s\n", kAutorunPath);
    {
        int probe_local = 0;
        logf("loader: stack near %p, arena1 %p-%p, arena2 %p-%p\n", static_cast<void*>(&probe_local),
             SYS_GetArena1Lo(), SYS_GetArena1Hi(), SYS_GetArena2Lo(), SYS_GetArena2Hi());
    }
    mem::LogLimits();
    mem::LogUsage("autorun");
    std::vector<std::string> script_lines;
    {
        // Read whole and closed before any command runs: never retain an
        // SD FILE across a USB IOS reload. Lines end at '\n'; a last line
        // without one counts too.
        std::string script;
        ReadWholeFile(kAutorunPath, script);
        LineReader lines(script);
        std::string loaded;
        while (lines.next(loaded)) script_lines.push_back(std::move(loaded));
    }
    LaunchSource source;
    Session s(source, kAutorunLogPath);
    bool allow_fallback = true;
    bool install_resident = false;
    std::vector<MemReplacement> replacements;
    std::vector<VirtualFile> virtual_files;
    std::vector<SdReplacement> sd_replacements;
    std::vector<PackageSelection> packages;  // compiled together, later ones on top
    CompiledMod mods;
    // Recompiles every package named so far and logs the combined result.
    const auto recompile = [&](std::string& err) {
        CompiledMod mod;
        if (!s.ensure_layout(err) || !compile_packages(packages, s.probe, s.partition, mod, err)) return false;
        LogCompileNotes(mod);
        logf("  %u package(s): %u table entries, %u relocation(s), %u memory patch(es)%s%s\n",
             static_cast<unsigned>(packages.size()), static_cast<unsigned>(mod.entries.size()),
             static_cast<unsigned>(mod.relocations.size()), static_cast<unsigned>(mod.memory.size()),
             mod.savegame_dir.empty() ? "" : ", savegame in ", mod.savegame_dir.c_str());
        if (!mod.entries.empty() || !mod.mem.empty() || !mod.relocations.empty() || !mod.savegame_dir.empty())
            install_resident = true;
        mods = std::move(mod);
        return true;
    };
    std::string error;
    int line_number = 0;
    for (std::string line : script_lines) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        WordReader words(line);
        std::string cmd;
        words.next(cmd);
        logf("> %s\n", line.c_str());
        bool ok = true;
        if (cmd == "usb") {
            std::string wanted; int slot=0; words.next(wanted); words.next(slot);
            UsbCatalog catalog; std::string scan_error;
            if (wanted.empty() || !scan_usb_games(catalog, scan_error)) { ok=false; error=wanted.empty()?"usb needs a path or ID6":scan_error; }
            else {
                UsbGame* found=nullptr; for (UsbGame& g:catalog.games) if (g.path==wanted || g.id==wanted) { found=&g; break; }
                if (!found) { ok=false; error="USB image '"+wanted+"' was not found in the USB catalog"; }
                else if (!check_image_game(*found, error)) ok=false;
                else { source=LaunchSource{}; source.kind=LaunchSource::Kind::Usb; source.game=*found; source.cios_slot=slot; s=Session(source, kAutorunLogPath); }
            }
        } else if (cmd == "sd") {
            std::string wanted; int slot=0; words.next(wanted); words.next(slot);
            ImageCatalog catalog; std::string scan_error;
            if (wanted.empty() || !scan_sd_games(catalog, scan_error)) { ok=false; error=wanted.empty()?"sd needs a path or ID6":scan_error; }
            else {
                ImageGame* found=nullptr; for (ImageGame& g:catalog.games) if (g.path==wanted || g.id==wanted) { found=&g; break; }
                if (!found) { ok=false; error="SD image '"+wanted+"' was not found in the SD catalog"; }
                else if (!check_image_game(*found, error)) ok=false;
                else { source=LaunchSource{}; source.kind=LaunchSource::Kind::Sd; source.game=*found; source.cios_slot=slot; s=Session(source, kAutorunLogPath); }
            }
        } else if (cmd == "rvz") {
            // Dolphin only: boot the RVZ's stub as the disc and read the
            // partitions from the RVZ on the card (d2x does this on a Wii).
            std::string path; words.next(path);
            if (path.empty()) { ok=false; error="rvz needs an sd:/ path"; }
            else if (!serve_disc_from_rvz(path, error)) ok=false;
        } else if (cmd == "disc") {
            source=LaunchSource{}; s=Session(source, kAutorunLogPath);
        } else if (cmd == "channelread" || cmd == "channelboot") {
            std::string title_text;
            words.next(title_text);
            // Optional: the emulated NAND folder on the SD card that holds
            // the channel (sd:/nands/<name>), as the menu's Channels view
            // passes it; d2x then serves the channel from there.
            std::string emu_nand;
            words.next(emu_nand);
            char* end = nullptr;
            const u64 title = std::strtoull(title_text.c_str(), &end, 16);
            if (title_text.size() != 16 || end == nullptr || *end != '\0' || channel_code(title).empty()) {
                ok = false;
                error = "channelread/channelboot needs a 16-digit channel title ID";
            } else {
                ChannelBootFiles files;
                ok = mem::GuardLaunchMemory(error, [&] {
                    return read_channel_boot(title, emu_nand, files, error);
                });
                if (ok && cmd == "channelboot") {
                    ok = mem::GuardLaunchMemory(error, [&] {
                        return stage_moved_channel(title, {}, error) && boot_channel(std::move(files), error);
                    });
                    if (reload_terminal_failure()) halt_after_terminal_reload();
                }
            }
        } else if (cmd == "netscan") {
            // The network packs' list, as the menu's scan fetches it.
            const std::string line = RefreshNetworkPacks(nullptr);
            logf("  %s\n", line.empty() ? "no RiiFS server configured" : line.c_str());
        } else if (cmd == "resync") {
            ForceNextSync();
        } else if (cmd == "usbcheck") {
            // Settings > Check the GameCube adapter > Check each cIOS,
            // straight away (its results in sd:/riftwii/usbcheck.txt).
            logf("  %s\n", RunUsbCheck(true).c_str());
        } else if (cmd == "probe") {
            ok = s.ensure_probe(error);
        } else if (cmd == "layout") {
            ok = s.ensure_layout(error);
        } else if (cmd == "meta") {
            std::string dir;
            words.next(dir);
            if (dir.empty()) dir = "sd:/riftwii/dump";
            ok = s.ensure_layout(error) && dump_metadata(s.probe, s.partition, dir, error);
        } else if (cmd == "dump") {
            std::string disc_path, sd_path;
            words.next(disc_path);
            words.next(sd_path);
            if (disc_path.empty()) {
                ok = false;
                error = "dump needs a disc path";
            } else {
                if (sd_path.empty()) sd_path = "sd:/riftwii/dump/" + basename_of(disc_path);
                ok = s.ensure_layout(error) && dump_file(s.partition, disc_path, sd_path, error);
            }
        } else if (cmd == "dol") {
            std::string sd_path;
            words.next(sd_path);
            if (sd_path.empty()) sd_path = "sd:/riftwii/dump/main.dol";
            ok = s.ensure_layout(error) && dump_dol(s.partition, sd_path, error);
        } else if (cmd == "nofallback") {
            allow_fallback = false;
        } else if (cmd == "hook") {
            install_resident = true;  // E2: resident runtime, DI reads reported over the Gecko
        } else if (cmd == "replace") {
            // E3: `replace <disc path> <sd path>`, same size as the FST entry.
            std::string disc_path, sd_path;
            words.next(disc_path);
            words.next(sd_path);
            MemReplacement r;
            ok = !disc_path.empty() && !sd_path.empty() && s.ensure_layout(error) &&
                 LoadReplacement(s.partition, disc_path, sd_path, r, error);
            if (ok) {
                replacements.push_back(std::move(r));
                install_resident = true;
            } else if (error.empty()) {
                error = "replace needs a disc path and an SD path";
            }
        } else if (cmd == "grow") {
            // E5: `grow <disc path> <sd path>`, any size; the file moves to
            // the virtual window.
            std::string disc_path, sd_path;
            words.next(disc_path);
            words.next(sd_path);
            VirtualFile v;
            ok = !disc_path.empty() && !sd_path.empty() && s.ensure_layout(error) &&
                 LoadVirtualFile(s.partition, disc_path, sd_path, v, error);
            if (ok) {
                virtual_files.push_back(std::move(v));
                install_resident = true;
            } else if (error.empty()) {
                error = "grow needs a disc path and an SD path";
            }
        } else if (cmd == "sdreplace") {
            // E4: `sdreplace <disc path> <sd path>`, same size as the FST
            // entry, served from the card's sectors by the runtime.
            std::string disc_path, sd_path;
            words.next(disc_path);
            words.next(sd_path);
            SdReplacement r;
            ok = !disc_path.empty() && !sd_path.empty() && s.ensure_layout(error) &&
                 LoadSdReplacement(s.partition, disc_path, sd_path, r, error);
            if (ok) {
                sd_replacements.push_back(std::move(r));
                install_resident = true;
            } else if (error.empty()) {
                error = "sdreplace needs a disc path and an SD path";
            }
        } else if (cmd == "keep") {
            // E7: `keep <disc path>`: the file moves to the virtual window
            // with its own disc bytes, which the runtime fetches with a
            // DVDLowRead of its own.
            std::string disc_path;
            words.next(disc_path);
            VirtualFile v;
            v.original = true;
            ok = !disc_path.empty() && s.ensure_layout(error);
            if (ok) {
                std::uint32_t index = s.partition.fst.find(disc_path, false);
                if (index == Fst::npos) index = s.partition.fst.find(disc_path, true);
                if (index == Fst::npos || s.partition.fst.entries()[index].is_directory ||
                    !s.partition.fst.path_of(index, v.disc_path)) {
                    ok = false;
                    error = "no such disc file '" + disc_path + "'";
                } else {
                    logf("Keep %s (%u bytes at 0x%llx) in the virtual window\n", v.disc_path.c_str(),
                         s.partition.fst.entries()[index].size,
                         static_cast<unsigned long long>(s.partition.fst.entries()[index].offset));
                    virtual_files.push_back(std::move(v));
                    install_resident = true;
                }
            } else if (error.empty()) {
                error = "keep needs a disc path";
            }
        } else if (cmd == "sdgrow") {
            // E4+E5: `sdgrow <disc path> <sd path>`, any size, served from
            // the card through the virtual window.
            std::string disc_path, sd_path;
            words.next(disc_path);
            words.next(sd_path);
            VirtualFile v;
            ok = !disc_path.empty() && !sd_path.empty() && s.ensure_layout(error) &&
                 LoadSdVirtualFile(s.partition, disc_path, sd_path, v, error);
            if (ok) {
                virtual_files.push_back(std::move(v));
                install_resident = true;
            } else if (error.empty()) {
                error = "sdgrow needs a disc path and an SD path";
            }
        } else if (cmd == "xml") {
            // `xml <sd path>`: a Riivolution-format package with its
            // default choices. All packages named so far are compiled
            // together (later ones on top of earlier ones) so the log
            // shows the combined result each time.
            std::string sd_path;
            words.next(sd_path);
            if (sd_path.empty()) {
                ok = false;
                error = "xml needs an SD path";
            } else {
                PackageSelection selection;
                selection.xml_sd_path = sd_path;
                packages.push_back(selection);
                ok = recompile(error);
                if (!ok) packages.pop_back();
            }
        } else if (cmd == "set") {
            // `set <option>=<choice>`: a choice for the last `xml` package
            // (option as "Option" or "Section/Option"; an empty choice or
            // "disabled" turns the option off), then everything is
            // recompiled.
            std::string rest;
            words.rest(rest);
            rest = trim(rest);
            const std::size_t eq = rest.find('=');
            if (packages.empty() || eq == std::string::npos) {
                ok = false;
                error = packages.empty() ? "set needs a preceding xml" : "set needs <option>=<choice>";
            } else {
                packages.back().choices.emplace_back(trim(rest.substr(0, eq)), trim(rest.substr(eq + 1)));
                ok = recompile(error);
                if (!ok) packages.back().choices.pop_back();
            }
        } else if (cmd == "launch") {
            // The GUI's path without the GUI: identify, scan, restore the
            // saved choices, compile and boot.
            FrontendState state;
            if (source.kind != LaunchSource::Kind::Disc) {
                state.use_usb = source.kind == LaunchSource::Kind::Usb;
                state.use_sd = source.kind == LaunchSource::Kind::Sd;
                state.game_id = source.game.id;
                state.disc_title = source.game.title;
                state.game_revision = source.game.revision;
                state.game_disc_number = source.game.disc_number;
                state.disc_status = std::string(source.kind == LaunchSource::Kind::Usb ? "USB: " : "SD: ") + source.game.id + "  " + source.game.title;
                state.choices_path = std::string(kChoicesDir) + "/" + source.game.id + ".txt";
            } else IdentifyDisc(state);
            logf("  %s\n", state.disc_status.c_str());
            // As the headless launch: a scan that ran out of memory keeps only
            // the packs read so far, and launching with those would drop mods.
            bool scan_ran_out = false;
            {
                riftwii::OutOfMemoryScope out_of_memory;
                logf("  %s\n", ScanPackages(state).c_str());
                scan_ran_out = out_of_memory.failed();
            }
            for (std::size_t i = 0; i < state.model.packages.size(); ++i) {
                const auto& p = state.model.packages[i];
                const std::string detail = p.valid ? "" : " (" + p.detail + ")";
                logf("  %s: %s%s\n", p.file.c_str(),
                     p.valid ? (p.for_disc ? (p.enabled ? "on" : "off") : "other disc") : "invalid", detail.c_str());
                for (std::size_t o = 0; p.valid && o < p.package.options.size(); ++o) {
                    logf("    %s/%s = %s\n", p.package.options[o].section.c_str(), p.package.options[o].name.c_str(),
                         state.model.choice_name(i, o).c_str());
                }
            }
            const std::vector<PackageChoices> selections = state.model.selections();
            PrepareLaunchExtras(state);  // the game's cheats and video settings
            if (state.game_id.empty()) {
                ok = false;
                error = "launch needs a disc";
            } else if (scan_ran_out) {
                logf("  out of memory reading the packs' folders\n");
                ok = false;
                error = "out of memory reading the packs' folders";
            } else if (!needs_launch_pipeline(!selections.empty(), state.model.save_mode)) {
                logf("  nothing enabled and NAND saves selected: booting as is\n");
                ok = mem::GuardLaunchMemory(error, [&] { return RunBoot(allow_fallback, error, source); });
                if (reload_terminal_failure()) halt_after_terminal_reload();
                LogOpen(kAutorunLogPath, true);
            } else {
                logf(selections.empty() ? "  no packages enabled: applying selected Save Mode\n"
                                        : "launch: handing over to the game\n");
                ok = mem::GuardLaunchMemory(error, [&] {
                    return RunLaunch(selections, error, source, state.model.save_mode, state.game_id);
                });
                if (reload_terminal_failure()) halt_after_terminal_reload();
                LogOpen(kAutorunLogPath, true);
            }
        } else if (cmd == "boot") {
            BootOptions options;
            options.allow_ios_fallback = allow_fallback;
            options.preserve_current_ios = source.kind != LaunchSource::Kind::Disc || MenuCiosSlot() != 0;
            options.install_resident = install_resident;
            options.resident_gecko = install_resident;
            options.replacements = replacements;
            options.replacements.insert(options.replacements.end(), mods.mem.begin(), mods.mem.end());
            options.virtual_files = virtual_files;
            options.sd_replacements = sd_replacements;
            options.verify_sd = true;
            options.table_entries = mods.entries;
            options.relocations = mods.relocations;
            options.memory_patches = mods.memory;
            options.main_dol = mods.main_dol;
            options.banner_names = mods.banner_names;
            options.savegame_dir = mods.savegame_dir;
            options.savegame_clone = mods.savegame_clone;
            if (!s.ensure_probe(error)) {
                ok = false;
            } else {
                logf("boot: handing over to the game\n");
                ok = boot_game(s.probe, std::move(options), error);  // returns only on failure, with the card remounted
                if (reload_terminal_failure()) halt_after_terminal_reload();
                LogOpen(kAutorunLogPath, true);
            }
        } else {
            ok = false;
            error = "unknown command";
        }
        if (!ok) {
            if (reload_terminal_failure()) halt_after_terminal_reload();
            logf("FAILED (line %d): %s\n", line_number, error.c_str());
            break;
        }
        logf("ok\n");
    }
    logf("autorun finished\n");
    // Only the Dolphin power-off below follows. The stop goes out as the
    // menu's power-off sends it, so a run shows the log line.
    if (running_in_dolphin()) di::stop_motor_for_power_off();
    LogClose();
    if (running_in_dolphin()) {
        // Stopping the emulated console makes Dolphin flush the SD image
        // back to its sync folder, which is how the host reads the log.
        fatUnmount("sd:");
        logf("Dolphin detected: powering off so the SD card syncs\n");
        SYS_ResetSystem(SYS_POWEROFF_STANDBY, 0, 0);
    }
}

}  // namespace riftwii::wii
