// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "reportsend.hpp"

#include <dirent.h>
#include <malloc.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>
#include <string>
#include <vector>

#include <gccore.h>
#include <ogc/conf.h>
#include <wiiuse/wpad.h>

#include "boot.hpp"
#include "gcadapter.hpp"
#include "usbinput.hpp"
#include "ios_reload.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "menuios.hpp"
#include "online.hpp"
#include "plugins.hpp"
#include "restart.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/gamefault.hpp"
#include "riftwii/gccrash.hpp"
#include "riftwii/http.hpp"
#include "riftwii/problemreport.hpp"
#include "riftwii/profiles.hpp"
#include "riftwii/sha256.hpp"
#include "riftwii/wpadlog.hpp"
#include "rtwlog.h"
#include "wiidrc.h"
#include "wpadhook.hpp"
#include "nanduse.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kSessionLog = "sd:/riftwii/session.log";
constexpr const char* kPreviousLog = "sd:/riftwii/session-previous.log";
constexpr const char* kCrashFile = "sd:/riftwii/crash.txt";
constexpr const char* kStateFile = "sd:/riftwii/report-state.txt";
constexpr const char* kGameCrashFile = "sd:/riftwii/gamecrash.txt";
constexpr const char* kGameCrashRecord = "/shared2/riftwii/crash.bin";  // runtime/rtfault.h
constexpr const char* kWpadLogFile = "sd:/riftwii/wpadlog.txt";
constexpr const char* kWpadLogRecord = "/shared2/riftwii/wpadlog.bin";  // runtime/rtwlog.h
constexpr const char* kPasteUrl = "https://paste.rs/";
// When paste.rs cannot be reached (some networks block it) or fails:
// dpaste.com, a form post with the text as "content".
constexpr const char* kDpasteUrl = "https://dpaste.com/api/v2/";
// More than this of one file is never read (the report keeps far less).
constexpr std::size_t kReadCap = 2u << 20;

bool g_game_crashed = false;  // ImportGameCrash found one this start

bool read_text(const std::string& path, std::string& out) {
    out.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buffer[4096];
    std::size_t got;
    // Checked as it grows: gather's caller says when there is no room.
    while (out.size() < kReadCap && (got = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
        if (!try_append(out, buffer, got)) {
            std::fclose(f);
            out.clear();
            return false;
        }
    }
    std::fclose(f);
    return true;
}

ReportPart file_part(const std::string& path) {
    ReportPart part;
    part.name = path;
    part.found = read_text(path, part.text);
    // The player's RiiTag key never goes into a report: it is posted publicly.
    if (part.found && path == "sd:/riftwii/settings.txt") part.text = hide_settings_secrets(part.text);
    return part;
}

// crash.txt's time and size, "" when there is none.
std::string crash_stamp() {
    struct stat st;
    if (stat(kCrashFile, &st) != 0) return "";
    return std::to_string(static_cast<long long>(st.st_mtime)) + " " + std::to_string(static_cast<long long>(st.st_size));
}

std::string asked_stamp(bool& known) {
    std::string text;
    known = read_text(kStateFile, text);
    const std::size_t at = text.find("crash ");
    if (at == std::string::npos) return "";
    return text.substr(at + 6, text.find('\n', at) - at - 6);
}

void save_asked(const std::string& stamp) {
    if (FILE* f = std::fopen(kStateFile, "wb")) {
        std::fprintf(f, "crash %s\n", stamp.c_str());
        std::fclose(f);
    }
}

// A title's version and content count from its TMD view; false when the
// title is not installed.
bool title_version(u64 title, unsigned& version, unsigned& contents) {
    static u8 view[0x1000] ATTRIBUTE_ALIGN(32);
    u32 size = 0;
    if (ES_GetTMDViewSize(title, &size) < 0 || size < sizeof(tmd_view) || size > sizeof(view)) return false;
    if (ES_GetTMDView(title, reinterpret_cast<tmd_view*>(view), size) < 0) return false;
    const tmd_view* v = reinterpret_cast<const tmd_view*>(view);
    version = v->title_version;
    contents = v->num_contents;
    return true;
}

const char* region_name() {
    CONF_Init();
    switch (CONF_GetRegion()) {
        case CONF_REGION_JP: return "Japan";
        case CONF_REGION_US: return "USA";
        case CONF_REGION_EU: return "Europe";
        case CONF_REGION_KR: return "Korea";
        case CONF_REGION_CN: return "China";
        default: return "unknown";
    }
}

// Every IOS on the console with its version, a cIOS slot's content count
// as well (a stub has few; d2x has many).
std::string describe_ios() {
    std::string out;
    for (unsigned slot = 3; slot < 256; ++slot) {
        unsigned version = 0, contents = 0;
        if (!title_version(0x100000000ull | slot, version, contents)) continue;
        if (!out.empty()) out += ", ";
        out += std::to_string(slot) + " v" + std::to_string(version);
        if (slot >= 200) out += " (" + std::to_string(contents) + " contents)";
    }
    return out.empty() ? "(none readable)" : out;
}

std::string describe_system(const std::string& reason) {
    char when[64] = "unknown time";
    const std::time_t now = std::time(nullptr);
    if (const std::tm* t = std::localtime(&now)) std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", t);
    unsigned menu_version = 0, menu_contents = 0;
    const bool menu = title_version(0x100000002ull, menu_version, menu_contents);
    std::string s = "RiftWii problem report\n";
    s += "Why: " + reason + "\n";
    s += std::string("RiftWii: ") + RIFTWII_VERSION + "\n";
    // Plugins run code the DOL does not carry: named with their stamps, so
    // a crash in one can be looked up in that plugin's ELF.
    s += "Plugins: " + plugin::Summary() + "\n";
    s += std::string("Made: ") + when + " (the console's clock)\n";
    s += std::string("Console: ") + (running_in_dolphin() ? "Dolphin" : is_wii_u() ? "Wii U (vWii)" : "Wii") +
         ", region " + region_name() + ", System Menu " +
         (menu ? "v" + std::to_string(menu_version) : std::string("unreadable")) + "\n";
    s += "Running: IOS" + std::to_string(IOS_GetVersion()) + " rev " + std::to_string(IOS_GetRevision());
    const int cios = MenuCiosSlot();
    s += cios != 0 ? ", the menu on cIOS " + std::to_string(cios) : std::string(", the menu on the Homebrew Channel's IOS");
    s += "\nMenu IOS setting: " + std::string(LoadMenuIos() == 0 ? "the Homebrew Channel's" : "IOS" + std::to_string(LoadMenuIos())) + "\n";
    s += "IOS installed: " + describe_ios() + "\n";
    s += "Controllers now: " + DescribeControllers() + "\n";
    const RestartNote& note = CurrentRestartNote();
    if (note.kind != RestartKind::None) s += "This run started after: " + note.message + "\n";
    // Test switches turn parts of RiftWii off: named up front, so a report
    // does not read like a bug when one was left in settings.txt.
    const auto tests = Settings().other.find("debug_off");
    if (tests != Settings().other.end() && !tests->second.empty())
        s += "Test switches ON in settings.txt: debug_off = " + tests->second + "\n";
    return s;
}

// The top of a pack folder: names and sizes, so a missing or misnamed
// file shows.
ReportPart listing(const std::string& dir) {
    ReportPart part;
    part.name = dir + " (folder listing)";
    DIR* d = opendir(dir.c_str());
    if (!d) {
        part.found = false;
        return part;
    }
    unsigned shown = 0;
    while (dirent* e = readdir(d)) {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
        if (++shown > 400) {
            part.text += "(more not listed)\n";
            break;
        }
        const std::string path = dir + "/" + e->d_name;
        struct stat st;
        const bool ok = stat(path.c_str(), &st) == 0;
        if (ok && S_ISDIR(st.st_mode)) part.text += std::string(e->d_name) + "/\n";
        else part.text += std::string(e->d_name) + "  " + (ok ? std::to_string(static_cast<long long>(st.st_size)) : "?") + "\n";
    }
    closedir(d);
    return part;
}

// The XMLs of the packs the last launched game had on.
void add_packs(const std::string& choices, std::vector<ReportPart>& parts) {
    for (const std::string& file : enabled_pack_files(choices)) {
        if (file.find(" @ ") != std::string::npos) {
            parts.push_back({file + " (from a RiiFS server, not on the card)", "", false});
            continue;
        }
        if (file.find("..") != std::string::npos) continue;
        bool found = false;
        for (const char* dir : {"sd:/riivolution/", "sd:/apps/riivolution/", "usb:/riivolution/",
                                "usb:/apps/riivolution/", "sd:/"}) {
            ReportPart part = file_part(dir + file);
            if (!part.found) continue;
            parts.push_back(part);
            found = true;
            break;
        }
        if (!found) parts.push_back({file, "", false});
    }
}

void add_gc_remotes(std::vector<ReportPart>& parts) {
    for (const char* path : {"sd:/riftwii/gc/trace.bin", "sd:/riftwii/gc/trace-crash.bin"}) {
        std::string raw;
        if (!read_text(path, raw)) continue;  // at most kReadCap; a trace is 128 KB
        const std::vector<std::uint8_t> trace(raw.begin(), raw.end());
        std::string text;
        if (!riftwii::describe_gc_remotes(trace, text)) continue;
        parts.push_back({std::string("Wii Remotes in the GameCube game (") + path + ")", text, true});
        return;
    }
}

// The report in `report`, or false when there is no room to make it: the
// files are read through checked buffers, and the copies assemble_report
// makes of them are asked for before it runs.
bool gather(const std::string& reason, std::size_t limit, std::string& report) {
    OutOfMemoryScope out_of_memory;
    std::vector<ReportPart> parts;
    parts.push_back(file_part(kCrashFile));
    parts.push_back(file_part(kGameCrashFile));
    parts.push_back(file_part(kWpadLogFile));
    const ReportPart boot = file_part("sd:/riftwii/boot.log");
    parts.push_back(boot);
    // Check each cIOS's results, early: a long report is cut at its end.
    parts.push_back(file_part("sd:/riftwii/usbcheck.txt"));
    ReportPart previous = file_part(kPreviousLog);
    if (!previous.found) previous = file_part(std::string(kPreviousLog) + ".backup");
    parts.push_back(previous);
    // The open session log is closed while it is read.
    LogClose();
    parts.push_back(file_part(kSessionLog));
    LogReopen();
    parts.push_back(file_part("sd:/riftwii/cardlog.txt"));
    parts.push_back(file_part("sd:/riftwii/settings.txt"));
    parts.push_back(file_part("sd:/riftwii/menu_ios.txt"));
    parts.push_back(file_part("sd:/riftwii/update.txt"));
    parts.push_back(file_part("sd:/riftwii/channel.log"));
    // The game and packs of the last launch.
    const std::string id = launched_game_id(boot.text);
    if (!id.empty()) {
        // A launch from one of the game's profile tiles ran that tile's
        // choices (riftwii/profiles.hpp), its mods and cheats among them.
        const ReportPart lastTile = file_part(riftwii::kLastTilePath);
        const std::string tile = riftwii::launched_choices_key(id, lastTile.found ? lastTile.text : "");
        ReportPart choices = file_part("sd:/riftwii/choices/" + tile + ".txt");
        parts.push_back(choices);
        // The border note is the game's, whichever tile ran it.
        parts.push_back(file_part("sd:/riftwii/choices/" + id + ".video"));
        // The cheats picked, as the cheat file has them (a tester's own
        // code that did nothing could not be seen before).
        const ReportPart cheats = file_part(CheatPath(id));
        if (choices.found) parts.push_back({"cheats picked for " + tile, picked_cheats_report(choices.text, cheats.text), true});
        add_packs(choices.text, parts);
    }
    // The Wii Remotes of the last GameCube game: how far its Bluetooth got,
    // from the loader's trace (kept as trace-crash.bin after a crash), so a
    // report says why a remote did nothing even when the game did not crash.
    if (boot.text.find("GameCube (native): ") != std::string::npos) add_gc_remotes(parts);
    parts.push_back(listing("sd:/riivolution"));
    parts.push_back(listing("sd:/riftwii"));
    std::size_t held = 0;
    for (const ReportPart& part : parts) held += part.text.size();
    if (out_of_memory.failed() || !have_room(2 * held + limit)) return false;
    report = assemble_report(describe_system(reason), parts, limit);
    return true;
}

}  // namespace

namespace {

constexpr const char* kLogDir = "sd:/riftwii/logs";
constexpr unsigned kKeptLogFolders = 10;

std::string LogStamp(std::time_t when) {
    struct tm local;
    if (when <= 0 || localtime_r(&when, &local) == nullptr) return std::string();
    if (local.tm_year + 1900 < 2020) return std::string();
    char text[32];
    std::snprintf(text, sizeof text, "%04d%02d%02d-%02d%02d%02d", local.tm_year + 1900, local.tm_mon + 1,
                  local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
    return text;
}

bool CopyFile(const std::string& from, const std::string& to) {
    FILE* in = std::fopen(from.c_str(), "rb");
    if (!in) return false;
    FILE* out = std::fopen(to.c_str(), "wb");
    bool ok = out != nullptr;
    char buffer[4096];
    std::size_t got = 0;
    while (ok && (got = std::fread(buffer, 1, sizeof buffer, in)) > 0)
        ok = std::fwrite(buffer, 1, got, out) == got;
    if (std::ferror(in)) ok = false;
    std::fclose(in);
    if (out) {
        if (std::fflush(out) != 0 || fsync(fileno(out)) != 0) ok = false;
        if (std::fclose(out) != 0) ok = false;
    }
    if (!ok) std::remove(to.c_str());
    return ok;
}

bool MoveFile(const std::string& from, const std::string& to) {
    if (std::rename(from.c_str(), to.c_str()) == 0) return true;
    if (!CopyFile(from, to)) return false;
    return std::remove(from.c_str()) == 0;
}

bool PublishFile(const std::string& temp, const std::string& destination) {
    if (std::rename(temp.c_str(), destination.c_str()) == 0) return true;
    // Some FAT implementations cannot rename over an existing file. Keep
    // that file as a recoverable backup until the staged copy is published.
    const std::string backup = destination + ".backup";
    struct stat info;
    if (stat(destination.c_str(), &info) != 0) return false;
    if (stat(backup.c_str(), &info) == 0 && std::remove(backup.c_str()) != 0) return false;
    if (std::rename(destination.c_str(), backup.c_str()) != 0) return false;
    if (std::rename(temp.c_str(), destination.c_str()) != 0) {
        std::rename(backup.c_str(), destination.c_str());
        return false;
    }
    std::remove(backup.c_str());
    return true;
}

void DeleteFolder(const std::string& path) {
    if (DIR* dir = opendir(path.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            std::remove((path + "/" + name).c_str());
        }
        closedir(dir);
    }
    std::remove(path.c_str());
}

bool LogFolderName(const std::string& name) {
    std::size_t suffix = 15;
    if (name.rfind("undated-", 0) == 0) {
        suffix = 7;
    } else {
        if (name.size() < 15 || name[8] != '-') return false;
        for (std::size_t i = 0; i < 15; ++i) {
            if (i != 8 && (name[i] < '0' || name[i] > '9')) return false;
        }
        if (name.size() == 15) return true;
    }
    if (name.size() <= suffix + 1 || name[suffix] != '-') return false;
    return name.find_first_not_of("0123456789", suffix + 1) == std::string::npos;
}

struct LogFolder {
    std::string name;
    std::time_t when = 0;
};

void PruneLogFolders(const std::string& current) {
    std::vector<LogFolder> folders;
    if (DIR* dir = opendir(kLogDir)) {
        while (const dirent* entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (!LogFolderName(name) && name.rfind("undated", 0) != 0) continue;
            const std::string path = std::string(kLogDir) + "/" + name;
            struct stat st;
            folders.push_back({name, stat(path.c_str(), &st) == 0 ? st.st_mtime : 0});
        }
        closedir(dir);
    }
    std::sort(folders.begin(), folders.end(), [](const LogFolder& a, const LogFolder& b) {
        return a.when != b.when ? a.when < b.when : a.name < b.name;
    });
    for (std::size_t i = 0; i < folders.size() && folders.size() > kKeptLogFolders;) {
        const std::string path = std::string(kLogDir) + "/" + folders[i].name;
        if (path == current) {
            ++i;
            continue;
        }
        DeleteFolder(path);
        folders.erase(folders.begin() + static_cast<std::vector<LogFolder>::difference_type>(i));
    }
}

// mkdir reserves the name rather than reusing a folder after a stat check.
std::string NewLogFolder(const std::string& stamp) {
    const std::string base = std::string(kLogDir) + "/" + (stamp.empty() ? "undated" : stamp);
    const unsigned first = stamp.empty() ? 1 : 0;
    for (unsigned n = first; n < 1000; ++n) {
        const std::string path = base + (n == 0 ? "" : "-" + std::to_string(n));
        if (mkdir(path.c_str(), 0777) == 0) return path;
        if (errno != EEXIST) return "";
    }
    return "";
}

// Crash records survive reporting. Remember the last archived contents and
// timestamp so an unchanged old record is not attached to every new session.
void CopyNewDiagnostic(const char* source, const std::string& folder, const char* name) {
    struct stat info;
    if (stat(source, &info) != 0 || info.st_size < 0 || static_cast<std::size_t>(info.st_size) > kReadCap) return;
    std::string contents;
    if (!read_text(source, contents) || contents.size() != static_cast<std::size_t>(info.st_size)) return;
    const Sha256Digest digest = sha256(reinterpret_cast<const std::uint8_t*>(contents.data()), contents.size());
    std::string fingerprint = std::to_string(static_cast<long long>(info.st_mtime)) + " ";
    constexpr char hex[] = "0123456789abcdef";
    for (std::uint8_t byte : digest) {
        fingerprint.push_back(hex[byte >> 4]);
        fingerprint.push_back(hex[byte & 15]);
    }
    const std::string marker = std::string(kLogDir) + "/" + name + ".last";
    std::string previous;
    if (read_text(marker, previous) && previous == fingerprint) return;
    if (!CopyFile(source, folder + "/" + name)) return;
    const std::string temp = marker + ".part";
    FILE* file = std::fopen(temp.c_str(), "wb");
    if (!file) return;
    bool ok = std::fwrite(fingerprint.data(), 1, fingerprint.size(), file) == fingerprint.size();
    if (std::fflush(file) != 0 || fsync(fileno(file)) != 0) ok = false;
    if (std::fclose(file) != 0) ok = false;
    if (ok) PublishFile(temp, marker);
    std::remove(temp.c_str());
}

}  // namespace

bool RotateSessionLog() {
    // A power loss between the two FAT renames still leaves report input.
    const std::string backup = std::string(kPreviousLog) + ".backup";
    struct stat previous;
    if (stat(kPreviousLog, &previous) != 0 && errno == ENOENT)
        std::rename(backup.c_str(), kPreviousLog);
    struct stat st;
    if (stat(kSessionLog, &st) != 0) return errno == ENOENT;
    mkdir("sd:/riftwii", 0777);
    if (mkdir(kLogDir, 0777) != 0 && errno != EEXIST) return false;
    const std::string folder = NewLogFolder(LogStamp(st.st_mtime));
    if (folder.empty()) return false;
    const std::string archived = folder + "/session.log";
    if (!MoveFile(kSessionLog, archived)) {
        logf("The session log could not be archived; startup will append to it.\n");
        return false;
    }
    CopyFile("sd:/riftwii/boot.log", folder + "/boot.log");
    CopyNewDiagnostic(kCrashFile, folder, "crash.txt");
    CopyNewDiagnostic(kGameCrashFile, folder, "gamecrash.txt");
    CopyNewDiagnostic(kWpadLogFile, folder, "wpadlog.txt");
    // Publish only a complete, synced copy. A failed copy or rename leaves
    // the existing report input intact; the new log is safe in its archive.
    const std::string temp = std::string(kPreviousLog) + ".part";
    if (!CopyFile(archived, temp) || !PublishFile(temp, kPreviousLog)) {
        std::remove(temp.c_str());
        logf("The previous-session log could not be refreshed; its old copy and the new archive are kept.\n");
        return true;
    }
    PruneLogFolders(folder);
    return true;
}

std::string DescribeControllers() {
    static const char* const kExpansion[] = {"", " + Nunchuk", " + Classic Controller", " + guitar", " (Balance Board)"};
    std::string out;
    const auto add = [&out](const std::string& s) { out += (out.empty() ? "" : ", ") + s; };
    for (int i = 0; i < 4; ++i) {
        u32 type = 0;
        if (WPAD_Probe(i, &type) != WPAD_ERR_NONE) continue;
        std::string s = "Wii Remote " + std::to_string(i + 1);
        if (const WPADData* d = WPAD_Data(i)) {
            const unsigned exp = d->exp.type;
            if (exp == WPAD_EXP_CLASSIC && d->exp.classic.type == CLASSIC_TYPE_WIIU) {
                // libogc reads it as a Classic Controller; games do not
                // (the next launch keeps the Wii Remote log for that).
                s = "Wii U Pro Controller " + std::to_string(i + 1);
                note_wiiu_pro_seen();
            } else if (exp < sizeof(kExpansion) / sizeof(kExpansion[0])) s += kExpansion[exp];
            else if (exp != WPAD_EXP_NONE) s += " + expansion " + std::to_string(exp);
        }
        add(s);
    }
    const u32 ports = PAD_ScanPads();
    for (int i = 0; i < 4; ++i) {
        if (ports & (1u << i)) add("GameCube port " + std::to_string(i + 1));
    }
    GcAdapterView view;
    if (GcAdapterMenuLastView(view) && view.open) {
        std::string s = "GameCube adapter (ports";
        bool any = false;
        for (unsigned i = 0; i < GCAD_PORTS; ++i) {
            if (!view.present[i]) continue;
            s += " " + std::to_string(i + 1);
            any = true;
        }
        add(s + (any ? ")" : " empty)"));
    }
    if (WiiDRC_Inited() && WiiDRC_Connected()) add("Wii U GamePad");
    const std::string usb = UsbInputDescribe();
    if (!usb.empty()) add(usb);
    if (out.empty()) out = "none found";
    const PadPairings pairings = ReadPadPairings();
    return out + "; " + DescribePadPairings(pairings);
}

// A GameCube game's crash: its loader's trace (riftwii/gccrash.hpp), kept
// as trace-crash.bin once read so it is asked about once; the next launch
// makes a new trace.bin. The game's ID from the launch's log.
static bool import_gc_crash() {
    static const char kTrace[] = "sd:/riftwii/gc/trace.bin";
    static const char kKept[] = "sd:/riftwii/gc/trace-crash.bin";
    std::string raw;
    if (!read_text(kTrace, raw)) return false;
    const std::vector<std::uint8_t> trace(raw.begin(), raw.end());
    std::string id, text;
    std::string boot;
    if (read_text("sd:/riftwii/boot.log", boot)) {
        // The loader's own line: "GameCube (native): GMSE04 (sd:/...)".
        static const char kLine[] = "GameCube (native): ";
        const std::size_t at = boot.find(kLine);
        if (at != std::string::npos && at + sizeof(kLine) - 1 + 6 <= boot.size()) id = boot.substr(at + sizeof(kLine) - 1, 6);
    }
    if (!describe_gc_crash(trace, id, text)) return false;
    std::remove(kKept);
    std::rename(kTrace, kKept);
    if (FILE* f = std::fopen(kGameCrashFile, "wb")) {
        std::fputs(text.c_str(), f);
        std::fclose(f);
    }
    g_game_crashed = true;
    logf("Game crash: %s (from %s, saved as %s)\n", text.substr(0, text.find('\n')).c_str(), kTrace, kGameCrashFile);
    std::size_t line = 0;
    while (line < text.size()) {
        const std::size_t end = text.find('\n', line);
        logf("  %s\n", text.substr(line, end - line).c_str());
        if (end == std::string::npos) break;
        line = end + 1;
    }
    return true;
}

void ImportGameCrash() {
    if (import_gc_crash()) return;
    if (NandOpen() < 0) return;
    static u8 record[512] ATTRIBUTE_ALIGN(32);
    const s32 fd = ISFS_Open(kGameCrashRecord, ISFS_OPEN_READ);
    if (fd < 0) {
        NandClose();
        return;
    }
    const s32 got = ISFS_Read(fd, record, sizeof(record));
    ISFS_Close(fd);
    ISFS_Delete(kGameCrashRecord);
    // /dev/fs is not kept open: an IOS reload with it open damages the heap.
    NandClose();
    std::string text, error;
    if (got < 0 || !describe_game_fault(record, static_cast<std::size_t>(got), text, error)) {
        logf("Game crash: %s: %s; deleted\n", kGameCrashRecord, got < 0 ? "unreadable" : error.c_str());
        return;
    }
    if (FILE* f = std::fopen(kGameCrashFile, "wb")) {
        std::fputs(text.c_str(), f);
        std::fclose(f);
    }
    g_game_crashed = true;
    const std::string first = text.substr(0, text.find('\n'));
    logf("Game crash: %s (saved as %s)\n", first.c_str(), kGameCrashFile);
    // The whole record in the log too, so an older report still has it.
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t end = text.find('\n', at);
        logf("  %s\n", text.substr(at, end - at).c_str());
        if (end == std::string::npos) break;
        at = end + 1;
    }
}

void ImportWpadLog() {
    if (NandOpen() < 0) return;
    const s32 fd = ISFS_Open(kWpadLogRecord, ISFS_OPEN_READ);
    if (fd < 0) {
        NandClose();
        return;
    }
    // On the heap for the moment it is read: 82 KB kept for good would
    // only take room from the menu. Whole cache lines: the read invalidates
    // the line holding the record's last bytes (82536 is not a multiple of
    // 32), and with the heap's next header in it that header went back to
    // what the game left in memory; the next allocation failed and RiftWii
    // stopped at every return from a game that wrote this log.
    const std::size_t room = (sizeof(rtwlog_file) + 31) & ~std::size_t(31);
    u8* record = static_cast<u8*>(memalign(32, room));
    const s32 got = record != nullptr ? ISFS_Read(fd, record, sizeof(rtwlog_file)) : -1;
    ISFS_Close(fd);
    ISFS_Delete(kWpadLogRecord);
    // /dev/fs is not kept open: an IOS reload with it open damages the heap.
    NandClose();
    std::string text, error;
    const bool described = got >= 0 && describe_wpad_log(record, static_cast<std::size_t>(got), text, error);
    std::free(record);
    if (!described) {
        logf("Wii Remote log: %s: %s; deleted\n", kWpadLogRecord, got < 0 ? "unreadable" : error.c_str());
        return;
    }
    if (FILE* f = std::fopen(kWpadLogFile, "wb")) {
        std::fputs(text.c_str(), f);
        std::fclose(f);
    }
    const std::string first = text.substr(0, text.find('\n'));
    logf("%s (saved as %s for the next problem report)\n", first.c_str(), kWpadLogFile);
}

bool UnreportedCrash(std::string& what) {
    if (g_game_crashed) {
        what = "game";
        return true;
    }
    const RestartNote& note = CurrentRestartNote();
    bool known = false;
    const std::string asked = asked_stamp(known);
    const std::string stamp = crash_stamp();
    if (note.kind == RestartKind::Crashed) {
        what = "crash";
        return stamp.empty() || stamp != asked;
    }
    if (note.kind == RestartKind::LaunchFailed) {
        what = "launch";
        return true;
    }
    if (stamp.empty() || stamp == asked) return false;
    // The first run with reports: an old crash.txt is not asked about.
    if (!known) {
        save_asked(stamp);
        return false;
    }
    what = "crash";
    return true;
}

void NoteCrashAsked() {
    const std::string stamp = crash_stamp();
    if (!stamp.empty()) save_asked(stamp);
}

ReportOutcome SendProblemReport(const std::string& reason) {
    ReportOutcome out;
    std::string report;
    if (!gather(reason, kReportLimit, report)) {
        out.error = "not enough memory to gather the report";
        logf("Problem report: not sent: %s\n", out.error.c_str());
        return out;
    }
    out.bytes = report.size();
    if (FILE* f = std::fopen(kReportPath, "wb")) {
        out.saved = std::fwrite(report.data(), 1, report.size(), f) == report.size();
        out.saved = std::fclose(f) == 0 && out.saved;
    }
    int status = 0;
    std::string answer;
    if (HttpPost(kPasteUrl, "text/plain; charset=utf-8", report, status, answer, out.error)) {
        out.sent = paste_link(status, answer, out.link, out.partial, out.error);
    }
    if (!out.sent && status >= 500) {
        // A server error (paste.rs answered a 117 KB report with "500:
        // oops"): once more, half the size, before giving up.
        logf("Problem report: paste.rs answered %d to %u bytes; sending a shorter one\n", status,
             static_cast<unsigned>(report.size()));
        const std::size_t half = kReportLimit / 2 < report.size() / 2 ? kReportLimit / 2 : report.size() / 2;
        std::string().swap(report);
        status = 0;
        answer.clear();
        out.error.clear();
        if (!gather(reason, half, report)) {
            out.error = "not enough memory to gather the report";
        } else {
            out.bytes = report.size();
            if (HttpPost(kPasteUrl, "text/plain; charset=utf-8", report, status, answer, out.error)) {
                out.sent = paste_link(status, answer, out.link, out.partial, out.error);
            }
        }
    }
    if (!out.sent) {
        logf("Problem report: paste.rs: %s; trying dpaste.com\n", out.error.c_str());
        // Form-encoded, the text grows by up to three times: half the
        // size, and out of memory is a failure, not a crash.
        std::string why;
        // Half the report sent last (or half the limit, when the shorter
        // try found no room to gather one).
        const std::size_t keep =
            report.empty() || kReportLimit / 2 < report.size() ? kReportLimit / 2 : report.size();
        std::string().swap(report);
        std::string small;
        // The form body is checked for room before it is made: the encoded
        // text can be three times the size.
        static const char kFormStart[] = "syntax=text&expiry_days=90&content=";
        std::string body;
        if (!gather(reason, keep, small) || !try_reserve(body, sizeof(kFormStart) - 1 + url_encoded_size(small))) {
            why = "not enough memory to send it";
        } else {
            body = kFormStart;
            url_encode_append(small, body);
            out.bytes = small.size();
            std::string().swap(small);
            status = 0;
            answer.clear();
            if (HttpPost(kDpasteUrl, "application/x-www-form-urlencoded", body, status, answer, why)) {
                bool partial = false;
                out.sent = paste_link(status, answer, out.link, partial, why, "dpaste.com");
            }
        }
        if (!out.sent) out.error += "; dpaste.com: " + why;
    }
    if (out.sent) {
        logf("Problem report: sent, %s (%u bytes%s)\n", out.link.c_str(), static_cast<unsigned>(out.bytes),
             out.partial ? ", paste.rs kept only the start" : "");
    } else {
        logf("Problem report: not sent: %s (%u bytes, %s)\n", out.error.c_str(), static_cast<unsigned>(out.bytes),
             out.saved ? "saved to sd:/riftwii/report.txt" : "not saved either");
    }
    return out;
}

}  // namespace riftwii::wii
