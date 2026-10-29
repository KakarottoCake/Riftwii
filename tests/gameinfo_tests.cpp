// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gameinfo.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "netsock.hpp"
#include "online.hpp"
#include "zipbuild.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

static int failures;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "Line " << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

namespace riftwii::wii {
std::string MenuLanguage() { return "en"; }
void logf(const char*, ...) {}

bool HttpGet(const std::string&, std::vector<std::uint8_t>&, std::string&, std::size_t, int,
             const HttpProgress&, HttpRedirects) {
    CHECK(false);  // A local database scan must not need a download.
    return false;
}

bool g_background_busy = false;
bool g_defer_job = false;
bool g_start_allowed = true;
void (*g_pending_job)() = nullptr;

void FinishBackgroundJob() {
    CHECK(g_pending_job != nullptr);
    if (!g_pending_job) return;
    const auto job = g_pending_job;
    g_pending_job = nullptr;
    job();
    g_background_busy = false;
}

// Runs immediately unless a test needs a read to remain in flight.
bool NetRunInBackground(void (*job)(), bool cancellable, bool run_here) {
    CHECK(cancellable);
    CHECK(!run_here);  // A failed thread start must leave Home answering.
    if (!g_start_allowed) return false;
    if (g_defer_job) {
        CHECK(g_pending_job == nullptr);
        g_pending_job = job;
        g_background_busy = true;
    } else {
        job();
    }
    return true;
}
bool NetBackgroundBusy() { return g_background_busy; }
void NetWaitForBackground() {
    if (g_pending_job) FinishBackgroundJob();
}
bool g_cancel = false;
bool NetCancelRequested() { return g_cancel; }
}  // namespace riftwii::wii

namespace {
namespace fs = std::filesystem;
using namespace riftwii;
using namespace riftwii::wii;

constexpr const char* kCachePath = "sd:/riftwii/gameinfo-EN.txt";
constexpr const char* kZipPath = "sd:/riftwii/wiitdb-EN.zip";
constexpr const char* kHomebrewPath = "sd:/riftwii/homebrew-titles.txt";

void Write(const char* path, const char* data, std::size_t size) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(data, static_cast<std::streamsize>(size));
    file.close();
    CHECK(file.good());
}

std::string ReadCache() {
    std::ifstream file(kCachePath, std::ios::binary);
    CHECK(file.good());
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void ExpectFailedScanPreservesCache(const std::vector<std::uint8_t>& bytes, const GameInfo& later,
                                   const std::string& original_cache, bool expect_progress) {
    Write(kZipPath, reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::string source;
    std::time_t source_time = 0;
    CHECK(GameInfoSource(source, source_time));
    CHECK(source == kZipPath);

    GameInfo out;
    std::string error;
    bool made_progress = false;
    const auto progress = [&](double done) {
        if (done > 0) made_progress = true;
        return true;
    };
    CHECK(FindGameInfo("RMCE01", {"RMCE01", later.id}, out, error, progress) == GameInfoResult::Failed);
    CHECK(!error.empty());
    CHECK(made_progress == expect_progress);
    GameInfo cached;
    CHECK(PeekGameInfo(later.id, cached));
    CHECK(serialize_game_infos({{cached.id, cached}}) == original_cache);
    CHECK(!PeekGameInfo("RMCE01", out));
    CHECK(ReadCache() == original_cache);
    CHECK(!fs::exists(std::string(kCachePath) + ".part"));
}

void TestStoredTruncationPreservesCache() {
    fs::create_directories("sd:/riftwii");
    GameInfo later;
    later.id = "RSBE01";
    later.developer = "Cached developer";
    later.synopsis = "Keep these details after an incomplete scan.";
    const std::string original_cache = serialize_game_infos({{later.id, later}});
    Write(kCachePath, original_cache.data(), original_cache.size());
    // Force a refresh without a sleep or dependence on timestamp resolution.
    fs::last_write_time(kCachePath, fs::file_time_type::clock::now() - std::chrono::hours(24));
    GameInfo cached;
    CHECK(PeekGameInfo(later.id, cached));
    CHECK(serialize_game_infos({{cached.id, cached}}) == original_cache);

    // "<game " with its attributes, as GameTDB writes it: the scanner looks
    // for that, so the early game is really found before the failure.
    const std::string early_game =
        "<game name=\"partial\"><id>RMCE01</id><developer>Partial result</developer></game>";
    const std::string partial_xml = early_game + std::string(300u << 10, ' ');
    auto bytes = ziptest::make_zip({{"wiitdb.xml", partial_xml, 0}});
    bytes.resize(30 + std::string("wiitdb.xml").size() + partial_xml.size());
    // Both declared lengths exceed the bytes on disk. There is a usable
    // early game, but the cached later game is beyond the truncated response.
    for (const std::size_t offset : {18u, 22u}) {
        for (unsigned i = 0; i < 4; ++i) {
            bytes[offset + i] = static_cast<std::uint8_t>((1u << 20) >> (8 * i));
        }
    }
    ExpectFailedScanPreservesCache(bytes, later, original_cache, false);

    // Truncation is rejected before feeding XML today. Also fail after
    // delivery, so publishing partial scanner results cannot pass unnoticed.
    const auto bad_crc = ziptest::make_zip({{"wiitdb.xml", partial_xml, 0, true}});
    ExpectFailedScanPreservesCache(bad_crc, later, original_cache, true);

    // Control: the same database intact finds the early game, so the
    // failed scans above did see it and still published nothing.
    const auto intact = ziptest::make_zip({{"wiitdb.xml", "<datafile>" + partial_xml + "</datafile>", 0}});
    Write(kZipPath, reinterpret_cast<const char*>(intact.data()), intact.size());
    GameInfo found;
    std::string error;
    CHECK(FindGameInfo("RMCE01", {"RMCE01"}, found, error, [](double) { return true; }) ==
          GameInfoResult::Found);
    CHECK(found.developer == "Partial result");
    // The read also kept GameTDB's homebrew apps for the Homebrew view:
    // none in this database, and the file is written all the same, so
    // the database is not read again for them next session.
    CHECK(fs::exists(kHomebrewPath));
    std::vector<riftwii::HomebrewTitle> titles;
    // The empty list loads: read, with no apps, so the match is done.
    CHECK(LoadHomebrewTitles(titles) && titles.empty());
    // Kept, it is not read again; a list too big to load is, however new.
    ForgetHomebrewTitlesRead();
    CHECK(!HomebrewTitlesNeedReading());
    const std::string oversized(riftwii::kMaxHomebrewTitlesBytes + 1, 'x');
    Write(kHomebrewPath, oversized.data(), oversized.size());
    CHECK(HomebrewTitlesNeedReading());
    // So is one within the budget and new enough, but damaged.
    const std::string damaged = "# RiftWii's homebrew list\nnot an app line\n";
    Write(kHomebrewPath, damaged.data(), damaged.size());
    CHECK(HomebrewTitlesNeedReading());
    // A bad line after a good one: made again, and not loaded meanwhile.
    const std::string partly_damaged = "# RiftWii's homebrew list\n9XGX\tSNES9xGX\nnot an app line\n";
    Write(kHomebrewPath, partly_damaged.data(), partly_damaged.size());
    CHECK(HomebrewTitlesNeedReading());
    CHECK(!LoadHomebrewTitles(titles) && titles.empty());
    // An intact one is not, checked once and then remembered.
    const std::string intact_list = "# RiftWii's homebrew list\n9XGX\tSNES9xGX\n";
    Write(kHomebrewPath, intact_list.data(), intact_list.size());
    CHECK(!HomebrewTitlesNeedReading());
    CHECK(!HomebrewTitlesNeedReading());

    fs::remove(kZipPath);
    fs::remove(kCachePath);
    fs::remove(kHomebrewPath);
    fs::remove("sd:/riftwii");
    fs::remove("sd:");
}

void TestIncompleteXmlPreservesLists(bool zipped) {
    ForgetGameInfo();
    fs::create_directories("sd:/riftwii");
    GameInfo cached;
    cached.id = "RSBE01";
    cached.developer = "Keep these details";
    const std::string original_cache = serialize_game_infos({{cached.id, cached}});
    Write(kCachePath, original_cache.data(), original_cache.size());
    const std::string original_homebrew = "# RiftWii's homebrew list\n9XGX\tSNES9xGX\n";
    Write(kHomebrewPath, original_homebrew.data(), original_homebrew.size());
    fs::last_write_time(kHomebrewPath, fs::file_time_type::clock::now() - std::chrono::hours(24));
    const unsigned written_before = HomebrewTitlesWritten();

    const char* path = zipped ? kZipPath : "sd:/wiitdb.xml";
    const std::string partial_xml =
        "<datafile><game name=\"Partial mod\"><id>SMNE02</id><type>CUSTOM</type></game>";
    const auto write_database = [&](const std::string& xml) {
        if (zipped) {
            const auto bytes = ziptest::make_zip({{"wiitdb.xml", xml, 0}});
            Write(path, reinterpret_cast<const char*>(bytes.data()), bytes.size());
        } else {
            Write(path, xml.data(), xml.size());
        }
    };
    write_database(partial_xml);

    GameInfo found;
    std::string error;
    CHECK(FindGameInfo("SMNE02", {"SMNE02", "RSBE01"}, found, error, {}) == GameInfoResult::Failed);
    CHECK(error.find("ends early") != std::string::npos);
    CHECK(ReadCache() == original_cache);
    CHECK(!IsCustomGame("SMNE02"));
    CHECK(PeekGameInfo("RSBE01", found) && found.developer == cached.developer);

    CHECK(StartGameInfoRead({"SMNE02"}, false) == GameInfoReadStart::Started);
    bool changed = false;
    CHECK(TakeGameInfoRead(changed) && !changed);
    CHECK(!IsCustomGame("SMNE02"));
    CHECK(ReadCache() == original_cache);
    CHECK(!ReadHomebrewTitles(error, {}));
    CHECK(error.find("ends early") != std::string::npos);
    CHECK(HomebrewTitlesNeedReading());
    CHECK(HomebrewTitlesWritten() == written_before);
    std::vector<HomebrewTitle> titles;
    CHECK(LoadHomebrewTitles(titles) && titles.size() == 1);
    if (!titles.empty()) CHECK(titles[0].id == "9XGX");

    // Repairing the file allows a retry; the failed scan marked no game absent.
    write_database(partial_xml + "</datafile>\r\n\t ");
    CHECK(FindGameInfo("SMNE02", {"SMNE02"}, found, error, {}) == GameInfoResult::Found);
    CHECK(found.custom);

    fs::remove(path);
    fs::remove(kCachePath);
    fs::remove(kHomebrewPath);
    fs::remove("sd:/riftwii");
    fs::remove("sd:");
}

// Home's read of the database for the drives' games: the custom ones
// (<type>CUSTOM</type>) are known from then on, and kept in the cache.
void TestBackgroundReadFindsCustomGames() {
    ForgetGameInfo();
    fs::create_directories("sd:/riftwii");
    const std::string xml =
        "<datafile>\n"
        "<game name=\"New Super Mario Remake\"><id>SMNE02</id><type>CUSTOM</type></game>\n"
        "<game name=\"Super Smash Bros. Brawl\"><id>RSBE01</id><type/></game>\n"
        "</datafile>\n";
    const auto zip = ziptest::make_zip({{"wiitdb.xml", xml + std::string(300u << 10, ' '), 0}});
    Write(kZipPath, reinterpret_cast<const char*>(zip.data()), zip.size());
    CHECK(!IsCustomGame("SMNE02"));
    // Cancelled (the SD card taken out): stopped, nothing cached, and the
    // games are asked for again by the next read.
    g_cancel = true;
    CHECK(StartGameInfoRead({"SMNE02", "RSBE01"}, false) == GameInfoReadStart::Started);
    CHECK(GameInfoReadPending());
    // Not taken yet: a rescan's read waits.
    CHECK(StartGameInfoRead({"SMNE02", "RSBE01"}, false) == GameInfoReadStart::Busy);
    bool changed = false;
    CHECK(TakeGameInfoRead(changed));
    CHECK(!changed);
    CHECK(!GameInfoReadPending());
    CHECK(!IsCustomGame("SMNE02"));
    g_cancel = false;

    CHECK(StartGameInfoRead({"SMNE02", "RSBE01"}, false) == GameInfoReadStart::Started);
    CHECK(TakeGameInfoRead(changed));
    CHECK(changed);
    CHECK(IsCustomGame("SMNE02"));
    CHECK(!IsCustomGame("RSBE01"));
    CHECK(ReadCache().find("type=custom") != std::string::npos);
    // Taken once; every game looked for, so nothing to read again.
    CHECK(!TakeGameInfoRead(changed));
    CHECK(StartGameInfoRead({"SMNE02", "RSBE01"}, false) == GameInfoReadStart::NothingToRead);

    fs::remove(kZipPath);
    fs::remove(kCachePath);
    fs::remove(kHomebrewPath);
    fs::remove("sd:/riftwii");
    fs::remove("sd:");
}

void TestBackgroundHandoffAndRescan() {
    ForgetGameInfo();
    fs::create_directories("sd:/riftwii");
    const std::string xml =
        "<datafile>"
        "<game name=\"First mod\"><id>SMNE02</id><type>CUSTOM</type></game>"
        "<game name=\"Rescanned mod\"><id>SMNE03</id><type>CUSTOM</type></game>"
        "<game name=\"SNES9xGX\"><id>9XGX</id><type>Homebrew</type>"
        "<locale lang=\"EN\"><title>SNES9xGX</title></locale></game>"
        "</datafile>";
    const auto zip = ziptest::make_zip({{"wiitdb.xml", xml, 0}});
    Write(kZipPath, reinterpret_cast<const char*>(zip.data()), zip.size());

    // A thread that cannot start is Busy, with no pending answer to block a retry.
    g_start_allowed = false;
    CHECK(StartGameInfoRead({"SMNE02"}, false) == GameInfoReadStart::Busy);
    CHECK(!GameInfoReadPending());
    g_start_allowed = true;

    g_defer_job = true;
    CHECK(StartGameInfoRead({"SMNE02"}, false) == GameInfoReadStart::Started);
    bool changed = false;
    CHECK(!TakeGameInfoRead(changed));
    CHECK(!fs::exists(kHomebrewPath));
    // The rescan adds another ID while the previous read is still running.
    CHECK(StartGameInfoRead({"SMNE02", "SMNE03"}, false) == GameInfoReadStart::Busy);
    FinishBackgroundJob();
    // The completed read is still pending until taken, even with a free thread.
    CHECK(StartGameInfoRead({"SMNE02", "SMNE03"}, false) == GameInfoReadStart::Busy);

    // A cover fetch has taken the worker meanwhile. It must not delay handoff.
    g_background_busy = true;
    const unsigned written_before = HomebrewTitlesWritten();
    CHECK(TakeGameInfoRead(changed) && changed);
    CHECK(IsCustomGame("SMNE02"));
    CHECK(!IsCustomGame("SMNE03"));
    CHECK(HomebrewTitlesWritten() == written_before + 1);
    std::vector<HomebrewTitle> titles;
    CHECK(LoadHomebrewTitles(titles) && titles.size() == 1);
    if (!titles.empty()) CHECK(titles[0].id == "9XGX");
    CHECK(!HomebrewTitlesNeedReading());
    CHECK(StartGameInfoRead({"SMNE02", "SMNE03"}, false) == GameInfoReadStart::Busy);

    // Once the cover finishes, retry the rescan's IDs and keep the first result.
    g_background_busy = false;
    CHECK(StartGameInfoRead({"SMNE02", "SMNE03"}, false) == GameInfoReadStart::Started);
    FinishBackgroundJob();
    CHECK(TakeGameInfoRead(changed) && changed);
    CHECK(IsCustomGame("SMNE02"));
    CHECK(IsCustomGame("SMNE03"));
    CHECK(StartGameInfoRead({"SMNE02", "SMNE03"}, false) == GameInfoReadStart::NothingToRead);
    g_defer_job = false;

    fs::remove(kZipPath);
    fs::remove(kCachePath);
    fs::remove(kHomebrewPath);
    fs::remove("sd:/riftwii");
    fs::remove("sd:");
}

void TestCardReplacementDiscardsGameInfo() {
    ForgetGameInfo();
    fs::create_directories("sd:/riftwii");
    const std::string xml =
        "<datafile><game name=\"Old card's mod\"><id>SMNE02</id><type>CUSTOM</type></game></datafile>";
    const auto zip = ziptest::make_zip({{"wiitdb.xml", xml, 0}});
    Write(kZipPath, reinterpret_cast<const char*>(zip.data()), zip.size());
    CHECK(StartGameInfoRead({"SMNE02", "SMNE03"}, false) == GameInfoReadStart::Started);
    bool changed = false;
    CHECK(TakeGameInfoRead(changed) && changed);
    CHECK(IsCustomGame("SMNE02"));
    // Leave a completed answer untaken when the card is removed.
    CHECK(StartGameInfoRead({"SMNE04"}, false) == GameInfoReadStart::Started);
    fs::remove(kZipPath);
    fs::remove(kCachePath);
    fs::remove(kHomebrewPath);
    ForgetGameInfo();
    CHECK(!GameInfoReadPending());
    CHECK(!TakeGameInfoRead(changed));
    CHECK(!IsCustomGame("SMNE02"));
    CHECK(StartGameInfoRead({"SMNE02"}, false) == GameInfoReadStart::NothingToRead);
    CHECK(!fs::exists(kCachePath));
    CHECK(!fs::exists(kHomebrewPath));

    // The replacement's cache is read in the same language. An ID absent
    // from the old database must be looked for in the new database too.
    GameInfo replacement;
    replacement.id = "SMNE02";
    replacement.developer = "New card";
    const std::string cache = serialize_game_infos({{replacement.id, replacement}});
    Write(kCachePath, cache.data(), cache.size());
    const std::string new_xml =
        "<datafile><game name=\"New card's mod\"><id>SMNE03</id><type>CUSTOM</type></game></datafile>";
    const auto new_zip = ziptest::make_zip({{"wiitdb.xml", new_xml, 0}});
    Write(kZipPath, reinterpret_cast<const char*>(new_zip.data()), new_zip.size());
    fs::last_write_time(kZipPath, fs::last_write_time(kCachePath));
    ForgetGameInfo();
    GameInfo found;
    CHECK(PeekGameInfo("SMNE02", found) && found.developer == "New card");
    CHECK(!IsCustomGame("SMNE02"));
    CHECK(StartGameInfoRead({"SMNE03"}, false) == GameInfoReadStart::Started);
    CHECK(TakeGameInfoRead(changed) && changed);
    CHECK(IsCustomGame("SMNE03"));

    fs::remove(kZipPath);
    fs::remove(kCachePath);
    fs::remove(kHomebrewPath);
    fs::remove("sd:/riftwii");
    fs::remove("sd:");
}
}  // namespace

int main() {
    TestStoredTruncationPreservesCache();
    TestIncompleteXmlPreservesLists(false);
    TestIncompleteXmlPreservesLists(true);
    TestBackgroundReadFindsCustomGames();
    TestBackgroundHandoffAndRescan();
    TestCardReplacementDiscardsGameInfo();
    return failures ? 1 : 0;
}
