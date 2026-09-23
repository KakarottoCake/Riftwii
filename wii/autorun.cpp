// SPDX-License-Identifier: GPL-3.0-or-later
#include "autorun.hpp"

#include <fat.h>
#include <gccore.h>
#include <ogc/system.h>
#include <sys/stat.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>

#include "boot.hpp"
#include "di.hpp"
#include "frontend.hpp"
#include "ios_reload.hpp"
#include "log.hpp"
#include "menuios.hpp"
#include "modplan.hpp"
#include "netpacks.hpp"
#include "sdfile.hpp"

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

// Shared state for one session: the probe and, lazily, the layout.
struct Session {
    LaunchSource source;
    DiscProbe probe;
    bool probed = false;
    OpenedPartition partition;
    bool laid_out = false;
    void* frag_storage = nullptr;
    std::size_t frag_storage_bytes = 0;
    const char* log_path = nullptr;

    explicit Session(const LaunchSource& launch_source = LaunchSource(), const char* active_log = nullptr)
        : source(launch_source), log_path(active_log) {}

    bool ensure_probe(std::string& error) {
        if (probed) return true;
        if (source.kind != LaunchSource::Kind::Disc) {
            const int slots[] = {source.cios_slot ? source.cios_slot : 249, source.cios_slot ? 0 : 250,
                                 source.cios_slot ? 0 : 251};
            bool active = false;
            for (int slot : slots) {
                if (!slot) continue;
                if (activate_image_game(source.game, slot, frag_storage, frag_storage_bytes, log_path, error)) { active=true; break; }
                if (reload_terminal_failure()) return false;
            }
            if (!active) return false;
            logf("%s: virtual-disc probe\n", source.kind == LaunchSource::Kind::Usb ? "USB" : "SD");
            if (!probe_disc(probe, error, ProbeOptions{true})) return false;
            if (probe.header.game_id != source.game.id || probe.header.version != source.game.revision ||
                probe.header.disc_number != source.game.disc_number) {
                error="d2x virtual disc identity does not match the selected image";
                return false;
            }
        } else if (!probe_disc(probe, error)) return false;
        probed = true;
        return true;
    }
    bool ensure_layout(std::string& error) {
        if (!ensure_probe(error)) return false;
        if (laid_out) return true;
        if (!read_partition_layout(partition, error)) return false;
        laid_out = true;
        return true;
    }
};

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
    std::ifstream file(sd_path, std::ios::binary);
    if (!file) {
        error = "cannot open " + sd_path;
        return false;
    }
    out.bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
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
    std::ifstream file(sd_path, std::ios::binary);
    if (!file) {
        error = "cannot open " + sd_path;
        return false;
    }
    out.bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
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

bool RunBoot(bool allow_ios_fallback, std::string& error, const LaunchSource& source) {
    Session s(source, "sd:/riftwii/boot.log");
    if (!s.ensure_probe(error)) return false;
    BootOptions options;
    options.allow_ios_fallback = allow_ios_fallback;
    options.preserve_current_ios = source.kind != LaunchSource::Kind::Disc || MenuCiosSlot() != 0;
    return boot_game(s.probe, options, error);
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
    Session s;
    if (!s.ensure_probe(error)) return false;
    game_id = s.probe.header.game_id;
    title = s.probe.header.title;
    if (revision) *revision = s.probe.header.version;
    if (disc_number) *disc_number = s.probe.header.disc_number;
    return true;
}

// A compile's warnings and notes for the log. A big mod has a note per
// file and every log line is a card write, so past kMaxNoteLines only the
// skipped files are listed and the rest counted.
void LogCompileNotes(const CompiledMod& mod) {
    constexpr std::size_t kMaxNoteLines = 60;
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

bool BootCompiled(const CompiledMod& mod, std::string& error, const LaunchSource& source,
                    const std::string& save_mode, const std::string& game_id) {
    Session s(source, "sd:/riftwii/boot.log");
    if (!s.ensure_probe(error)) return false;
    if (mod.has_source_identity && !same_disc_identity(mod.source_identity, s.probe.header.identity())) {
        error = "disc changed since preflight; recompile the selected packages";
        return false;
    }
    // The UI's save separation applies only when no <savegame external>
    // patch claimed a folder (XML wins); headless runs pass "nand".
    const SaveOverride saves = resolve_save_override(save_mode, mod.savegame_dir, game_id);
    const bool xml_saves = !mod.savegame_dir.empty();
    const std::string dir = xml_saves ? mod.savegame_dir : saves.dir;
    BootOptions options;
    options.allow_ios_fallback = true;
    options.preserve_current_ios = source.kind != LaunchSource::Kind::Disc || MenuCiosSlot() != 0;
    options.install_resident = !mod.entries.empty() || !mod.relocations.empty() || !dir.empty();
    options.resident_gecko = false;
    options.table_entries = mod.entries;
    options.relocations = mod.relocations;
    options.memory_patches = mod.memory;
    options.main_dol = mod.main_dol;
    options.savegame_dir = dir;
    options.savegame_clone = xml_saves ? mod.savegame_clone : saves.clone;
    if (!xml_saves && !saves.note.empty()) logf("Saves: %s\n", saves.note.c_str());
    return boot_game(s.probe, options, error);
}

bool RunLaunch(const std::vector<PackageChoices>& packages, std::string& error, const LaunchSource& source,
               const std::string& save_mode, const std::string& game_id) {
    // Keep one session across activation, DI probing, package compilation and
    // boot. In particular, a USB fragment list must survive the cIOS reload.
    Session s(source, "sd:/riftwii/boot.log"); if (!s.ensure_layout(error)) return false;
    CompiledMod mod; if (!compile_packages(packages, s.probe, s.partition, mod, error)) return false;
    LogCompileNotes(mod);
    logf("%u package(s): %u table entries, %u relocation(s), %u memory patch(es)\n",
         static_cast<unsigned>(packages.size()), static_cast<unsigned>(mod.entries.size()),
         static_cast<unsigned>(mod.relocations.size()), static_cast<unsigned>(mod.memory.size()));
    const SaveOverride saves = resolve_save_override(save_mode, mod.savegame_dir, game_id);
    const bool xml_saves = !mod.savegame_dir.empty();
    const std::string dir = xml_saves ? mod.savegame_dir : saves.dir;
    BootOptions options; options.allow_ios_fallback=true; options.preserve_current_ios=source.kind != LaunchSource::Kind::Disc || MenuCiosSlot() != 0;
    options.install_resident=!mod.entries.empty() || !mod.relocations.empty() || !dir.empty();
    options.table_entries=mod.entries; options.relocations=mod.relocations; options.memory_patches=mod.memory;
    options.main_dol=mod.main_dol;
    options.savegame_dir=dir; options.savegame_clone=xml_saves ? mod.savegame_clone : saves.clone;
    if (!xml_saves && !saves.note.empty()) logf("Saves: %s\n", saves.note.c_str());
    return boot_game(s.probe,options,error);
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

void RunAutorun() {
    LogOpen(kAutorunLogPath);
    logf("RiftWii autorun: %s\n", kAutorunPath);
    {
        int probe_local = 0;
        logf("loader: stack near %p, arena1 %p-%p, arena2 %p-%p\n", static_cast<void*>(&probe_local),
             SYS_GetArena1Lo(), SYS_GetArena1Hi(), SYS_GetArena2Lo(), SYS_GetArena2Hi());
    }
    std::vector<std::string> script_lines;
    {
        std::ifstream script(kAutorunPath);
        std::string loaded;
        while (std::getline(script, loaded)) script_lines.push_back(std::move(loaded));
    }  // Never retain an SD FILE across a USB IOS reload.
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
        if (!mod.entries.empty() || !mod.relocations.empty() || !mod.savegame_dir.empty()) install_resident = true;
        mods = std::move(mod);
        return true;
    };
    std::string error;
    int line_number = 0;
    for (std::string line : script_lines) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream words(line);
        std::string cmd;
        words >> cmd;
        logf("> %s\n", line.c_str());
        bool ok = true;
        if (cmd == "usb") {
            std::string wanted; int slot=0; words >> wanted >> slot;
            UsbCatalog catalog; std::string scan_error;
            if (wanted.empty() || !scan_usb_games(catalog, scan_error)) { ok=false; error=wanted.empty()?"usb needs a path or ID6":scan_error; }
            else {
                UsbGame* found=nullptr; for (UsbGame& g:catalog.games) if (g.path==wanted || g.id==wanted) { found=&g; break; }
                if (!found) { ok=false; error="USB image '"+wanted+"' was not found in the USB catalog"; }
                else if (!check_image_game(*found, error)) ok=false;
                else { source=LaunchSource{}; source.kind=LaunchSource::Kind::Usb; source.game=*found; source.cios_slot=slot; s=Session(source, kAutorunLogPath); }
            }
        } else if (cmd == "sd") {
            std::string wanted; int slot=0; words >> wanted >> slot;
            ImageCatalog catalog; std::string scan_error;
            if (wanted.empty() || !scan_sd_games(catalog, scan_error)) { ok=false; error=wanted.empty()?"sd needs a path or ID6":scan_error; }
            else {
                ImageGame* found=nullptr; for (ImageGame& g:catalog.games) if (g.path==wanted || g.id==wanted) { found=&g; break; }
                if (!found) { ok=false; error="SD image '"+wanted+"' was not found in the SD catalog"; }
                else if (!check_image_game(*found, error)) ok=false;
                else { source=LaunchSource{}; source.kind=LaunchSource::Kind::Sd; source.game=*found; source.cios_slot=slot; s=Session(source, kAutorunLogPath); }
            }
        } else if (cmd == "disc") {
            source=LaunchSource{}; s=Session(source, kAutorunLogPath);
        } else if (cmd == "netscan") {
            // The network packs' list, as the menu's scan fetches it.
            const std::string line = RefreshNetworkPacks(nullptr);
            logf("  %s\n", line.empty() ? "no RiiFS server configured" : line.c_str());
        } else if (cmd == "resync") {
            ForceNextSync();
        } else if (cmd == "probe") {
            ok = s.ensure_probe(error);
        } else if (cmd == "layout") {
            ok = s.ensure_layout(error);
        } else if (cmd == "meta") {
            std::string dir;
            words >> dir;
            if (dir.empty()) dir = "sd:/riftwii/dump";
            ok = s.ensure_layout(error) && dump_metadata(s.probe, s.partition, dir, error);
        } else if (cmd == "dump") {
            std::string disc_path, sd_path;
            words >> disc_path >> sd_path;
            if (disc_path.empty()) {
                ok = false;
                error = "dump needs a disc path";
            } else {
                if (sd_path.empty()) sd_path = "sd:/riftwii/dump/" + basename_of(disc_path);
                ok = s.ensure_layout(error) && dump_file(s.partition, disc_path, sd_path, error);
            }
        } else if (cmd == "dol") {
            std::string sd_path;
            words >> sd_path;
            if (sd_path.empty()) sd_path = "sd:/riftwii/dump/main.dol";
            ok = s.ensure_layout(error) && dump_dol(s.partition, sd_path, error);
        } else if (cmd == "nofallback") {
            allow_fallback = false;
        } else if (cmd == "hook") {
            install_resident = true;  // E2: resident runtime, DI reads reported over the Gecko
        } else if (cmd == "replace") {
            // E3: `replace <disc path> <sd path>`, same size as the FST entry.
            std::string disc_path, sd_path;
            words >> disc_path >> sd_path;
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
            words >> disc_path >> sd_path;
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
            words >> disc_path >> sd_path;
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
            words >> disc_path;
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
            words >> disc_path >> sd_path;
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
            words >> sd_path;
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
            std::getline(words, rest);
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
            logf("  %s\n", ScanPackages(state).c_str());
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
            if (state.game_id.empty()) {
                ok = false;
                error = "launch needs a disc";
            } else if (!needs_launch_pipeline(!selections.empty(), state.model.save_mode)) {
                logf("  nothing enabled and NAND saves selected: booting as is\n");
                ok = RunBoot(allow_fallback, error, source);
                if (reload_terminal_failure()) halt_after_terminal_reload();
                LogOpen(kAutorunLogPath, true);
            } else {
                logf(selections.empty() ? "  no packages enabled: applying selected Save Mode\n"
                                        : "launch: handing over to the game\n");
                ok = RunLaunch(selections, error, source, state.model.save_mode, state.game_id);
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
            options.virtual_files = virtual_files;
            options.sd_replacements = sd_replacements;
            options.verify_sd = true;
            options.table_entries = mods.entries;
            options.relocations = mods.relocations;
            options.memory_patches = mods.memory;
            options.main_dol = mods.main_dol;
            options.savegame_dir = mods.savegame_dir;
            options.savegame_clone = mods.savegame_clone;
            if (!s.ensure_probe(error)) {
                ok = false;
            } else {
                logf("boot: handing over to the game\n");
                ok = boot_game(s.probe, options, error);  // returns only on failure, with the card remounted
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
