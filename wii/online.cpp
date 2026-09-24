// SPDX-License-Identifier: GPL-3.0-or-later
#include "online.hpp"

#include <network.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <ctime>

#include "log.hpp"
#include "netsock.hpp"
#include "riftwii/cheats.hpp"
#include "riftwii/http.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kTitlesUrl = "http://www.gametdb.com/wiitdb.txt?LANG=";
constexpr const char* kCheatsUrl = "http://codes.rc24.xyz/txt.php?txt=";
constexpr std::time_t kWeek = 7 * 24 * 60 * 60;

bool get_once(const HttpUrl& url, HttpResponse& response, std::string& error, std::size_t max_bytes, int timeout_ms) {
    NetServer server;
    if (!ResolveServer(url.host, url.port, server, error)) return false;
    SocketTransport socket;
    if (!socket.connect(server, timeout_ms, error)) return false;
    const std::string request = http_get_request(url);
    if (!socket.send(request.data(), request.size())) {
        error = "cannot send the request to " + url.host;
        return false;
    }
    std::vector<std::uint8_t> raw;
    std::uint8_t chunk[4096];
    // Read until the response is whole or the server closes.
    while (!http_response_complete(raw)) {
        std::size_t got = 0;
        if (!socket.receive_some(chunk, sizeof(chunk), got)) {
            error = "no answer from " + url.host;
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
