// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gameinfo.hpp"

#include <sys/stat.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

#include "loadersettings.hpp"
#include "log.hpp"
#include "netsock.hpp"
#include "online.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/textlines.hpp"
#include "riftwii/zipstream.hpp"

namespace riftwii::wii {
namespace {

// FALLBACK: English where a text has no translation.
// Over https: a forged database would mark games custom (Home's Mods view).
// GameTDB's chain ends at ISRG Root X1, in tlsroots.c; its http address only
// redirects here.
constexpr const char* kWiitdbUrl = "https://www.gametdb.com/wiitdb.zip?FALLBACK=TRUE&LANG=";
// In one language the database is about 2 MB packed and 10 MB of XML, all
// of them 10 MB and 41 MB (2026); room to grow, and a stop for anything
// that unpacks without end.
constexpr std::size_t kMaxZip = 24u << 20;
constexpr std::uint64_t kMaxXml = 160ull << 20;

// Other loaders keep the same database here; read when RiftWii has none.
const char* const kOtherDatabases[] = {
    "sd:/apps/usbloader_gx/wiitdb.xml", "sd:/wiitdb.xml", "sd:/config/wiitdb.xml", "sd:/usb-loader/wiitdb.xml",
};

// Not per language: the file keeps every language's title of each app.
constexpr const char* kHomebrewTitlesPath = "sd:/riftwii/homebrew-titles.txt";
// GameTDB's mods of the player's games (riftwii/modcover.hpp), for the mod
// tiles' own covers; English titles, so not per language either.
constexpr const char* kModTitlesPath = "sd:/riftwii/mod-titles.txt";
unsigned g_mod_titles_written = 0;
// The database read for this card's list this session, written or not
// (cleared by ForgetGameInfo, a card swap): the list's state is then no
// reason to read the database again. A card that can't be written (full,
// locked) would otherwise have every Home visit read it again.
bool g_mod_titles_read_here = false;
// This card's list found readable this session (once, not on every visit).
bool g_mod_titles_checked_here = false;
// Read from the database this session already (and the file written), for
// the same reason as Cache::read_now; or stopped by the player.
bool g_homebrew_read_now = false;
// Reads that failed this session (a scan or the file's write): tried again
// when the view is opened, but not every time on a card that keeps failing.
unsigned g_homebrew_failed = 0;
constexpr unsigned kMaxHomebrewTries = 3;
unsigned g_homebrew_written = 0;
// The list file last found usable, by its size and time, so it is read
// to check once and not on every visit to the Homebrew view.
off_t g_homebrew_checked_size = -1;
std::time_t g_homebrew_checked_time = 0;

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

// `path` made `text`, through a ".part" file: the old file stays until the
// new one is whole.
bool ReplaceListFile(const char* path, const std::string& text) {
    const std::string temp = std::string(path) + ".part";
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = std::fclose(f) == 0 && ok;
    if (!ok) {
        std::remove(temp.c_str());
        return false;
    }
    // FAT can't rename over a file: the old list is moved aside first and
    // put back if the new one can't take its place, so a failed save never
    // leaves the view with no list at all.
    const std::string backup = std::string(path) + ".backup";
    struct stat st;
    const bool had_old = stat(path, &st) == 0;
    std::remove(backup.c_str());
    if (had_old && std::rename(path, backup.c_str()) != 0) {
        std::remove(temp.c_str());
        return false;
    }
    if (std::rename(temp.c_str(), path) != 0) {
        if (had_old) std::rename(backup.c_str(), path);
        std::remove(temp.c_str());
        return false;
    }
    if (had_old) std::remove(backup.c_str());
    return true;
}

// Written even with no mods found, as the homebrew list is: the file says
// the database was read for them.
bool SaveModTitles(const std::vector<CustomGame>& games) {
    g_mod_titles_read_here = true;
    std::size_t written = 0;
    std::string text = serialize_custom_games(games, &written);
    // A list cut short (the scan's bound, or the file's) could make a tile's
    // name look like it matches one entry when a dropped one matches too:
    // then no list at all, so no tile gets a mod's cover by a guess.
    const bool cut = games.size() >= kMaxCustomGames || written != games.size();
    if (cut) {
        logf("Mod covers: GameTDB lists more mods of your games than RiftWii keeps (%u); mod tiles keep their games' covers\n",
             static_cast<unsigned>(games.size()));
        text = serialize_custom_games({});
    }
    if (!ReplaceListFile(kModTitlesPath, text)) {
        // The older list stays, still usable; older than the database, it
        // is made again by the next StartGameInfoRead.
        logf("Mod covers: %s could not be written\n", kModTitlesPath);
        return false;
    }
    ++g_mod_titles_written;
    g_mod_titles_checked_here = true;
    if (!cut)
        logf("Mod covers: %u of GameTDB's mods of your games kept in %s\n", static_cast<unsigned>(games.size()),
             kModTitlesPath);
    return true;
}

// Written even with no apps (a database without homebrew): the file's
// time says the database was read, so it is not read again next session.
// The old file stays until the new one is whole.
bool SaveHomebrewTitles(const std::vector<HomebrewTitle>& titles) {
    std::string text;
    if (!serialize_homebrew_titles(titles, text)) {
        logf("Homebrew: no room in memory for the list of GameTDB's apps\n");
        return false;
    }
    // serialize_homebrew_titles keeps within the budget; checked again so
    // the file is never one LoadHomebrewTitles refuses.
    if (text.size() > kMaxHomebrewTitlesBytes) return false;
    if (!ReplaceListFile(kHomebrewTitlesPath, text)) return false;
    g_homebrew_read_now = true;
    ++g_homebrew_written;
    logf("Homebrew: %u of GameTDB's apps kept in %s, for their covers\n", static_cast<unsigned>(titles.size()),
         kHomebrewTitlesPath);
    return true;
}

std::uint32_t Le(const std::uint8_t* p, int bytes) {
    std::uint32_t v = 0;
    for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

using Progress = std::function<bool(double)>;

bool ScanComplete(const std::string& path, const WiitdbScanner& scanner, std::string& error) {
    if (scanner.complete()) return true;
    error = path + " ends early, before the database's closing tag";
    return false;
}

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
    // A read error ends the loop as the file's end does: without this
    // check a part of the database would pass for all of it.
    if (ok && std::ferror(f)) {
        error = "could not read all of " + path;
        ok = false;
    }
    std::fclose(f);
    return ok && ScanComplete(path, scanner, error);
}

// Keep scanner results private until the complete entry passes its length
// and CRC checks; FindGameInfo only updates the cache after success.
bool ScanZip(const std::string& path, WiitdbScanner& scanner, std::string& error, const Progress& progress) {
    const ZipSink feed = [&](const std::uint8_t* bytes, std::size_t count) {
        scanner.feed(reinterpret_cast<const char*>(bytes), count);
        return true;
    };
    return zip_stream_first(path, kMaxZip, kMaxXml, feed, error, progress) &&
           ScanComplete(path, scanner, error);
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
    // The same pass found the homebrew apps: kept for the Homebrew view.
    if (!SaveHomebrewTitles(scanner.homebrew())) logf("Homebrew: %s could not be written\n", kHomebrewTitlesPath);
    SaveModTitles(scanner.customs());
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

// Reading the list file: no room for it is the menu's, not the file's
// fault, so it is not a reason to make the list again; a file that can't
// be opened or comes back short (a card error, or the file changed under
// it) is.
enum class ListRead { Ok, NoRoom, Failed };

static ListRead ReadHomebrewTitlesFile(const struct stat& st, std::string& text) {
    if (static_cast<std::uint64_t>(st.st_size) > kMaxHomebrewTitlesBytes) return ListRead::Failed;
    // Its size comes from the card: a checked allocation.
    if (!try_resize(text, static_cast<std::size_t>(st.st_size))) return ListRead::NoRoom;
    FILE* f = std::fopen(kHomebrewTitlesPath, "rb");
    if (!f) return ListRead::Failed;
    const std::size_t got = std::fread(&text[0], 1, text.size(), f);
    std::fclose(f);
    return got == text.size() ? ListRead::Ok : ListRead::Failed;
}

bool LoadHomebrewTitles(std::vector<HomebrewTitle>& out) {
    out.clear();
    struct stat st;
    std::string text;
    if (!Stat(kHomebrewTitlesPath, st) || ReadHomebrewTitlesFile(st, text) != ListRead::Ok) return false;
    // The same test as HomebrewTitlesNeedReading's, so a list it would
    // make again (a bad line among good ones) is not used meanwhile.
    if (!homebrew_titles_usable(text)) return false;
    // A database without homebrew writes the empty list: read, with no apps in it.
    parse_homebrew_titles(text, out);
    return true;
}

unsigned HomebrewTitlesWritten() { return g_homebrew_written; }

bool LoadModTitles(std::vector<CustomGame>& out) {
    out.clear();
    struct stat st;
    if (stat(kModTitlesPath, &st) != 0 || static_cast<std::uint64_t>(st.st_size) > kMaxCustomGamesBytes) return false;
    std::string text;
    // Its size comes from the card: a checked allocation.
    if (!try_resize(text, static_cast<std::size_t>(st.st_size))) return false;
    FILE* f = std::fopen(kModTitlesPath, "rb");
    if (!f) return false;
    const std::size_t got = text.empty() ? 0 : std::fread(&text[0], 1, text.size(), f);
    std::fclose(f);
    if (got != text.size()) return false;
    out = parse_custom_games(text);
    return true;
}

unsigned ModTitlesWritten() { return g_mod_titles_written; }

bool HomebrewTitlesNeedReading() {
    if (g_homebrew_read_now || g_homebrew_failed >= kMaxHomebrewTries) return false;
    std::string source;
    std::time_t source_time = 0;
    if (!GameInfoSource(source, source_time)) return false;
    struct stat st;
    if (!Stat(kHomebrewTitlesPath, st)) return true;
    // A list too big to load (an older RiftWii's, or a damaged one) is
    // made again, however new it is.
    if (static_cast<std::uint64_t>(st.st_size) > kMaxHomebrewTitlesBytes) return true;
    if (source_time > st.st_mtime) return true;
    // So is one that is new enough but damaged: it would never load, and
    // its time would keep it from being made again.
    if (st.st_size == g_homebrew_checked_size && st.st_mtime == g_homebrew_checked_time) return false;
    std::string text;
    const ListRead read = ReadHomebrewTitlesFile(st, text);
    if (read == ListRead::NoRoom) return false;  // no room to check now: as before
    if (read == ListRead::Failed) {
        logf("Homebrew: %s could not be read in full; it is made again from the game database\n",
             kHomebrewTitlesPath);
        return true;
    }
    if (!homebrew_titles_usable(text)) {
        logf("Homebrew: %s is damaged; it is made again from the game database\n", kHomebrewTitlesPath);
        return true;
    }
    g_homebrew_checked_size = st.st_size;
    g_homebrew_checked_time = st.st_mtime;
    return false;
}

void ForgetHomebrewTitlesRead() {
    g_homebrew_read_now = false;
    g_homebrew_failed = 0;
    g_homebrew_checked_size = -1;
    g_homebrew_checked_time = 0;
}

bool ReadHomebrewTitles(std::string& error, const std::function<bool(double done)>& progress) {
    std::string source;
    std::time_t source_time = 0;
    if (!GameInfoSource(source, source_time)) {
        error = "there is no game database on the SD card";
        return false;
    }
    // No game wanted: only the homebrew apps the scanner keeps anyway.
    WiitdbScanner scanner({}, MenuLocale());
    const bool zip = source.size() > 4 && source.compare(source.size() - 4, 4, ".zip") == 0;
    logf("Homebrew: reading %s for GameTDB's homebrew apps\n", source.c_str());
    const bool read = zip ? ScanZip(source, scanner, error, progress) : ScanXml(source, scanner, error, progress);
    if (!read) {
        logf("Homebrew: %s not read: %s\n", source.c_str(), error.c_str());
        // Stopped by the player: asked again next session, not every time
        // the view is opened. Any other failure is tried again.
        if (error == "stopped")
            g_homebrew_read_now = true;
        else
            ++g_homebrew_failed;
        return false;
    }
    if (!SaveHomebrewTitles(scanner.homebrew())) {
        ++g_homebrew_failed;
        error = std::string("could not write ") + kHomebrewTitlesPath + " (card full?)";
        logf("Homebrew: %s\n", error.c_str());
        return false;
    }
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
// on the menu's thread once it has ended. g_read_done says so on its own,
// so the answer is taken even when another job holds the thread by then
// (a cover download started the same turn).
std::atomic<bool> g_read_done{false};
struct BackgroundRead {
    bool started = false;
    std::string lang;
    std::string source;  // empty: download the database first
    std::set<std::string> ids;
    bool ok = false;
    std::string error;
    std::map<std::string, GameInfo> games;
    std::vector<HomebrewTitle> homebrew;
    std::vector<CustomGame> customs;
};
BackgroundRead g_read;

void ReadInBackground() {
    if (g_read.source.empty()) {
        // A cancelled job's download fails within a tenth of a second.
        if (!DownloadDatabase(g_read.lang, g_read.error, nullptr)) return;
        g_read.source = ZipPath(g_read.lang);
    }
    WiitdbScanner scanner(g_read.ids, g_read.lang);
    // The SD card taken out, or RiftWii left: the read stops (as "stopped")
    // instead of holding the card for the seconds the rest would take.
    const Progress go_on = [](double) { return !NetCancelRequested(); };
    const bool zip = g_read.source.size() > 4 && g_read.source.compare(g_read.source.size() - 4, 4, ".zip") == 0;
    const bool read = zip ? ScanZip(g_read.source, scanner, g_read.error, go_on)
                          : ScanXml(g_read.source, scanner, g_read.error, go_on);
    if (!read) return;
    g_read.games = scanner.games();
    if (!try_assign_range(g_read.homebrew, scanner.homebrew().begin(), scanner.homebrew().end())) {
        g_read.error = "no room in memory for the list of GameTDB's apps";
        return;
    }
    if (!try_assign_range(g_read.customs, scanner.customs().begin(), scanner.customs().end())) {
        g_read.error = "no room in memory for the list of GameTDB's mods";
        return;
    }
    g_read.ok = true;
}

void RunBackgroundRead() {
    ReadInBackground();
    g_read_done.store(true, std::memory_order_release);
}

}  // namespace

bool DownloadGameInfo(std::string& error, const std::function<void(std::size_t received)>& received) {
    if (!DownloadDatabase(MenuLocale(), error, received)) return false;
    // The next FindGameInfo reads the new database, and the homebrew
    // apps are read from it again.
    g_cache = Cache();
    g_homebrew_read_now = false;
    g_homebrew_failed = 0;
    return true;
}

bool IsCustomGame(const std::string& id) {
    LoadCache(MenuLocale());
    const auto found = g_cache.games.find(id);
    return found != g_cache.games.end() && found->second.custom;
}

void ForgetGameInfo() {
    // The caller has stopped the card's jobs. Join before discarding an
    // untaken answer, so it cannot be saved onto the replacement card.
    NetWaitForBackground();
    g_read = BackgroundRead();
    g_read_done.store(false, std::memory_order_relaxed);
    g_cache = Cache();
    ForgetHomebrewTitlesRead();
    g_mod_titles_read_here = false;
    g_mod_titles_checked_here = false;
}

GameInfoReadStart StartGameInfoRead(const std::set<std::string>& wanted, bool download) {
    if (g_read.started || NetBackgroundBusy()) return GameInfoReadStart::Busy;
    const std::string lang = MenuLocale();
    LoadCache(lang);
    std::string source;
    std::time_t source_time = 0;
    const bool have_source = GameInfoSource(source, source_time);
    if (!have_source && !download) return GameInfoReadStart::NothingToRead;
    const bool stale = have_source && !g_cache.read_now && source_time > g_cache.written;
    // Every game on the drives already looked for: nothing to read.
    bool missing = !have_source;
    for (const std::string& id : wanted)
        if (!g_cache.games.count(id) && !g_cache.not_listed.count(id)) missing = true;
    // The mods' list (#385) missing (read by a RiftWii from before it), too
    // big to load, or older than the database (its last write failed):
    // read once more for it, once a session per card (a console clock
    // behind the database's date would make every list look older; a card
    // that can't be written would never have one).
    struct stat mods;
    if (have_source && !g_mod_titles_read_here) {
        bool again = stat(kModTitlesPath, &mods) != 0 ||
                     static_cast<std::uint64_t>(mods.st_size) > kMaxCustomGamesBytes || mods.st_mtime < source_time;
        // One that can't be read (a card error) too; looked at once a card.
        if (!again && !g_mod_titles_checked_here) {
            std::vector<CustomGame> list;
            again = !LoadModTitles(list);
            g_mod_titles_checked_here = !again;
        }
        if (again) missing = true;
    }
    if (!stale && !missing) return GameInfoReadStart::NothingToRead;
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
    g_read_done.store(false, std::memory_order_relaxed);
    if (NetRunInBackground(RunBackgroundRead, true, false)) return GameInfoReadStart::Started;
    g_read = BackgroundRead();
    return GameInfoReadStart::Busy;
}

bool GameInfoReadPending() { return g_read.started; }

bool TakeGameInfoRead(bool& changed) {
    changed = false;
    if (!g_read.started || !g_read_done.load(std::memory_order_acquire)) return false;
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
    // As with the About page's read, the same pass found the Homebrew
    // view's apps. Write on the menu's thread, after the complete scan.
    if (!SaveHomebrewTitles(read.homebrew)) logf("Homebrew: %s could not be written\n", kHomebrewTitlesPath);
    SaveModTitles(read.customs);
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
