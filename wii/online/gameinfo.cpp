// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gameinfo.hpp"

#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

#include "loadersettings.hpp"
#include "log.hpp"
#include "netsock.hpp"
#include "online.hpp"
#include "riftwii/textlines.hpp"
#include "riftwii/zipstream.hpp"

namespace riftwii::wii {
namespace {

// FALLBACK: English where a text has no translation.
constexpr const char* kWiitdbUrl = "http://www.gametdb.com/wiitdb.zip?FALLBACK=TRUE&LANG=";
// In one language the database is about 2 MB packed and 10 MB of XML, all
// of them 10 MB and 41 MB (2026); room to grow, and a stop for anything
// that unpacks without end.
constexpr std::size_t kMaxZip = 24u << 20;
constexpr std::uint64_t kMaxXml = 160ull << 20;

// Other loaders keep the same database here; read when RiftWii has none.
const char* const kOtherDatabases[] = {
    "sd:/apps/usbloader_gx/wiitdb.xml", "sd:/wiitdb.xml", "sd:/config/wiitdb.xml", "sd:/usb-loader/wiitdb.xml",
};

std::string CachePath(const std::string& lang) { return "sd:/riftwii/gameinfo-" + lang + ".txt"; }
std::string ZipPath(const std::string& lang) { return "sd:/riftwii/wiitdb-" + lang + ".zip"; }
std::string MenuLocale() { return gametdb_locale(MenuLanguage()); }

bool Stat(const char* path, struct stat& st) { return stat(path, &st) == 0 && st.st_size > 0; }

// The cache as read into memory once a session (per menu language).
struct Cache {
    bool loaded = false;
    std::string lang;
    std::map<std::string, GameInfo> games;
    std::set<std::string> not_listed;  // looked for, and not in the database
    std::time_t written = 0;           // the cache file's time
    // Read from the database this session: not again because the
    // database's time is ahead of the cache's (a clock set wrong).
    bool read_now = false;
};
Cache g_cache;

constexpr const char* kNotListedLine = "# not listed: ";

void LoadCache(const std::string& lang) {
    if (g_cache.loaded && g_cache.lang == lang) return;
    g_cache = Cache();
    g_cache.loaded = true;
    g_cache.lang = lang;
    const std::string path = CachePath(lang);
    struct stat st;
    if (!Stat(path.c_str(), st) || st.st_size > (4 << 20)) return;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return;
    std::string text(static_cast<std::size_t>(st.st_size), '\0');
    const std::size_t got = std::fread(&text[0], 1, text.size(), f);
    std::fclose(f);
    text.resize(got);
    parse_game_infos(text, g_cache.games);
    // A cache from before the games' <type> was kept says no game is
    // custom: read the database again (as if it were newer) once there is one.
    const bool has_types = game_infos_have_types(text);
    // The games looked for and not found, on one line of their own.
    const std::size_t at = text.find(kNotListedLine);
    if (at != std::string::npos) {
        const std::size_t start = at + std::char_traits<char>::length(kNotListedLine);
        const std::size_t end = text.find('\n', start);
        WordReader ids(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        std::string id;
        while (ids.next(id) && g_cache.not_listed.size() < 4096) g_cache.not_listed.insert(id);
    }
    g_cache.written = has_types ? st.st_mtime : 0;
}

bool SaveCache() {
    std::string text = serialize_game_infos(g_cache.games);
    if (!g_cache.not_listed.empty()) {
        text += kNotListedLine;
        for (const std::string& id : g_cache.not_listed) text += id + " ";
        text += "\n";
    }
    const std::string path = CachePath(g_cache.lang);
    const std::string temp = path + ".part";
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    std::remove(path.c_str());
    if (!ok || std::rename(temp.c_str(), path.c_str()) != 0) {
        std::remove(temp.c_str());
        return false;
    }
    g_cache.written = std::time(nullptr);
    return true;
}

std::uint32_t Le(const std::uint8_t* p, int bytes) {
    std::uint32_t v = 0;
    for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

using Progress = std::function<bool(double)>;

// Feeds a plain wiitdb.xml to the scanner a piece at a time.
bool ScanXml(const std::string& path, WiitdbScanner& scanner, std::string& error, const Progress& progress) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        error = "cannot open " + path;
        return false;
    }
    struct stat st;
    const double total = Stat(path.c_str(), st) ? static_cast<double>(st.st_size) : 1.0;
    std::vector<char> piece(64u << 10);
    std::uint64_t done = 0;
    std::size_t got;
    bool ok = true;
    while ((got = std::fread(piece.data(), 1, piece.size(), f)) > 0) {
        scanner.feed(piece.data(), got);
        done += got;
        // Cut short, the scan has not seen every game: said as a failure,
        // or each game after the cut would be cached as not listed (BUG-10).
        if (done > kMaxXml) {
            error = path + " is larger than a database would be";
            ok = false;
            break;
        }
        if (progress && !progress(static_cast<double>(done) / total)) {
            error = "stopped";
            ok = false;
            break;
        }
    }
    std::fclose(f);
    return ok;
}

// Keep scanner results private until the complete entry passes its length
// and CRC checks; FindGameInfo only updates the cache after success.
bool ScanZip(const std::string& path, WiitdbScanner& scanner, std::string& error, const Progress& progress) {
    const ZipSink feed = [&](const std::uint8_t* bytes, std::size_t count) {
        scanner.feed(reinterpret_cast<const char*>(bytes), count);
        return true;
    };
    return zip_stream_first(path, kMaxZip, kMaxXml, feed, error, progress);
}

}  // namespace

bool GameInfoSource(std::string& path, std::time_t& written) {
    struct stat st;
    const std::string own = ZipPath(MenuLocale());
    if (Stat(own.c_str(), st)) {
        path = own;
        written = st.st_mtime;
        return true;
    }
    for (const char* other : kOtherDatabases) {
        if (!Stat(other, st)) continue;
        path = other;
        written = st.st_mtime;
        return true;
    }
    return false;
}

GameInfoResult FindGameInfo(const std::string& id, const std::set<std::string>& wanted, GameInfo& out,
                            std::string& error, const std::function<bool(double done)>& progress) {
    const std::string lang = MenuLocale();
    LoadCache(lang);
    std::string source;
    std::time_t source_time = 0;
    const bool have_source = GameInfoSource(source, source_time);
    const bool stale = have_source && !g_cache.read_now && source_time > g_cache.written;
    if (!stale) {
        const auto found = g_cache.games.find(id);
        if (found != g_cache.games.end()) {
            out = found->second;
            return GameInfoResult::Found;
        }
        if (g_cache.not_listed.count(id)) return GameInfoResult::NotListed;
    }
    if (!have_source) return GameInfoResult::NoDatabase;

    // Read for this game, the others on the drives and those already kept.
    std::set<std::string> ids = wanted;
    ids.insert(id);
    for (const auto& kept : g_cache.games) ids.insert(kept.first);
    ids.insert(g_cache.not_listed.begin(), g_cache.not_listed.end());
    WiitdbScanner scanner(ids, lang);
    const bool zip = source.size() > 4 && source.compare(source.size() - 4, 4, ".zip") == 0;
    logf("Game details: reading %s for %u game(s)\n", source.c_str(), static_cast<unsigned>(ids.size()));
    const bool read = zip ? ScanZip(source, scanner, error, progress) : ScanXml(source, scanner, error, progress);
    if (!read) {
        logf("Game details: %s not read: %s\n", source.c_str(), error.c_str());
        return GameInfoResult::Failed;
    }
    g_cache.games = scanner.games();
    g_cache.read_now = true;
    g_cache.not_listed.clear();
    for (const std::string& wanted_id : ids)
        if (!g_cache.games.count(wanted_id)) g_cache.not_listed.insert(wanted_id);
    logf("Game details: %u game(s) in the database, %u found, %u not listed (%u skipped)\n",
         static_cast<unsigned>(scanner.blocks()), static_cast<unsigned>(g_cache.games.size()),
         static_cast<unsigned>(g_cache.not_listed.size()), static_cast<unsigned>(scanner.skipped()));
    if (!SaveCache()) logf("Game details: %s could not be written\n", CachePath(lang).c_str());
    const auto found = g_cache.games.find(id);
    if (found == g_cache.games.end()) return GameInfoResult::NotListed;
    out = found->second;
    return GameInfoResult::Found;
}

bool PeekGameInfo(const std::string& id, GameInfo& out) {
    LoadCache(MenuLocale());
    const auto found = g_cache.games.find(id);
    if (found == g_cache.games.end()) return false;
    out = found->second;
    return true;
}

namespace {

// Downloads the database in `lang` to the card; the cache is left alone,
// so this may run on the network's background thread.
bool DownloadDatabase(const std::string& lang, std::string& error,
                      const std::function<void(std::size_t received)>& received) {
    std::vector<std::uint8_t> body;
    HttpProgress progress = nullptr;
    if (received) progress = [&](std::size_t bytes) { received(bytes); };
    if (!HttpGet(kWiitdbUrl + lang, body, error, kMaxZip, 30000, progress)) {
        logf("Game details: download failed: %s\n", error.c_str());
        return false;
    }
    if (body.size() < (256u << 10) || Le(body.data(), 4) != 0x04034B50u) {
        error = "GameTDB sent something that is not its database";
        logf("Game details: %s (%u bytes)\n", error.c_str(), static_cast<unsigned>(body.size()));
        return false;
    }
    mkdir("sd:/riftwii", 0777);
    const std::string path = ZipPath(lang);
    const std::string temp = path + ".part";
    FILE* f = std::fopen(temp.c_str(), "wb");
    bool ok = f && std::fwrite(body.data(), 1, body.size(), f) == body.size();
    if (f) std::fclose(f);
    std::remove(path.c_str());
    ok = ok && std::rename(temp.c_str(), path.c_str()) == 0;
    if (!ok) {
        std::remove(temp.c_str());
        error = "could not write " + path + " (card full?)";
        logf("Game details: %s\n", error.c_str());
        return false;
    }
    logf("Game details: %u bytes of GameTDB's database saved to %s\n", static_cast<unsigned>(body.size()), path.c_str());
    return true;
}

// The read on the network's background thread (StartGameInfoRead): set up
// on the menu's thread, written by the job only while it runs, and taken
// on the menu's thread once it has ended.
struct BackgroundRead {
    bool started = false;
    std::string lang;
    std::string source;  // empty: download the database first
    std::set<std::string> ids;
    bool ok = false;
    std::string error;
    std::map<std::string, GameInfo> games;
};
BackgroundRead g_read;

void RunBackgroundRead() {
    if (g_read.source.empty()) {
        if (!DownloadDatabase(g_read.lang, g_read.error, nullptr)) return;
        g_read.source = ZipPath(g_read.lang);
    }
    WiitdbScanner scanner(g_read.ids, g_read.lang);
    const bool zip = g_read.source.size() > 4 && g_read.source.compare(g_read.source.size() - 4, 4, ".zip") == 0;
    const bool read = zip ? ScanZip(g_read.source, scanner, g_read.error, nullptr)
                          : ScanXml(g_read.source, scanner, g_read.error, nullptr);
    if (!read) return;
    g_read.games = scanner.games();
    g_read.ok = true;
}

}  // namespace

bool DownloadGameInfo(std::string& error, const std::function<void(std::size_t received)>& received) {
    if (!DownloadDatabase(MenuLocale(), error, received)) return false;
    // The next FindGameInfo reads the new database.
    g_cache = Cache();
    return true;
}

bool IsCustomGame(const std::string& id) {
    LoadCache(MenuLocale());
    const auto found = g_cache.games.find(id);
    return found != g_cache.games.end() && found->second.custom;
}

bool StartGameInfoRead(const std::set<std::string>& wanted, bool download) {
    if (g_read.started || NetBackgroundBusy()) return false;
    const std::string lang = MenuLocale();
    LoadCache(lang);
    std::string source;
    std::time_t source_time = 0;
    const bool have_source = GameInfoSource(source, source_time);
    if (!have_source && !download) return false;
    const bool stale = have_source && !g_cache.read_now && source_time > g_cache.written;
    // Every game on the drives already looked for: nothing to read.
    bool missing = !have_source;
    for (const std::string& id : wanted)
        if (!g_cache.games.count(id) && !g_cache.not_listed.count(id)) missing = true;
    if (!stale && !missing) return false;
    BackgroundRead read;
    read.lang = lang;
    read.source = have_source ? source : "";
    read.ids = wanted;
    for (const auto& kept : g_cache.games) read.ids.insert(kept.first);
    read.ids.insert(g_cache.not_listed.begin(), g_cache.not_listed.end());
    read.started = true;
    logf("Game details: reading %s for %u game(s) in the background\n",
         have_source ? source.c_str() : "GameTDB's database (downloading it first)",
         static_cast<unsigned>(read.ids.size()));
    g_read = std::move(read);
    if (NetRunInBackground(RunBackgroundRead, false, false)) return true;
    g_read = BackgroundRead();
    return false;
}

bool TakeGameInfoRead(bool& changed) {
    changed = false;
    if (!g_read.started || NetBackgroundBusy()) return false;
    NetWaitForBackground();
    BackgroundRead read = std::move(g_read);
    g_read = BackgroundRead();
    if (!read.ok) {
        logf("Game details: the background read failed: %s\n", read.error.c_str());
        return true;
    }
    // The menu's language changed meanwhile: the cache is another's now.
    if (read.lang != MenuLocale()) return true;
    LoadCache(read.lang);
    std::set<std::string> custom_before, custom_after;
    for (const auto& kept : g_cache.games)
        if (kept.second.custom) custom_before.insert(kept.first);
    g_cache.games = std::move(read.games);
    g_cache.read_now = true;
    g_cache.not_listed.clear();
    for (const std::string& id : read.ids)
        if (!g_cache.games.count(id)) g_cache.not_listed.insert(id);
    for (const auto& kept : g_cache.games)
        if (kept.second.custom) custom_after.insert(kept.first);
    logf("Game details: %u game(s) found, %u not listed, %u custom\n", static_cast<unsigned>(g_cache.games.size()),
         static_cast<unsigned>(g_cache.not_listed.size()), static_cast<unsigned>(custom_after.size()));
    if (!SaveCache()) logf("Game details: %s could not be written\n", CachePath(read.lang).c_str());
    changed = custom_before != custom_after;
    return true;
}

}  // namespace riftwii::wii
