// SPDX-License-Identifier: GPL-3.0-or-later
#include "online.hpp"

#include <network.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "log.hpp"
#include "netsock.hpp"
#include "tls.hpp"
#include "riftwii/cheats.hpp"
#include "riftwii/http.hpp"
#include "riftwii/update.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kTitlesUrl = "http://www.gametdb.com/wiitdb.txt?LANG=";
constexpr const char* kCheatsUrl = "http://codes.rc24.xyz/txt.php?txt=";
constexpr std::time_t kWeek = 7 * 24 * 60 * 60;
constexpr std::time_t kDay = 24 * 60 * 60;
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

bool CheckForUpdate(bool force, std::string& latest, bool& newer, std::string& error) {
    newer = false;
    latest.clear();
    // "checked <seconds>" and "latest <tag>" from the last time.
    std::time_t checked = 0;
    if (FILE* f = std::fopen(kUpdateNote, "rb")) {
        char line[128];
        while (std::fgets(line, sizeof(line), f)) {
            std::string text(line);
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
            if (text.rfind("checked ", 0) == 0) checked = static_cast<std::time_t>(std::strtoll(text.c_str() + 8, nullptr, 10));
            if (text.rfind("latest ", 0) == 0) latest = text.substr(7);
        }
        std::fclose(f);
    }
    const std::time_t now = std::time(nullptr);
    if (force || latest.empty() || now < checked || now - checked >= kDay) {
        std::vector<std::uint8_t> body;
        if (!HttpGet(kReleasesApi, body, error, 256u << 10)) return false;
        std::string tag;
        if (!release_tag_from_json(std::string(body.begin(), body.end()), tag)) {
            error = "GitHub's answer names no release";
            return false;
        }
        latest = tag;
        if (FILE* f = std::fopen(kUpdateNote, "wb")) {
            std::fprintf(f, "checked %lld\nlatest %s\n", static_cast<long long>(now), latest.c_str());
            std::fclose(f);
        }
        logf("Update check: newest release %s, this is %s\n", latest.c_str(), RIFTWII_VERSION);
    }
    newer = compare_versions(latest, RIFTWII_VERSION) > 0;
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
