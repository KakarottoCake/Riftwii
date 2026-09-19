// SPDX-License-Identifier: GPL-3.0-or-later
#include "autorun.hpp"

#include <fat.h>
#include <gccore.h>
#include <ogc/system.h>
#include <sys/stat.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "boot.hpp"
#include "ios_reload.hpp"
#include "log.hpp"

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
        } else if (cmd == "nofallback") {
            allow_fallback = false;
        } else if (cmd == "boot") {
            BootOptions options;
            options.allow_ios_fallback = allow_fallback;
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
