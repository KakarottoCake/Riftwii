// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "online.hpp"

#include <network.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <gccore.h>
#include <ogc/lwp_watchdog.h>

#include "bearssl.h"
#include "loadersettings.hpp"
#include "log.hpp"
#include "netsock.hpp"
#include "tls.hpp"
#include "riftwii/cheats.hpp"
#include "riftwii/dol.hpp"
#include "riftwii/http.hpp"
#include "riftwii/themepack.hpp"
#include "riftwii/update.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kTitlesUrl = "http://www.gametdb.com/wiitdb.txt?LANG=";
constexpr const char* kCheatsUrl = "http://codes.rc24.xyz/txt.php?txt=";
constexpr std::time_t kWeek = 7 * 24 * 60 * 60;
// The newest few: GitHub lists them by day, then by the tag's name as text,
// so the newest version is picked from them (newest_release_tag).
constexpr const char* kReleasesApi = "https://api.github.com/repos/KakarottoCake/Riftwii/releases?per_page=10";
constexpr const char* kLatestApi = "https://api.github.com/repos/KakarottoCake/Riftwii/releases/latest";
constexpr const char* kUpdateNote = "sd:/riftwii/update.txt";
constexpr const char* kReleaseByTagApi = "https://api.github.com/repos/KakarottoCake/Riftwii/releases/tags/";
// The release's themes and channel installer, for the in-app update.
constexpr const char* kUpdatePackAsset = "riftwii-update.pack";
// The version whose themes are on the card (ApplyThemePack).
constexpr const char* kThemesDone = "sd:/riftwii/themes_updated.txt";
// The rest of the zip's sd:/apps (the channel installer), the same way.
constexpr const char* kAppsDone = "sd:/riftwii/apps_updated.txt";

// Hosts that could not be connected to this session: asked again, they
// fail at once, so a server the network cannot reach (GameTDB, for one
// tester) costs one timeout and not one per download.
std::vector<std::string> g_unreachable;

bool exchange(const HttpUrl& url, const std::string& request, HttpResponse& response, std::string& error,
              std::size_t max_bytes, int timeout_ms, const HttpProgress& progress = nullptr) {
    NetServer server;
    if (!ResolveServer(url.host, url.port, server, error)) return false;
    // By address: GameTDB's names and covers come from one server under
    // two names.
    const std::string where = server.label();
    if (std::find(g_unreachable.begin(), g_unreachable.end(), where) != g_unreachable.end()) {
        error = url.host + " (" + where + ") could not be reached earlier; not tried again until RiftWii starts again";
        return false;
    }
    SocketTransport socket;
    if (!socket.connect(server, timeout_ms, error)) {
        g_unreachable.push_back(where);
        return false;
    }
    TlsStream tls;
    if (url.tls && !tls.open(socket, url.host, error)) return false;
    const bool sent = url.tls ? tls.send(request.data(), request.size()) : socket.send(request.data(), request.size());
    if (!sent) {
        error = "cannot send the request to " + url.host;
        return false;
    }
    std::vector<std::uint8_t> raw;
    std::uint8_t chunk[4096];
    HttpReadState reading;
    // Each read waits at most the socket's timeout; the whole answer gets
    // four times this call's timeout, or longer for a large download at
    // 16 KiB/s, so a server sending a trickle cannot hold the menu forever.
    const std::uint64_t budget_ms =
        std::max<std::uint64_t>(4ull * static_cast<std::uint64_t>(timeout_ms), max_bytes / 16u);
    const u64 started = gettime();
    // Read until the response is whole or the server closes.
    while (!http_response_complete(raw, reading)) {
        if (ticks_to_millisecs(diff_ticks(started, gettime())) > budget_ms) {
            error = url.host + " is answering too slowly; given up after " + std::to_string(budget_ms / 1000) + " s";
            return false;
        }
        std::size_t got = 0;
        const bool ok = url.tls ? tls.receive_some(chunk, sizeof(chunk), got) : socket.receive_some(chunk, sizeof(chunk), got);
        if (!ok) {
            error = "no answer from " + url.host;
            if (url.tls && tls.last_error() != 0) error += " (TLS error " + std::to_string(tls.last_error()) + ")";
            return false;
        }
        if (got == 0) break;  // closed
        raw.insert(raw.end(), chunk, chunk + got);
        if (progress) progress(raw.size());
        if (raw.size() > max_bytes + 4096) {
            error = url.host + " sent more than " + std::to_string(max_bytes) + " bytes";
            return false;
        }
    }
    return parse_http_response(raw, response, error);
}

bool write_file(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string& error) {
    const std::string temp = path + ".part";
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) {
        error = "cannot write " + temp;
        return false;
    }
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose(f);
    if (!ok) {
        std::remove(temp.c_str());
        error = "cannot write " + temp + " (card full?)";
        return false;
    }
    std::remove(path.c_str());
    if (std::rename(temp.c_str(), path.c_str()) != 0) {
        error = "cannot rename " + temp;
        return false;
    }
    return true;
}

}  // namespace

bool HttpGet(const std::string& url, std::vector<std::uint8_t>& body, std::string& error, std::size_t max_bytes,
             int timeout_ms, const HttpProgress& progress) {
    if (!NetStart(error)) return false;
    HttpUrl parsed;
    if (!parse_http_url(url, parsed, error)) return false;
    for (int hop = 0; hop < 4; ++hop) {
        HttpResponse response;
        if (!exchange(parsed, http_get_request(parsed), response, error, max_bytes, timeout_ms, progress)) return false;
        if (response.status >= 300 && response.status < 400 && response.headers.count("location")) {
            // Never from https to plain http: an update's DOL or update
            // pack would then come over a connection anyone can change.
            HttpUrl next;
            if (!http_redirect(parsed, response.headers["location"], next, error)) return false;
            parsed = next;
            continue;
        }
        if (response.status != 200) {
            error = parsed.host + " answered " + std::to_string(response.status);
            return false;
        }
        body = std::move(response.body);
        return true;
    }
    error = "too many redirects for " + http_url_for_messages(url);
    return false;
}

bool HttpPost(const std::string& url, const std::string& content_type, const std::string& body, int& status,
              std::string& answer, std::string& error, int timeout_ms) {
    status = 0;
    if (!NetStart(error)) return false;
    HttpUrl parsed;
    if (!parse_http_url(url, parsed, error)) return false;
    HttpResponse response;
    if (!exchange(parsed, http_post_request(parsed, content_type, body), response, error, 64u << 10, timeout_ms))
        return false;
    status = response.status;
    answer.assign(response.body.begin(), response.body.end());
    return true;
}

std::string TitlesPath(const std::string& lang) { return "sd:/riftwii/titles-" + lang + ".txt"; }

bool UpdateTitles(const std::string& lang, bool force, std::string& error) {
    const std::string path = TitlesPath(lang);
    struct stat st;
    if (!force && stat(path.c_str(), &st) == 0 && st.st_size > 1024 && std::time(nullptr) - st.st_mtime < kWeek) {
        return true;
    }
    std::vector<std::uint8_t> body;
    const std::string gametdb_lang = lang == "ja" ? "JA" : lang == "es" ? "ES" : lang == "pt" ? "PT" : lang == "it" ? "IT" : lang == "fr" ? "FR" : lang == "ko" ? "KO" : "EN";
    if (!HttpGet(kTitlesUrl + gametdb_lang, body, error)) return false;
    const std::string head(body.begin(), body.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(body.size(), 64)));
    if (body.size() < 1024 || head.find(" = ") == std::string::npos) {
        error = "GameTDB sent something that is not a title list";
        return false;
    }
    mkdir("sd:/riftwii", 0777);
    if (!write_file(path, body, error)) return false;
    logf("Titles: %u bytes of game names (%s) from GameTDB\n", static_cast<unsigned>(body.size()), gametdb_lang.c_str());
    return true;
}

namespace {

// sd:/riftwii/update.txt: what the last check found, and what was
// installed since.
struct UpdateNote {
    std::time_t checked = 0;
    std::string channel;  // the channel `latest` was asked on
    std::string latest;
    ReleaseAsset dol;
    std::string installed;
};

UpdateNote ReadUpdateNote() {
    UpdateNote note;
    if (FILE* f = std::fopen(kUpdateNote, "rb")) {
        char line[1024];
        while (std::fgets(line, sizeof(line), f)) {
            std::string text(line);
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
            if (text.rfind("checked ", 0) == 0) note.checked = static_cast<std::time_t>(std::strtoll(text.c_str() + 8, nullptr, 10));
            if (text.rfind("latest ", 0) == 0) note.latest = text.substr(7);
            if (text.rfind("channel ", 0) == 0) note.channel = text.substr(8);
            if (text.rfind("dol ", 0) == 0) note.dol.url = text.substr(4);
            if (text.rfind("sha256 ", 0) == 0) note.dol.sha256 = text.substr(7);
            if (text.rfind("size ", 0) == 0) note.dol.size = std::strtoull(text.c_str() + 5, nullptr, 10);
            if (text.rfind("installed ", 0) == 0) note.installed = text.substr(10);
        }
        std::fclose(f);
    }
    return note;
}

void WriteUpdateNote(const UpdateNote& note) {
    if (FILE* f = std::fopen(kUpdateNote, "wb")) {
        std::fprintf(f, "checked %lld\nchannel %s\nlatest %s\n", static_cast<long long>(note.checked),
                     note.channel.c_str(), note.latest.c_str());
        if (!note.dol.url.empty()) {
            std::fprintf(f, "dol %s\nsha256 %s\nsize %llu\n", note.dol.url.c_str(), note.dol.sha256.c_str(),
                         note.dol.size);
        }
        if (!note.installed.empty()) std::fprintf(f, "installed %s\n", note.installed.c_str());
        std::fclose(f);
    }
}

bool EndsWithDol(const std::string& path) {
    if (path.size() < 4) return false;
    std::string tail = path.substr(path.size() - 4);
    for (char& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return tail == ".dol";
}

// The boot.dol this run came from: the Homebrew Channel passes its path
// as argv[0] ("sd:/apps/riftwii/boot.dol"; older loaders leave out "sd:").
// Empty when it is not a file on the SD card.
std::string RunningDolPath() {
    if (__system_argv != nullptr && __system_argv->argvMagic == ARGV_MAGIC && __system_argv->argc > 0 &&
        __system_argv->argv != nullptr && __system_argv->argv[0] != nullptr) {
        std::string path = __system_argv->argv[0];
        if (path.rfind("/apps/", 0) == 0) path = "sd:" + path;
        struct stat st;
        if (path.rfind("sd:/", 0) == 0 && EndsWithDol(path) && stat(path.c_str(), &st) == 0) return path;
    }
    struct stat st;
    if (stat("sd:/apps/riftwii/boot.dol", &st) == 0) return "sd:/apps/riftwii/boot.dol";
    return "";
}

std::string Sha256Hex(const std::vector<std::uint8_t>& bytes) {
    br_sha256_context ctx;
    br_sha256_init(&ctx);
    br_sha256_update(&ctx, bytes.data(), bytes.size());
    unsigned char digest[32];
    br_sha256_out(&ctx, digest);
    static const char hex[] = "0123456789abcdef";
    std::string out;
    for (unsigned char b : digest) {
        out += hex[b >> 4];
        out += hex[b & 15];
    }
    return out;
}

// <ahb_access/> before </app> when meta.xml lacks it: the Homebrew
// Channel then starts RiftWii with hardware access, which a pack's
// Homebrew Channel app (CTGP-R 1.03's) gets passed on. False when the
// text already has it or has no </app>.
bool AddAhbAccess(std::string& text) {
    if (text.find("<ahb_access") != std::string::npos) return false;
    const std::size_t end = text.rfind("</app>");
    if (end == std::string::npos) return false;
    const bool crlf = text.find("\r\n") != std::string::npos;
    text.insert(end, std::string("  <ahb_access/>") + (crlf ? "\r\n" : "\n"));
    return true;
}

// meta.xml's <version> next to the DOL, so the Homebrew Channel shows the
// new one. Best effort: the DOL is what counts.
void UpdateMetaVersion(const std::string& dol_path, const std::string& latest) {
    const std::string meta = dol_path.substr(0, dol_path.rfind('/') + 1) + "meta.xml";
    FILE* f = std::fopen(meta.c_str(), "rb");
    if (!f) return;
    std::string text;
    char buf[1024];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);
    const std::size_t open = text.find("<version>");
    const std::size_t close = text.find("</version>");
    if (open == std::string::npos || close == std::string::npos || close < open) return;
    // The releases' own spelling: "v2610-123" becomes "2610-123" (and an
    // older "v2.0.3-beta", "2.0.3 Beta").
    std::string version = !latest.empty() && (latest[0] == 'v' || latest[0] == 'V') ? latest.substr(1) : latest;
    const std::size_t dash = version.find('-');
    if (dash != std::string::npos && dash + 1 < version.size() &&
        !std::isdigit(static_cast<unsigned char>(version[dash + 1]))) {
        version[dash] = ' ';
        version[dash + 1] = static_cast<char>(std::toupper(static_cast<unsigned char>(version[dash + 1])));
    }
    text.replace(open + 9, close - open - 9, version);
    AddAhbAccess(text);
    std::string error;
    if (!write_file(meta, std::vector<std::uint8_t>(text.begin(), text.end()), error)) logf("Update: meta.xml: %s\n", error.c_str());
}

// `channel` is read by the caller: the settings belong to the menu thread.
bool CheckChannel(const std::string& channel, bool force, std::string& latest, bool& newer, std::string& error) {
    newer = false;
    UpdateNote note = ReadUpdateNote();
    // The last answer stands in only for the same channel.
    if (note.channel != channel) note = UpdateNote{};
    latest = note.latest;
    // Asked at every start: releases can come hours apart, and a day-old
    // answer hid them until the next day (testers had to look in Settings).
    std::vector<std::uint8_t> body;
    std::string tag;
    bool asked = HttpGet(channel == "stable" ? kLatestApi : kReleasesApi, body, error, 512u << 10) &&
                 (channel == "stable" ? release_tag_from_json(std::string(body.begin(), body.end()), tag)
                                      : newest_release_tag(std::string(body.begin(), body.end()), tag));
    // Beta: that release's own answer, for its riftwii.dol (the list's
    // first riftwii.dol is another release's).
    if (asked && channel != "stable") {
        std::vector<std::uint8_t>().swap(body);
        asked = HttpGet(kReleaseByTagApi + tag, body, error, 256u << 10);
    }
    if (!asked && channel == "stable" && error.find("answered 404") != std::string::npos) {
        // GitHub has no latest release while every release is a
        // pre-release: nothing to offer on Stable yet.
        logf("Update check (stable channel): no stable release yet, this is %s\n", RIFTWII_VERSION);
        error.clear();
        latest.clear();
        return true;
    }
    if (!asked) {
        if (error.empty()) error = "GitHub's answer names no release";
        error += channel == "stable" ? " (Stable channel)" : " (Beta channel)";
        if (force || latest.empty() || note.dol.url.empty()) return false;
        logf("Update check: %s; using the last answer (%s)\n", error.c_str(), latest.c_str());
    } else {
        const std::string json(body.begin(), body.end());
        latest = tag;
        note.channel = channel;
        note.checked = std::time(nullptr);
        note.latest = tag;
        note.dol = ReleaseAsset{};
        release_asset_from_json(json, "riftwii.dol", note.dol);
        WriteUpdateNote(note);
        logf("Update check (%s channel): newest release %s, this is %s%s\n", channel.c_str(), latest.c_str(), RIFTWII_VERSION,
             note.dol.url.empty() ? " (it has no riftwii.dol)" : "");
    }
    newer = compare_versions(latest, RIFTWII_VERSION) > 0;
    return true;
}

struct StartCheck {
    std::string channel;
    bool ok = false;
    std::string latest, error;
    bool newer = false;
    bool taken = true;
};
StartCheck g_start_check;

// The themes this release comes with and the rest of its sd:/apps (the
// channel installer), for a RiftWii the in-app update installed (a zip
// brings its own): riftwii-update.pack, fetched with the start's check,
// on its thread; each part written on Home's (ApplyPack), once per
// version.
std::vector<std::uint8_t> g_update;  // the whole pack, until both parts are written
struct PackKind {
    const char* header;
    const char* done;  // holds the version the card has
    const char* label;  // for the log
    std::size_t from = 0, to = 0;  // its part of g_update (empty: nothing to write)
    bool none = false;  // the release has no pack: nothing to wait for
};
PackKind g_themes{kThemePackHeader, kThemesDone, "Themes", 0, 0, false};
PackKind g_apps{kAppsPackHeader, kAppsDone, "Apps", 0, 0, false};

std::string FirstLine(const char* path) {
    std::string text;
    if (FILE* f = std::fopen(path, "rb")) {
        char line[128];
        if (std::fgets(line, sizeof(line), f)) text = line;
        std::fclose(f);
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    return text;
}

void FetchPacks() {
    std::vector<std::uint8_t>().swap(g_update);
    for (PackKind* k : {&g_themes, &g_apps}) {
        k->from = k->to = 0;
        k->none = false;
    }
    const UpdateNote note = ReadUpdateNote();
    if (note.installed.empty() || compare_versions(note.installed, RIFTWII_VERSION) != 0) return;
    const bool themes = FirstLine(kThemesDone) != RIFTWII_VERSION;
    const bool apps = FirstLine(kAppsDone) != RIFTWII_VERSION;
    if (!themes && !apps) return;
    std::string json;
    {
        std::vector<std::uint8_t> body;
        std::string error;
        if (!HttpGet(kReleaseByTagApi + note.installed, body, error, 512u << 10)) {
            logf("Packs: cannot ask GitHub about %s: %s\n", note.installed.c_str(), error.c_str());
            return;
        }
        json.assign(body.begin(), body.end());
    }
    ReleaseAsset pack;
    if (!release_asset_from_json(json, kUpdatePackAsset, pack) || pack.url.empty()) {
        logf("Packs: %s has no %s\n", note.installed.c_str(), kUpdatePackAsset);
        g_themes.none = g_apps.none = true;
        return;
    }
    if (pack.sha256.empty()) {
        // As for the DOL: a download that cannot be checked is not used.
        logf("Packs: GitHub gives no SHA-256 for %s; not used\n", kUpdatePackAsset);
        g_themes.none = g_apps.none = true;
        return;
    }
    std::vector<std::uint8_t> body;
    std::string error;
    if (!HttpGet(pack.url, body, error, 16u << 20, 30000)) {
        logf("Packs: %s: %s\n", kUpdatePackAsset, error.c_str());
        return;
    }
    if ((pack.size != 0 && body.size() != pack.size) || Sha256Hex(body) != pack.sha256) {
        logf("Packs: %s did not arrive whole (%u bytes)\n", kUpdatePackAsset, static_cast<unsigned>(body.size()));
        return;
    }
    // The themes part ends where the apps part starts.
    ThemePack first;
    std::size_t split = 0;
    if (!parse_theme_pack(body.data(), body.size(), first, error, kThemePackHeader, &split)) {
        logf("Packs: %s: %s\n", kUpdatePackAsset, error.c_str());
        g_themes.none = g_apps.none = true;
        return;
    }
    g_update.swap(body);
    if (themes) {
        g_themes.from = 0;
        g_themes.to = split;
    }
    if (apps) {
        g_apps.from = split;
        g_apps.to = g_update.size();
    }
}

// Whether `path` holds exactly these bytes.
bool SameFile(const std::string& path, const std::uint8_t* data, std::size_t size) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || static_cast<std::size_t>(st.st_size) != size) return false;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::uint8_t chunk[4096];
    std::size_t at = 0;
    bool same = true;
    while (same && at < size) {
        const std::size_t want = std::min(sizeof(chunk), size - at);
        same = std::fread(chunk, 1, want, f) == want && std::memcmp(chunk, data + at, want) == 0;
        at += want;
    }
    std::fclose(f);
    return same;
}

void MarkDone(const char* path) {
    if (FILE* f = std::fopen(path, "wb")) {
        std::fprintf(f, "%s\n", RIFTWII_VERSION);
        std::fclose(f);
    }
}

// What the apps pack may write: the channel installer's folder and
// RiftWii's icon. RiftWii's boot.dol and meta.xml are the update's own,
// and a player's music.ogg stays.
bool AppsFileAllowed(const std::string& path) {
    return path.compare(0, 16, "riftwii_channel/") == 0 || path == "riftwii/icon.png";
}

// The card brought up to a pack: sd:/riftwii/themes (its themes' files
// written where they differ, a player's own themes are not touched,
// retired themes' files removed) or sd:/apps. On Home's thread, the GUI
// halted.
void ApplyPack(PackKind& k) {
    const bool themes = &k == &g_themes;
    if (k.none) {
        k.none = false;
        MarkDone(k.done);
    }
    if (k.to <= k.from || k.to > g_update.size()) return;
    const std::uint8_t* const bytes = g_update.data() + k.from;
    const std::size_t size = k.to - k.from;
    k.from = k.to = 0;
    ThemePack pack;
    std::string error;
    if (!parse_theme_pack(bytes, size, pack, error, k.header)) {
        logf("%s: %s: %s\n", k.label, kUpdatePackAsset, error.c_str());
        MarkDone(k.done);
        return;
    }
    const std::string root = themes ? "sd:/riftwii/themes/" : "sd:/apps/";
    mkdir("sd:/riftwii", 0777);
    mkdir(themes ? "sd:/riftwii/themes" : "sd:/apps", 0777);
    unsigned written = 0, same = 0, failed = 0;
    for (const ThemePackFile& f : pack.files) {
        if (!themes && !AppsFileAllowed(f.path)) {
            logf("%s: %s skipped\n", k.label, f.path.c_str());
            continue;
        }
        const std::string path = root + f.path;
        mkdir((root + f.path.substr(0, f.path.find('/'))).c_str(), 0777);
        if (SameFile(path, bytes + f.offset, f.size)) {
            ++same;
            continue;
        }
        const std::vector<std::uint8_t> data(bytes + f.offset, bytes + f.offset + f.size);
        if (write_file(path, data, error)) {
            ++written;
        } else {
            ++failed;
            logf("%s: %s\n", k.label, error.c_str());
        }
    }
    for (const RetiredTheme& r : themes ? pack.retired : std::vector<RetiredTheme>()) {
        const std::string dir = root + r.name;
        unsigned removed = 0;
        for (const std::string& name : r.files) removed += std::remove((dir + "/" + name).c_str()) == 0;
        rmdir(dir.c_str());  // only when nothing of the player's is left in it
        if (removed) logf("Themes: %s retired (%u file(s) removed)\n", r.name.c_str(), removed);
        if (Settings().theme == r.name) {
            struct stat st;
            const bool there = !r.replacement.empty() && stat((root + r.replacement).c_str(), &st) == 0;
            Settings().theme = there ? r.replacement : "default";
            SaveSettings();
            logf("Themes: the theme in use is now %s\n", Settings().theme.c_str());
        }
    }
    logf("%s: %u file(s) brought up to %s's, %u already were%s\n", k.label, written, RIFTWII_VERSION, same,
         failed ? " (some could not be written; tried again next start)" : "");
    if (!failed) MarkDone(k.done);
}

void RunStartCheck() {
    StartCheck& c = g_start_check;
    c.ok = CheckChannel(c.channel, false, c.latest, c.newer, c.error);
    FetchPacks();
}

}  // namespace

bool CheckForUpdate(bool force, std::string& latest, bool& newer, std::string& error) {
    // This answer replaces the start's, which is not asked about again.
    NetWaitForBackground();
    g_start_check.taken = true;
    return CheckChannel(effective_update_channel(Settings().update_channel, RIFTWII_VERSION), force, latest, newer,
                        error);
}

void StartUpdateCheck() {
    if (NetBackgroundBusy()) return;
    g_start_check = StartCheck{};
    g_start_check.channel = effective_update_channel(Settings().update_channel, RIFTWII_VERSION);
    g_start_check.taken = false;
    NetRunInBackground(RunStartCheck);
}

bool TakeUpdateCheck(bool& ok, std::string& latest, bool& newer, std::string& error) {
    if (g_start_check.taken || NetBackgroundBusy()) return false;
    NetWaitForBackground();
    g_start_check.taken = true;
    ApplyPack(g_themes);
    ApplyPack(g_apps);
    std::vector<std::uint8_t>().swap(g_update);
    ok = g_start_check.ok;
    latest = g_start_check.latest;
    newer = g_start_check.newer;
    error = g_start_check.error;
    return true;
}

bool AppsPackPending() {
    const UpdateNote note = ReadUpdateNote();
    return Settings().online && !note.installed.empty() &&
           compare_versions(note.installed, RIFTWII_VERSION) == 0 && FirstLine(kAppsDone) != RIFTWII_VERSION;
}

namespace {
std::string g_declined;
}  // namespace

void NoteUpdateDeclined(const std::string& latest) {
    g_declined = latest;
    logf("Update: the player chose \"Not now\" twice for %s (asked again, then declined on purpose); "
         "staying on %s\n", latest.c_str(), RIFTWII_VERSION);
}

void LogDeclinedUpdate() {
    if (g_declined.empty()) return;
    logf("Update: %s is out, but the player chose \"Not now\" twice at start; this is still %s\n",
         g_declined.c_str(), RIFTWII_VERSION);
}

bool UpdateInstalled(const std::string& latest) {
    const UpdateNote note = ReadUpdateNote();
    return !note.installed.empty() && note.installed == latest;
}

namespace {
volatile int g_card_writes = 0;
}  // namespace

CardWriteHold::CardWriteHold() { ++g_card_writes; }
CardWriteHold::~CardWriteHold() { --g_card_writes; }
bool CardWritesBusy() { return g_card_writes > 0; }

bool InstallUpdate(const std::string& latest, std::string& where, std::string& error,
                   const std::function<void(double done)>& progress) {
    UpdateNote note = ReadUpdateNote();
    if (note.latest != latest || note.dol.url.empty()) {
        error = "release " + latest + " has no riftwii.dol attached";
        return false;
    }
    where = RunningDolPath();
    if (where.empty()) {
        error = "RiftWii was not started from a boot.dol on the SD card, so there is nothing to replace";
        return false;
    }
    logf("Update: downloading %s for %s\n", latest.c_str(), where.c_str());
    std::vector<std::uint8_t> body;
    // GitHub's redirect answers are small; the DOL is the bulk. Its size
    // is known from the release, so the share is the bytes against it
    // (headers make it a hair early, capped below 1 until it is whole).
    HttpProgress got;
    if (progress && note.dol.size != 0) {
        const double total = static_cast<double>(note.dol.size);
        got = [&progress, total](std::size_t received) {
            progress(std::min(0.99, static_cast<double>(received) / total));
        };
    }
    if (!HttpGet(note.dol.url, body, error, 16u << 20, 30000, got)) return false;
    if (progress) progress(1.0);
    if (note.dol.size != 0 && body.size() != note.dol.size) {
        error = "the download has " + std::to_string(body.size()) + " bytes, GitHub says " +
                std::to_string(note.dol.size);
        return false;
    }
    // GitHub gives every asset a SHA-256; without one the download cannot
    // be told from a damaged or swapped one, so it is not installed.
    if (note.dol.sha256.empty()) {
        error = "GitHub gives no SHA-256 for this release's riftwii.dol, so it cannot be checked. Download it from " +
                std::string(kReleasesPage) + " instead";
        return false;
    }
    if (Sha256Hex(body) != note.dol.sha256) {
        error = "the download does not match GitHub's SHA-256";
        return false;
    }
    DolHeader header;
    if (!parse_dol_header(body.data(), body.size(), header, error)) {
        error = "the download is not a DOL: " + error;
        return false;
    }
    if (header.image_size() > body.size()) {
        error = "the download is shorter than its DOL header says";
        return false;
    }
    // New file first, then the swap; the old one stays as boot.dol.old.
    // Nothing may stop it half way (the power button waits for it).
    const CardWriteHold hold;
    const std::string fresh = where + ".new";
    const std::string old = where + ".old";
    if (!write_file(fresh, body, error)) return false;
    struct stat st;
    if (stat(fresh.c_str(), &st) != 0 || static_cast<std::size_t>(st.st_size) != body.size()) {
        std::remove(fresh.c_str());
        error = "the new DOL did not land whole on the card";
        return false;
    }
    std::remove(old.c_str());
    if (std::rename(where.c_str(), old.c_str()) != 0) {
        std::remove(fresh.c_str());
        error = "cannot move the old " + where + " aside";
        return false;
    }
    if (std::rename(fresh.c_str(), where.c_str()) != 0) {
        std::rename(old.c_str(), where.c_str());
        error = "cannot put the new DOL in place";
        return false;
    }
    UpdateMetaVersion(where, latest);
    note.installed = latest;
    WriteUpdateNote(note);
    logf("Update: %s installed in %s (%u bytes, SHA-256 checked)\n", latest.c_str(), where.c_str(),
         static_cast<unsigned>(body.size()));
    return true;
}

std::string CheatPath(const std::string& game_id) { return std::string(kCheatDir) + "/" + game_id + ".txt"; }

namespace {

std::string read_cheat_file(const std::string& game_id) {
    std::string text;
    if (FILE* f = std::fopen(CheatPath(game_id).c_str(), "rb")) {
        char buf[4096];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
        std::fclose(f);
    }
    return text;
}

}  // namespace

std::vector<CheatField> CheatFields(const std::string& game_id, const std::string& name) {
    return cheat_fields(read_cheat_file(game_id), name);
}

bool FillCheatValues(const std::string& game_id, const std::string& name, const std::vector<CheatField>& fields,
                     std::string& error) {
    std::string text = read_cheat_file(game_id);
    if (text.empty()) {
        error = "cannot read " + CheatPath(game_id);
        return false;
    }
    if (!fill_cheat_values(text, name, fields, error)) return false;
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    if (!write_file(CheatPath(game_id), bytes, error)) return false;
    std::string values;
    for (const CheatField& f : fields) values += std::string(values.empty() ? "" : ", ") + f.letter + "=" + f.value;
    logf("Cheats: values for \"%s\" (%s): %s\n", name.c_str(), game_id.c_str(), values.c_str());
    return true;
}

bool DownloadCheats(const std::string& game_id, std::string& error) {
    std::vector<std::uint8_t> body;
    if (!HttpGet(kCheatsUrl + url_encode(game_id), body, error, 1u << 20)) {
        // The archive answers 404 for a game it has no cheats for.
        if (error.size() >= 12 && error.compare(error.size() - 12, 12, "answered 404") == 0)
            error = "the cheat archive has no cheats for " + game_id;
        return false;
    }
    CheatFile file;
    std::string why;
    if (!parse_cheat_text(std::string(body.begin(), body.end()), file, why)) {
        error = "the cheat archive has no cheats for " + game_id;
        return false;
    }
    mkdir("sd:/riftwii", 0777);
    mkdir(kCheatDir, 0777);
    // Cheats added by hand and values filled in survive the download (a
    // tester lost theirs to one).
    std::string old = read_cheat_file(game_id);
    std::size_t kept = 0;
    if (!old.empty()) {
        const std::string merged = merge_cheat_text(std::string(body.begin(), body.end()), old, kept);
        body.assign(merged.begin(), merged.end());
    }
    if (!write_file(CheatPath(game_id), body, error)) return false;
    logf("Cheats: %u for %s from the GeckoCodes archive, %u of the player's own kept\n",
         static_cast<unsigned>(file.cheats.size()), game_id.c_str(), static_cast<unsigned>(kept));
    return true;
}

void EnsureMetaAhbAccess() {
    const std::string dol = RunningDolPath();
    if (dol.empty()) return;
    const std::string meta = dol.substr(0, dol.rfind('/') + 1) + "meta.xml";
    FILE* f = std::fopen(meta.c_str(), "rb");
    if (!f) return;
    std::string text;
    char buf[1024];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);
    bool changed = AddAhbAccess(text);
    if (changed) logf("meta.xml: hardware access asked of the Homebrew Channel from the next start (%s)\n", meta.c_str());
    // The Homebrew Channel shows <version>: this build's (an older RiftWii's
    // update wrote "2610 123" for 2610-123).
    const std::size_t open = text.find("<version>"), close = text.find("</version>");
    if (open != std::string::npos && close != std::string::npos && close > open &&
        text.compare(open + 9, close - open - 9, RIFTWII_VERSION) != 0) {
        text.replace(open + 9, close - open - 9, RIFTWII_VERSION);
        changed = true;
    }
    if (!changed) return;
    std::string error;
    if (!write_file(meta, std::vector<std::uint8_t>(text.begin(), text.end()), error)) logf("meta.xml: %s\n", error.c_str());
}

}  // namespace riftwii::wii
