// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "reportsend.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

#include <gccore.h>
#include <ogc/conf.h>
#include <wiiuse/wpad.h>

#include "boot.hpp"
#include "gcadapter.hpp"
#include "ios_reload.hpp"
#include "log.hpp"
#include "menuios.hpp"
#include "online.hpp"
#include "restart.hpp"
#include "riftwii/problemreport.hpp"
#include "wiidrc.h"

namespace riftwii::wii {
namespace {

constexpr const char* kSessionLog = "sd:/riftwii/session.log";
constexpr const char* kPreviousLog = "sd:/riftwii/session-previous.log";
constexpr const char* kCrashFile = "sd:/riftwii/crash.txt";
constexpr const char* kStateFile = "sd:/riftwii/report-state.txt";
constexpr const char* kPasteUrl = "https://paste.rs/";
// More than this of one file is never read (the report keeps far less).
constexpr std::size_t kReadCap = 2u << 20;

bool read_text(const std::string& path, std::string& out) {
    out.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buffer[4096];
    std::size_t got;
    while (out.size() < kReadCap && (got = std::fread(buffer, 1, sizeof(buffer), f)) > 0) out.append(buffer, got);
    std::fclose(f);
    return true;
}

ReportPart file_part(const std::string& path) {
    ReportPart part;
    part.name = path;
    part.found = read_text(path, part.text);
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

std::string gather(const std::string& reason) {
    std::vector<ReportPart> parts;
    parts.push_back(file_part(kCrashFile));
    parts.push_back(file_part("sd:/riftwii/gamecrash.txt"));
    const ReportPart boot = file_part("sd:/riftwii/boot.log");
    parts.push_back(boot);
    parts.push_back(file_part(kPreviousLog));
    // The open session log is closed while it is read.
    LogClose();
    parts.push_back(file_part(kSessionLog));
    LogReopen();
    parts.push_back(file_part("sd:/riftwii/cardlog.txt"));
    parts.push_back(file_part("sd:/riftwii/settings.txt"));
    parts.push_back(file_part("sd:/riftwii/menu_ios.txt"));
    parts.push_back(file_part("sd:/riftwii/update.txt"));
    // The game and packs of the last launch.
    const std::string id = launched_game_id(boot.text);
    if (!id.empty()) {
        ReportPart choices = file_part("sd:/riftwii/choices/" + id + ".txt");
        parts.push_back(choices);
        parts.push_back(file_part("sd:/riftwii/choices/" + id + ".video"));
        add_packs(choices.text, parts);
    }
    parts.push_back(listing("sd:/riivolution"));
    parts.push_back(listing("sd:/riftwii"));
    return assemble_report(describe_system(reason), parts, kReportLimit);
}

}  // namespace

void RotateSessionLog() {
    struct stat st;
    if (stat(kSessionLog, &st) != 0) return;
    std::remove(kPreviousLog);
    std::rename(kSessionLog, kPreviousLog);
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
            if (exp < sizeof(kExpansion) / sizeof(kExpansion[0])) s += kExpansion[exp];
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
    if (out.empty()) out = "none found";
    const PadPairings pairings = ReadPadPairings();
    return out + "; " + DescribePadPairings(pairings);
}

bool UnreportedCrash(std::string& what) {
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
    const std::string report = gather(reason);
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
