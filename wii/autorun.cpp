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
#include "ios_reload.hpp"
#include "log.hpp"
#include "modplan.hpp"
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
    DiscProbe probe;
    bool probed = false;
    OpenedPartition partition;
    bool laid_out = false;

    bool ensure_probe(std::string& error) {
        if (probed) return true;
        if (!probe_disc(probe, error)) return false;
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

bool RunBoot(bool allow_ios_fallback, std::string& error) {
    Session s;
    if (!s.ensure_probe(error)) return false;
    BootOptions options;
    options.allow_ios_fallback = allow_ios_fallback;
    return boot_game(s.probe, options, error);
}

bool RunDump(const std::vector<std::string>& disc_paths, const std::string& sd_dir, std::string& error) {
    Session s;
    if (!s.ensure_layout(error)) return false;
    if (!dump_metadata(s.probe, s.partition, sd_dir, error)) return false;
    for (const std::string& p : disc_paths) {
        if (!dump_file(s.partition, p, sd_dir + "/" + basename_of(p), error)) return false;
    }
    return true;
}

void RunAutorun() {
    LogOpen(kAutorunLogPath);
    logf("Riftwii autorun: %s\n", kAutorunPath);
    {
        int probe_local = 0;
        logf("loader: stack near %p, arena1 %p-%p, arena2 %p-%p\n", static_cast<void*>(&probe_local),
             SYS_GetArena1Lo(), SYS_GetArena1Hi(), SYS_GetArena2Lo(), SYS_GetArena2Hi());
    }
    std::ifstream script(kAutorunPath);
    std::string line;
    Session s;
    bool allow_fallback = true;
    bool install_resident = false;
    std::vector<MemReplacement> replacements;
    std::vector<VirtualFile> virtual_files;
    std::vector<SdReplacement> sd_replacements;
    std::vector<CompiledMod> mods;
    std::uint64_t window_cursor = kVirtualWindowStart;
    std::string error;
    int line_number = 0;
    while (std::getline(script, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream words(line);
        std::string cmd;
        words >> cmd;
        logf("> %s\n", line.c_str());
        bool ok = true;
        if (cmd == "probe") {
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
            // default choices, compiled to table entries and relocations.
            std::string sd_path;
            words >> sd_path;
            CompiledMod mod;
            ok = !sd_path.empty() && s.ensure_layout(error) &&
                 compile_package(sd_path, s.probe, s.partition, window_cursor, mod, error);
            if (ok) {
                for (const std::string& w : mod.warnings) logf("  warning: %s\n", w.c_str());
                for (const std::string& n : mod.notes) logf("  %s\n", n.c_str());
                logf("  %u table entries, %u relocation(s), %u memory patch(es)\n",
                     static_cast<unsigned>(mod.entries.size()), static_cast<unsigned>(mod.relocations.size()),
                     static_cast<unsigned>(mod.memory.size()));
                if (!mod.entries.empty() || !mod.relocations.empty()) install_resident = true;
                mods.push_back(std::move(mod));
            } else if (error.empty()) {
                error = "xml needs an SD path";
            }
        } else if (cmd == "boot") {
            BootOptions options;
            options.allow_ios_fallback = allow_fallback;
            options.install_resident = install_resident;
            options.resident_gecko = install_resident;
            options.replacements = replacements;
            options.virtual_files = virtual_files;
            options.sd_replacements = sd_replacements;
            options.verify_sd = true;
            for (const CompiledMod& mod : mods) {
                options.table_entries.insert(options.table_entries.end(), mod.entries.begin(), mod.entries.end());
                options.relocations.insert(options.relocations.end(), mod.relocations.begin(),
                                           mod.relocations.end());
                options.memory_patches.insert(options.memory_patches.end(), mod.memory.begin(), mod.memory.end());
            }
            if (!s.ensure_probe(error)) {
                ok = false;
            } else {
                logf("boot: handing over to the game\n");
                ok = boot_game(s.probe, options, error);  // returns only on failure, with the card remounted
                LogOpen(kAutorunLogPath, true);
            }
        } else {
            ok = false;
            error = "unknown command";
        }
        if (!ok) {
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
