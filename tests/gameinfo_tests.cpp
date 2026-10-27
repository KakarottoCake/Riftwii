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

// The network's background thread, run here and now: the job has ended
// by the time Take is asked.
bool NetRunInBackground(void (*job)(), bool, bool) {
    job();
    return true;
}
bool NetBackgroundBusy() { return false; }
void NetWaitForBackground() {}
}  // namespace riftwii::wii

namespace {
namespace fs = std::filesystem;
using namespace riftwii;
using namespace riftwii::wii;

constexpr const char* kCachePath = "sd:/riftwii/gameinfo-EN.txt";
constexpr const char* kZipPath = "sd:/riftwii/wiitdb-EN.zip";

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
    const auto intact = ziptest::make_zip({{"wiitdb.xml", partial_xml, 0}});
    Write(kZipPath, reinterpret_cast<const char*>(intact.data()), intact.size());
    GameInfo found;
    std::string error;
    CHECK(FindGameInfo("RMCE01", {"RMCE01"}, found, error, [](double) { return true; }) ==
          GameInfoResult::Found);
    CHECK(found.developer == "Partial result");

    fs::remove(kZipPath);
    fs::remove(kCachePath);
    fs::remove("sd:/riftwii");
    fs::remove("sd:");
}
// Home's read of the database for the drives' games: the custom ones
// (<type>CUSTOM</type>) are known from then on, and kept in the cache.
void TestBackgroundReadFindsCustomGames() {
    fs::create_directories("sd:/riftwii");
    const std::string xml =
        "<datafile>\n"
        "<game name=\"New Super Mario Remake\"><id>SMNE02</id><type>CUSTOM</type></game>\n"
        "<game name=\"Super Smash Bros. Brawl\"><id>RSBE01</id><type/></game>\n"
        "</datafile>\n";
    const auto zip = ziptest::make_zip({{"wiitdb.xml", xml + std::string(300u << 10, ' '), 0}});
    Write(kZipPath, reinterpret_cast<const char*>(zip.data()), zip.size());
    CHECK(!IsCustomGame("SMNE02"));
    CHECK(StartGameInfoRead({"SMNE02", "RSBE01"}, false));
    bool changed = false;
    CHECK(TakeGameInfoRead(changed));
    CHECK(changed);
    CHECK(IsCustomGame("SMNE02"));
    CHECK(!IsCustomGame("RSBE01"));
    CHECK(ReadCache().find("type=custom") != std::string::npos);
    // Taken once; every game looked for, so nothing to read again.
    CHECK(!TakeGameInfoRead(changed));
    CHECK(!StartGameInfoRead({"SMNE02", "RSBE01"}, false));

    fs::remove(kZipPath);
    fs::remove(kCachePath);
    fs::remove("sd:/riftwii");
    fs::remove("sd:");
}
}  // namespace

int main() {
    TestStoredTruncationPreservesCache();
    TestBackgroundReadFindsCustomGames();
    return failures ? 1 : 0;
}
