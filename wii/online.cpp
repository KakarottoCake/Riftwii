// SPDX-License-Identifier: GPL-3.0-or-later
#include "online.hpp"

#include <network.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include <gccore.h>

#include "bearssl.h"
#include "log.hpp"
#include "netsock.hpp"
#include "tls.hpp"
#include "riftwii/cheats.hpp"
#include "riftwii/dol.hpp"
#include "riftwii/http.hpp"
#include "riftwii/update.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kTitlesUrl = "http://www.gametdb.com/wiitdb.txt?LANG=";
constexpr const char* kCheatsUrl = "http://codes.rc24.xyz/txt.php?txt=";
constexpr std::time_t kWeek = 7 * 24 * 60 * 60;
constexpr const char* kReleasesApi = "https://api.github.com/repos/KakarottoCake/Riftwii/releases?per_page=1";
constexpr const char* kUpdateNote = "sd:/riftwii/update.txt";

bool get_once(const HttpUrl& url, HttpResponse& response, std::string& error, std::size_t max_bytes, int timeout_ms) {
    NetServer server;
    if (!ResolveServer(url.host, url.port, server, error)) return false;
    SocketTransport socket;
    if (!socket.connect(server, timeout_ms, error)) return false;
    TlsStream tls;
    if (url.tls && !tls.open(socket, url.host, error)) return false;
    const std::string request = http_get_request(url);
    const bool sent = url.tls ? tls.send(request.data(), request.size()) : socket.send(request.data(), request.size());
    if (!sent) {
        error = "cannot send the request to " + url.host;
        return false;
    }
    std::vector<std::uint8_t> raw;
    std::uint8_t chunk[4096];
    // Read until the response is whole or the server closes.
    while (!http_response_complete(raw)) {
        std::size_t got = 0;
        const bool ok = url.tls ? tls.receive_some(chunk, sizeof(chunk), got) : socket.receive_some(chunk, sizeof(chunk), got);
        if (!ok) {
            error = "no answer from " + url.host;
            if (url.tls && tls.last_error() != 0) error += " (TLS error " + std::to_string(tls.last_error()) + ")";
            return false;
        }
        if (got == 0) break;  // closed
        raw.insert(raw.end(), chunk, chunk + got);
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
             int timeout_ms) {
    if (!NetStart(error)) return false;
    std::string where = url;
    for (int hop = 0; hop < 4; ++hop) {
        HttpUrl parsed;
        if (!parse_http_url(where, parsed, error)) return false;
        HttpResponse response;
        if (!get_once(parsed, response, error, max_bytes, timeout_ms)) return false;
        if (response.status >= 300 && response.status < 400 && response.headers.count("location")) {
            where = response.headers["location"];
            continue;
        }
        if (response.status != 200) {
            error = parsed.host + " answered " + std::to_string(response.status);
            return false;
        }
        body = std::move(response.body);
        return true;
    }
    error = "too many redirects for " + url;
    return false;
}

std::string TitlesPath(const std::string& lang) { return "sd:/riftwii/titles-" + lang + ".txt"; }

bool UpdateTitles(const std::string& lang, bool force, std::string& error) {
    const std::string path = TitlesPath(lang);
    struct stat st;
    if (!force && stat(path.c_str(), &st) == 0 && st.st_size > 1024 && std::time(nullptr) - st.st_mtime < kWeek) {
        return true;
    }
    std::vector<std::uint8_t> body;
    const std::string gametdb_lang = lang == "ja" ? "JA" : lang == "es" ? "ES" : lang == "pt" ? "PT" : lang == "it" ? "IT" : "EN";
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
        std::fprintf(f, "checked %lld\nlatest %s\n", static_cast<long long>(note.checked), note.latest.c_str());
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
    // The releases' own spelling: "v2.0.3-beta" becomes "2.0.3 Beta".
    std::string version = !latest.empty() && (latest[0] == 'v' || latest[0] == 'V') ? latest.substr(1) : latest;
    const std::size_t dash = version.find('-');
    if (dash != std::string::npos && dash + 1 < version.size()) {
        version[dash] = ' ';
        version[dash + 1] = static_cast<char>(std::toupper(static_cast<unsigned char>(version[dash + 1])));
    }
    text.replace(open + 9, close - open - 9, version);
    std::string error;
    if (!write_file(meta, std::vector<std::uint8_t>(text.begin(), text.end()), error)) logf("Update: meta.xml: %s\n", error.c_str());
}

}  // namespace

bool CheckForUpdate(bool force, std::string& latest, bool& newer, std::string& error) {
    newer = false;
    UpdateNote note = ReadUpdateNote();
    latest = note.latest;
    // Asked at every start: releases can come hours apart, and a day-old
    // answer hid them until the next day (testers had to look in Settings).
    std::vector<std::uint8_t> body;
    std::string tag;
    const bool asked = HttpGet(kReleasesApi, body, error, 256u << 10) &&
                       release_tag_from_json(std::string(body.begin(), body.end()), tag);
    if (!asked) {
        if (error.empty()) error = "GitHub's answer names no release";
        if (force || latest.empty() || note.dol.url.empty()) return false;
        logf("Update check: %s; using the last answer (%s)\n", error.c_str(), latest.c_str());
    } else {
        const std::string json(body.begin(), body.end());
        latest = tag;
        note.checked = std::time(nullptr);
        note.latest = tag;
        note.dol = ReleaseAsset{};
        release_asset_from_json(json, "riftwii.dol", note.dol);
        WriteUpdateNote(note);
        logf("Update check: newest release %s, this is %s%s\n", latest.c_str(), RIFTWII_VERSION,
             note.dol.url.empty() ? " (it has no riftwii.dol)" : "");
    }
    newer = compare_versions(latest, RIFTWII_VERSION) > 0;
    return true;
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

bool InstallUpdate(const std::string& latest, std::string& where, std::string& error) {
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
    if (!HttpGet(note.dol.url, body, error, 16u << 20, 30000)) return false;
    if (note.dol.size != 0 && body.size() != note.dol.size) {
        error = "the download has " + std::to_string(body.size()) + " bytes, GitHub says " +
                std::to_string(note.dol.size);
        return false;
    }
    if (!note.dol.sha256.empty() && Sha256Hex(body) != note.dol.sha256) {
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
    logf("Update: %s installed in %s (%u bytes%s)\n", latest.c_str(), where.c_str(),
         static_cast<unsigned>(body.size()), note.dol.sha256.empty() ? "" : ", SHA-256 checked");
    return true;
}

std::string CheatPath(const std::string& game_id) { return std::string(kCheatDir) + "/" + game_id + ".txt"; }

bool DownloadCheats(const std::string& game_id, std::string& error) {
    std::vector<std::uint8_t> body;
    if (!HttpGet(kCheatsUrl + url_encode(game_id), body, error, 1u << 20)) return false;
    CheatFile file;
    std::string why;
    if (!parse_cheat_text(std::string(body.begin(), body.end()), file, why)) {
        error = "the cheat archive has no cheats for " + game_id;
        return false;
    }
    mkdir("sd:/riftwii", 0777);
    mkdir(kCheatDir, 0777);
    if (!write_file(CheatPath(game_id), body, error)) return false;
    logf("Cheats: %u for %s from the GeckoCodes archive\n", static_cast<unsigned>(file.cheats.size()), game_id.c_str());
    return true;
}

}  // namespace riftwii::wii
